/*
 * FlXtR Secret of Mana Turbo opt-in input proxy.
 * A Libretro core wrapper; loads the installed Snes9x 2005 Plus as a library.
 * It does NOT patch emulator code, ROM, or save data. Game-RAM writes are
 * limited to the explicit experimental clone_enemy command (undoable) and
 * the opt-in living-world npc_walk command (src/som_npc_walk.h), which is
 * off unless its enable marker exists and the ROM fingerprints match.
 *
 * Exact usage and safety caveats: docs/secret-of-mana-ai-input-bridge.md
 */
#define _POSIX_C_SOURCE 200809L
#include "vendor/libretro.h"
#include "som_ai_actions.h"
#include "som_overlay.h"
#include "som_camera.h"
#include "som_ai_player2.h"
#include "som_gambit.h"
#include "som_disciplines.h"
#include "som_class_guide.h"
#include "som_magic.h"
#include "som_energy.h"
#include "som_ally_moves.h"
#include "som_familiar.h"
#include "som_npc_walk.h"
#include "som_npc_work.h"
#include "som_general.h"
#include "som_raid.h"
#include "som_net_pad.h"
#include "som_pads.h"
#include "som_songs.h"
#include "som_combo.h"
#include "som_bunshin.h"
#include "som_telekinesis.h"
#include "som_bunshin_draw.h"
#include "som_mage_bolt.h"
#include "som_jukebox.h"
#include "som_xp.h"
#include "som_progress.h"
#include "som_perks.h"
#include "som_weapon_styles.h"
#include "som_weapon_mask.h"
#include "som_music_mix.h"
#include "som_perf.h"
#include <time.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <dirent.h>
#include <errno.h>

/* Our own build of the same pinned snes9x2005_plus (scripts/build-cores.sh
 * with patches/snes9x2005-flxtr-music-voices.patch) when installed: it adds
 * the music-free mixing switch. Otherwise the stock core. */
#ifndef SOM_PROXY_FLXTR_CORE
#define SOM_PROXY_FLXTR_CORE "/home/apps/retroarch/cores/experimental/snes9x2005_plus_flxtr_libretro.so"
#endif
#ifndef SOM_PROXY_BASE_CORE
#define SOM_PROXY_BASE_CORE "/home/apps/retroarch/cores/snes9x2005_plus_libretro.so"
#endif
#define SOM_TRIGGER_POLL_FRAMES 2u
#define SOM_TRIGGER_DEFAULT "/tmp/flxtr-som-ai-go"
#define SOM_HOLD_FRAMES 30u
#define SOM_WRAM_SIZE 0x20000u
/* Ten snapshots per second keep the SkyLoft planner on fresh positions. */
#define SOM_WRAM_EXPORT_FRAMES 6u        /* while the AI plays */
#define SOM_WRAM_IDLE_EXPORT_FRAMES 30u  /* otherwise: living world reads every 2 s */
/* Modern cores (including the pinned snes9x2005) read the whole pad with one
 * bitmask query when the frontend reports RETRO_ENVIRONMENT_GET_INPUT_BITMASKS.
 * The vendored libretro.h predates these constants. */
#ifndef RETRO_DEVICE_ID_JOYPAD_MASK
#define RETRO_DEVICE_ID_JOYPAD_MASK 256
#endif

static void *backend;
static retro_input_state_t physical_state;
static retro_input_poll_t physical_poll;
static unsigned long frame_number;
static int armed, cancelled, completed, game_loaded, on_turbo, test_enabled;
static int direct_transport;
static unsigned long start_frame, injected_queries;
static SomAction active_action;
static unsigned action_frames;
static int autonomous_mode;
static unsigned auto_step;
static unsigned long auto_deadline;
static uint16_t last_physical_buttons;
static unsigned long last_attack_frame;
/* Combo ladder (src/som_combo.h): confirmed hits climb the level, holding
 * B or Y unleashes it. combo_hp/ids/live watch the monsters' HP. */
static SomCombo combo;
/* Perks (src/som_perks.h): on unless FLXTR_SOM_PERKS=0 at game load. */
static int perks_enabled=1;
static int som_perk(unsigned effect);
/* Passive skills (src/som_perks.h ids 5-12). A "fight" ends after
 * SOM_FIGHT_QUIET_FRAMES with no live monster within SOM_FIGHT_NEAR_PX. A
 * monster hit within SOM_REEL_FRAMES of the previous hit is still reeling. */
#define SOM_FIGHT_NEAR_PX 160
#define SOM_FIGHT_QUIET_FRAMES 180ul
#define SOM_REEL_FRAMES 90ul
#define SOM_TIGER_COOLDOWN 60ul
static unsigned long perk_quiet,monster_hit_frame[3],tiger_ready;
static uint8_t perk_monster_live[3];
static uint8_t perk_monster_species[3];   /* what lived in each slot (Raise Dead) */
static int last_kill_species=-1;
#define SOM_KILL_CREDIT_FRAMES 120ul    /* a death this soon after our hit is our kill */
static int perk_first_hit_used[3],perk_swing_done[3];
static unsigned perk_hero_hp_prev[3],perk_last_hit_damage=8u;
static unsigned monster_hit_who[3];       /* whose hit it was: his kill heal */
static uint16_t perk_enemy_hp[3];
static uint8_t perk_enemy_id[3];
static int som_perk_of(unsigned who,unsigned effect);
static unsigned som_perk_bonus(unsigned who,unsigned slot,unsigned damage,const char *what);
static void som_perk_hit(uint8_t *ram,unsigned who,unsigned slot,unsigned drop,unsigned hp_after);
static void som_monster_hit(unsigned slot);
static unsigned som_pad2_member(const uint8_t *ram);
static void som_perks_frame(uint8_t *ram);
static uint16_t som_word(const uint8_t *p);
static void som_put_word(uint8_t *p,uint16_t v);
/* The chain level with the Finisher Mastery and Weapon Flow perks. */
static unsigned som_combo_effective_level(void);
static SomFamiliar familiar={-1,-1,0,0};
static uint16_t combo_hp[3];
static uint8_t combo_ids[3],combo_live[3];
static int combo_input_held;
static unsigned long combo_hold_start;
static int combo_hold_tried;
/* L2+B: native level-1 charged primary attack.
 * L2+Y: native level-2 charged secondary attack.
 * These are existing weapon animations, not new ROM routines. */
static unsigned special_stage,special_weapon;
static unsigned long special_started;
static uint16_t special_chord;
static int special_suppress;
#define SOM_SPECIAL_TIMEOUT 360ul
static int som_special_start(unsigned which,unsigned stage);
static int som_special_start_weapon(unsigned weapon,unsigned stage,int quiet);
static void som_monk_glove(uint8_t *ram);
static int som_iai_start(unsigned level);
static int som_iai_start_raw(unsigned level);
static void som_special_tick(void);
static int som_monk_chain_press(uint8_t *ram,unsigned who,uint16_t held_dirs);
static void som_monk_chain_dirs(const uint8_t *ram,unsigned who,uint16_t pressed_dirs);
static int som_monk_in_string(unsigned who);
static unsigned som_string_weapon(unsigned cls);
static int som_blade_string(unsigned cls);
static int som_string_off(unsigned cls);
static void som_monk_chain_frame(uint8_t *ram);
static void som_combo_clear(void){som_combo_reset(&combo);}
/* which: 0 = B (primary weapon), 1 = Y (secondary). */
static void som_combo_swing(unsigned which){
    som_combo_on_swing(&combo,which,frame_number);last_attack_frame=frame_number;
}

static const char *trigger_file;
static const char *action_file="/tmp/flxtr-som-ai-action";
static const char *wram_file="/tmp/flxtr-som-wram.bin";
static const char *wram_temp_file="/tmp/flxtr-som-wram.bin.tmp";
/* Remove snapshots left by the previous proxy on load/unload. */
static const char *hot_file="/tmp/flxtr-som-hot.bin";
static const char *ring_ai_file="/tmp/flxtr-som-ai-enabled";
static int wram_export_error_logged;
static int ring_ai_selector_open,ring_ai_pending,ring_ai_enabled;
/* Idle auto-start: after SOM_IDLE_AUTOSTART_FRAMES (one minute at 60 fps)
 * without any physical button, raise the same AI signal as "ai_on". Once
 * per idle period: a new period starts only with a real button press, so a
 * bridge that stops itself is never restarted in a loop, and "ai_off" stays
 * off until the player has touched the controller again. The marker file
 * SOM_IDLE_DISABLE_FILE turns the feature off. Pausing RetroArch stops
 * retro_run, so time in the RetroArch menu does not count as idle. */
#define SOM_IDLE_AUTOSTART_FRAMES 3600ul
#define SOM_IDLE_DISABLE_FILE "/home/apps/retroarch/cores/experimental/som_ai_idle_autostart_disable"
static const char *idle_disable_file=SOM_IDLE_DISABLE_FILE;
static unsigned long last_input_frame;
static int idle_started;
static uint8_t ring_ai_initial_cell;
static unsigned long ring_ai_grace_until;
/* In-game switch that works with a solo hero (the Action Grid only exists
 * once companions have joined): hold L3 ALONE for one second and
 * release to raise the same AI signal as "ai_on". The SNES has neither, so
 * the game never reacts to them. (Select cannot be used: on the Steam Link
 * it is RetroArch's hotkey-enable button, which RetroArch withholds from
 * the core while held.) Any button still stops the AI. A hold that starts
 * while the AI runs only stops it, so the release cannot switch it back on. */
#define SOM_SWITCH_HOLD_FRAMES 60ul
static int switch_holding,switch_spoiled;
static unsigned long switch_hold_start;
static int som_write_ring_ai_marker(void);
/* Gambits (src/som_gambit.h): while the AI plays, buttons drive this menu
 * instead of taking control. The enabled mask persists for the bridge. */
#define SOM_GAMBIT_FILE "/home/apps/retroarch/cores/experimental/som_ai_gambits"
static const char *gambit_file=SOM_GAMBIT_FILE;
#define SOM_COMBAT_FILE "/home/apps/retroarch/cores/experimental/som_ai_combat"
static const char *combat_file=SOM_COMBAT_FILE;
#define SOM_QUEST_FILE "/home/apps/retroarch/cores/experimental/som_ai_quest"
static const char *quest_file=SOM_QUEST_FILE;
#define SOM_BRAIN_FILE "/home/apps/retroarch/cores/experimental/som_ai_brain"
static const char *brain_file=SOM_BRAIN_FILE;
#define SOM_DEBUG_MESSAGES_FILE "/home/apps/retroarch/cores/experimental/som_ai_debug_messages"
static const char *debug_messages_file=SOM_DEBUG_MESSAGES_FILE;
#define SOM_P2_AI_FILE "/home/apps/retroarch/cores/experimental/som_ai_player2"
static const char *p2_ai_file=SOM_P2_AI_FILE;
/* Independent from the six Gambit priority digits, shared with the
 * external song-id -> yt-dlp playback bridge. */
#define SOM_MUSIC_GENRE_FILE "/home/apps/retroarch/cores/experimental/som_music_genre"
static const char *music_genre_file=SOM_MUSIC_GENRE_FILE;
static void som_music_genre_load(void);
static void som_music_genre_save(void);
static SomGambits gambits={0,0,{4,3,3,3,4,1},0,SOM_PAGE_ACTIONS,
    {3,2,2,2,3,3,3,3},{3,3},0,0,1,0,0,0};
static SomDisciplines disciplines;
/* The acting hero (2026-11-22, "write them with companions in mind"): the
 * player's moves (X class moves, L2 specials, throw, bash, Blink, portals,
 * lock-on...) run on whoever holds pad 1 (0 boy, 1 girl, 2 sprite), in her
 * class, from her own energy. It changes only while the hero who has it is
 * idle (som_actor_update), so a move never jumps body halfway. */
static unsigned som_actor;
#define SOM_ACT(ram) ((ram)+0xE000u+0x200u*som_actor)
static unsigned som_actor_class(void){
    return som_disc_member_class(&disciplines,som_actor);
}
static void som_actor_update(void);
/* The hero's sprite palette (its +0x8B attribute: palette 1 = 7E:DF20 for
 * the boy, live 2026-11-14) follows the chosen discipline's colour ramp.
 * The original colours are kept and re-read whenever the game rewrites
 * the palette (map change, events), so the tint never stacks. */
#define SOM_HERO_CLOTHES 0x7F00u     /* palette colours 8-14 */
/* The companions' clothes, judged from their live palettes (2026-11-20,
 * palette 2 at 7E:DF40, 3 at 7E:DF60): hair 2-4 and skin 6-7 match the
 * boy's; the girl's greens 5/8/9/13/14 and red ramp 10-12, the sprite's
 * red ramp 9/13/14 and greens 10-12 (yellow 8, near his skin, kept). */
#define SOM_GIRL_CLOTHES 0x7F20u
#define SOM_SPRITE_CLOTHES 0x7E00u
static const uint16_t som_member_clothes[SOM_PARTY_MEMBERS]={
    SOM_HERO_CLOTHES,SOM_GIRL_CLOTHES,SOM_SPRITE_CLOTHES};
/* The girl's hair (player, 2026-11-22): black with silver in the middle
 * shade, over her colours 2-4 (darkest to lightest; live WRAM 2026-11-22
 * shows her orange hair there: 176,56,24 / 216,56,0 / 240,104,16). SNES
 * BGR555: 2 black, 3 silver, 4 black with a cool sheen. */
#define SOM_GIRL_HAIR_FIRST 2u
static const uint16_t som_girl_hair[3]={
    (5u<<10)|(3u<<5)|3u,(25u<<10)|(22u<<5)|22u,(10u<<10)|(7u<<5)|7u};
static void som_member_hair(uint8_t *pal,unsigned who){
    if(who!=1u)return;
    for(unsigned i=0;i<3u;i++){
        pal[2u*(SOM_GIRL_HAIR_FIRST+i)]=(uint8_t)(som_girl_hair[i]&0xFFu);
        pal[2u*(SOM_GIRL_HAIR_FIRST+i)+1u]=(uint8_t)(som_girl_hair[i]>>8);
    }
}
static uint8_t member_pal_orig[SOM_PARTY_MEMBERS][32],member_pal_written[SOM_PARTY_MEMBERS][32];
static int member_pal_have[SOM_PARTY_MEMBERS];
static unsigned member_pal_addr[SOM_PARTY_MEMBERS];
/* The chosen discipline, levels and experience survive game restarts. */
#define SOM_DISC_FILE "/home/apps/retroarch/cores/experimental/som_ai_disciplines"
#define SOM_DISCIPLINES_V1 9u          /* disciplines before rogue/samurai/archer */
static const char *disc_file=SOM_DISC_FILE;
static void som_disc_save(void){
    /* The device file belongs to the older saves: a classless game keeps its
     * classes in its own progression record. */
    if(disciplines.classless)return;
    char temp[256];snprintf(temp,sizeof(temp),"%s.tmp",disc_file);
    FILE *out=fopen(temp,"w");
    if(!out)return;
    int ok=fprintf(out,"v1 %u",(unsigned)disciplines.selected)>0;
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT&&ok;i++)
        ok=fprintf(out," %u %lu",(unsigned)disciplines.level[i],
                   (unsigned long)disciplines.experience[i])>0;
    ok=ok&&fprintf(out," p %u %u",(unsigned)disciplines.party[0],
                   (unsigned)disciplines.party[1])>0;
    ok=ok&&fprintf(out," o")>0;           /* the player's class order */
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT&&ok;i++)
        ok=fprintf(out," %u",(unsigned)disciplines.order[i])>0;
    ok=ok&&fprintf(out,"\n")>0;
    if(fclose(out)!=0)ok=0;
    if(ok)rename(temp,disc_file);else unlink(temp);
}
static void som_disc_load(void){
    FILE *in=fopen(disc_file,"r");
    if(!in)return;
    SomDisciplines loaded;som_disc_init(&loaded);
    unsigned selected=0;
    int ok=fscanf(in,"v1 %u",&selected)==1&&
        (selected<SOM_DISCIPLINE_COUNT||selected==SOM_DISCIPLINE_NONE);
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT&&ok;i++){
        unsigned level=0;unsigned long xp=0;
        int got=fscanf(in," %u %lu",&level,&xp);
        /* Older files have fewer disciplines; the rest start fresh. */
        if(got==EOF&&i>=SOM_DISCIPLINES_V1)break;
        ok=got==2&&level>=1u&&level<=100u;
        if(ok){loaded.level[i]=(uint16_t)level;loaded.experience[i]=(uint32_t)xp;}
    }
    /* The companions' classes (files before 2026-11-20 have none: defaults). */
    unsigned girl=0,sprite=0;
    if(ok&&fscanf(in," p %u %u",&girl,&sprite)==2){
        if(girl<SOM_DISCIPLINE_COUNT||girl==SOM_DISCIPLINE_NONE)loaded.party[0]=(uint8_t)girl;
        if(sprite<SOM_DISCIPLINE_COUNT||sprite==SOM_DISCIPLINE_NONE)loaded.party[1]=(uint8_t)sprite;
        /* The class order (files before 2026-11-22 have none: 0..15). */
        uint8_t order[SOM_DISCIPLINE_COUNT];unsigned got=0;
        char tag=0;
        if(fscanf(in," %c",&tag)==1&&tag=='o'){
            for(;got<SOM_DISCIPLINE_COUNT;got++){
                unsigned v=0;
                if(fscanf(in," %u",&v)!=1||v>=SOM_DISCIPLINE_COUNT)break;
                order[got]=(uint8_t)v;
            }
        }
        if(got==SOM_DISCIPLINE_COUNT&&som_disc_order_valid(order))
            memcpy(loaded.order,order,sizeof(order));
    }
    fclose(in);
    if(!ok){fprintf(stderr,"[som-ai] DISCIPLINES file unreadable; starting fresh\n");return;}
    loaded.selected=(uint8_t)selected;
    disciplines=loaded;
}
/* One party member's tint: the boy (slot 0) and, since 2026-11-20, the
 * girl (1) and the sprite (2), each in their own class colours. */
static void som_member_palette_frame(uint8_t *ram,unsigned who){
    uint8_t *o=ram+0xE000u+0x200u*who;
    /* Not in the party (no object, no HP yet): leave that palette alone. */
    if(o[0]!=1||(who&&!(o[0x184]|o[0x185])))return;
    const unsigned pal=(o[0x8B]>>1)&7u;
    const unsigned addr=0xDE00u+(128u+pal*16u)*2u;
    uint8_t *current=ram+addr;
    const unsigned profile=som_disc_class_profile(som_disc_member_class(&disciplines,who));
    uint8_t *orig=member_pal_orig[who],*written=member_pal_written[who];
    if(member_pal_have[who]&&addr!=member_pal_addr[who])member_pal_have[who]=0;
    uint8_t *working=ram+0x0700u+pal*32u;
    const int field_play=ram[0xE4]==1&&ram[0xE6]==0x0F;
    if(profile==SOM_PAL_ORIGINAL&&who!=1u){     /* the girl still gets her hair */
        if(member_pal_have[who]&&!memcmp(current,written,32)){
            memcpy(current,orig,32);
            if(!memcmp(working,written,32))memcpy(working,orig,32);
        }
        member_pal_have[who]=0;
        /* No discipline: still no block/dash glow on the boy. The shadow
         * keeps the plain colours (the glow only touches the shown buffer). */
        if(who==0u&&field_play)memcpy(working,current,32);
        return;
    }
    if(!member_pal_have[who]||memcmp(current,written,32)){
        memcpy(orig,current,32);              /* the game wrote fresh colours */
        member_pal_have[who]=1;member_pal_addr[who]=addr;
    }
    uint8_t tinted[32];
    memcpy(tinted,orig,32);
    if(profile!=SOM_PAL_ORIGINAL)som_pal_apply(tinted,profile);
    /* Clothes only: outline, hair and skin keep their colours (the girl's
     * hair is black). */
    uint8_t dressed[32];
    memcpy(dressed,orig,32);
    for(unsigned i=0;i<16u;i++)
        if(som_member_clothes[who]&(1u<<i)){dressed[2u*i]=tinted[2u*i];dressed[2u*i+1u]=tinted[2u*i+1u];}
    som_member_hair(dressed,who);
    memcpy(current,dressed,32);
    /* The game works on (and sends to the screen) a second buffer,
     * 7E:0700 + palette*32 (live trace 2026-11-14: hero palette 1 at 7E:0720
     * equals 7E:DF20, and only it changes during dash/block flashes).
     * For the boy it is held at the tint every frame of field play: the
     * dash/block glow is the attack-palette routine $C1:CA04 (cidwel/som-decomp
     * actor-attack-palette.md), which reloads the 15 ROM colours (the
     * "old palette" coming back) and then flashes them. retro_run ends at
     * vblank, so this write is what the NMI sends to CGRAM: no glow, no
     * relapse. The companions keep their flashes (hit, cast): only plain
     * or tinted colours are replaced, which also undoes the ROM reload. */
    if((who==0u&&field_play)||!memcmp(working,orig,32)||!memcmp(working,written,32))
        memcpy(working,dressed,32);
    memcpy(written,dressed,32);
}
static void som_hero_palette_frame(uint8_t *ram){
    if(!ram)return;
    for(unsigned who=0;who<SOM_PARTY_MEMBERS;who++)som_member_palette_frame(ram,who);
}
static int som_manual_disciplines;
static int class_guide_open;
static uint16_t pad2_raw,pad2_wanted,pad2_pressed;
static int pad2_start_block;
static unsigned pad2_seals;
static int pad2_string_waits,p2_context;
static void som_lock_swap(int reset);      /* player 2's own lock-on ... */
static void som_string_swap(int reset);    /* ... and B string */
static int som_class_x(uint8_t *ram);
static int som_pad2_borrow(unsigned who);
static void som_p2ai_switched(void);
static int act_ai;                     /* the ring menu's AI page is open */
static void som_gambit_result(int result);
static int som_handle_command(const char *command);
static int som_actor_busy(void);
static unsigned pad2_actor=SOM_PARTY_MEMBERS,pad2_combo_actor=SOM_PARTY_MEMBERS;
static unsigned pad2_spell_cursor,pad2_spell_class=SOM_DISCIPLINE_NONE;
static unsigned long pad2_x_start;
static int pad2_x_tapped;
static unsigned pad2_guard_hp,pad2_guard_map;
static uint16_t auto_target_enemy_hp[3],auto_target_party_hp[3];
static uint8_t auto_target_enemy_id[3],auto_target_enemy_live[3],auto_target_ready;
static unsigned auto_target_map;
/* After the menu closes, the game sees no button until all are let go (the
 * closing A must not also close the ring under it). */
static int menu_hold_until_release,menu_was_open;
/* Start in the field: the game sees Y (open_ring) for this many frames and
 * no Start while it is held. */
#define SOM_START_RING_FRAMES 3ul
static int start_ring_held;
static unsigned long start_ring_until;
#define SOM_RING_CONFIG_ENTRIES 7u
#define SOM_RING_CLASSES_DESCRIPTOR 0x9276u
static int som_ring_open_idle(const uint8_t *ram){
    /* Bit 7 is set in native submenus such as Status and Equipment. */
    return ram&&ram[0xE000]==1&&ram[0x7D]==1&&ram[0xD0]==0&&
        !(ram[0x1801]&0x80u)&&ram[0x1812]==0&&ram[0x1809]!=0x55u;
}
static int classes_ring_enabled;
static uint16_t ring_hidden;
static SomStatusTexture classes_background;
static int som_ring_on_classes(const uint8_t *ram){
    if(!classes_ring_enabled||!som_ring_open_idle(ram)||ram[0x1809]!=2u||
       ram[0x180B]!=SOM_RING_CONFIG_ENTRIES||ram[0x180A]>=SOM_RING_CONFIG_ENTRIES)return 0;
    /* Native Status remembers its descriptor and can rotate the list when
     * reopened. Read the live descriptor under the cursor, not its index. */
    const uint8_t *entry=ram+0x1880u+2u*ram[0x180A];
    return (entry[0]|((unsigned)entry[1]<<8))==SOM_RING_CLASSES_DESCRIPTOR;
}
/* The Start menu's MUSIC page (src/som_jukebox.h): requests go to the Steam
 * Link's music bridge, its answer is read while the page is open. */
#define SOM_JUKE_REQUEST "/tmp/flxtr-som-jukebox.req"
#define SOM_JUKE_LIST "/tmp/flxtr-som-jukebox.list"
static const char *juke_request_file=SOM_JUKE_REQUEST,*juke_list_file=SOM_JUKE_LIST;
static SomJukebox jukebox;
static int jukebox_ready;
static void som_juke_send(const char *line){
    char temp[256];snprintf(temp,sizeof(temp),"%s.tmp",juke_request_file);
    FILE *out=fopen(temp,"w");
    if(!out)return;
    const int ok=fprintf(out,"%s\n",line)>0;
    if(fclose(out)!=0||!ok){unlink(temp);return;}
    rename(temp,juke_request_file);
    fprintf(stderr,"[som-ai] MUSIC request: %s\n",line);
}
static void som_juke_refresh(void){
    FILE *in=fopen(juke_list_file,"r");
    if(!in)return;
    static char text[SOM_JUKE_ROWS*SOM_JUKE_TEXT+256];
    const size_t n=fread(text,1,sizeof(text)-1u,in);
    fclose(in);
    text[n]=0;
    som_juke_load(&jukebox,text);
}
static void som_music_genre_load(void){
    FILE *in=fopen(music_genre_file,"r");
    unsigned genre=0;
    if(in){
        if(fscanf(in,"%u",&genre)!=1)genre=0;
        fclose(in);
    }
    gambits.music_genre=genre<SOM_MUSIC_GENRE_COUNT?genre:0;
}
static void som_music_genre_save(void){
    char temp[256];snprintf(temp,sizeof(temp),"%s.tmp",music_genre_file);
    FILE *out=fopen(temp,"w");if(!out)return;
    int ok=fprintf(out,"%u\n",gambits.music_genre)>0;
    if(fclose(out)!=0)ok=0;
    if(ok)rename(temp,music_genre_file);else unlink(temp);
}
static void som_gambit_load(void){
    char text[16]={0};
    FILE *in=fopen(gambit_file,"r");
    if(in){if(!fgets(text,sizeof(text),in))text[0]=0;fclose(in);}
    som_gambit_parse(&gambits,text[0]?text:NULL);gambits.open=0;gambits.cursor=0;
    gambits.infinite_mp=0;                 /* test switch never carries over */
    gambits.page=SOM_PAGE_ACTIONS;
    char combat[16]={0};
    in=fopen(combat_file,"r");
    if(in){if(!fgets(combat,sizeof(combat),in))combat[0]=0;fclose(in);}
    som_combat_parse(&gambits,combat[0]?combat:NULL);
    char quest[16]={0};
    in=fopen(quest_file,"r");
    if(in){if(!fgets(quest,sizeof(quest),in))quest[0]=0;fclose(in);}
    som_quest_parse(&gambits,quest[0]?quest:NULL);
    char brain[16]={0};
    in=fopen(brain_file,"r");
    if(in){if(!fgets(brain,sizeof(brain),in))brain[0]=0;fclose(in);}
    som_brain_parse(&gambits,brain[0]?brain:NULL);
    char debug[8]={0};
    in=fopen(debug_messages_file,"r");
    if(in){if(!fgets(debug,sizeof(debug),in))debug[0]=0;fclose(in);}
    gambits.debug_messages=debug[0]=='1';
    char second[8]={0};
    in=fopen(p2_ai_file,"r");
    if(in){if(!fgets(second,sizeof(second),in))second[0]=0;fclose(in);}
    gambits.player2=second[0]=='1';
}
static void som_gambit_write(const char *path,const char *text){
    char temp[256];
    snprintf(temp,sizeof(temp),"%s.tmp",path);
    FILE *out=fopen(temp,"w");
    if(!out)return;
    int ok=fprintf(out,"%s\n",text)>0;
    if(fclose(out)!=0)ok=0;
    if(ok)rename(temp,path);else unlink(temp);
}
static void som_gambit_save(void){
    char text[SOM_GAMBIT_COUNT+1],combat[SOM_COMBAT_COUNT+1];
    som_gambit_format(&gambits,text);som_combat_format(&gambits,combat);
    som_gambit_write(gambit_file,text);
    som_gambit_write(combat_file,combat);
    char quest[SOM_QUEST_COUNT+1];
    som_quest_format(&gambits,quest);
    som_gambit_write(quest_file,quest);
    char brain[16];
    som_brain_format(&gambits,brain);
    som_gambit_write(brain_file,brain);
    som_gambit_write(debug_messages_file,gambits.debug_messages?"1":"0");
}
#define SOM_MENU_SCAN_START 0x1800u
#define SOM_MENU_SCAN_LENGTH 0x0200u
static uint8_t som_previous_menu[SOM_MENU_SCAN_LENGTH];
static int som_have_previous_menu;
static retro_environment_t environment_callback;
/* Optional AI message box, drawn on a private copy of each frame. */
#define SOM_MESSAGE_POLL_FRAMES 15u
#define SOM_MESSAGE_FRAMES 240u
static const char *message_file="/tmp/flxtr-som-ai-message";
static retro_video_refresh_t frontend_video;
static int pixel_format; /* RETRO_PIXEL_FORMAT_0RGB1555 until the core says otherwise */
static SomOverlay overlay;
/* Speech bubbles ('^') are debug output: without AI debug messages they are
 * accepted and dropped. Plain status boxes always show. */
static int som_bubbles_shown(void);
static int som_overlay_say(SomOverlay *o,const char *text,unsigned frames){
    if(text&&text[0]=='^'&&!som_bubbles_shown())return text[1]!=0;
    return som_overlay_set(o,text,frames);
}
/* Teaching: while the marker exists, physical presses are appended to a log
 * as the action names the bridge can replay. Nothing is injected. */
static const char *record_marker="/tmp/flxtr-som-ai-record";
static const char *record_file="/tmp/flxtr-som-ai-recorded";
static int recording;
static uint8_t *overlay_frame;
static size_t overlay_frame_size;
/* Automatic camera (som_camera.h); this marker file turns it off. */
#define SOM_CAMERA_OFF_FILE "/home/apps/retroarch/cores/experimental/som_ai_camera_off"
static const char *camera_off_file=SOM_CAMERA_OFF_FILE;
static SomCamera camera;
static int camera_off=-1,camera_shot;
static SomCamGovern camera_govern;
static void load_backend(void); /* RetroArch may query core info before init. */
static void som_auto_state_path_set(const char *rom);
/* Every gameplay feature is gated on this: only the Turbo ROM by its file
 * name (any other game, or no path, gets a pass-through core). */
static int som_rom_is_turbo(const char *path){
    return path&&strstr(path,"Secret of Mana, Turbo.smc")!=NULL;
}

#define FORWARD(ret, name, args, callargs) \
    static ret (*backend_##name) args; \
    RETRO_API ret name args { \
        load_backend(); \
        return backend_##name callargs; \
    }
#define FORWARD_VOID(name, args, callargs) \
    static void (*backend_##name) args; \
    RETRO_API void name args { \
        load_backend(); \
        backend_##name callargs; \
    }

FORWARD_VOID(retro_set_audio_sample,(retro_audio_sample_t cb),(cb))
/* Audio batches pass through som_audio_batch, which mixes the soundtrack in
 * (src/som_music_mix.h); retro_set_audio_sample_batch is defined below. */
static void (*backend_retro_set_audio_sample_batch)(retro_audio_sample_batch_t cb);
FORWARD_VOID(retro_set_controller_port_device,(unsigned port,unsigned device),(port,device))
FORWARD_VOID(retro_reset,(void),())
/* Save states carry the save-owned progression (src/som_progress.h) in a
 * trailer after the core's own state: retro_serialize* are defined below. */
static size_t (*backend_retro_serialize_size)(void);
static bool (*backend_retro_serialize)(void *data,size_t size);
static bool (*backend_retro_unserialize)(const void *data,size_t size);
FORWARD_VOID(retro_cheat_reset,(void),())
FORWARD_VOID(retro_cheat_set,(unsigned index,bool enabled,const char *code),(index,enabled,code))
FORWARD(bool,retro_load_game_special,(unsigned type,const struct retro_game_info *info,size_t count),(type,info,count))
FORWARD(unsigned,retro_get_region,(void),())
FORWARD(void *,retro_get_memory_data,(unsigned id),(id))
FORWARD(size_t,retro_get_memory_size,(unsigned id),(id))
/* Save-owned progression (src/som_progress.h). One record per save-block
 * fingerprint in SOM_PROG_DIR; writes are atomic (temp + fsync + rename) and
 * keep the previous generation as .prev; a record of an unknown version is
 * never overwritten. The device-wide v1 file keeps only the session's class
 * choice as before; its old levels are preserved once as legacy-v1.txt and
 * are applied only by the explicit `progress_claim_legacy` command. */
#ifndef SOM_PROG_DIR
#define SOM_PROG_DIR "/home/apps/retroarch/cores/experimental/som_progress"
#endif
static const char *prog_dir=SOM_PROG_DIR;
static SomProgress prog;
static uint64_t prog_rom,prog_seen[SOM_PROG_SLOTS];
static int prog_baseline,prog_was_field,prog_session_first=1;
static void som_prog_path(uint64_t owner,const char *suffix,char *out,size_t cap){
    snprintf(out,cap,"%s/%016llx.prog%s",prog_dir,(unsigned long long)owner,suffix);
}
static int som_prog_read(uint64_t owner,SomProgress *p){
    char path[300];
    for(int gen=0;gen<2;gen++){
        som_prog_path(owner,gen?".prev":"",path,sizeof(path));
        FILE *in=fopen(path,"r");
        if(!in)continue;
        char text[SOM_PROG_TEXT_MAX+1];
        const size_t n=fread(text,1,SOM_PROG_TEXT_MAX,in);fclose(in);text[n]=0;
        const int got=som_prog_parse(text,n,p);
        if(got==SOM_PROG_OK&&p->owner==owner)return SOM_PROG_OK;
        fprintf(stderr,"[som-ai] PROGRESS %s %s\n",path,
                got==SOM_PROG_UNKNOWN_VERSION?"has an unknown version: left alone":"is damaged");
        if(got==SOM_PROG_UNKNOWN_VERSION)return got;
    }
    return SOM_PROG_CORRUPT;
}
static int som_prog_write(void){
    if(!prog.owner)return 0;                 /* a new game before its first save */
    som_prog_from_disc(&prog,&disciplines);
    prog.perks[0]=som_perk_bits(&disciplines);
    char text[SOM_PROG_TEXT_MAX];
    const size_t n=som_prog_format(&prog,text,sizeof(text));
    char path[300],prev[300],temp[300];
    som_prog_path(prog.owner,"",path,sizeof(path));
    som_prog_path(prog.owner,".prev",prev,sizeof(prev));
    som_prog_path(prog.owner,".tmp",temp,sizeof(temp));
    SomProgress existing;
    FILE *old=fopen(path,"r");
    if(old){
        char buf[SOM_PROG_TEXT_MAX+1];const size_t m=fread(buf,1,SOM_PROG_TEXT_MAX,old);fclose(old);
        buf[m]=0;
        if(som_prog_parse(buf,m,&existing)==SOM_PROG_UNKNOWN_VERSION){
            fprintf(stderr,"[som-ai] PROGRESS %s has an unknown version: not overwritten\n",path);
            return 0;
        }
    }
    mkdir(prog_dir,0755);
    FILE *out=n?fopen(temp,"w"):NULL;
    int ok=out&&fwrite(text,1,n,out)==n;
    if(out){ok=fflush(out)==0&&ok;if(ok)fsync(fileno(out));ok=fclose(out)==0&&ok;}
    if(!ok){unlink(temp);fprintf(stderr,"[som-ai] PROGRESS write failed: %s\n",path);return 0;}
    if(old)rename(path,prev);
    rename(temp,path);
    return 1;
}
/* Back from a classless game: the device's classes again. */
static void som_disc_init_keep(void){
    som_disc_init(&disciplines);som_disc_load();
}
/* Called every 30 frames: notice a save the game just wrote (a slot block
 * changed and matches the game in play) and the moment a save is loaded or
 * a new game starts (field play entered). */
static void som_prog_frame(void){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size)return;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    const uint8_t *sram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    const size_t sram_size=backend_retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if(!ram||!sram||backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE||
       sram_size<SOM_PROG_SLOTS*SOM_PROG_SLOT_SIZE)return;
    const int in_play=som_prog_slot_in_play(ram,sram,sram_size);
    for(unsigned k=0;k<SOM_PROG_SLOTS;k++){
        const uint64_t fp=som_prog_slot_fingerprint(sram,k);
        if(prog_baseline&&fp!=prog_seen[k]&&in_play==(int)k){
            prog.owner=fp;prog.slot=k;prog.rom=prog_rom;
            const int ok=som_prog_write();
            fprintf(stderr,"[som-ai] PROGRESS saved with slot %u (%016llx)%s\n",k+1u,
                    (unsigned long long)fp,ok?"":": write failed");
        }
        prog_seen[k]=fp;
    }
    prog_baseline=1;
    const int field=ram[0xE000]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F&&ram[0xD0]==0;
    if(field&&!prog_was_field){
        if(in_play>=0&&prog_seen[in_play]!=prog.owner){
            const uint64_t fp=prog_seen[in_play];
            SomProgress loaded;
            const int got=som_prog_read(fp,&loaded);
            const int was_classless=disciplines.classless;
            if(was_classless)som_disc_init_keep();
            if(got==SOM_PROG_OK){prog=loaded;som_prog_to_disc(&prog,&disciplines);}
            else{
                som_prog_fresh(&prog,prog_rom);
                const uint8_t keep=disciplines.selected;
                som_prog_to_disc(&prog,&disciplines);disciplines.selected=keep;
            }
            prog.owner=fp;prog.slot=(unsigned)in_play;
            fprintf(stderr,"[som-ai] PROGRESS slot %d loaded (%016llx): %s\n",in_play+1,
                    (unsigned long long)fp,got==SOM_PROG_OK?"its progression restored":
                    "no record, fresh progression");
        }else if(in_play<0&&ram[0xE181]==0&&(prog_session_first||prog.owner||!disciplines.classless)){
            /* Level 1 and no save: a new game, and new games are classless. */
            som_prog_fresh(&prog,prog_rom);som_prog_classless(&prog);
            som_prog_to_disc(&prog,&disciplines);
            fprintf(stderr,"[som-ai] PROGRESS new game: classless\n");
        }else if(in_play<0&&prog_session_first){
            const uint8_t keep=disciplines.selected;
            som_prog_fresh(&prog,prog_rom);som_prog_to_disc(&prog,&disciplines);
            disciplines.selected=keep;
            fprintf(stderr,"[som-ai] PROGRESS new game (no matching save): fresh progression\n");
        }
        prog_session_first=0;
    }
    prog_was_field=field;
}
/* Classless games, every 30 frames: what each hero may become (level and
 * stats), what they have been, and a note when the first choice opens. */
static void som_unlock_frame(const uint8_t *ram){
    if(!disciplines.classless||!ram||ram[0xE4]!=1||ram[0xE6]!=0x0F)return;
    for(unsigned who=0;who<SOM_PARTY_MEMBERS;who++){
        const uint8_t *o=ram+0xE000u+0x200u*who;
        if(o[0]!=1||!(o[0x184]|o[0x185]))continue;
        const uint16_t had=disciplines.can[who];
        disciplines.can[who]=som_unlock_mask(o[0x181]+1u,o+0x188,disciplines.held[who]);
        const unsigned cls=som_disc_member_class(&disciplines,who);
        if(cls<SOM_DISCIPLINE_COUNT&&!(disciplines.held[who]>>cls&1u)){
            disciplines.held[who]|=(uint16_t)(1u<<cls);
            fprintf(stderr,"[som-ai] CLASS %s becomes %s\n",som_party_names[who],som_discipline_titles[cls]);
            if(prog.owner)som_prog_write();
        }
        if(!had&&disciplines.can[who]&&!disciplines.held[who]){
            char text[64];
            snprintf(text,sizeof(text),"%s can choose a class|R3 ring: Classes, or L2+R1",som_party_names[who]);
            som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES);
            fprintf(stderr,"[som-ai] CLASS %s may choose: %04x\n",som_party_names[who],
                    (unsigned)disciplines.can[who]);
        }
    }
}
/* Explicit migration: apply the preserved device-wide v1 levels to the save
 * in play (never automatic). */
static int som_prog_claim_legacy(void){
    char path[300],done[300];
    snprintf(path,sizeof(path),"%s/legacy-v1.txt",prog_dir);
    snprintf(done,sizeof(done),"%s/legacy-v1.claimed.txt",prog_dir);
    if(!prog.owner){fprintf(stderr,"[som-ai] PROGRESS claim refused: no save in play\n");return 0;}
    FILE *in=fopen(path,"r");
    if(!in){fprintf(stderr,"[som-ai] PROGRESS claim refused: no legacy file\n");return 0;}
    unsigned selected=0;int ok=fscanf(in,"v1 %u",&selected)==1;
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT&&ok;i++){
        unsigned level=0;unsigned long xp=0;
        if(fscanf(in," %u %lu",&level,&xp)!=2)break;
        if(level<1u||level>100u){ok=0;break;}
        if(level>disciplines.level[i]||(level==disciplines.level[i]&&xp>disciplines.experience[i])){
            disciplines.level[i]=(uint16_t)level;disciplines.experience[i]=(uint32_t)xp;
        }
    }
    fclose(in);
    if(!ok){fprintf(stderr,"[som-ai] PROGRESS claim refused: legacy file damaged\n");return 0;}
    som_prog_write();rename(path,done);
    fprintf(stderr,"[som-ai] PROGRESS legacy v1 levels applied to slot %u\n",prog.slot+1u);
    return 1;
}
/* Preserve the device-wide v1 levels once, before anything rewrites them. */
static void som_prog_preserve_legacy(void){
    char path[300],done[300];
    snprintf(path,sizeof(path),"%s/legacy-v1.txt",prog_dir);
    snprintf(done,sizeof(done),"%s/legacy-v1.claimed.txt",prog_dir);
    if(access(path,F_OK)==0||access(done,F_OK)==0)return;
    FILE *in=fopen(disc_file,"r");
    if(!in)return;
    char text[512];const size_t n=fread(text,1,sizeof(text),in);fclose(in);
    mkdir(prog_dir,0755);
    FILE *out=fopen(path,"w");
    if(out){fwrite(text,1,n,out);fclose(out);
        fprintf(stderr,"[som-ai] PROGRESS kept the old device-wide levels as %s\n",path);}
}
FORWARD(unsigned,retro_api_version,(void),())
FORWARD_VOID(retro_get_system_info,(struct retro_system_info *info),(info))
FORWARD_VOID(retro_get_system_av_info,(struct retro_system_av_info *info),(info))

static void (*backend_set_environment)(retro_environment_t);
static void (*backend_set_video_refresh)(retro_video_refresh_t);
static void (*backend_set_input_poll)(retro_input_poll_t);
static void (*backend_set_input_state)(retro_input_state_t);
static void (*backend_init)(void);
static void (*backend_deinit)(void);
static bool (*backend_load_game)(const struct retro_game_info *);
static void (*backend_unload_game)(void);
static void (*backend_run)(void);
/* Gambit test switch: keep the boy's MP full so spells can be tried before
 * he has any (he has 0/0 MP early on). Only current MP +0x186 is written,
 * every frame in field play; the game still pays each cast and this
 * refills it. Turning it off clamps MP back to the real maximum. */
#define SOM_TEST_MP 99u
static uint8_t *som_live_wram(void){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size||
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE)return NULL;
    return (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
}
static void som_infinite_mp_frame(void){
    uint8_t *ram=gambits.infinite_mp?som_live_wram():NULL;
    if(!ram||ram[0xE000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F)return;
    ram[0xE186]=SOM_TEST_MP;
}
static void som_infinite_mp_changed(void){
    uint8_t *ram=som_live_wram();
    if(!gambits.infinite_mp&&ram&&ram[0xE000]==1&&ram[0xE186]>ram[0xE187])
        ram[0xE186]=ram[0xE187];
    som_overlay_say(&overlay,gambits.infinite_mp?"^Infinite MP ON|test only":
                    "^Infinite MP off",SOM_MESSAGE_FRAMES);
    fprintf(stderr,"[som-ai] TEST infinite MP %s\n",gambits.infinite_mp?"on":"off");
}

/* Keep all Libretro callbacks on the same thread as the original frontend.
 * Never try to emulate frames from a timer/network worker. */
/* Each discipline fights with two weapons: B attacks with the first, Y with
 * the second (player's design, 2026-11-14). Weapon types: 0 glove, 1 sword,
 * 2 axe, 3 spear, 4 whip, 5 bow, 6 boomerang, 7 javelin. */
static const uint8_t som_disc_weapons[SOM_DISCIPLINE_COUNT][2]={
    {1,2},   /* Swordsmanship: sword, axe */
    {0,3},   /* Martial Arts: glove, spear */
    {1,6},   /* Ninjutsu: sword, chakram (boomerang engine slot) */
    /* Casters hold no weapon: bare hands (the glove); B and Y cast. */
    {0,0},   /* Magic */
    {0,0},   /* Divine Arts */
    {0,0},   /* Dark Arts */
    {0,0},   /* Summoning */
    {0,0},   /* Support & Dance */
    {1,3},   /* Weapon Flow: sword, spear */
    {1,5},   /* Roguecraft: sword, bow */
    {1,5},   /* Bushido: katana (sword), yumi (bow) */
    {5,7},   /* Archery: bow, javelin */
    {4,6},   /* Vampire Hunting: whip, boomerang (the thrown cross) */
    {0,0},   /* Necromancy: bare hands */
    {2,5},   /* Artifice: hammer (axe), crossbow (bow) */
    {0,0},   /* Psionics: bare hands */
};
/* Each class's weapons are real game weapons at a fixed forge grade (their
 * art and name), with stats set by the class, not by the grade (player's
 * design, 2026-11-20): the B weapon keeps its styles (som_weapon_styles.h,
 * e.g. the samurai's Masamune as Tanto, Katana or Dai Katana); the Y weapon
 * (and Weapon Flow's X bow) hits like the type's tier-0 row at class level 1
 * rising to its top plain row at class level 8, times power_pct. Grades are
 * rows the direct equip can apply (no stat-bonus rows). Names: the ROM's
 * name block (Turbo: sword 5 = Masamune). Slots: B, Y, X (0xFF = none). */
typedef struct { uint8_t grade, power_pct; } SomClassArm;
static const SomClassArm som_class_arms[SOM_DISCIPLINE_COUNT][3]={
    {{4,100},{4,100},{0xFF,0}},   /* Swordsman: Excalibur, Gold Axe */
    {{5,100},{5, 90},{0xFF,0}},   /* Monk: Hyper Fist, Coral Spear */
    {{2,100},{7,100},{0xFF,0}},   /* Ninja: Kusanagi, Shuriken */
    {{4, 70},{4, 70},{0xFF,0}},   /* Mage: bare hands (Metal Glove) */
    {{4, 70},{4, 70},{0xFF,0}},   /* Cleric: bare hands */
    {{4, 70},{4, 70},{0xFF,0}},   /* Warlock: bare hands */
    {{4, 70},{4, 70},{0xFF,0}},   /* Summoner: bare hands */
    {{4, 70},{4, 70},{0xFF,0}},   /* Dancer: bare hands */
    {{3,100},{4,100},{6,100}},    /* Weaponmaster: Orchicalcum, Halberd, Wingbow */
    {{2,100},{2, 95},{0xFF,0}},   /* Rogue: Kusanagi, Longbow */
    {{5,110},{6, 85},{0xFF,0}},   /* Samurai: Masamune, Wingbow */
    {{8,115},{8,110},{0xFF,0}},   /* Archer: Garuda Buster, Valkyrian */
    {{5,110},{2,100},{0xFF,0}},   /* Hunter: Morning Star, Silver Boomerang */
    {{4, 70},{4, 70},{0xFF,0}},   /* Necromancer: bare hands */
    {{5,100},{3, 95},{0xFF,0}},   /* Artificer: Werebuster, Great Bow */
    {{4, 70},{4, 70},{0xFF,0}},   /* Mentalist: bare hands */
};
/* The class's grade for weapon `type` (B, then Y, then X), else 0xFF. */
static unsigned som_class_arm_slot(unsigned cls,unsigned type){
    if(cls>=SOM_DISCIPLINE_COUNT)return 3u;
    for(unsigned slot=0;slot<2u;slot++)if(som_disc_weapons[cls][slot]==type)return slot;
    if(cls==SOM_DISC_FLOW&&type==5u)return 2u;
    return 3u;
}
/* Discipline experience (src/som_xp.h): only verified events reach here.
 * The file is written on every level-up and at most once a minute. */
static SomXpDecay xp_decay;
static int xp_dirty;
static unsigned long xp_saved_frame;
enum {SOM_XPK_HIT=1,SOM_XPK_FLOW,SOM_XPK_DODGE,SOM_XPK_SPELL,SOM_XPK_CLONE,SOM_XPK_FAMILIAR};
static void som_xp_give(unsigned discipline,unsigned base,unsigned species,unsigned kind,
                        const char *why){
    if(discipline>=SOM_DISCIPLINE_COUNT)return;
    const unsigned amount=som_xp_scaled(&xp_decay,species,base,frame_number);
    const unsigned before=disciplines.level[discipline];
    const uint32_t perks_before=som_perk_bits(&disciplines);
    if(!amount||!som_disc_award(&disciplines,discipline,amount,
                                som_xp_event(frame_number,kind),1))return;
    xp_dirty=1;
    const uint32_t gained=som_perk_bits(&disciplines)&~perks_before;
    for(size_t i=0;i<SOM_PERK_COUNT&&perks_enabled;i++)
        if(som_perks[i].id<32u&&(gained&(1u<<som_perks[i].id)))
            fprintf(stderr,"[som-ai] PERK unlocked: %s\n",som_perks[i].name);
    if(disciplines.level[discipline]!=before){
        char text[64];
        snprintf(text,sizeof(text),"^%s|level %u",som_discipline_names[discipline],
                 (unsigned)disciplines.level[discipline]);
        som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES);
        som_disc_save();som_prog_write();xp_dirty=0;xp_saved_frame=frame_number;
    }
    fprintf(stderr,"[som-ai] XP %s +%u (%s) -> Lv%u %lu/%u\n",som_discipline_names[discipline],
            amount,why,(unsigned)disciplines.level[discipline],
            (unsigned long)disciplines.experience[discipline],
            som_disc_next_xp(disciplines.level[discipline]));
}
static void som_xp_flush(int now){
    if(xp_dirty&&(now||frame_number-xp_saved_frame>=3600ul)){
        som_disc_save();som_prog_write();xp_dirty=0;xp_saved_frame=frame_number;
    }
}
/* A spell cast at a monster waits for that monster's HP to drop. */
static int xp_spell_slot=-1;
static uint16_t xp_spell_hp;
static uint8_t xp_spell_species;
static unsigned long xp_spell_frame;
static int som_equip_weapon(uint8_t *ram,unsigned type);
static int som_equip_weapon_direct(uint8_t *ram,unsigned type);
static int som_equip_ally_weapon(uint8_t *ram,unsigned who,unsigned type);
#define SOM_EQUIP_OK 1
#define SOM_EQUIP_BUSY 0                /* hero mid-action: retry later */
#define SOM_EQUIP_REFUSED (-1)          /* will not work: use the ring */
static int som_cast_spell(uint8_t *ram,unsigned spell);
static int som_cast_spell_by(uint8_t *ram,unsigned who,unsigned spell);
static const uint8_t *som_rom_at(uint32_t snes,size_t length);
#define SOM_QUICK_CAST_FRAMES 240ul      /* quick cast: how long the skip may wait */
static unsigned long quick_cast_until[3];   /* per hero: 0 none */
typedef struct {
    unsigned long deadline;
    unsigned target,map,mp,cost;
    int paid;
} SomRevival;
static SomRevival revivals[SOM_PARTY_MEMBERS];
#define SOM_SPELL_FIRE_BALL 0x0Cu
/* X casts the current spell of the chosen discipline (som_magic.h); holding
 * X steps to the next one. One cursor per discipline (+1: no discipline). */
static unsigned spell_cursor[SOM_DISCIPLINE_COUNT+1u];
static unsigned long x_press_frame;
static int x_cycled;
/* Artificer portals (player, 2026-11-20): hold X (SOM_PORTAL_HOLD_FRAMES)
 * to place portal A where he stands, again for portal B; stepping into one
 * takes him to the other (Blink's hidden travel, in smoke). A third hold
 * starts a new pair; a map change closes them. Tap X stays the bomb. */
#define SOM_PORTAL_HOLD_FRAMES 30ul
#define SOM_PORTAL_ENTER_PX 10
#define SOM_PORTAL_REARM_PX 24
typedef struct { int count,x[2],y[2],armed; unsigned map,layer; } SomPortals;
static SomPortals portals;
static int portal_x_used;
static int som_portal_place(uint8_t *ram);
static int som_portal_place_raw(uint8_t *ram);
/* An NPC to talk to or a chest to open right in front of the hero (both
 * are the game's attack press in Turbo): casters' B goes to the game then,
 * so they talk and open chests with no spell and no swing. NPCs: objects
 * from 7E:E600 with identity >= 0x80 and an event (tools/som_dialogue.py);
 * chests: identity 0x56. */
/* who: the hero looking (0 boy, 1 girl, 2 sprite). */
static int som_interact_ahead_by(const uint8_t *ram,unsigned who){
    if(!ram||who>2u)return 0;
    const uint8_t *hero=ram+0xE000u+0x200u*who;
    const int hx=(int)(hero[2]|hero[3]<<8),hy=(int)(hero[4]|hero[5]<<8);
    const unsigned facing=hero[0x010];
    int fx=0,fy=0;
    if(facing==0x02u)fx=1;else if(facing==0x82u)fx=-1;else if(facing==0x01u)fy=1;else fy=-1;
    for(unsigned slot=3u;slot<12u;slot++){
        const uint8_t *o=ram+0xE000u+0x200u*slot;
        if(o[0]!=1u||((o[0x0B]^hero[0x0B])&0x80u))continue;
        const unsigned event=(unsigned)(o[0x3E]|o[0x3F]<<8)&0xFFFu;
        const unsigned status=(unsigned)o[0x190]|(unsigned)o[0x191]<<8;
        const int npc=o[0x180]>=0x80u&&event>0u&&event<0x800u;
        const int chest=o[0x180]==0x56u&&!(status&0x8000u);
        if(!npc&&!chest)continue;
        const int dx=(int)(o[2]|o[3]<<8)-hx,dy=(int)(o[4]|o[5]<<8)-hy;
        const int along=dx*fx+dy*fy,across=fx?dy:dx;
        if(along>=0&&along<=28&&across>=-14&&across<=14)return 1;
    }
    return 0;
}
/* FlXtR's own MP (each class's energy: Chakra, Chi, Ki...) and AP (the
 * evade pips), src/som_energy.h. energy_free: tests and no-cost runs. */
static SomEnergy energy;
static int energy_free;
static unsigned long energy_refused_at;
static int som_clone_jutsu(uint8_t *ram,const char *jutsu);
static int som_clone_jutsu_raw(uint8_t *ram,const char *jutsu);
/* Ninjutsu, Ninja Storm style (player's design, 2026-11-20): a tapped X
 * waits SOM_X_A_WINDOW frames before the jutsu goes off; A inside that wait
 * is Blink instead, the higher evade, toward the held direction. */
#define SOM_X_A_WINDOW 15ul
#define SOM_BLINK_DASH_PX 96
static unsigned long x_cast_at;     /* frame the waiting jutsu goes off, 0 none */
static unsigned x_cast_spell;
static int som_blink_dash(uint8_t *ram,int dx,int dy);
static int som_blink_dash_raw(uint8_t *ram,int dx,int dy);
/* X was pressed with L2 (an L2+X move): its release is not a cast or an
 * Iaijutsu, even when L2 lets go in the same frame (swordsman cast a saber
 * after Whirlwind, 2026-11-20). */
static int x_l2_chord,x_tap_armed;
#define SOM_X_HOLD_FRAMES 30ul
/* Where X's list starts: a caster's first two spells are on B and Y. */
/* Casters' spell sets (player's design, 2026-11-20): a caster holds no
 * weapon, so its three styles (R1) are spell sets: set k puts the class's
 * spells 2k+1 and 2k+2 on B and Y (wrapping), X cycles the others. The
 * default style (1) is set I, the list's own first two. */
static unsigned som_caster_set(unsigned cls);
static unsigned som_caster_spell(unsigned cls,unsigned which){
    const unsigned n=som_class_spell_count(cls);
    return som_class_spell(cls,n?(2u*som_caster_set(cls)+which)%n:0u);
}
static unsigned som_x_spell_base(void){
    const unsigned n=som_class_spell_count(som_actor_class());
    return som_actor_class()<SOM_DISCIPLINE_COUNT&&som_disc_caster(som_actor_class())&&
           n>SOM_CASTER_BUTTON_SPELLS?SOM_CASTER_BUTTON_SPELLS:0u;
}
static unsigned som_current_spell(void){
    const unsigned d=som_actor_class()<SOM_DISCIPLINE_COUNT?som_actor_class():SOM_DISCIPLINE_COUNT;
    const unsigned base=som_x_spell_base(),n=som_class_spell_count(som_actor_class());
    if(base&&n>base)                         /* a caster: after its set's B and Y */
        return som_class_spell(som_actor_class(),
            (2u*som_caster_set(som_actor_class())+base+spell_cursor[d]%(n-base))%n);
    return som_class_spell(som_actor_class(),
                           n>base?base+spell_cursor[d]%(n-base):spell_cursor[d]);
}
static void som_next_spell(void){
    const unsigned d=som_actor_class()<SOM_DISCIPLINE_COUNT?som_actor_class():SOM_DISCIPLINE_COUNT;
    const unsigned n=som_class_spell_count(som_actor_class()),base=som_x_spell_base();
    if(n<=base)return;                    /* Bushido / Weapon Flow: no spells */
    spell_cursor[d]=(spell_cursor[d]+1u)%(n-base);
}
/* Seals: D-pad presses counted while L2 is held (som_magic.h). */
static unsigned seal_count;
static int seal_mode;
static int special_from_ai;           /* special_<n> command while the AI plays */
/* A evade: the hero plays the game's own back-roll pose 0x8D (rolls
 * 55 px away from the facing, 65 frames; som-decomp hero-poses.md 1-3:
 * pose = +0x1C 0x40, +0x11 code, +0x42 busy, as ACTOR_ANIM does) and is
 * untouchable meanwhile: status 0x0400 (+0x191 bit 2), "no hit is
 * resolved on the actor" ($C0:4F7B, rom-combat.md 12). Only the
 * disciplines with a dodge (som_disc_has_dodge). */
static int evade_active,evade_a_hidden,evade_xp_given;
/* The mage's evade is a Blink step: the same carried dodge, the hero hidden
 * in smoke while he moves (evade_blink_hid: the evade hid him). */
static int evade_blink,evade_blink_hid;
static void som_evade_blink_end(uint8_t *ram);
static unsigned evade_hp;
static unsigned evade_frames;
static unsigned long evade_started;
#define SOM_TRAIL_LENGTH 16u
static int trail_x[SOM_TRAIL_LENGTH],trail_y[SOM_TRAIL_LENGTH];
static unsigned long trail_frame[SOM_TRAIL_LENGTH],trail_until;
static unsigned trail_owner;
static int som_evade_start(uint8_t *ram);
static int som_evade_start_raw(uint8_t *ram);
/* Kage Bunshin (src/som_bunshin.h): clones the proxy keeps and draws over
 * the frame (src/som_bunshin_draw.h); touch makes them explode, the hero
 * can swap into them. */
static SomBunshin bunshin;
/* The clones out are Shunshin's: explosive, never a substitution (no swap);
 * a monster reaching the hero or the clones' time running out detonates
 * them. 0: Kage Bunshin clones (swap on A, substitution on touch). */
static int bunshin_shunshin;
static unsigned bunshin_owner;
static int evade_dir_x,evade_dir_y;    /* D-pad aim: clone swap or hop */
static int hop_dx,hop_dy;              /* hop dodge step per frame */
static unsigned hop_frames_left;
static void som_hop_step(void);
static void som_evade_shield(uint8_t *ram);
static SomBoom bunshin_booms[SOM_BUN_BOOMS];
/* Mage bolts (src/som_mage_bolt.h): every caster's B is a glove punch that
 * throws a painted ball of its B spell's element. */
static SomBolt mage_bolts[SOM_BOLTS];
/* Y (player, 2026-11-22): the class's second spell, the game's own cast
 * with its original effect, damage and MP; heals go to the most hurt hero. */
static int som_most_hurt_hero(const uint8_t *ram,unsigned spell);
static uint8_t mage_swing_prev[3];
static unsigned long mage_ally_next[3];
/* Kawarimi: the log left standing where the hero was (world px). */
#define SOM_KAWARIMI_LOG_FRAMES 48ul
static int kawarimi_log_x,kawarimi_log_y;
static unsigned long kawarimi_log_start;
static unsigned kawarimi_log_map;
/* The AI's "kawarimi" arms it for SOM_KAWARIMI_ARM_FRAMES; it fires only
 * when a monster touches (or hits) the hero meanwhile. */
#define SOM_KAWARIMI_ARM_FRAMES 180ul
static unsigned long kawarimi_armed_until;
static int som_kawarimi_check(uint8_t *ram);
static int som_kawarimi_arm(uint8_t *ram);
static int som_kawarimi_arm_raw(uint8_t *ram);
static int som_shunshin_start(uint8_t *ram);
static int som_shunshin_start_raw(uint8_t *ram);
/* Bushido kit (2026-11-20). Issen (L2+X): a straight dash-cut along the
 * facing, SOM_ISSEN_SPEED px x SOM_ISSEN_FRAMES (~84 px), untouchable like
 * the dodge; each monster passed is cut once. Iai counter: a hit landing
 * on the samurai's guard (R2) is cancelled and answered with a cut, on the
 * Iaijutsu cooldown. Zanshin: a kill readies Iaijutsu at once. */
#define SOM_ISSEN_SPEED 6
#define SOM_ISSEN_FRAMES 14u
#define SOM_ISSEN_COOLDOWN 240ul
#define SOM_ISSEN_REACH 20
static int issen_active;
static unsigned issen_hit;
static unsigned long issen_ready;
static int som_issen_start(uint8_t *ram);
static int som_issen_start_raw(uint8_t *ram);
/* Martial Arts kit (2026-11-20). Hundred Fists (L2+X): for
 * SOM_HUNDRED_FRAMES the game sees B every 4 frames (his own punches) and
 * every SOM_HUNDRED_TICK frames each monster in front takes a little more;
 * the D-pad is held still. Counter palm: like the samurai's Iai counter on
 * its own cooldown. Rising Dragon: every third combo hit adds a strike. */
#define SOM_HUNDRED_FRAMES 60ul
#define SOM_HUNDRED_TICK 8ul
#define SOM_HUNDRED_COOLDOWN 300ul
#define SOM_HUNDRED_AHEAD 18
#define SOM_HUNDRED_REACH 22
#define SOM_PALM_COOLDOWN 120ul
static unsigned long hundred_until,hundred_ready,palm_ready;
static int som_hundred_start(uint8_t *ram);
static int som_hundred_start_raw(uint8_t *ram);
/* L2+X for the swordsman, rogue and archer (2026-11-20):
 * Whirlwind (swordsman): a spin cutting every monster within the blast box
 * around him. Shadowstep (rogue): Blink's hidden travel to just behind the
 * nearest monster (seen from the hero); the next hit within
 * SOM_BACKSTAB_FRAMES is a backstab. Volley (archer): three arrows in a
 * cone, every monster up to SOM_VOLLEY_RANGE px ahead and within
 * 16 + distance/3 px of the line. */
#define SOM_DISC_ROGUE 9u
#define SOM_DISC_ARCHER 11u
#define SOM_WHIRL_COOLDOWN 240ul
#define SOM_SHADOW_COOLDOWN 300ul
#define SOM_SHADOW_RANGE 128
#define SOM_SHADOW_BEHIND 24
#define SOM_BACKSTAB_FRAMES 150ul
#define SOM_VOLLEY_COOLDOWN 180ul
#define SOM_VOLLEY_RANGE 128
static unsigned long class_move_ready,backstab_until;
static int som_class_move(uint8_t *ram);
static int som_class_move_raw(uint8_t *ram);
/* New classes' X (2026-11-20). Hunter: holy water, a flask that lands
 * SOM_HOLY_AHEAD px ahead and burns for SOM_HOLY_FRAMES, hurting every
 * monster on the fire each SOM_HOLY_TICK; L2+X Grand Cross hits every
 * monster within SOM_CROSS_RANGE. Necromancer: raise the dead, a familiar
 * cloned from a monster (the game's own clone_enemy) right after a kill.
 * Artificer: a bomb set SOM_BOMB_AHEAD px ahead that goes off after
 * SOM_BOMB_FUSE; L2+X lays a mine (up to SOM_MINES) that a monster sets
 * off. Mentalist: the nearest monster fights for him for
 * SOM_MIND_FRAMES (its slot becomes the familiar; then it is let go). */
#define SOM_HOLY_AHEAD 40
#define SOM_HOLY_FRAMES 90ul
#define SOM_HOLY_TICK 15ul
#define SOM_HOLY_REACH 24
#define SOM_HOLY_COOLDOWN 240ul
#define SOM_CROSS_RANGE 128
#define SOM_CROSS_COOLDOWN 1200ul
#define SOM_RAISE_WINDOW 600ul
#define SOM_RAISE_COOLDOWN 900ul
#define SOM_BOMB_AHEAD 24
#define SOM_BOMB_FUSE 60ul
#define SOM_BOMB_COOLDOWN 180ul
#define SOM_MINES 3u
#define SOM_MINE_TRIGGER 16
#define SOM_MINE_COOLDOWN 120ul
#define SOM_MIND_FRAMES 1200ul
#define SOM_MIND_RANGE 96
#define SOM_MIND_COOLDOWN 1800ul
static int holy_x,holy_y;
static unsigned long holy_until,x_move_ready,last_kill_frame,mind_until;
static int bomb_x,bomb_y;static unsigned long bomb_at;
static int mine_x[SOM_MINES],mine_y[SOM_MINES];static unsigned mine_map[SOM_MINES];
static uint8_t mine_live[SOM_MINES];
static uint8_t mind_palette[32];static int mind_slot=-1;
static SomTelekinesis telekinesis={.slot=-1};
static unsigned long telekinesis_ready,rescue_ready[SOM_PARTY_MEMBERS];
static int som_telekinesis_cast(uint8_t *ram);
static int som_telekinesis_cast_raw(uint8_t *ram);
static int som_class_x(uint8_t *ram);
static int som_class_x_raw(uint8_t *ram);
/* Opt-in imported dagger / masked Bushido blades. Katana keeps native art.
 * Mask pack is generated from user's ROM and optional SD3 dagger PNG, with
 * separate opaque blade and quarter-opacity slash-effect pixels. */
#define SOM_WEAPON_MASK_FILE "/home/apps/retroarch/cores/experimental/som_weapon_masks.bin"
#define SOM_WEAPON_MASK_ON_FILE "/home/apps/retroarch/cores/experimental/som_ai_weapon_masks_on"
static const char *weapon_mask_file=SOM_WEAPON_MASK_FILE;
static const char *weapon_mask_on_file=SOM_WEAPON_MASK_ON_FILE;
static unsigned long weapon_mask_on_checked;
static int weapon_mask_on,weapon_mask_tried,weapon_mask_faulted;
static uint8_t *weapon_mask_pack;
static size_t weapon_mask_pack_size;
static uint32_t *weapon_mask_index;    /* [Tanto,Dai,Kusarigama][72][256] */
static uint8_t weapon_native_tiles[0x800];
static int weapon_tiles_hidden;
static SomNpcWorkState weapon_masked;
static int kusarigama_attack_active,kusarigama_attack_released,kusarigama_swing_seen;
static unsigned kusarigama_attack_who;
static unsigned long kusarigama_attack_started;
static void som_weapon_mask_frame(uint8_t *ram);
static void som_weapon_mask_pick(const uint8_t *ram);
static int som_kusarigama_art_ready(void);
/* Lock-on targeting (player's request, 2026-11-20, Ocarina of Time
 * style): tap L1 to lock onto the nearest
 * monster within SOM_LOCK_RANGE px, tap again to let go. While locked the
 * hero keeps facing it, before and after the game's frame, so walking
 * strafes around it and every attack goes its way; a marker floats over
 * it. The lock drops when the monster dies, changes, or gets
 * SOM_LOCK_DROP px away, and on a map change. Player only (not the AI). */
#define SOM_LOCK_RANGE 160
#define SOM_LOCK_DROP 220
static int lock_slot=-1;
static uint8_t lock_identity;
static unsigned lock_who;
/* Locked on, the D-pad circles the target (player, 2026-11-23: "when
 * targeting player will walk in a circle around the radius of targetted
 * enemy"): som_lock_steer, som_lock_orbit_after. */
static int lock_orbiting,lock_radius;
static uint16_t lock_dpad_out;
static int som_lock_evasion_direction(int *dx,int *dy);
static void som_lock_steer(uint8_t *ram,uint16_t dirs);
static unsigned lock_map;
static void som_lock_toggle(uint8_t *ram);
static void som_lock_frame(uint8_t *ram);
static void som_lock_frame2(uint8_t *ram);
static int som_lock_marker(int *x,int *y);
static void som_lock_toggle_for(uint8_t *ram,unsigned who);
static void som_auto_target_frame(uint8_t *ram);
static void som_class_x_frame(uint8_t *ram);
static void som_hundred_frame(uint8_t *ram);
static void som_issen_cut(uint8_t *ram,int x,int y);
/* Hold A (Circle) SOM_A_HOLD_FRAMES: Kawarimi. The tap's hop still fires on
 * the press; the held request waits up to SOM_A_HOLD_WAIT frames for the
 * hero to be free (the hop over), even if A is let go meanwhile. */
#define SOM_A_HOLD_FRAMES 20ul
#define SOM_A_HOLD_WAIT 90ul
static unsigned long a_press_frame,a_kawarimi_since;
static int a_hold_done=1,a_kawarimi_pending;
static void som_bunshin_frame(uint8_t *ram);
static int som_bunshin_swap(uint8_t *ram,int flee,int fx,int fy,int dir_x,int dir_y);
/* R2+A bash (swordsman): the game's own run for up to SOM_BASH_LUNGE_FRAMES
 * (A held: 3 px per frame, stopped by walls, hero-movement.md 4.1), then one
 * frame with nothing pressed (A released), then the game's own sword swing
 * (B) at a full gauge. Only the pad the game sees is changed; R2 (block),
 * the D-pad, Y and X are hidden while it plays. */
#define SOM_BASH_LUNGE_FRAMES 14ul      /* ~42 px */
#define SOM_BASH_STRIKE_FRAMES 3ul
#define SOM_BASH_REACH 22               /* px: stop the lunge when this close */
enum {SOM_BASH_NONE,SOM_BASH_LUNGE,SOM_BASH_RELEASE,SOM_BASH_STRIKE};
static int bash_stage;
static unsigned long bash_started;
/* Flying kick (Martial Arts, X): the same run, then the glove; while it
 * plays the hero is hidden and drawn as the glove's kick in a jump arc
 * (frames: tools/som_cartwheel.py, kinds 0x90-0x92). With a combo level
 * ready the landing kick is the combo finisher at that level. */
/* Hero move frames for the flying kick (the pack also holds the old drawn
 * cartwheel, unused since the game's own plays): see tools/som_cartwheel.py. */
#define SOM_CARTWHEEL_PACK_FILE "/home/apps/retroarch/cores/experimental/som_cartwheel_frames.bin"
static void som_load_pack_file(const char *path,const char *what,uint8_t **pack,size_t *size);
static uint8_t *cartwheel_pack;
static size_t cartwheel_pack_size;
static int cartwheel_pack_tried;
/* Ukemi is the hero's own six SNES cannon-flight somersault poses
 * (pose script 34, kinds 0xC0..0xC2). Ninja/Samurai use collision-aware
 * movement; monks and rogues retain the native 0x93 cartwheel. */
#define SOM_UKEMI_DRAW_KIND 0xCCu
#define SOM_FORWARD_CART_KIND 0xC8u   /* the game's cartwheel, drawn facing forward */
#define SOM_SIDE_CART_SPEED 2u        /* side cartwheels: 2 px x 13 frames = 26 px */
#define SOM_SIDE_CART_FRAMES 13u
/* Ukemi's up/down tumble is the cannon-flight ball: drawn this size, about
 * the feet, so it rolls on the ground rather than flying (player, 2026-11-20). */
#define SOM_UPDOWN_CART_DIR 2u     /* which mirroring matches the game's down cartwheel */
static SomNpcWorkState ukemi_draw;
static int ukemi_hid;
static unsigned ukemi_map;
#define SOM_FLYKICK_LIFT 12             /* px at the top of the jump */
#define SOM_FLYKICK_KIND 0x90u
static int bash_kick,flykick_from_ai;
static SomNpcWorkState flykick;
static int flykick_hid;
static int som_flykick_start(uint8_t *ram);
static int som_flykick_start_raw(uint8_t *ram);
/* Fuma (Ninjutsu, boomerang): when the hero's boomerang hits a monster, a
 * shadow clone appears in smoke beside it, on the hero's side, swings the
 * sword at it once and vanishes in smoke. The strike lands through the
 * monster's pending damage +0x1F1 (like the clone explosion), worth the
 * boomerang's hit times (2 + level) / 2, level = the Ninjutsu level capped
 * at the sword's learned charge level (hero +0x1C0 low nibble) and 8.
 * Frames: tools/som_cartwheel.py, kinds 0xA0-0xA2, drawn in the hero's
 * live (class) colours at a fixed world spot. Headless check 2026-11-18:
 * a thrown boomerang hit is the combo HP drop (rabite 40 -> 15). */
#define SOM_FUMA_KIND 0xA0u
#define SOM_FUMA_APPEAR 16ul            /* smoke first (mostly cleared), then the swing */
#define SOM_FUMA_LINGER 12ul            /* last pose held before the smoke */
#define SOM_FUMA_COOLDOWN 30ul
#define SOM_FUMA_OFFSET 20              /* px from the monster */
static SomNpcWorkState fuma;
static int fuma_target=-1,fuma_struck;
static unsigned fuma_target_id,fuma_damage,fuma_level;
static unsigned long fuma_strike_at,fuma_end_at,fuma_ready;
static void som_fuma_start(const uint8_t *ram,unsigned slot,unsigned base);
static void som_fuma_frame(uint8_t *ram);
/* Blink (AI "blink_<tx>_<ty>", any class): the hero vanishes in smoke,
 * travels unseen in a straight line to the centre of tile (tx, ty) on this
 * map, over walls, and reappears in smoke. The landing spot must be open
 * ground (som_bun_spot_open) within SOM_BLINK_RANGE tiles; the AI picks a
 * tile the hero has walked on. For a hero boxed in where walking cannot get
 * out (behind slide tiles above map 130's cliffs, 2026-11-18). The camera
 * follows at SOM_BLINK_SPEED px per frame, the rate checked to stream new
 * tile rows (som_camera_follow_axis). No MP: the boy often has none. */
#define SOM_BLINK_RANGE 12
#define SOM_BLINK_SPEED 4
#define SOM_BLINK_COOLDOWN 600ul
static int blink_active,blink_hid,blink_tx,blink_ty,blink_is_swap;
static unsigned long blink_ready;
static int som_blink_start(uint8_t *ram,int tx,int ty);
static int som_blink_start_raw(uint8_t *ram,int tx,int ty);
static int som_blink_go(uint8_t *ram,int x,int y);
/* Fuma swap (ninja: A while the clone shows, or AI "fuma_swap"): the hero
 * takes the clone's place by the same hidden travel as Blink (no Blink
 * cooldown or range: the clone is never far); a strike not yet made lands
 * at once and the clone is gone. Refused where the hero cannot stand. */
static int som_fuma_swap(uint8_t *ram);
/* Companion (test team-mate, AI/tool command companion_<class>): a familiar
 * (a cloned monster the proxy fights with, src/som_familiar.h) whose own
 * sprite is hidden and which is drawn as the girl with custom parts: her
 * body and walk, a villager's head and colours, her class's primary weapon
 * swing (tools/som_cartwheel.py kinds 0xB0-0xB8), recoloured with her
 * class palette (src/som_class_palette.h). No summoner class needed; the
 * party and save are untouched. */
#define SOM_COMPANION_WALK 0xB0u
#define SOM_COMPANION_SWING 0xB1u
#define SOM_COMPANION_MOVING 8ul       /* frames a step keeps the walk going */
/* She is a Shinobi: the player's sister classes (Martial Arts, Ninjutsu,
 * Roguecraft, Bushido), switched by her situation at most every
 * SOM_COMPANION_SWITCH_FRAMES, never into the hero's class of the moment
 * (the pair covers two styles); the next pick down the list instead. */
#define SOM_COMPANION_SWITCH_FRAMES 480ul
#define SOM_COMPANION_LOOK_FRAMES 60ul     /* she reads the situation every second */
#define SOM_COMPANION_TOUGH_HP 200u
static const uint8_t som_shinobi[4]={9u,10u,2u,1u};     /* priority order below */
static struct {
    int active,px,py;
    unsigned cls,facing,dir;
    unsigned long moved_at,swing_at,strike_seen,walk_start,switched_at;
    SomNpcWorkState draw;
} companion;
/* Her Shinobi class for this moment: hurt -> Roguecraft, tough monster or a
 * crowd -> Bushido, target far -> Ninjutsu, one close -> Martial Arts; the
 * hero's class is skipped for the next in that order. */
static unsigned som_companion_pick(const uint8_t *ram,unsigned slot,const char **why){
    const uint8_t *o=ram+SOM_NPC_OBJECT(slot);
    /* The familiar's own HP is refilled every frame (som_familiar_tick):
     * "hurt" is the hero's. */
    const unsigned hp=som_bun_u16(ram+0xE182),max=som_bun_u16(ram+0xE184);
    const int x=(int)som_bun_u16(o+2),y=(int)som_bun_u16(o+4);
    unsigned near=0,tough=0;int far=1;
    for(unsigned m=0;m<3u;m++){
        if((int)m==familiar.slot)continue;
        const uint8_t *e=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*m;
        if(!som_bun_monster_alive(e))continue;
        const int dx=abs((int)som_bun_u16(e+2)-x),dy=abs((int)som_bun_u16(e+4)-y);
        if(dx>160||dy>160)continue;
        near++;
        if(som_bun_u16(e+0x184)>=SOM_COMPANION_TOUGH_HP)tough=1;
        if(dx+dy<=64)far=0;
    }
    int want;                                   /* index into som_shinobi */
    if(max&&hp*2u<max){want=0;*why="hero hurt: Roguecraft";}
    else if(tough||near>=2u){want=1;*why=tough?"tough monster: Bushido":"crowd: Bushido";}
    else if(near&&far){want=2;*why="target far: Ninjutsu";}
    else{want=3;*why=near?"one monster close: Martial Arts":"calm: Martial Arts";}
    if(som_shinobi[want]==disciplines.selected){
        want=(want+1)%4;*why="the hero has that one: next Shinobi class";
    }
    return som_shinobi[want];
}
static void som_companion_frame(uint8_t *ram);
static void som_blink_frame(uint8_t *ram);
static void som_camera_shift(uint8_t *ram,unsigned axis,int d,unsigned layer2);
static void som_camera_follow_axis(uint8_t *ram,const uint8_t *hero,unsigned axis,int d,int leash);
static int som_camera_step_ok(const uint8_t *ram,const uint8_t *hero,unsigned axis,int d);
static void som_camera_catch_frame(uint8_t *ram);
static unsigned long camera_catch_until;
static int som_blink_spot_ok(const uint8_t *ram,int x,int y);
static void som_blink_bring_party(uint8_t *ram);
static void som_flykick_frame(uint8_t *ram);
static int som_bash_start(uint8_t *ram);
static int som_bash_start_raw(uint8_t *ram);
static void som_bash_tick(void);
static void som_class_supply_frame(void);
static int class_supply_pending, class_equip_pending;
static unsigned long class_last_equip_attempt;
/* Temporary class-move guidance for the native AI companions. */
static struct {
    unsigned long until;
    unsigned map, slot, identity;
} ally_order;
#define SOM_ALLY_ORDER_FRAMES 480ul
#define SOM_ALLY_SUPPORT_SLOT 3u
/* Companions still to be handed their class weapon (bit who). */
static unsigned ally_equip_pending=6u;
static unsigned long ally_equip_tried;
static void som_party_class_saved(void){
    som_disc_save();
    ally_equip_pending|=1u<<(disciplines.who%SOM_PARTY_MEMBERS);
    ally_equip_tried=0;
    const unsigned who=disciplines.who,cls=som_disc_member_class(&disciplines,who);
    fprintf(stderr,"[som-ai] PARTY %s class=%s (saved)\n",som_party_names[who%SOM_PARTY_MEMBERS],
            cls<SOM_DISCIPLINE_COUNT?som_discipline_names[cls]:"none");
}
static void som_class_selected(void);
/* $D9 is pad 1's binding. Its low three bits select Boy, Girl or
 * Sprite. $DA/$DB belong to pads 2 and 3. */
static unsigned som_pad1_member_in(const uint8_t *ram){
    if(!ram)return 0;
    /* Pad 1 gone (its hero died, or Start): pad 2's hero leads (som_lead_update). */
    const unsigned binding=(ram[0xD9u]&7u)?ram[0xD9u]&7u:ram[0xDAu]&7u;
    for(unsigned who=SOM_PARTY_MEMBERS;who-->0u;)
        if(binding&(1u<<who))return who;
    return 0;
}
static unsigned som_pad1_member(void){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size||
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE)return 0;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram)return 0;
    return som_pad1_member_in(ram);
}
static void som_class_cycle_member(unsigned who,int dir){
    if(who>=SOM_PARTY_MEMBERS||dir==0)return;
    /* Through the player's class order (the menu's Y moves a class). */
    const unsigned cur=som_disc_member_class(&disciplines,who);
    const unsigned next=som_disc_order_step_for(&disciplines,who,cur,dir);
    if(next>=SOM_DISCIPLINE_COUNT||next==cur){
        if(next>=SOM_DISCIPLINE_COUNT)som_overlay_say(&overlay,"No class yet|Level 5 opens the first",SOM_MESSAGE_FRAMES/2u);
        return;
    }
    if(who){                        /* the girl or the sprite: her own class */
        disciplines.party[who-1u]=(uint8_t)next;disciplines.who=(uint8_t)who;
        som_party_class_saved();
        char text[64];
        snprintf(text,sizeof(text),"^%s %s|Lv%u  L2+R1 next  L2+L1 back",
                 som_party_names[who],som_discipline_titles[next],(unsigned)disciplines.level[next]);
        som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/2u);
        fprintf(stderr,"[som-ai] PARTY %s class=%u by L2+%s\n",som_party_names[who],next,
                dir>0?"R1":"L1");
        return;
    }
    disciplines.selected=(uint8_t)next;
    som_disc_save();
    som_class_selected();
    char text[64];
    snprintf(text,sizeof(text),"^%s|Lv%u  L2+R1 next  L2+L1 back",som_discipline_titles[next],
             (unsigned)disciplines.level[next]);
    som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] DISCIPLINE selected=%u by L2+%s (saved)\n",next,dir>0?"R1":"L1");
}
static void som_class_cycle(int dir){som_class_cycle_member(som_pad1_member(),dir);}
static void som_class_selected(void){
    class_supply_pending=disciplines.selected<SOM_DISCIPLINE_COUNT;
    class_equip_pending=class_supply_pending;
    class_last_equip_attempt=0;
}
/* Set each poll: the player is in field play with a discipline and Y (not
 * L2+Y) held. */
static int y_weapon_swap, y_weapon_blocked, b_weapon_blocked, x_spell_blocked;
static int x_weapon_swap;                 /* Weapon Flow: X attacks as B (third weapon) */
static int string_b_waits;                /* B pressed, its string weapon not in hand yet */
static unsigned long iai_ready;           /* Bushido: next frame Iaijutsu may start */
/* Taijutsu (Ninjutsu's B, player's design 2026-11-19): B, B are glove
 * punches, the third B is the sword; SOM_TAIJUTSU_RESET frames without a B
 * start the chain over. Ninjutsu keeps its colours throughout (the glove
 * does not make the hero a monk). */
#define SOM_TAIJUTSU_RESET 90ul
#define SOM_TAIJUTSU_PUNCHES 2u
static unsigned taijutsu_step,taijutsu_want=SOM_NO_WEAPON;
static int taijutsu_counted;
static unsigned long taijutsu_last;
/* The B chains, by class: Ninjutsu's Taijutsu; Weapon Flow's flow through
 * weapons beyond its own three; Martial Arts' glove power attacks (player,
 * 2026-11-20). Each step is a weapon, and a power level (0: a plain swing;
 * N: that weapon's power attack N, charged by the proxy). The spear (Y for
 * the monk and Weapon Flow) only ever comes as the finisher. Weapon types:
 * 0 glove 1 sword 2 axe 3 spear 4 whip 5 bow 6 boomerang 7 javelin. */
#define SOM_CHAIN_TOP 8u        /* a finisher at the highest level learned */
static const uint8_t som_chain_ninjutsu[]={0,0,1};
static const uint8_t som_chain_ninjutsu_levels[]={0,0,0};
static const uint8_t som_chain_flow[]={1,2,4,7,3};
static const uint8_t som_chain_flow_levels[]={0,0,0,0,SOM_CHAIN_TOP};
static unsigned som_b_chain_levels(unsigned cls,const uint8_t **steps,const uint8_t **levels){
    if(cls==SOM_DISC_NINJUTSU){*steps=som_chain_ninjutsu;*levels=som_chain_ninjutsu_levels;
                               return sizeof(som_chain_ninjutsu);}
    if(cls==SOM_DISC_WEAPON_FLOW){*steps=som_chain_flow;*levels=som_chain_flow_levels;
                                  return sizeof(som_chain_flow);}
    *steps=*levels=NULL;return 0;
}
static unsigned som_b_chain(unsigned cls,const uint8_t **steps){
    const uint8_t *levels;return som_b_chain_levels(cls,steps,&levels);
}
static unsigned som_taijutsu_peek(void){
    const uint8_t *steps;const unsigned n=som_b_chain(disciplines.selected,&steps);
    if(!n)return SOM_NO_WEAPON;
    if(frame_number-taijutsu_last>SOM_TAIJUTSU_RESET||taijutsu_step>=n)taijutsu_step=0;
    return steps[taijutsu_step];
}
/* The power level of the step som_taijutsu_peek() names (0: plain). */
static unsigned som_taijutsu_peek_level(void){
    const uint8_t *steps,*levels;const unsigned n=som_b_chain_levels(disciplines.selected,&steps,&levels);
    if(!n)return 0;
    (void)som_taijutsu_peek();
    return levels[taijutsu_step];
}
static void som_taijutsu_advance(void){
    const uint8_t *steps;const unsigned n=som_b_chain(disciplines.selected,&steps);
    taijutsu_step=n?(taijutsu_step+1u)%n:0u;
    taijutsu_last=frame_number;
}
static SomStyles styles;
static unsigned som_caster_set(unsigned cls){
    return cls<SOM_DISCIPLINE_COUNT?(styles.selected[cls]+2u)%SOM_STYLES_PER_CLASS:0u;
}
static int style_b_hidden,throw_active;
static void som_style_cycle(int to,unsigned who);
static void som_style_give(unsigned species);
static const char *const som_weapon_types[8];
static int som_throw_back_pressed(const uint8_t *ram,uint16_t pressed);
static int som_throw_start(uint8_t *ram);
static int som_throw_start_raw(uint8_t *ram);
static int som_cycle_weapon(uint8_t *ram);
/* Verified field play: hero object active, field mode (7E:00E4 = 1,
 * 7E:00E6 = 0x0F), no event running (7E:00D0 = 0) and the ring closed
 * (7E:007D != 1). Overlays the player opens (Start, L2+R2) need it; on the
 * title screen, in menus, events and transitions, or with unreadable
 * memory, the press goes to the game unchanged. */
static int som_overlay_field(void){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size||
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE)return 0;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    return ram&&ram[0xE000]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F&&ram[0xD0]==0&&ram[0x7D]!=1;
}
static uint8_t *som_field_ram(void){
    uint8_t *ram=som_live_wram();
    return ram&&ram[0xE4]==1&&ram[0xE6]==0x0F&&ram[0xD0]==0&&ram[0x7D]!=1?ram:NULL;
}
/* Observe HP changes following swings, not button-tap counts. */
static void som_combo_frame(void){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size||
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE)return;
    const uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram)return;
    const int field=ram[0xE000]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F&&ram[0xD0]==0;
    if(!field||som_manual_disciplines||disciplines.open){
        som_combo_clear();return;
    }
    for(unsigned i=0;i<3;i++){
        const uint8_t *e=ram+0xE600u+0x200u*i;
        const unsigned hp=(unsigned)e[0x182]|((unsigned)e[0x183]<<8);
        const int live=e[0]==1&&e[0x180]<0x80;
        const unsigned mixes_before=combo.mixes;
        if(combo.pending&&live&&combo_live[i]&&(int)i!=familiar.slot&&
           combo_ids[i]==e[0x180]&&hp<combo_hp[i]&&som_combo_on_hit(&combo,frame_number)){
            last_attack_frame=frame_number;
            const unsigned weapon=ram[0xE1E4],sel=disciplines.selected;
            som_xp_give(sel==SOM_DISC_NINJUTSU&&weapon==0u?SOM_DISC_NINJUTSU:   /* Taijutsu */
                        som_xp_weapon_discipline(weapon,sel,
                            sel<SOM_DISCIPLINE_COUNT?som_disc_weapons[sel]:NULL),
                        SOM_XP_HIT,e[0x180],SOM_XPK_HIT,"weapon hit");
            som_style_give(e[0x180]);
            if(sel==SOM_DISC_NINJUTSU&&weapon==6u&&!fuma.active)   /* Fuma: boomerang hit */
                som_fuma_start(ram,i,combo_hp[i]-hp);
            som_perk_hit((uint8_t*)ram,pad2_combo_actor<SOM_PARTY_MEMBERS?pad2_combo_actor:
                         som_pad1_member_in(ram),i,combo_hp[i]-hp,hp);
            if(sel==SOM_DISC_ROGUE&&frame_number<backstab_until){          /* Backstab */
                uint8_t *m=(uint8_t*)ram+0xE600u+0x200u*i;
                const unsigned bonus=12u+2u*disciplines.level[SOM_DISC_ROGUE];
                const unsigned pending=m[0x1F1]+bonus;
                m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
                backstab_until=0;
                som_overlay_say(&overlay,"^Backstab!",SOM_MESSAGE_FRAMES/3u);
                fprintf(stderr,"[som-ai] BACKSTAB: +%u on monster %u\n",bonus,i);
            }
            if(sel==SOM_DISC_MARTIAL&&combo.hits&&combo.hits%3u==0u){   /* Rising Dragon */
                uint8_t *m=(uint8_t*)ram+0xE600u+0x200u*i;
                const unsigned bonus=8u+disciplines.level[SOM_DISC_MARTIAL];
                const unsigned pending=m[0x1F1]+bonus;
                m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
                som_overlay_say(&overlay,"^Rising Dragon!",SOM_MESSAGE_FRAMES/3u);
                fprintf(stderr,"[som-ai] RISING DRAGON: hit %u, +%u on monster %u\n",
                        combo.hits,bonus,i);
            }
            if(combo.mixes!=mixes_before)
                som_xp_give(SOM_DISC_FLOW,SOM_XP_FLOW,e[0x180],SOM_XPK_FLOW,"weapon switch hit");
            if(som_combo_effective_level())
                fprintf(stderr,"[som-ai] COMBO %u hits, %u mixes: level %u ready\n",
                        combo.hits,combo.mixes,som_combo_effective_level());
        }
        if(xp_spell_slot==(int)i&&live&&e[0x180]==xp_spell_species&&hp<xp_spell_hp){
            som_xp_give(som_xp_spell_discipline(disciplines.selected),SOM_XP_SPELL,
                        e[0x180],SOM_XPK_SPELL,"spell hit");
            xp_spell_slot=-1;
        }
        combo_hp[i]=(uint16_t)hp;combo_ids[i]=e[0x180];combo_live[i]=(uint8_t)live;
    }
    if(xp_spell_slot>=0&&frame_number-xp_spell_frame>SOM_XP_SPELL_WINDOW)xp_spell_slot=-1;
    som_combo_tick(&combo,frame_number,combo_input_held);
}
static int som_perk_of(unsigned who,unsigned effect){
    return som_perk_value_for(effect,&disciplines,who,perks_enabled);
}
/* The acting hero's passive (his or her own moves). */
static int som_perk(unsigned effect){return som_perk_of(som_actor,effect);}
static void som_monster_hit(unsigned slot){
    if(slot>2u)return;
    monster_hit_frame[slot]=frame_number;monster_hit_who[slot]=som_actor;
}
/* Extra damage from Coup de Grace (the monster still reels from a hit) and
 * Last Resort (hero below a quarter of max HP), as a share of `damage`. */
static unsigned som_perk_bonus(unsigned who,unsigned slot,unsigned damage,const char *what){
    if(slot>2u||who>=SOM_PARTY_MEMBERS||!damage||!backend_retro_get_memory_data)return 0;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram)return 0;
    int pct=0;
    const char *names="";
    if(monster_hit_frame[slot]&&frame_number-monster_hit_frame[slot]<=SOM_REEL_FRAMES){
        const int coup=som_perk_of(who,SOM_EFFECT_REELING_BONUS);
        if(coup){pct+=coup;names="Coup de Grace";}
    }
    const unsigned hp=som_word(ram+0xE182u+0x200u*who),max=som_word(ram+0xE184u+0x200u*who);
    if(max&&hp*4u<max){
        const int last=som_perk_of(who,SOM_EFFECT_LOW_HP_BONUS);
        if(last){pct+=last;names=*names?"Coup de Grace + Last Resort":"Last Resort";}
    }
    const unsigned bonus=damage*(unsigned)pct/100u;
    if(bonus)fprintf(stderr,"[som-ai] PERK %s %s: +%u on the %s (monster %u)\n",som_party_names[who],names,bonus,what,slot);
    return bonus;
}
/* After a hero hit lowered monster `slot` by `drop` to `hp_after`. */
static void som_perk_hit(uint8_t *ram,unsigned who,unsigned slot,unsigned drop,unsigned hp_after){
    if(!ram||slot>2u)return;
    /* Bonus damage we added earlier lands as a drop too: it is not a new hit
     * to earn a bonus on (it snowballed, 2026-11-25). */
    static unsigned owed[3];
    static unsigned long owed_at[3];
    if(frame_number-owed_at[slot]>90ul)owed[slot]=0;
    const unsigned ours=owed[slot]<drop?owed[slot]:drop;
    owed[slot]-=ours;drop-=ours;
    if(drop)perk_last_hit_damage=drop;
    uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
    if(hp_after){
        const unsigned bonus=som_perk_bonus(who,slot,drop,"hit");
        if(bonus){
            owed[slot]+=bonus;owed_at[slot]=frame_number;
            const unsigned pending=m[0x1F1]+bonus;
            m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
        }
    }
    monster_hit_frame[slot]=frame_number;monster_hit_who[slot]=who;
}
/* The guard is up: the AI's block action, or physical R2. */
static int som_guarding(void){
    return (armed&&!cancelled&&!completed&&active_action==SOM_ACT_BLOCK&&
            frame_number>=start_frame&&frame_number-start_frame<action_frames)||
           (!ring_ai_enabled&&(last_physical_buttons&(1u<<RETRO_DEVICE_ID_JOYPAD_R2)));
}
/* R2 also holds the game's own R on the player's pad: its block pose, and
 * its block. What still gets through is halved here. Guard stays on the
 * controller's actor; the native shoulder input can
 * change poses on other party members, so guard is handled without it. */
static unsigned guard_who,guard_hp,guard_map;
static int som_bubbles_shown(void){return gambits.debug_messages;}
/* A guarded hit is logged (a bubble in debug). */
static void som_guard_note(unsigned player,unsigned lost,unsigned back){
    if(!overlay.frames_left)som_overlay_say(&overlay,"^Block!",SOM_MESSAGE_FRAMES/6u);
    fprintf(stderr,"[som-ai] GUARD player %u: hit %u -> %u\n",player,lost,lost-back);
}
static void som_guard_begin(void){
    guard_hp=0;
    if(!som_guarding()||som_manual_disciplines||menu_hold_until_release)return;
    const uint8_t *ram=som_field_ram();
    if(!ram)return;
    guard_who=som_pad1_member_in(ram);
    const uint8_t *hero=ram+0xE000u+0x200u*guard_who;
    if(hero[0]!=1)return;
    guard_map=som_word(ram+0xDC);
    guard_hp=som_word(hero+0x182);
}
static void som_guard_finish(void){
    if(!guard_hp||!som_guarding())return;
    uint8_t *ram=som_field_ram();
    if(!ram||som_word(ram+0xDC)!=guard_map||som_pad1_member_in(ram)!=guard_who)return;
    uint8_t *hero=ram+0xE000u+0x200u*guard_who;
    const unsigned hp=som_word(hero+0x182);
    if(hero[0]==1&&hp&&hp<guard_hp){
        som_put_word(hero+0x182,(uint16_t)(hp+(guard_hp-hp+1u)/2u));
        som_guard_note(1u,guard_hp-hp,(guard_hp-hp+1u)/2u);
    }
}
static int som_guard_up(const uint8_t *ram,unsigned who){
    if(som_pad1_member_in(ram)==who&&som_guarding())return 1;
    return pad2_actor==who&&som_pad2_member(ram)==who&&pad2_guard_hp!=0;
}
/* After each frame, per hero: Uncanny Dodge (the first hit of a fight is
 * cut), the class parries and Tiger Drop (a hit landing on the guard is
 * countered). */
static void som_perks_hero(uint8_t *ram,unsigned who){
    uint8_t *hero=ram+0xE000u+0x200u*who;
    const unsigned hp=som_word(hero+0x182),max=som_word(hero+0x184),prev=perk_hero_hp_prev[who];
    if(hero[0]!=1||!max){perk_hero_hp_prev[who]=0;return;}
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    const int hurt=prev&&hp&&hp<prev&&!(evade_active&&som_actor==who);
    unsigned now_hp=hp;
    if(hurt&&!perk_first_hit_used[who]){
        perk_first_hit_used[who]=1;
        const int pct=som_perk_of(who,SOM_EFFECT_FIRST_HIT_CUT);
        const unsigned back=(prev-hp)*(unsigned)pct/100u;
        if(back){
            now_hp=hp+back>max?max:hp+back;
            som_put_word(hero+0x182,(uint16_t)now_hp);
            fprintf(stderr,"[som-ai] PERK %s Uncanny Dodge: first hit of the fight %u -> %u\n",
                    som_party_names[who],prev-hp,prev-hp-back);
        }
    }
    const int guarding=som_guard_up(ram,who);
    const unsigned cls=som_disc_member_class(&disciplines,who);
    const int samurai=cls==SOM_DISC_BUSHIDO,monk=cls==SOM_DISC_MARTIAL;
    if((samurai||monk)&&guarding&&frame_number>=(samurai?iai_ready:palm_ready)&&
       (hero[0x059]||hero[0x1F1]||hero[0x1F2])){
        const int foe=som_bun_toucher(ram,hx,hy,SOM_BUN_HERO_TOUCH_X*2,SOM_BUN_HERO_TOUCH_Y*2,
                                      familiar.slot);
        if(foe>=0){
            uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)foe;
            const unsigned damage=samurai?20u+2u*disciplines.level[SOM_DISC_BUSHIDO]:
                                          14u+2u*disciplines.level[SOM_DISC_MARTIAL];
            const unsigned pending=m[0x1F1]+damage;
            m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
            hero[0x059]=0;hero[0x1F1]=hero[0x1F2]=0;     /* the hit is parried */
            if(hurt){som_put_word(hero+0x182,(uint16_t)prev);now_hp=prev;}
            monster_hit_frame[foe]=frame_number;monster_hit_who[foe]=who;
            if(samurai)iai_ready=frame_number+SOM_IAI_COOLDOWN_FRAMES;
            else palm_ready=frame_number+SOM_PALM_COOLDOWN;
            som_bun_boom(bunshin_booms,(int)som_bun_u16(m+2),(int)som_bun_u16(m+4),1,frame_number);
            som_overlay_say(&overlay,samurai?"^Iai counter!":"^Counter palm!",SOM_MESSAGE_FRAMES/3u);
            fprintf(stderr,"[som-ai] %s %s: parried, hit monster %d for %u\n",som_party_names[who],
                    samurai?"IAI COUNTER":"COUNTER PALM",foe,damage);
        }
    }
    const int counter=som_perk_of(who,SOM_EFFECT_GUARD_COUNTER);
    if(counter&&guarding&&frame_number>=tiger_ready&&
       (hurt||hero[0x059]||hero[0x1F1]||hero[0x1F2])){
        const int foe=som_bun_toucher(ram,hx,hy,SOM_BUN_HERO_TOUCH_X*2,SOM_BUN_HERO_TOUCH_Y*2,
                                      familiar.slot);
        if(foe>=0){
            uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)foe;
            unsigned damage=perk_last_hit_damage*(2u+2u*(unsigned)counter)/2u;
            damage+=som_perk_bonus(who,(unsigned)foe,damage,"Tiger Drop");
            const unsigned pending=m[0x1F1]+damage;
            m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
            monster_hit_frame[foe]=frame_number;monster_hit_who[foe]=who;
            tiger_ready=frame_number+SOM_TIGER_COOLDOWN;
            som_overlay_say(&overlay,"^Tiger Drop!",SOM_MESSAGE_FRAMES/3u);
            fprintf(stderr,"[som-ai] PERK %s Tiger Drop: guarded hit countered for %u on monster %d\n",
                    som_party_names[who],damage,foe);
        }
    }
    perk_hero_hp_prev[who]=now_hp;
}
/* Hits the combo watcher does not see: a hero swinging near a monster that
 * lost HP. One credit per swing, so a bonus cannot feed itself. */
static void som_perks_swings(uint8_t *ram){
    for(unsigned who=0;who<SOM_PARTY_MEMBERS;who++)
        if(!(ram[0xE01Cu+0x200u*who]&0xA0u))perk_swing_done[who]=0;
    for(unsigned slot=0;slot<3u;slot++){
        const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
        const unsigned hp=som_word(m+0x182),before=perk_enemy_hp[slot];
        const int same=m[0x180]==perk_enemy_id[slot];
        perk_enemy_hp[slot]=(uint16_t)hp;perk_enemy_id[slot]=m[0x180];
        if(!same||hp>=before||(int)slot==familiar.slot||m[0]!=1||m[0x180]>=0x80u||
           monster_hit_frame[slot]==frame_number)continue;
        int best=-1;long best_d=0;
        for(unsigned who=0;who<SOM_PARTY_MEMBERS;who++){
            const uint8_t *o=ram+0xE000u+0x200u*who;
            if(o[0]!=1||!(o[0x1C]&0xA0u)||perk_swing_done[who])continue;
            const long d=labs((long)som_bun_u16(m+2)-(long)som_bun_u16(o+2))+
                         labs((long)som_bun_u16(m+4)-(long)som_bun_u16(o+4));
            if(d>(som_weapon_long_range(o[0x1E4])?160:64))continue;
            if(best<0||d<best_d){best=(int)who;best_d=d;}
        }
        if(best<=0||som_pad1_member_in(ram)==(unsigned)best)continue;   /* the lead: combo watcher */
        perk_swing_done[best]=1;
        som_perk_hit(ram,(unsigned)best,slot,before-hp,hp);
    }
}
static void som_perks_frame(uint8_t *ram){
    if(!ram||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0){
        memset(perk_hero_hp_prev,0,sizeof(perk_hero_hp_prev));return;
    }
    int near=0;
    for(unsigned who=0;who<SOM_PARTY_MEMBERS;who++){
        const uint8_t *o=ram+0xE000u+0x200u*who;
        if(o[0]==1&&som_bun_toucher(ram,(int)som_bun_u16(o+2),(int)som_bun_u16(o+4),
                                    SOM_FIGHT_NEAR_PX,SOM_FIGHT_NEAR_PX,familiar.slot)>=0)near=1;
    }
    if(near)perk_quiet=0;
    else if(perk_quiet<SOM_FIGHT_QUIET_FRAMES)perk_quiet++;
    if(perk_quiet>=SOM_FIGHT_QUIET_FRAMES)                          /* a new fight */
        memset(perk_first_hit_used,0,sizeof(perk_first_hit_used));
    som_perks_swings(ram);
    for(unsigned who=0;who<SOM_PARTY_MEMBERS;who++)som_perks_hero(ram,who);
    /* Battle Rhythm: a monster a hero hit that dies within
     * SOM_KILL_CREDIT_FRAMES heals that hero. */
    for(unsigned slot=0;slot<3u;slot++){
        const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
        const int live=som_bun_monster_alive(m)&&som_word(m+0x182)>0;
        if(perk_monster_live[slot]&&!live&&(int)slot!=familiar.slot&&monster_hit_frame[slot]&&
           frame_number-monster_hit_frame[slot]<=SOM_KILL_CREDIT_FRAMES){
            const unsigned who=monster_hit_who[slot]%SOM_PARTY_MEMBERS;
            uint8_t *hero=ram+0xE000u+0x200u*who;
            const unsigned heal=hero[0]==1&&som_word(hero+0x182)?
                som_word(hero+0x184)*(unsigned)som_perk_of(who,SOM_EFFECT_KILL_HEAL)/100u:0u;
            if(heal){
                som_put_word(hero+0x1F3,(uint16_t)(som_word(hero+0x1F3)+heal));
                fprintf(stderr,"[som-ai] PERK %s Battle Rhythm: kill heals %u HP\n",som_party_names[who],heal);
            }
            if(som_disc_member_class(&disciplines,who)==SOM_DISC_BUSHIDO&&frame_number<iai_ready){
                iai_ready=frame_number;                    /* Zanshin */
                som_overlay_say(&overlay,"^Zanshin|Iaijutsu ready",SOM_MESSAGE_FRAMES/3u);
                fprintf(stderr,"[som-ai] ZANSHIN: kill readies Iaijutsu\n");
            }
            monster_hit_frame[slot]=0;
        }
        if(perk_monster_live[slot]&&!live&&(int)slot!=familiar.slot){
            last_kill_frame=frame_number;last_kill_species=perk_monster_species[slot];
        }
        if(live)perk_monster_species[slot]=m[0x180];
        perk_monster_live[slot]=(uint8_t)live;
    }
}
static unsigned som_combo_effective_level(void){
    const unsigned base=som_combo_level(&combo);
    if(!base)return 0;                          /* perks never start a chain */
    return som_combo_level_with(&combo,SOM_COMBO_MIX_MAX+(unsigned)som_perk(SOM_EFFECT_FLOW_MIX),
                                (unsigned)som_perk(SOM_EFFECT_FINISHER_LEVEL));
}
/* Network player 2 (src/som_net_pad.h): pad 2 from a Tailscale device. */
static SomNetPad net_pad={-1,0,0,0,0,0,0};
static int net_pad_was_active;
static uint64_t som_now_ms(void){
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;
}
static void som_net_pad_frame(void){
    if(net_pad.fd<0)return;
    const uint64_t now=som_now_ms();
    som_net_pad_poll(&net_pad,now);
    const int active=som_net_pad_active(&net_pad,now);
    if(net_pad.joined){
        net_pad.joined=0;
        struct in_addr a;a.s_addr=htonl(net_pad.owner_ip);
        char text[64];snprintf(text,sizeof(text),"Player 2 joined|%s",inet_ntoa(a));
        som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] NETPAD player 2 joined from %s\n",inet_ntoa(a));
    }else if(net_pad_was_active&&!active){
        som_overlay_say(&overlay,"Player 2 left",SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] NETPAD player 2 left\n");
    }
    net_pad_was_active=active;
}
/* Field combat translates X/Y before the game sees the pad. Reserve
 * native ring input for the synthetic Y pulse generated by Start. */
static inline uint16_t som_manual_pad_hidden(uint16_t raw){
    const uint16_t hidden=(1u<<RETRO_DEVICE_ID_JOYPAD_R)|(1u<<RETRO_DEVICE_ID_JOYPAD_R2)|
        (1u<<RETRO_DEVICE_ID_JOYPAD_L)|(1u<<RETRO_DEVICE_ID_JOYPAD_L3)|
        (1u<<RETRO_DEVICE_ID_JOYPAD_X)|(1u<<RETRO_DEVICE_ID_JOYPAD_Y);
    const uint16_t l2_chord=(1u<<RETRO_DEVICE_ID_JOYPAD_B)|(1u<<RETRO_DEVICE_ID_JOYPAD_Y)|
        (1u<<RETRO_DEVICE_ID_JOYPAD_L)|(1u<<RETRO_DEVICE_ID_JOYPAD_R);
    return (uint16_t)(hidden|((raw&l2_chord)?1u<<RETRO_DEVICE_ID_JOYPAD_L2:0u));
}
/* The game unbinds pad 1 when its hero dies or it presses Start. While only
 * pad 2 is bound, the two ports swap roles on both sides of the proxy: pad 2
 * drives everything pad 1 does (moves, evades, menus, AI) and the result goes
 * to the game's port 2. Pad 1's Start still joins, which swaps back. */
static int lead_swapped;
static retro_input_state_t frontend_state;
static SomPads pads;
static int pads_on=-1;                 /* -1 unknown; only inside the real RetroArch */
static unsigned long pads_frames;
static int16_t som_pad_port(unsigned port,unsigned device,unsigned index,unsigned id){
    if(pads_on>0&&pads.seen&&port<2u&&index==0&&(device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD){
        const uint16_t b=som_pads_buttons(&pads,port);
        return id==RETRO_DEVICE_ID_JOYPAD_MASK?(int16_t)b:(id<16u&&(b&(1u<<id))?1:0);
    }
    return frontend_state(port,device,index,id);
}
static void som_pads_frame(void){
    if(pads_on<0){
        char exe[256];
        const ssize_t n=readlink("/proc/self/exe",exe,sizeof(exe)-1u);
        exe[n>0?n:0]=0;
        const char *base=strrchr(exe,'/');
        pads_on=base&&!strncmp(base+1,"retroarch",9u);
        som_pads_init(&pads);
    }
    if(!pads_on)return;
    if(som_pads_poll(&pads,pads_frames++)){
        fprintf(stderr,"[som-ai] PADS Select+Start held: quitting\n");
        raise(SIGTERM);                /* RetroArch's clean exit, with auto-save */
    }
}
static int16_t som_lead_state(unsigned port,unsigned device,unsigned index,unsigned id){
    if(!frontend_state)return 0;
    if(lead_swapped&&port<2u){
        port=1u-port;
        if(port==1u&&index==0&&(device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&
           net_pad.fd>=0&&som_net_pad_active(&net_pad,som_now_ms())){
            const uint16_t remote=som_net_pad_buttons(&net_pad,som_now_ms());
            return id==RETRO_DEVICE_ID_JOYPAD_MASK?(int16_t)remote:
                (id<16u&&(remote&(1u<<id))?1:0);
        }
    }
    return som_pad_port(port,device,index,id);
}
static uint16_t som_pad2_buttons(void){
    if(!lead_swapped&&net_pad.fd>=0&&som_net_pad_active(&net_pad,som_now_ms()))
        return som_net_pad_buttons(&net_pad,som_now_ms());
    uint16_t buttons=0;
    for(unsigned key=0;physical_state&&key<16u;key++)
        if(physical_state(1,RETRO_DEVICE_JOYPAD,0,key))buttons|=(uint16_t)(1u<<key);
    return buttons;
}

static unsigned som_pad2_member(const uint8_t *ram){
    if(!ram)return SOM_PARTY_MEMBERS;
    if(!(ram[0xD9]&7u))return SOM_PARTY_MEMBERS;   /* pad 2 leads instead */
    const unsigned binding=(ram[0xDA]&7u)&~(ram[0xD9]&7u);
    for(unsigned who=0;who<SOM_PARTY_MEMBERS;who++)
        if(binding==(1u<<who)&&ram[0xE000u+0x200u*who]==1u)return who;
    return SOM_PARTY_MEMBERS;
}

/* Keep Player 2's X cycle within spells the direct cast path can target. */
static int som_cast_target_kind(const uint8_t *row){
    if(!row)return -1;
    const unsigned kind=row[8]&~0x50u;
    return kind==0x81u||kind==0x01u||row[8]==0u?(int)kind:-1;
}

static unsigned som_pad2_spell_index(unsigned cls,unsigned start){
    const unsigned count=som_class_spell_count(cls);
    for(unsigned i=0;i<count;i++){
        const unsigned index=(start+i)%count,spell=som_class_spell(cls,index);
        if(som_cast_target_kind(som_rom_at(0xD02AD0u+64u*spell,16u))>=0)return index;
    }
    return start;
}

/* Player 2's string runs as her: the acting hero is hers for the call, so
 * a move's hop or jutsu is hers too, and stays hers while that plays. */
static void som_pad2_string(uint8_t *ram,unsigned who,uint16_t dirs,int press){
    const unsigned keep=som_actor;
    const int lent=!som_actor_busy();
    if(lent)som_actor=who;
    p2_context=1;
    if(press)som_monk_chain_press(ram,who,dirs);
    else som_monk_chain_frame(ram);
    p2_context=0;
    if(lent&&!som_actor_busy())som_actor=keep;
}
/* The AI plays player 2 (AI menu, "Player 2 AI"): pad 2's buttons come from
 * som_ai_player2.h. It joins with Start like a player, follows the lead,
 * fights what is close with the class's own B, and steps out when it is
 * stuck far away, so the game walks the hero back. A press on the real
 * pad 2 while the player plays takes over for good (the switch goes off). */
#define SOM_P2AI_REACH 170
static unsigned long p2ai_next_start,p2ai_next_b;
static unsigned p2ai_start_left,p2ai_stuck;
static int p2ai_leaving,p2ai_x,p2ai_y;
static void som_p2ai_switched(void){
    som_gambit_write(p2_ai_file,gambits.player2?"1":"0");
    p2ai_leaving=!gambits.player2;p2ai_start_left=0;p2ai_next_start=frame_number+30ul;
    som_overlay_say(&overlay,gambits.player2?"Player 2 AI on":"Player 2 AI off",SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] PLAYER 2 AI %s\n",gambits.player2?"on":"off");
}
static void som_p2ai_press_start(unsigned long wait){
    p2ai_start_left=4u;p2ai_next_start=frame_number+wait;
}
static uint16_t som_p2ai_buttons(const uint8_t *ram){
    const uint16_t start=1u<<RETRO_DEVICE_ID_JOYPAD_START;
    if((!gambits.player2&&!p2ai_leaving&&!p2ai_start_left)||!ram||lead_swapped||som_manual_disciplines||
       menu_hold_until_release||!(ram[0xD9]&7u)||(net_pad.fd>=0&&som_net_pad_active(&net_pad,som_now_ms()))){
        p2ai_start_left=0;return 0;
    }
    if(p2ai_start_left){p2ai_start_left--;return start;}
    const unsigned who=som_pad2_member(ram);
    if(p2ai_leaving){
        p2ai_leaving=0;
        if(who<SOM_PARTY_MEMBERS)som_p2ai_press_start(60ul);
        return 0;
    }
    if(who>=SOM_PARTY_MEMBERS){
        unsigned others=0;
        const unsigned lead=som_pad1_member_in(ram);
        for(unsigned i=0;i<SOM_PARTY_MEMBERS;i++)
            others+=i!=lead&&ram[0xE000u+0x200u*i]==1u&&som_word(ram+0xE182u+0x200u*i);
        if(others&&frame_number>=p2ai_next_start)som_p2ai_press_start(240ul);
        p2ai_stuck=0;
        return 0;
    }
    const uint8_t *me=ram+0xE000u+0x200u*who,*lead=ram+0xE000u+0x200u*som_pad1_member_in(ram);
    SomAiObservation o;memset(&o,0,sizeof(o));
    o.verified_state=o.in_game=o.partner_controllable=1;
    o.partner_alive=som_word(me+0x182)!=0;
    o.partner_x=som_word(me+2);o.partner_y=som_word(me+4);
    o.leader_x=som_word(lead+2);o.leader_y=som_word(lead+4);
    long best=SOM_P2AI_REACH;
    for(unsigned i=0;i<3u;i++){
        const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*i;
        if((int)i==familiar.slot||!som_bun_monster_alive(m)||!som_word(m+0x182))continue;
        const long d=labs((long)som_word(m+2)-o.leader_x)+labs((long)som_word(m+4)-o.leader_y);
        if(d<best){best=d;o.enemy_valid=1;o.enemy_x=som_word(m+2);o.enemy_y=som_word(m+4);}
    }
    o.can_attack=!me[0x1ED]&&!me[0x01C];
    SomAiStrategy plan=som_ai_default_strategy();
    const unsigned cls=som_disc_member_class(&disciplines,who);
    if(cls<SOM_DISCIPLINE_COUNT&&(som_disc_caster(cls)||cls==SOM_DISC_ARCHERY))plan.attack_radius=80;
    uint16_t out=som_ai_decide(o,plan).buttons;
    if(out&SOM_PAD_B){                   /* separate presses, not a hold */
        if(frame_number>=p2ai_next_b)p2ai_next_b=frame_number+30ul;
        if(p2ai_next_b-frame_number<=27ul)out&=(uint16_t)~SOM_PAD_B;
    }
    const long far=labs((long)(o.leader_x-o.partner_x))+labs((long)(o.leader_y-o.partner_y));
    const int moved=o.partner_x!=p2ai_x||o.partner_y!=p2ai_y;
    p2ai_x=o.partner_x;p2ai_y=o.partner_y;
    p2ai_stuck=far>90&&!moved?p2ai_stuck+1u:0u;
    if(p2ai_stuck>=120u){
        p2ai_stuck=0;som_p2ai_press_start(360ul);
        fprintf(stderr,"[som-ai] PLAYER 2 AI stuck: stepping out\n");
        return 0;
    }
    return out;
}
/* What a press in the AI menu did, from the AI's own menu or the ring's page. */
static void som_gambit_result(int result){
    switch(result){
    case SOM_GAMBIT_GENRE_CHANGED:
        som_music_genre_save();
        fprintf(stderr,"[som-ai] MUSIC genre %s (%u)\n",
                som_music_genre_names[gambits.music_genre],gambits.music_genre);
        break;
    case SOM_GAMBIT_CHANGED:
        som_gambit_save();
        {char text[SOM_GAMBIT_COUNT+1],combat[SOM_COMBAT_COUNT+1],
              quest[SOM_QUEST_COUNT+1];
         som_gambit_format(&gambits,text);som_combat_format(&gambits,combat);
         som_quest_format(&gambits,quest);
         fprintf(stderr,"[som-ai] GAMBITS %s COMBAT %s QUEST %s\n",text,combat,quest);}
        break;
    case SOM_GAMBIT_BRAIN_CHANGED:
        som_gambit_save();
        fprintf(stderr,"[som-ai] BRAIN model %s, critic %s\n",
                som_ai_model_names[gambits.ai_model],gambits.critic?"on":"off");
        break;
    case SOM_GAMBIT_TEST_SWITCH:
        som_infinite_mp_changed();
        break;
    case SOM_GAMBIT_DEBUG_CHANGED:
        som_gambit_write(debug_messages_file,gambits.debug_messages?"1":"0");
        som_overlay_say(&overlay,gambits.debug_messages?"AI debug on|bubbles shown":
                        "AI debug off|bubbles hidden",SOM_MESSAGE_FRAMES);
        break;
    case SOM_GAMBIT_P2_CHANGED:som_p2ai_switched();break;
    case SOM_GAMBIT_EXIT:
        if(act_ai){                        /* from the ring menu: the AI starts */
            act_ai=0;gambits.open=0;gambits.resting=0;
            som_manual_disciplines=0;disciplines.open=0;menu_hold_until_release=1;
            som_handle_command("ai_on");
            break;
        }
        ally_order.until=0;
        ring_ai_enabled=0;unlink(ring_ai_file);
        som_overlay_say(&overlay,"You play|hold L3 1s: AI plays",
                        SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] GAMBIT EXIT: controls back to the player\n");
        break;
    default:break;
    }
}
static void som_pad2_poll(void){
    const uint16_t previous=pad2_raw,human=som_pad2_buttons();
    if(human&&gambits.player2&&!ring_ai_enabled&&!som_manual_disciplines&&!menu_hold_until_release){
        gambits.player2=0;p2ai_start_left=0;
        som_gambit_write(p2_ai_file,"0");
        som_overlay_say(&overlay,"Player 2 takes over",SOM_MESSAGE_FRAMES/2u);
        fprintf(stderr,"[som-ai] PLAYER 2 AI off: the pad took over\n");
    }
    const uint16_t robot=som_p2ai_buttons(som_field_ram());
    const uint16_t raw=ring_ai_enabled||human?human:robot;
    pad2_raw=raw;pad2_wanted=raw;pad2_pressed=(uint16_t)(raw&~previous);
    pad2_guard_hp=0;pad2_combo_actor=SOM_PARTY_MEMBERS;
    uint8_t *ram=som_field_ram();
    const unsigned who=som_pad2_member(ram);
    if(who!=pad2_actor){pad2_x_tapped=0;pad2_spell_class=SOM_DISCIPLINE_NONE;pad2_actor=who;}
    /* While the AI owns play, Player 2's separate controller can still open
     * and operate the AI menu; never leak those menu presses to the game. */
    /* A Start held while the AI stops must not reach the game afterwards. */
    const uint16_t start=1u<<RETRO_DEVICE_ID_JOYPAD_START;
    if(!(raw&start))pad2_start_block=0;
    if(ring_ai_enabled){pad2_start_block=(raw&start)!=0;pad2_wanted=robot;return;}
    if(pad2_start_block)pad2_wanted&=(uint16_t)~start;
    if(!ram||som_manual_disciplines||menu_hold_until_release)return;

    const uint16_t b=1u<<RETRO_DEVICE_ID_JOYPAD_B,y=1u<<RETRO_DEVICE_ID_JOYPAD_Y;
    const uint16_t x=1u<<RETRO_DEVICE_ID_JOYPAD_X,l2=1u<<RETRO_DEVICE_ID_JOYPAD_L2;
    const uint16_t l1=1u<<RETRO_DEVICE_ID_JOYPAD_L,r1=1u<<RETRO_DEVICE_ID_JOYPAD_R;
    const uint16_t r2=1u<<RETRO_DEVICE_ID_JOYPAD_R2;
    const uint16_t l3=1u<<RETRO_DEVICE_ID_JOYPAD_L3;
    pad2_wanted&=(uint16_t)~(x|y|l2|r1|r2);
    /* Start remains the native join button when no actor is bound. */
    if(who>=SOM_PARTY_MEMBERS)return;
    /* Player 2 has no ring-menu shortcut after joining. The game's own Start
     * (C0BDC5) joins and leaves: it passes through. */
    if((pad2_pressed&start)&&!pad2_start_block)
        fprintf(stderr,"[som-ai] PLAYER 2 START: leaving the party\n");

    const uint8_t *hero=ram+0xE000u+0x200u*who;
    if(raw&r2){
        pad2_guard_hp=som_word(hero+0x182);pad2_guard_map=som_word(ram+0xDC);
        if(!(raw&l2))pad2_wanted|=r1;      /* the game's block pose */
    }
    const unsigned cls=som_disc_member_class(&disciplines,who);
    if(cls>=SOM_DISCIPLINE_COUNT&&(!(raw&l2)||(raw&r2)||!(raw&(l1|r1)))){
        /* classless: the game's weapon, block, target and evade */
        const uint16_t a0=1u<<RETRO_DEVICE_ID_JOYPAD_A;
        pad2_wanted&=(uint16_t)~(a0|l1);
        if((pad2_pressed&l1)&&!(raw&(l2|r2))){som_lock_swap(0);som_lock_toggle_for(ram,who);som_lock_swap(0);}
        if((pad2_pressed&a0)&&!(raw&(l2|r2|l3))&&som_pad2_borrow(who)){
            evade_dir_x=(raw&(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT))?1:(raw&(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT))?-1:0;
            evade_dir_y=(raw&(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN))?1:(raw&(1u<<RETRO_DEVICE_ID_JOYPAD_UP))?-1:0;
            som_lock_swap(0);som_evade_start(ram);som_lock_swap(0);
            evade_dir_x=evade_dir_y=0;
        }
        return;
    }
    if((raw&l2)&&!(raw&r2)&&(raw&(l1|r1))){
        if(pad2_pressed&(l2|l1|r1))
            som_class_cycle_member(who,(raw&r1)?1:-1);
        pad2_wanted&=(uint16_t)~(l2|l1|r1);
        pad2_x_tapped=0;return;
    }
    if((raw&(l3|b))==(l3|b)){
        if(pad2_pressed&b)som_style_cycle(-1,who);
        pad2_wanted&=(uint16_t)~b;
        return;
    }
    const uint16_t dpad=(1u<<RETRO_DEVICE_ID_JOYPAD_UP)|(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)|
        (1u<<RETRO_DEVICE_ID_JOYPAD_LEFT)|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT);
    /* A: the class's evade, as on pad 1 (its dash is hidden the same way). */
    const uint16_t a=1u<<RETRO_DEVICE_ID_JOYPAD_A;
    if(som_disc_has_dodge(cls)){
        pad2_wanted&=(uint16_t)~a;
        if((pad2_pressed&a)&&!(raw&(l2|r2|l3))&&som_pad2_borrow(who)){
            evade_dir_x=(raw&(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT))?1:
                        (raw&(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT))?-1:0;
            evade_dir_y=(raw&(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN))?1:
                        (raw&(1u<<RETRO_DEVICE_ID_JOYPAD_UP))?-1:0;
            som_lock_swap(0);
            som_evade_start(ram);
            som_lock_swap(0);
            evade_dir_x=evade_dir_y=0;
        }
    }
    /* L2 + D-pad signs, then B / Y: the charge specials. */
    if((raw&l2)&&!(raw&(r2|l3))){
        if((pad2_pressed&dpad)&&pad2_seals<SOM_SEALS_MAX)pad2_seals++;
        if(pad2_pressed&(b|y)){
            const unsigned which=(pad2_pressed&y)!=0,stage=som_seal_level(pad2_seals,which);
            pad2_seals=0;
            if(!special_stage&&!special_suppress&&som_pad2_borrow(who)&&
               som_special_start(which,stage)){
                special_chord=(uint16_t)(l2|(which?y:b));
                special_suppress=1;
            }
        }
        pad2_wanted&=(uint16_t)~(dpad|b);
    }else pad2_seals=0;
    if((pad2_pressed&dpad)&&!(raw&(l2|r2))&&!som_string_off(cls)&&
       som_string_weapon(cls)!=SOM_NO_WEAPON){
        som_string_swap(0);
        som_monk_chain_dirs(ram,who,(uint16_t)(pad2_pressed&dpad));
        som_string_swap(0);
    }
    if((pad2_pressed&l1)&&!(raw&(l2|r2))){
        som_lock_swap(0);som_lock_toggle_for(ram,who);som_lock_swap(0);
    }
    pad2_wanted&=(uint16_t)~l1;
    som_lock_swap(0);
    if(lock_slot>=0&&lock_who==who){
        som_lock_steer(ram,(uint16_t)((raw&l2)?0:raw&dpad));
        if(lock_orbiting)
            pad2_wanted=(uint16_t)((pad2_wanted&~dpad)|lock_dpad_out);
    }
    som_lock_swap(0);
    if((pad2_pressed&r1)&&!(raw&(l2|r2)))som_style_cycle(-1,who);
    if((raw&l2)&&(raw&x)&&(pad2_pressed&(l2|x))){
        pad2_x_tapped=0;
        if(who>0u)pad2_combo_actor=who;
        return;
    }
    const unsigned role=som_disc_x_role(cls);
    const int caster=som_disc_caster(cls);
    if((raw&(b|y))&&!(raw&l2)){
        const unsigned slot=(raw&y)!=0;
        if(caster){
            /* The native B jab launches the caster's bolt on either pad. */
            if(slot){
                pad2_wanted&=(uint16_t)~b;
                if(pad2_pressed&y)som_cast_spell_by(ram,who,som_caster_spell(cls,1u));
            }
        }else{
            const SomWeaponStyle *style=som_style_of(&styles,cls,NULL);
            const unsigned type=slot||!style?som_disc_weapons[cls][slot]:style->weapon;
            som_string_swap(0);
            const int stringing=!slot&&som_blade_string(cls)&&som_monk_in_string(who);
            som_string_swap(0);
            const int equip=stringing?SOM_EQUIP_OK:who?som_equip_ally_weapon(ram,who,type):
                som_equip_weapon_direct(ram,type);
            if(equip==SOM_EQUIP_OK)pad2_wanted|=b;
            else if(slot||equip==SOM_EQUIP_BUSY)pad2_wanted&=(uint16_t)~b;
            /* B: the class's string, with player 2's own place in it. */
            const unsigned sw=som_string_off(cls)?SOM_NO_WEAPON:som_string_weapon(cls);
            if(!slot&&sw!=SOM_NO_WEAPON&&!special_suppress&&!(raw&(r2|l3))){
                som_string_swap(0);
                const int mid=som_monk_in_string(who);
                if((hero[0x1E4]==sw||(som_blade_string(cls)&&mid))&&
                   (mid||!som_interact_ahead_by(ram,who))){
                    pad2_wanted&=(uint16_t)~b;
                    if((pad2_pressed&b)||pad2_string_waits)
                        som_pad2_string(ram,who,(uint16_t)(raw&dpad),1);
                    pad2_string_waits=0;
                }else if((pad2_pressed&b)&&!(pad2_wanted&b))pad2_string_waits=1;
                som_string_swap(0);
            }
        }
    }
    if(!(raw&b))pad2_string_waits=0;
    if(raw&(l2|r2)){
        if(raw&l2)pad2_wanted&=(uint16_t)~b;
        pad2_x_tapped=0;return;
    }
    if(role==SOM_X_THIRD_WEAPON&&(raw&x)){
        const unsigned type=som_disc_third_weapon(cls);
        const int equip=who?som_equip_ally_weapon(ram,who,type):som_equip_weapon_direct(ram,type);
        if(equip==SOM_EQUIP_OK)pad2_wanted|=b;
    }else if(role==SOM_X_SPELLS){
        const unsigned count=som_class_spell_count(cls);
        if(!count)return;
        if(pad2_spell_class!=cls){
            pad2_spell_class=cls;
            pad2_spell_cursor=som_pad2_spell_index(cls,caster?2u%count:0u);
            pad2_x_tapped=0;
        }
        if(pad2_pressed&x){pad2_x_tapped=1;pad2_x_start=frame_number;}
        if((raw&x)&&pad2_x_tapped==1&&frame_number-pad2_x_start>=SOM_X_HOLD_FRAMES){
            pad2_x_tapped=2;
            pad2_spell_cursor=som_pad2_spell_index(cls,(pad2_spell_cursor+1u)%count);
            char message[64];
            snprintf(message,sizeof(message),"^%s|tap X: cast",
                     som_class_spell_label(cls,som_class_spell(cls,pad2_spell_cursor)));
            som_overlay_say(&overlay,message,SOM_MESSAGE_FRAMES/2u);
        }
        if(!(raw&x)&&(previous&x)){
            if(pad2_x_tapped==1)som_cast_spell_by(ram,who,som_class_spell(cls,pad2_spell_cursor));
            pad2_x_tapped=0;
        }
    }else if((pad2_pressed&x)&&!(som_pad2_borrow(who)&&som_class_x(ram))&&who>0u)
        pad2_combo_actor=who;
}

static void som_pad2_guard_finish(void){
    if(!pad2_guard_hp)return;
    uint8_t *ram=som_field_ram();
    if(!ram||som_word(ram+0xDC)!=pad2_guard_map||som_pad2_member(ram)!=pad2_actor)return;
    uint8_t *hero=ram+0xE000u+0x200u*pad2_actor;
    const unsigned hp=som_word(hero+0x182);
    if(hp&&hp<pad2_guard_hp){
        som_put_word(hero+0x182,(uint16_t)(hp+(pad2_guard_hp-hp+1u)/2u));
        som_guard_note(2u,pad2_guard_hp-hp,(pad2_guard_hp-hp+1u)/2u);
    }
}

/* After the swap, buttons already held are not new presses. */
static void som_lead_update(void){
    const uint8_t *ram=on_turbo?som_live_wram():NULL;
    const int want=ram&&!(ram[0xD9]&7u)&&(ram[0xDA]&7u);
    if(want==lead_swapped)return;
    lead_swapped=want;
    last_physical_buttons=0;
    for(unsigned key=0;physical_state&&key<16u;key++)
        if(physical_state(0,RETRO_DEVICE_JOYPAD,0,key))
            last_physical_buttons|=(uint16_t)(1u<<key);
    pad2_raw=som_pad2_buttons();
    start_ring_held=0;switch_holding=0;
    fprintf(stderr,"[som-ai] LEAD pad %d\n",want?2:1);
}
static int16_t som_input_state(unsigned port,unsigned device,unsigned index,unsigned id){
    if(lead_swapped&&port<2u)port=1u-port;
    int16_t actual=physical_state?physical_state(port,device,index,id):0;
    int bitmask=id==RETRO_DEVICE_ID_JOYPAD_MASK;
    if(port==1&&index==0&&(device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD){
        if(on_turbo){
            uint16_t buttons=som_manual_disciplines||menu_hold_until_release?0u:pad2_wanted;
            if(pad2_actor<SOM_PARTY_MEMBERS&&som_actor==pad2_actor){
                if(hundred_until)buttons=(uint16_t)(((frame_number/4ul)&1ul)?
                    1u<<RETRO_DEVICE_ID_JOYPAD_B:0u);
                else if(bash_stage)buttons=(uint16_t)(bash_stage==SOM_BASH_LUNGE?
                    1u<<RETRO_DEVICE_ID_JOYPAD_A:bash_stage==SOM_BASH_STRIKE?
                    1u<<RETRO_DEVICE_ID_JOYPAD_B:0u);
                else if(special_stage)buttons=1u<<RETRO_DEVICE_ID_JOYPAD_B;
                else if(special_suppress)buttons=0;
                else if(evade_active)buttons=0;
            }
            return bitmask?(int16_t)buttons:(id<16u&&(buttons&(1u<<id))?1:0);
        }
        if(net_pad.fd>=0&&som_net_pad_active(&net_pad,som_now_ms())){
            const uint16_t remote=som_net_pad_buttons(&net_pad,som_now_ms());
            return bitmask?(int16_t)remote:(id<16u&&(remote&(1u<<id))?1:0);
        }
    }
    if((ring_ai_enabled||som_manual_disciplines||menu_hold_until_release)&&port==0&&
       (device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0)
        actual=0;                    /* the player's buttons drive the gambit menu */
    /* Bash: the game sees only A (the run), then nothing, then B (swing). */
    const int p2_acts=pad2_actor<SOM_PARTY_MEMBERS&&som_actor==pad2_actor;
    if(hundred_until&&!p2_acts&&port==0&&(device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0){
        const uint16_t want=(uint16_t)(((frame_number/4ul)&1ul)?1u<<RETRO_DEVICE_ID_JOYPAD_B:0u);
        if(bitmask)return (int16_t)want;
        return id<16u&&(want&(1u<<id))?1:0;
    }
    if(bash_stage&&!p2_acts&&port==0&&(device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0){
        const uint16_t want=(uint16_t)(bash_stage==SOM_BASH_LUNGE?1u<<RETRO_DEVICE_ID_JOYPAD_A:
                                       bash_stage==SOM_BASH_STRIKE?1u<<RETRO_DEVICE_ID_JOYPAD_B:0u);
        if(bitmask)return (int16_t)want;
        return id<16u&&(want&(1u<<id))?1:0;
    }
    /* With a discipline, Y attacks with its second weapon: the game sees B
     * (so holding Y charges) and never the physical Y. */
    if((special_stage||special_suppress)&&!(pad2_actor<SOM_PARTY_MEMBERS&&som_actor==pad2_actor)&&port==0&&
       (device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0){
        if(special_stage){
            if(bitmask)return (int16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);
            return id==RETRO_DEVICE_ID_JOYPAD_B?1:0;
        }
        return 0; /* consume held combo after automatic release */
    }
    if((evade_a_hidden||evade_active)&&!(pad2_actor<SOM_PARTY_MEMBERS&&som_actor==pad2_actor)&&port==0&&
       (device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0){
        /* While it plays the D-pad is hidden too: a held direction made the
         * game replace the pose with walking (live, 2026-11-14). */
        const uint16_t a=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_A)|(evade_active?
            (1u<<RETRO_DEVICE_ID_JOYPAD_UP)|(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)|
            (1u<<RETRO_DEVICE_ID_JOYPAD_LEFT)|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT)|
            (1u<<RETRO_DEVICE_ID_JOYPAD_B)|(1u<<RETRO_DEVICE_ID_JOYPAD_Y)|
            (1u<<RETRO_DEVICE_ID_JOYPAD_X):0u));
        if(bitmask)actual=(int16_t)(actual&~a);
        else if(id<16u&&(a&(1u<<id)))actual=0;
    }
    if(lock_orbiting&&lock_who==som_pad1_member()&&port==0&&
       (device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0){
        const uint16_t dpad=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_UP)|
            (1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)|(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT)|
            (1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));
        if(bitmask)actual=(int16_t)(((uint16_t)actual&~dpad)|lock_dpad_out);
        else if(id<16u&&(dpad&(1u<<id)))actual=(lock_dpad_out>>id)&1u;
    }
    if((seal_mode||throw_active)&&port==0&&(device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0){
        const uint16_t dpad=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_UP)|
            (1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)|(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT)|
            (1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));
        if(bitmask)actual=(int16_t)(actual&~dpad);
        else if(id<16u&&(dpad&(1u<<id)))actual=0;
    }
    if((y_weapon_blocked||b_weapon_blocked||x_spell_blocked)&&port==0&&
       (device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0){
        const uint16_t hidden=(uint16_t)(
            (y_weapon_blocked?1u<<RETRO_DEVICE_ID_JOYPAD_Y:0u)|
            (b_weapon_blocked?1u<<RETRO_DEVICE_ID_JOYPAD_B:0u)|
            (x_spell_blocked?1u<<RETRO_DEVICE_ID_JOYPAD_X:0u));
        if(bitmask)actual=(int16_t)(actual&~hidden);
        else if(id<16u&&(hidden&(1u<<id)))actual=0;
    }
    if(x_weapon_swap&&port==0&&(device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0){
        if(bitmask)actual=(int16_t)((actual&~(1<<RETRO_DEVICE_ID_JOYPAD_X))|
                                     (1<<RETRO_DEVICE_ID_JOYPAD_B));
        else if(id==RETRO_DEVICE_ID_JOYPAD_X)actual=0;
        else if(id==RETRO_DEVICE_ID_JOYPAD_B)actual=1;
    }
    if(y_weapon_swap&&port==0&&(device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0){
        if(bitmask)actual=(int16_t)((actual&~(1<<RETRO_DEVICE_ID_JOYPAD_Y))|
                                     (1<<RETRO_DEVICE_ID_JOYPAD_B));
        else if(id==RETRO_DEVICE_ID_JOYPAD_Y)actual=0;
        else if(id==RETRO_DEVICE_ID_JOYPAD_B)actual=1;
    }
    if(port==0&&(device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0&&on_turbo){
        uint16_t hidden=ring_hidden;
        if(!ring_ai_enabled&&!som_manual_disciplines&&som_overlay_field())
            hidden|=som_manual_pad_hidden(last_physical_buttons);
        if(bitmask)actual=(int16_t)((uint16_t)actual&~hidden);
        else if(id<16u&&(hidden&(1u<<id)))actual=0;
        if(!ring_ai_enabled&&!som_manual_disciplines&&som_overlay_field()&&
           (last_physical_buttons&(1u<<RETRO_DEVICE_ID_JOYPAD_R2))&&
           !(last_physical_buttons&(1u<<RETRO_DEVICE_ID_JOYPAD_L2))){   /* the block pose */
            if(bitmask)actual=(int16_t)(actual|(1<<RETRO_DEVICE_ID_JOYPAD_R));
            else if(id==RETRO_DEVICE_ID_JOYPAD_R)actual=1;
        }
    }
    if((start_ring_held||frame_number<start_ring_until)&&port==0&&
       (device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0){
        const uint16_t start=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_R3);
        const uint16_t y=frame_number<start_ring_until?(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y):0u;
        if(bitmask)actual=(int16_t)(((uint16_t)actual&~start)|y);
        else if(id==RETRO_DEVICE_ID_JOYPAD_R3)actual=0;
        else if(id==RETRO_DEVICE_ID_JOYPAD_Y&&y)actual=1;
    }
    if(port==0 && (device & RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD &&
       index==0 && (id<16||bitmask) && armed && !cancelled && !completed && on_turbo){
        if(actual){cancelled=1;armed=0;autonomous_mode=0;fprintf(stderr,"[som-ai] cancelled: physical input at frame %lu\n",frame_number);return actual;}
        uint16_t wanted=active_action==SOM_ACT_BLOCK?0u:som_action_mask(active_action);
        if(frame_number>=start_frame &&
           frame_number<start_frame+action_frames && wanted &&
           (bitmask||(id<16 && (wanted & (uint16_t)(1u<<id))))){
            injected_queries++;
            return bitmask?(int16_t)wanted:1;
        }
    }
    return actual;
}
static void som_record_presses(uint16_t pressed){
    static const char *const names[16]={"confirm","open_ring","select","start",
        "step_up","step_down","step_left","step_right","cancel","menu_x",
        "prev_ring","next_ring","l2","r2","l3","r3"};
    if(!pressed)return;
    FILE *log=fopen(record_file,"a");
    if(!log)return;
    for(unsigned key=0;key<16;key++)
        if(pressed&(1u<<key))fprintf(log,"%s\n",names[key]);
    fclose(log);
}
static void som_input_poll(void){
    if(physical_poll)physical_poll();
    som_pads_frame();
    som_lead_update();
    som_actor_update();
    som_net_pad_frame();
    if(on_turbo)som_pad2_poll();
    /* Observe physical attack edges without mutating the game's inputs. */
    if(physical_state && on_turbo){
        uint16_t current=0;
        for(unsigned key=0;key<16;key++)
            if(physical_state(0,RETRO_DEVICE_JOYPAD,0,key))current|=(uint16_t)(1u<<key);
        const uint16_t attack=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);
        const uint16_t secondary=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);
        const uint16_t l2=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L2);
        const uint16_t r2=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_R2);
        const int borrowed=pad2_actor<SOM_PARTY_MEMBERS&&som_actor==pad2_actor;
        if(special_stage&&!borrowed&&current&&(current&~special_chord)){
            fprintf(stderr,"[som-ai] SPECIAL cancelled by manual input\n");
            special_stage=0;special_suppress=0;
            kusarigama_attack_active=kusarigama_attack_released=kusarigama_swing_seen=0;
        }
        if(special_suppress&&!((borrowed?pad2_raw:current)&special_chord))special_suppress=0;
        combo_input_held=(current&(attack|secondary))!=0;
        if(((current&attack)&&!(last_physical_buttons&attack))||
           ((current&secondary)&&!(last_physical_buttons&secondary)&&!(current&l2))){
            /* Modifier specials have their own charge, not basic hits. */
            if(!(current&(r2|l2|(1u<<RETRO_DEVICE_ID_JOYPAD_L3)))&&
               !som_manual_disciplines&&!ring_ai_enabled&&!bash_stage)
                som_combo_swing((current&secondary)&&!(last_physical_buttons&secondary)?1u:0u);
            combo_hold_start=frame_number;combo_hold_tried=0;
        }
        const uint16_t pressed=(uint16_t)(current&~last_physical_buttons);
        const uint16_t y_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);
        const uint16_t l2_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L2);
        const uint16_t b_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);
        uint8_t *attack_ram=ring_ai_enabled||som_manual_disciplines?NULL:som_field_ram();
        const int field=attack_ram&&!borrowed&&som_actor_class()<SOM_DISCIPLINE_COUNT;
        /* Seals: with L2 held, D-pad signs become seals; B/Y charges.
         * R2 remains the independent physical guard. */
        const uint16_t dpad=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_UP)|
            (1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)|(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT)|
            (1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));
        if(field&&(current&l2_bit)&&!(current&r2)&&!ring_ai_enabled&&
           !som_manual_disciplines&&!special_stage&&
           som_actor_class()<SOM_DISCIPLINE_COUNT){
            seal_mode=1;
            if((pressed&dpad)&&seal_count<SOM_SEALS_MAX){
                static const unsigned dir_ids[4]={RETRO_DEVICE_ID_JOYPAD_UP,
                    RETRO_DEVICE_ID_JOYPAD_DOWN,RETRO_DEVICE_ID_JOYPAD_LEFT,
                    RETRO_DEVICE_ID_JOYPAD_RIGHT};
                unsigned sign=0;
                for(unsigned d=0;d<4u;d++)if(pressed&(1u<<dir_ids[d])){sign=d;break;}
                seal_count++;
                char text[64];
                snprintf(text,sizeof(text),"^%s seal|level %u: B or Y",
                         som_seal_names[sign],seal_count);
                som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES);
            }
        }else{seal_mode=0;seal_count=0;}
        /* L2+attack is handled before ordinary B/Y weapon switching. */
        if(field&&!special_stage&&!special_suppress&&!ring_ai_enabled&&
           (current&l2_bit)&&(pressed&(y_bit|b_bit))&&!(current&r2)){
            unsigned which=(pressed&y_bit)?1u:0u;
            unsigned stage=som_seal_level(seal_count,which);
            seal_count=0;
            if(som_special_start(which,stage)){
                special_chord=(uint16_t)(l2_bit|(which?y_bit:b_bit));
                special_suppress=1;
            }
        }
        y_weapon_swap=0;y_weapon_blocked=0;b_weapon_blocked=0;x_spell_blocked=0;x_weapon_swap=0;
        uint8_t *wram=field?attack_ram:NULL;
        /* what everybody can do, class or not: target and evade */
        uint8_t *basic=attack_ram&&!borrowed?attack_ram:NULL;
        /* L1 is lock-on only. R1 tap cycles next style. L3+B retains the
         * previous L1+B modifier. The former L3 ring chords are consumed. */
        const uint16_t l1_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L);
        const uint16_t l3_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L3);
        const uint16_t r1_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_R);
        const uint16_t x_chord_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);
        const int retired_ring_chord=(current&l3_bit)&&(current&(x_chord_bit|y_bit));
        style_b_hidden=field&&(current&(l3_bit|b_bit))==(l3_bit|b_bit)&&
            !ring_ai_enabled&&!som_manual_disciplines&&
            som_actor_class()<SOM_DISCIPLINE_COUNT;
        /* L2 + R1 / L2 + L1: the next / previous class (player, 2026-11-20). */
        const uint16_t class_shoulders=(uint16_t)(current&(l1_bit|r1_bit));
        if(!ring_ai_enabled&&!som_manual_disciplines&&som_overlay_field()&&
           (current&l2_bit)&&!(current&r2)&&class_shoulders&&
           (pressed&(l2_bit|class_shoulders))&&!special_stage&&!bash_stage)
            som_class_cycle((class_shoulders&r1_bit)?1:-1);
        else if(basic&&!ring_ai_enabled&&!som_manual_disciplines&&(pressed&l1_bit)&&!(current&l2_bit))
            som_lock_toggle(basic); /* immediate L1 tap, no B/Y dependency */
        if(wram&&!ring_ai_enabled&&!som_manual_disciplines&&
           (pressed&r1_bit)&&!(current&(l2_bit|r2)))
            som_style_cycle(-1,som_pad1_member());
        if(style_b_hidden&&(pressed&b_bit))som_style_cycle(-1,som_pad1_member());
        if(style_b_hidden)b_weapon_blocked=1;
        /* Samurai's throw: R2 (block) + D-pad away from the facing. */
        if(wram&&som_actor_class()==SOM_DISC_BUSHIDO&&!ring_ai_enabled&&
           (current&r2)&&(pressed&dpad)&&
           som_throw_back_pressed(wram,pressed))
            som_throw_start(wram);
        /* Locked on: the D-pad strafes round the target (not with L2's seals). */
        if(lock_slot<0||lock_who==som_pad1_member())
            som_lock_steer(!(current&l2_bit)?basic:NULL,(uint16_t)(current&dpad));
        /* Ordinary attacks follow the controller binding. The proxy move
         * owner can still be finishing a move on a different party member. */
        const unsigned driver=som_pad1_member_in(attack_ram);
        const unsigned driver_cls=som_disc_member_class(&disciplines,driver);
        const int caster=attack_ram&&som_disc_caster(driver_cls)&&!retired_ring_chord;
        if(wram)som_monk_glove(wram);
        /* B passes through for the mage bolt. Y starts the native spell. */
        const int caster_talk=caster&&(current&b_bit)&&!(current&y_bit)&&
            som_interact_ahead_by(attack_ram,driver);
        if(caster&&!caster_talk&&(current&y_bit)&&!(current&(l2_bit|r2))&&!special_suppress){
            const unsigned spell=som_caster_spell(driver_cls,1u);
            y_weapon_blocked=1;
            if((pressed&y_bit)&&som_cast_spell_by(attack_ram,driver,spell))
                fprintf(stderr,"[som-ai] CASTER %s Y: %s\n",som_party_names[driver],
                        som_class_spell_label(driver_cls,spell));
        }
        if(attack_ram&&driver&&driver_cls<SOM_DISCIPLINE_COUNT&&!caster&&
           !retired_ring_chord&&!style_b_hidden&&(current&(y_bit|b_bit))&&
           !(current&(l2_bit|r2))&&!special_suppress){
            const unsigned which=(current&y_bit)?1u:0u;
            unsigned want=som_disc_weapons[driver_cls][which];
            const SomWeaponStyle *st=which?NULL:som_style_of(&styles,driver_cls,NULL);
            if(st)want=st->weapon;
            const int result=!which&&som_blade_string(driver_cls)&&som_monk_in_string(driver)?SOM_EQUIP_OK:
                som_equip_ally_weapon(attack_ram,driver,want);
            if(result==SOM_EQUIP_OK)y_weapon_swap=which;
            else if(which)y_weapon_blocked=1;
            else if(result==SOM_EQUIP_BUSY)b_weapon_blocked=1;
        }
        if(attack_ram&&!driver&&driver_cls<SOM_DISCIPLINE_COUNT&&!caster&&
           !style_b_hidden&&!retired_ring_chord&&(current&(y_bit|b_bit))&&
           !(current&(l2_bit|r2))&&!special_suppress){
            const unsigned which=(current&y_bit)?1u:0u;
            unsigned want=som_disc_weapons[driver_cls][which];
            /* A style on another weapon (the ninja's Kusarigama is a flail):
             * B takes the style's weapon, and the Taijutsu chain waits. */
            const SomWeaponStyle *b_style=som_style_of(&styles,disciplines.selected,NULL);
            const int style_weapon=!which&&b_style&&b_style->weapon!=want;
            if(style_weapon)want=b_style->weapon;
            const uint8_t *chain_steps;
            /* By hand these classes' B is the string; the AI keeps the chain. */
            const int by_string=som_blade_string(disciplines.selected)&&!style_weapon&&
                !ring_ai_enabled&&!som_manual_disciplines;
            const int chained=!by_string&&som_b_chain(disciplines.selected,&chain_steps)>0;
            if(!which&&!style_weapon&&chained){
                /* Taijutsu: the chain's weapon is chosen on the press and
                 * kept while B stays held (a held B may be charging). */
                if((pressed&b_bit)||taijutsu_want==SOM_NO_WEAPON){
                    taijutsu_want=som_taijutsu_peek();taijutsu_counted=0;
                    /* A power step (the monk's glove levels, a spear
                     * finisher): the proxy charges it and holds B itself. */
                    const unsigned level=(pressed&b_bit)?som_taijutsu_peek_level():0u;
                    if(level&&som_special_start_weapon(taijutsu_want,level,1)){
                        special_chord=b_bit;special_suppress=1;
                        som_taijutsu_advance();taijutsu_counted=1;
                        char text[48];
                        snprintf(text,sizeof(text),"^%s|power %u",som_weapon_types[taijutsu_want],
                                 special_stage);
                        som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/3u);
                        fprintf(stderr,"[som-ai] CHAIN power %s level %u\n",
                                som_weapon_types[taijutsu_want],special_stage);
                    }
                }
                want=taijutsu_want;
            }
            /* Switch on the press, or as soon as the hero is free while the
             * button stays held; the game then sees a fresh press. */
            int result=attack_ram[0xE1E4]==want||(by_string&&!which&&som_monk_in_string(driver))?SOM_EQUIP_OK:
                (pressed&(y_bit|b_bit))?som_equip_weapon(attack_ram,want):
                som_equip_weapon_direct(attack_ram,want);
            if(result==SOM_EQUIP_OK)y_weapon_swap=which;
            else if(which)y_weapon_blocked=1;  /* never the wrong weapon on Y */
            else if(result==SOM_EQUIP_BUSY)b_weapon_blocked=1;
            /* B with a weapon that cannot be switched directly keeps the
             * held weapon rather than leaving the hero unable to attack. */
            /* The swing this press lets through is the chain's: next step. */
            if(!which&&chained&&!style_weapon&&result==SOM_EQUIP_OK&&
               taijutsu_want!=SOM_NO_WEAPON&&!taijutsu_counted){
                som_taijutsu_advance();taijutsu_counted=1;
                fprintf(stderr,"[som-ai] %s %s\n",disciplines.selected==SOM_DISC_NINJUTSU?
                        "TAIJUTSU":"WEAPON FLOW",som_weapon_types[taijutsu_want]);
            }
        }
        /* Martial Arts' and Bushido's B: the Storm string and the Tekken commands
         * (som_monk_chain_press). B to a villager or a chest stays the game's
         * when the monk stands; mid-string (a lunge that ends beside one) B
         * keeps the string. */
        const unsigned string_weapon=som_string_off(driver_cls)?SOM_NO_WEAPON:som_string_weapon(driver_cls);
        if(attack_ram&&string_weapon!=SOM_NO_WEAPON&&!ring_ai_enabled&&
           !som_manual_disciplines&&!(current&(l2_bit|r2)))
            som_monk_chain_dirs(attack_ram,driver,(uint16_t)(pressed&dpad));
        if(attack_ram&&string_weapon!=SOM_NO_WEAPON&&!ring_ai_enabled&&
           !som_manual_disciplines&&!retired_ring_chord&&!style_b_hidden&&!special_suppress&&
           !(current&(l2_bit|r2|y_bit))&&(current&b_bit)&&
           (attack_ram[0xE000u+0x200u*driver+0x1E4u]==string_weapon||
            (som_blade_string(driver_cls)&&som_monk_in_string(driver)))&&
           (som_monk_in_string(driver)||                     /* mid-string: never talk */
            !som_interact_ahead_by(attack_ram,driver))){
            b_weapon_blocked=1;
            /* A press that waited for the weapon (Y's swing still played)
             * starts the string as the hero takes it, B still held. */
            if((pressed&b_bit)||string_b_waits)
                som_monk_chain_press(attack_ram,driver,(uint16_t)(current&dpad));
            string_b_waits=0;
        }
        else if((pressed&b_bit)&&string_weapon!=SOM_NO_WEAPON&&b_weapon_blocked)string_b_waits=1;
        if(!(current&b_bit))string_b_waits=0;
        if(!(current&b_bit))taijutsu_want=SOM_NO_WEAPON;
        /* X/Triangle casts the selected spell; L2+X keeps the game's X. */
        const uint16_t x_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);
        if(!wram||som_pad1_member_in(wram)!=som_actor||
           (current&(l2_bit|l3_bit|r2))||special_suppress)x_tap_armed=0;
        if((current&x_bit)&&(current&(l2_bit|l3_bit)))x_l2_chord=1;
        /* A: evade / jutsu for the disciplines with a dodge; the game's A
         * dash is gone for them until dash is re-added. Other classes keep
         * the native A. Field play only (menus keep A as cancel). */
        const uint16_t a_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_A);
        evade_a_hidden=basic&&!ring_ai_enabled&&!som_manual_disciplines&&
            som_disc_has_dodge(som_actor_class());
        if(evade_a_hidden&&(pressed&a_bit)){
            a_press_frame=frame_number;a_hold_done=0;
            if(x_cast_at&&som_actor_class()==SOM_DISC_NINJUTSU){
                const int bx=(current&(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT))?1:
                             (current&(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT))?-1:0;
                const int by=(current&(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN))?1:
                             (current&(1u<<RETRO_DEVICE_ID_JOYPAD_UP))?-1:0;
                x_cast_at=0;a_hold_done=1;           /* the jutsu becomes the Blink */
                som_blink_dash(wram,bx,by);
            }
            else if(som_actor_class()==0u&&(current&r2)){som_bash_start(wram);a_hold_done=1;}
            /* Samurai: A with a monster in grab reach grabs and throws it
             * (no Kawarimi: a ninja skill); otherwise A is the evade. */
            else if(som_actor_class()==SOM_DISC_BUSHIDO&&
                    som_bun_toucher(wram,(int)som_bun_u16(SOM_ACT(wram)+0x002),(int)som_bun_u16(SOM_ACT(wram)+0x004),
                                    SOM_BUN_HERO_TOUCH_X+8,SOM_BUN_HERO_TOUCH_Y+8,familiar.slot)>=0&&
                    som_throw_start(wram)){
                a_hold_done=1;
                fprintf(stderr,"[som-ai] THROW from A\n");
            }
            else{
                evade_dir_x=(current&(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT))?1:
                            (current&(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT))?-1:0;
                evade_dir_y=(current&(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN))?1:
                            (current&(1u<<RETRO_DEVICE_ID_JOYPAD_UP))?-1:0;
                som_evade_start(basic);
                evade_dir_x=evade_dir_y=0;
            }
        }
        /* Hold A: Kawarimi no Jutsu, armed once the hop is over. */
        if(!(current&a_bit))a_hold_done=1;
        if(evade_a_hidden&&(current&a_bit)&&!a_hold_done&&
           frame_number-a_press_frame>=SOM_A_HOLD_FRAMES){
            a_hold_done=1;a_kawarimi_pending=1;a_kawarimi_since=frame_number;
        }
        if(a_kawarimi_pending){
            if(!wram||ring_ai_enabled||kawarimi_armed_until||
               frame_number-a_kawarimi_since>SOM_A_HOLD_WAIT)a_kawarimi_pending=0;
            else if(som_actor_class()!=SOM_DISC_NINJUTSU)a_kawarimi_pending=0;
            else if(som_kawarimi_arm(wram)){
                a_kawarimi_pending=0;
                fprintf(stderr,"[som-ai] KAWARIMI from held A\n");
            }
        }
        /* The Ninja's heavy Kusarigama keeps the whip for normal B taps.
         * Hold B to charge the sword's native level-2 move, then mask only
         * its weapon art with the matched whip frames from the local ROM. */
        if(wram&&(current&b_bit)&&!(current&y_bit)&&!special_stage&&!special_suppress&&
           !ring_ai_enabled&&!som_manual_disciplines&&!bash_stage&&!combo_hold_tried&&
           som_actor_class()==SOM_DISC_NINJUTSU&&
           styles.selected[SOM_DISC_NINJUTSU]==SOM_STYLE_HEAVY&&
           (SOM_ACT(wram)[0x1C0]&0x0Fu)>=2u&&som_kusarigama_art_ready()&&
           frame_number-combo_hold_start>=SOM_COMBO_HOLD_FRAMES&&
           !SOM_ACT(wram)[0x1ED]&&!SOM_ACT(wram)[0x19B]&&!SOM_ACT(wram)[0x01C]){
            combo_hold_tried=1;
            if(som_special_start_weapon(1u,2u,0)){
                kusarigama_attack_active=1;kusarigama_attack_released=0;
                kusarigama_swing_seen=0;kusarigama_attack_who=som_actor;
                kusarigama_attack_started=frame_number;
                special_chord=b_bit;special_suppress=1;
                som_overlay_say(&overlay,"^Kusarigama|level 2 whip strike",SOM_MESSAGE_FRAMES/2u);
                fprintf(stderr,"[som-ai] KUSARIGAMA level 2 sword attack with whip art\n");
            }
        }
        /* Combo finisher: hold B or Y with a chain level ready. That button's
         * weapon charges straight to the level (capped at what it learned)
         * once the gauge is back, then fires as the game's own power attack. */
        if(wram&&combo_input_held&&!(current&(l2_bit|r2|l3_bit))&&
           !special_stage&&!special_suppress&&
           !ring_ai_enabled&&!som_manual_disciplines&&!bash_stage&&!combo_hold_tried&&
           som_actor_class()<SOM_DISCIPLINE_COUNT&&!som_disc_caster(som_actor_class())&&
           som_combo_effective_level()&&
           frame_number-combo_hold_start>=SOM_COMBO_HOLD_FRAMES&&
           !SOM_ACT(wram)[0x1ED]&&!SOM_ACT(wram)[0x19B]&&!SOM_ACT(wram)[0x01C]){
            const unsigned which=(current&y_bit)?1u:0u;
            const unsigned level=som_combo_effective_level(),hits=combo.hits;
            combo_hold_tried=1;
            if(som_special_start(which,level)){
                special_chord=which?y_bit:b_bit;special_suppress=1;
                char text[64];
                snprintf(text,sizeof(text),"^%u-hit combo|level %u finisher",hits,level);
                som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/2u);
                fprintf(stderr,"[som-ai] COMBO finisher: %u hits, level %u, %s\n",
                        hits,level,which?"Y":"B");
            }
        }
        const unsigned x_role=som_actor_class()<SOM_DISCIPLINE_COUNT?
            som_disc_x_role(som_actor_class()):SOM_X_SPELLS;
        uint8_t *xram=wram&&som_pad1_member_in(wram)==som_actor?wram:NULL;
        if(x_role==SOM_X_THIRD_WEAPON){
            /* Weapon Flow: X equips the third weapon and attacks with it
             * (the game sees B, so holding X charges it). */
            if(xram&&(current&x_bit)&&!(current&(l2_bit|r2))&&!retired_ring_chord&&!special_suppress){
                const unsigned want=som_disc_third_weapon(som_actor_class());
                const int result=SOM_ACT(xram)[0x1E4]==want?SOM_EQUIP_OK:
                    som_actor?som_equip_ally_weapon(xram,som_actor,want):
                    (pressed&x_bit)?som_equip_weapon(xram,want):som_equip_weapon_direct(xram,want);
                if(result==SOM_EQUIP_OK)x_weapon_swap=1;else x_spell_blocked=1;
            }
        }else if(x_role==SOM_X_FLYING_KICK){
            /* Martial Arts: X is the flying kick (the game never sees X). */
            if(xram&&(current&x_bit)&&!(current&(l2_bit|r2))&&!retired_ring_chord&&!special_suppress){
                x_spell_blocked=1;
                if(pressed&x_bit)som_flykick_start(xram);
            }
        }else if(x_role>=SOM_X_HOLY_WATER){
            /* Hunter, Necromancer, Artificer, Mentalist: X is the class move.
             * Artificer: a tap (on release) is the bomb, a hold a portal. */
            const int artificer=som_actor_class()==SOM_DISC_ARTIFICER;
            if(xram&&(current&x_bit)&&!(current&(l2_bit|r2))&&!retired_ring_chord&&!special_suppress){
                x_spell_blocked=1;
                if(!artificer){if(pressed&x_bit)som_class_x(xram);}
                else if(pressed&x_bit){x_press_frame=frame_number;portal_x_used=0;x_tap_armed=1;}
                else if(x_tap_armed&&!portal_x_used&&frame_number-x_press_frame>=SOM_PORTAL_HOLD_FRAMES){
                    portal_x_used=1;som_portal_place(xram);
                }
            }else if(artificer&&x_tap_armed&&xram&&(last_physical_buttons&x_bit)&&!(current&x_bit)&&
                     !(current&(l2_bit|r2))&&!retired_ring_chord&&!special_suppress&&!x_l2_chord){
                if(!portal_x_used)som_class_x(xram);
                portal_x_used=0;
            }
        }else if(x_role==SOM_X_IAI){
            /* Bushido: release X for Iaijutsu, katana charge level 3; held
             * SOM_X_HOLD_FRAMES or longer, level 4. No spells. */
            if(xram&&(current&x_bit)&&!(current&(l2_bit|r2))&&!retired_ring_chord&&!special_suppress){
                x_spell_blocked=1;
                if(pressed&x_bit){x_press_frame=frame_number;x_cycled=0;x_tap_armed=1;}
                else if(x_tap_armed&&!x_cycled&&frame_number-x_press_frame>=SOM_X_HOLD_FRAMES){
                    x_cycled=1;
                    som_overlay_say(&overlay,"^Iaijutsu|level 4: release X",SOM_MESSAGE_FRAMES/2u);
                }
            }else if(x_tap_armed&&xram&&(last_physical_buttons&x_bit)&&!(current&x_bit)&&
                     !(current&(l2_bit|r2))&&!retired_ring_chord&&!special_suppress&&!x_l2_chord){
                som_iai_start(x_cycled?SOM_IAI_HELD_LEVEL:SOM_IAI_LEVEL);
                x_cycled=0;
            }
        }else
        /* Tap X: cast on release. Hold X: next spell every 0.5 s. */
        if(xram&&(current&x_bit)&&!(current&(l2_bit|r2))&&!retired_ring_chord&&!special_suppress){
            x_spell_blocked=1;
            if(pressed&x_bit){x_press_frame=frame_number;x_cycled=0;x_tap_armed=1;}
            else if(x_tap_armed&&frame_number-x_press_frame>=SOM_X_HOLD_FRAMES){
                som_next_spell();x_cycled=1;x_press_frame=frame_number;
                char text[64];
                snprintf(text,sizeof(text),"^%s|tap X: cast",
                         som_class_spell_label(som_actor_class(),som_current_spell()));
                som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES);
            }
        }else if(x_tap_armed&&xram&&(last_physical_buttons&x_bit)&&!(current&x_bit)&&
                 !(current&(l2_bit|r2))&&!retired_ring_chord&&!special_suppress&&!x_l2_chord){
            if(!x_cycled){
                if(som_actor_class()==SOM_DISC_NINJUTSU&&evade_a_hidden){
                    x_cast_at=frame_number+SOM_X_A_WINDOW;x_cast_spell=som_current_spell();
                }else som_cast_spell_by(xram,som_actor,som_current_spell());
            }
            x_cycled=0;
        }
        if(x_cast_at&&frame_number>=x_cast_at){
            x_cast_at=0;
            if(xram&&som_actor_class()==SOM_DISC_NINJUTSU)som_cast_spell_by(xram,som_actor,x_cast_spell);
        }
        /* L2+X: Whirlwind, Shadowstep or Volley. */
        if(xram&&!ring_ai_enabled&&!som_manual_disciplines&&
           (som_actor_class()==SOM_DISC_SWORD||som_actor_class()==SOM_DISC_ROGUE||
            som_actor_class()==SOM_DISC_ARCHER||som_actor_class()==SOM_DISC_HUNTER||
            som_actor_class()==SOM_DISC_ARTIFICER)&&
           (current&l2_bit)&&(current&x_bit)&&!(current&r2)&&!special_suppress){
            x_spell_blocked=1;
            if(pressed&x_bit)som_class_move(xram);
        }
        /* Mentalist: L2+X pulls a native unopened chest toward the hero.
         * X without L2 remains Mind Control. */
        if(xram&&!ring_ai_enabled&&!som_manual_disciplines&&
           som_actor_class()==SOM_DISC_PSIONICS&&
           (current&l2_bit)&&(current&x_bit)&&!(current&r2)&&!special_suppress){
            x_spell_blocked=1;
            if(pressed&x_bit)som_telekinesis_cast(xram);
        }
        /* L2+X: Hundred Fists for the monk. */
        if(xram&&!ring_ai_enabled&&!som_manual_disciplines&&
           som_actor_class()==SOM_DISC_MARTIAL&&
           (current&l2_bit)&&(current&x_bit)&&!(current&r2)&&!special_suppress){
            x_spell_blocked=1;
            if(pressed&x_bit)som_hundred_start(xram);
        }
        /* L2+X: Issen for the samurai (the game never sees that X). */
        if(xram&&!ring_ai_enabled&&!som_manual_disciplines&&
           som_actor_class()==SOM_DISC_BUSHIDO&&
           (current&l2_bit)&&(current&x_bit)&&!(current&r2)&&!special_suppress){
            x_spell_blocked=1;
            if(pressed&x_bit)som_issen_start(xram);
        }
        /* L2+X: Shunshin no Jutsu (a clone on each side) for a ninja with a
         * short-range weapon in hand; the game never sees that X. Every
         * other case keeps L2+X reaching the game. */
        if(xram&&!ring_ai_enabled&&!som_manual_disciplines&&
           som_actor_class()==SOM_DISC_NINJUTSU&&som_weapon_short_range(SOM_ACT(xram)[0x1E4])&&
           (current&l2_bit)&&(current&x_bit)&&!(current&r2)&&!special_suppress){
            x_spell_blocked=1;
            if((pressed&x_bit)&&som_shunshin_start(xram))
                fprintf(stderr,"[som-ai] SHUNSHIN from L2+X\n");
        }
        if(!(current&x_bit))x_l2_chord=x_tap_armed=0;
        uint8_t *ring_ram=som_live_wram();
        const uint16_t menu_pressed=pressed;
        ring_hidden=som_ring_open_idle(ring_ram)?1u<<RETRO_DEVICE_ID_JOYPAD_START:0u;
        const int ring_party=!ring_ai_enabled&&!som_manual_disciplines&&!menu_hold_until_release&&
            (pressed&b_bit)&&som_ring_on_classes(ring_ram);
        if(ring_party){
            som_manual_disciplines=1;
            disciplines.open=1;
            disciplines.who=(uint8_t)(ring_ram[0x1808]<SOM_PARTY_MEMBERS?ring_ram[0x1808]:0u);
            {const unsigned cls=som_disc_member_class(&disciplines,disciplines.who);
             disciplines.cursor=som_disc_row_of(&disciplines,cls);disciplines.grab=0;}
            switch_spoiled=1;menu_hold_until_release=1;
            fprintf(stderr,"[som-ai] PARTY menu opened from the ring (%s)\n",
                    som_party_names[disciplines.who]);
        }
        /* The field's R3 press becomes the native ring's Y pulse. */
        const uint16_t class_start=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_R3);
        if(!(current&class_start))start_ring_held=0;
        if(ring_party){
            /* Opened from the ring this frame: that press is not a menu choice. */
        }else if(!ring_ai_enabled && !som_manual_disciplines && som_overlay_field() &&
           (pressed&class_start)){
            start_ring_held=1;start_ring_until=frame_number+SOM_START_RING_FRAMES;
            fprintf(stderr,"[som-ai] R3 opens the ring\n");
        }else if(som_manual_disciplines&&class_guide_open&&menu_pressed){
            const unsigned select=1u<<RETRO_DEVICE_ID_JOYPAD_SELECT;
            const unsigned back=1u<<RETRO_DEVICE_ID_JOYPAD_A;
            const unsigned up=(1u<<RETRO_DEVICE_ID_JOYPAD_UP)|(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT);
            const unsigned down=(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT);
            if(menu_pressed&(select|back))class_guide_open=0;
            else if(menu_pressed&up)
                disciplines.cursor=(disciplines.cursor+SOM_DISCIPLINE_COUNT-1u)%SOM_DISCIPLINE_COUNT;
            else if(menu_pressed&down)
                disciplines.cursor=(disciplines.cursor+1u)%SOM_DISCIPLINE_COUNT;
        }else if(som_manual_disciplines&&!jukebox.open&&!disciplines.grab&&
                 !act_ai&&(menu_pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_SELECT))){
            class_guide_open=1;
        }else if(som_manual_disciplines && menu_pressed &&
                 (menu_pressed&((1u<<RETRO_DEVICE_ID_JOYPAD_L)|(1u<<RETRO_DEVICE_ID_JOYPAD_R)))&&
                 !jukebox.typing){
            /* L1/R1: CLASSES, MUSIC, AI ACTIONS, COMBAT, QUEST. */
            if(!jukebox_ready){som_juke_init(&jukebox);jukebox_ready=1;}
            const unsigned pages=2u+SOM_GAMBIT_PAGES;
            unsigned page=act_ai?2u+gambits.page:jukebox.open?1u:0u;
            page=(page+((menu_pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_R))?1u:pages-1u))%pages;
            disciplines.open=page==0u;jukebox.open=page==1u;act_ai=page>=2u;
            gambits.open=gambits.resting=act_ai;
            if(act_ai){gambits.page=page-2u;gambits.cursor=0;}
            if(jukebox.open)som_juke_refresh();
        }else if(som_manual_disciplines&&menu_pressed&&act_ai){
            const int result=som_gambit_press(&gambits,menu_pressed);
            if(result==SOM_GAMBIT_CLOSED){
                act_ai=0;gambits.resting=0;som_manual_disciplines=0;disciplines.open=0;
            }else som_gambit_result(result);
        }else if(som_manual_disciplines && menu_pressed && jukebox.open){
            char request[160];
            if(!jukebox.typing&&(menu_pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_A))){
                jukebox.open=0;disciplines.open=0;som_manual_disciplines=0;
                fprintf(stderr,"[som-ai] MANUAL DISCIPLINES closed\n");
            }else if(som_juke_press(&jukebox,menu_pressed,request,sizeof(request)))
                som_juke_send(request);
        }else if(som_manual_disciplines && menu_pressed){
            int result=som_disc_press(&disciplines,menu_pressed);
            if(result==SOM_DISC_CLOSED){
                som_manual_disciplines=0;
                fprintf(stderr,"[som-ai] MANUAL DISCIPLINES closed\n");
            }else if(result==SOM_DISC_SELECTED){
                som_disc_save();
                som_class_selected();
                fprintf(stderr,"[som-ai] DISCIPLINE selected=%u (saved)\n",
                        (unsigned)som_actor_class());
            }else if(result==SOM_DISC_PARTY_SELECTED)som_party_class_saved();
            else if(result==SOM_DISC_LOCKED)fprintf(stderr,"[som-ai] CLASS locked\n");
            else if(result==SOM_DISC_ORDERED){
                som_disc_save();
                fprintf(stderr,"[som-ai] CLASS ORDER saved\n");
            }
        }
        if(act_ai&&!som_manual_disciplines){act_ai=0;gambits.open=0;gambits.resting=0;}
        if(jukebox.open&&frame_number%30u==0u)som_juke_refresh();
        {   /* A closed menu: hold the pad back from the game until let go. */
            if(menu_was_open&&!som_manual_disciplines)menu_hold_until_release=1;
            menu_was_open=som_manual_disciplines;
            if(menu_hold_until_release&&!som_manual_disciplines&&!current&&!pad2_raw)menu_hold_until_release=0;
        }
        if(recording)som_record_presses(pressed);
        if((pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_R2))&&!ring_ai_enabled)
            fprintf(stderr,"[som-ai] GUARD R2 held\n");
        last_physical_buttons=current;
        if(test_enabled)som_guard_begin();
        if(current||pad2_raw){last_input_frame=frame_number;idle_started=0;}
        const uint16_t ai_switch=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L3);
        if(current&&!(current&~ai_switch)){
            if(!switch_holding){
                switch_holding=1;switch_hold_start=frame_number;
                switch_spoiled=ring_ai_enabled||armed;
            }
        }else if(current){
            switch_spoiled=1;                 /* another button joined in */
        }else if(switch_holding){
            switch_holding=0;
            if(!switch_spoiled&&frame_number-switch_hold_start>=SOM_SWITCH_HOLD_FRAMES){
                if(som_write_ring_ai_marker()){
                    ring_ai_enabled=1;ring_ai_grace_until=frame_number+30;
                    idle_started=1;
                    som_overlay_say(&overlay,"AI ON|any button: gambits",SOM_MESSAGE_FRAMES);
                    fprintf(stderr,"[som-ai] SWITCH-HOLD AI START signal ready at %s\n",
                        ring_ai_file);
                }else fprintf(stderr,"[som-ai] SWITCH-HOLD AI start signal failed\n");
            }
        }
        /* The bridge removes the marker when it stops (safety stop, game
         * exit): never keep swallowing the player's buttons after that. */
        if(!direct_transport&&ring_ai_enabled&&frame_number%30u==0&&access(ring_ai_file,F_OK)!=0){
            ally_order.until=0;
            ring_ai_enabled=0;gambits.open=0;disciplines.open=0;
            som_overlay_say(&overlay,"AI stopped|you play",SOM_MESSAGE_FRAMES);
            fprintf(stderr,"[som-ai] AI marker gone: controls back to the player\n");
        }
        const uint16_t gambit_buttons=(uint16_t)(pressed|pad2_pressed);
        /* Either pad: the pad order shifts, so "player 2's" pad can be slot 1. */
        const uint16_t quit_chord=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_START)|
                                             (1u<<RETRO_DEVICE_ID_JOYPAD_L2));
        const int p1_quit=(current&quit_chord)==quit_chord&&(pressed&quit_chord);
        const int p2_quit=(pad2_raw&quit_chord)==quit_chord&&(pad2_pressed&quit_chord);
        if(ring_ai_enabled&&(p1_quit||p2_quit)&&frame_number>=ring_ai_grace_until){
            /* A player deliberately holds L2 and presses Start to exit AI;
             * Start alone stays swallowed and cannot interrupt a session. */
            ally_order.until=0;
            ring_ai_enabled=0;gambits.open=0;disciplines.open=0;
            som_manual_disciplines=0;jukebox.open=0;
            unlink(ring_ai_file);
            som_overlay_say(&overlay,"You play|L2 + Start stopped AI",SOM_MESSAGE_FRAMES);
            fprintf(stderr,"[som-ai] PLAYER %d L2+START: controls back to the player\n",p1_quit?1:2);
        }else if(ring_ai_enabled&&gambit_buttons&&frame_number>=ring_ai_grace_until){
            /* Shoulder tabs switch the overlay page, never reach the game. */
            const uint16_t tabs=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L)|
                                           (1u<<RETRO_DEVICE_ID_JOYPAD_R));
            if(gambit_buttons&tabs){
                som_gambit_turn_page(&gambits,(gambit_buttons&(1u<<RETRO_DEVICE_ID_JOYPAD_R))!=0);
                gambits.open=1;
            }else som_gambit_result(som_gambit_press(&gambits,gambit_buttons));
        }
        if(autonomous_mode && current && !som_manual_disciplines){
            autonomous_mode=0;armed=0;cancelled=1;
            fprintf(stderr,"[som-ai] AUTO STOP physical controller takeover\n");
        }
    }
    /* Check all physical buttons before injecting input, regardless of which
     * individual IDs the emulator requests in this frame. */
    if(armed&&!cancelled&&!completed&&on_turbo&&physical_state&&!ring_ai_enabled)
        for(unsigned id=0;id<16;id++)
            if(physical_state(0,RETRO_DEVICE_JOYPAD,0,id)){
                cancelled=1;armed=0;autonomous_mode=0;ally_order.until=0;
                fprintf(stderr,"[som-ai] physical controller override on frame %lu\n",frame_number);
                break;
            }
}

/* Candidate RAM offsets are for original SoM, UNVERIFIED on Turbo.
 * Read only and print raw bytes, never treat as validated state. */
static void som_snapshot(void){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size)return;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t size=backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||size<SOM_MENU_SCAN_START+SOM_MENU_SCAN_LENGTH){
        fprintf(stderr,"[som-ai] RAM unavailable or too small (size=%lu)\n",(unsigned long)size);
        return;
    }
    fprintf(stderr,"[som-ai] CANDIDATE frame=%lu size=%lu 7E180B=%02x 7E1846=%02x 7E1847=%02x (unverified Turbo)\n",
        frame_number,(unsigned long)size,ram[0x180b],ram[0x1846],ram[0x1847]);
    if(!som_have_previous_menu){
        memcpy(som_previous_menu,ram+SOM_MENU_SCAN_START,SOM_MENU_SCAN_LENGTH);
        som_have_previous_menu=1;
        fprintf(stderr,"[som-ai] MENU BASELINE frame=%lu range=7E1800..7E19FF; snapshot again after one controlled change\n",frame_number);
        return;
    }
    unsigned changed=0,printed=0;
    for(unsigned i=0;i<SOM_MENU_SCAN_LENGTH;i++){
        uint8_t now=ram[SOM_MENU_SCAN_START+i],before=som_previous_menu[i];
        if(now==before)continue;
        changed++;
        if(printed<48){
            fprintf(stderr,"[som-ai] MENU DIFF 7E%04X %02X->%02X\n",
                SOM_MENU_SCAN_START+i,(unsigned)before,(unsigned)now);
            printed++;
        }
    }
    fprintf(stderr,"[som-ai] MENU DIFF SUMMARY frame=%lu changed=%u printed=%u range=7E1800..7E19FF\n",
        frame_number,changed,printed);
    memcpy(som_previous_menu,ram+SOM_MENU_SCAN_START,SOM_MENU_SCAN_LENGTH);
}

/* Publish one complete raw WRAM image for the read-only SkyLoft observer.
 * Writing a temporary file and renaming it prevents SSH readers from seeing
 * a partially written frame. This never writes to emulator or game memory. */
static int som_export_wram(void){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size)return 0;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t size=backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||size!=SOM_WRAM_SIZE)return 0;
    int output_fd=open(wram_temp_file,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    if(output_fd<0)return 0;
    FILE *output=fdopen(output_fd,"wb");
    if(!output){close(output_fd);unlink(wram_temp_file);return 0;}
    int ok=fwrite(ram,1,SOM_WRAM_SIZE,output)==SOM_WRAM_SIZE;
    /* Readers only need the atomic rename; no fsync, so ten snapshots per
     * second never force flash writes if /tmp is not a RAM disk. */
    if(ok)ok=fflush(output)==0;
    if(fclose(output)!=0)ok=0;
    if(ok)ok=rename(wram_temp_file,wram_file)==0;
    if(!ok)unlink(wram_temp_file);
    return ok;
}

static int som_write_ring_ai_marker(void){
    if(direct_transport)return 1;
    int fd=open(ring_ai_file,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    if(fd<0)return 0;
    static const char enabled[]="enabled\n";
    int ok=write(fd,enabled,sizeof(enabled)-1)==(ssize_t)(sizeof(enabled)-1);
    if(ok)ok=fsync(fd)==0;
    if(close(fd)!=0)ok=0;
    if(!ok)unlink(ring_ai_file);
    return ok;
}

/* The existing Action Grid remains unmodified. Confirming its currently
 * selected 4x4 cell is an otherwise harmless gesture that requests external
 * AI control. Changing cells continues to configure the game's native AI. */
static void som_ring_ai_tick(void){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size)return;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE)return;
    int ring_open=ram[0x007D]==1;
    int action_grid=ring_open&&ram[0xA20F]==4;
    int selector=action_grid&&(ram[0xA212]&0x20)!=0;
    uint8_t cell=ram[0xA136]&0x0F;
    if(selector&&!ring_ai_selector_open)ring_ai_initial_cell=cell;
    if(!selector&&ring_ai_selector_open&&action_grid&&cell==ring_ai_initial_cell){
        ring_ai_pending=1;
        fprintf(stderr,"[som-ai] RING AI requested from unchanged Action Grid cell %u\n",
            (unsigned)cell);
    }
    ring_ai_selector_open=selector;
    if(ring_ai_pending&&!ring_open){
        ring_ai_pending=0;
        if(som_write_ring_ai_marker()){
            ring_ai_enabled=1;ring_ai_grace_until=frame_number+30;
            fprintf(stderr,"[som-ai] RING AI START signal ready at %s\n",ring_ai_file);
        }else fprintf(stderr,"[som-ai] RING AI start signal failed\n");
    }
}
static void som_idle_tick(void){
    if(!test_enabled||!game_loaded||!on_turbo||idle_started||ring_ai_enabled||armed)return;
    if(frame_number-last_input_frame<SOM_IDLE_AUTOSTART_FRAMES)return;
    if(frame_number%SOM_MESSAGE_POLL_FRAMES)return;
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size)return;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE)return;
    if(ram[0x007D]==1)return;            /* ring menu open: retry when closed */
    idle_started=1;
    if(access(idle_disable_file,F_OK)==0)return;
    if(som_write_ring_ai_marker()){
        ring_ai_enabled=1;ring_ai_grace_until=frame_number+30;
        fprintf(stderr,"[som-ai] IDLE AI START after %lu frames without input\n",
            frame_number-last_input_frame);
    }else fprintf(stderr,"[som-ai] IDLE AI start signal failed\n");
}
/* EXPERIMENTAL, the proxy's only game-memory write: copy one live monster
 * object (0x200 bytes, slots 7EE600/E800/EA00 per cidwel/som-decomp) into an
 * empty monster slot, 32 px to the right. A full emulator snapshot is taken
 * first; "undo_spawn" restores it exactly. The game's own spawner ($C0:DE3B)
 * is not called, so engine bookkeeping outside the slot is not updated. */
#define SOM_MONSTER_BASE 0xE600u
#define SOM_OBJECT_SIZE 0x200u
/* What the game's spawner sets up per monster slot, measured live on Turbo
 * 2026-11-14 (native spawns into slots 1 and 3, map 130): a sprite tile
 * buffer at 7E:5000 + slot*0x1000, a 16-colour sprite palette in the CGRAM
 * shadow at 7E:DFA0 + slot*0x20 (sprite palettes 5..7), and object fields
 * that point at them (VRAM tile numbers/addresses, OAM palette attribute
 * +0x8B = 0x2A/0x2C/0x2E, palette address +0xF8). A copy must keep the
 * target slot's own fields and get the source's tiles and colours, or it
 * draws from the wrong slot or not at all. */
#define SOM_MONSTER_GFX 0x5000u
#define SOM_MONSTER_GFX_SIZE 0x1000u
#define SOM_MONSTER_PALETTE 0xDFA0u
#define SOM_MONSTER_PALETTE_SIZE 0x20u
#define SOM_MIND_WORKING_ROW(slot) (0x0700u+32u*(5u+(unsigned)(slot)))  /* CGRAM working copy */
static const uint8_t som_slot_fields[]={0x6C,0x6E,0x86,0x8A,0x92,0x96,0x9A,0x9E,0xA2,0xA6,
    0xAA,0xAE,0xB2,0xB6,0xD2,0xD6,0xDA,0xF8,0xFA};   /* 16-bit fields */
#define SOM_CLONE_WAIT_FRAMES 600ul           /* slots refill ~3 s after a kill */
static unsigned long clone_pending_until;
static int clone_no_slot,last_clone_target=-1;
/* The OAM shadow 7E:0800-0A1F as it was when this frame started: the game
 * builds the next frame's sprites during the frame, so at video time the
 * live shadow is a frame ahead of the picture (and of the hero tiles the
 * NMI uploaded). Clones and trails draw from this copy (headless trace
 * 2026-11-23: the live shadow matched the following frame's picture). */
static uint8_t oam_shown[0x220];
static int oam_shown_valid;
/* Familiar (src/som_familiar.h): a clone bound to fight for the hero. Its
 * monster slot (1..3) is published for the SkyLoft bridge, which must not
 * attack it. */
#define SOM_FAMILIAR_FILE "/tmp/flxtr-som-familiar"
static const char *familiar_file=SOM_FAMILIAR_FILE;
static int familiar_pending;
/* Carried between maps: saved every second while it is with the hero and
 * restored into an empty slot once the new map has settled. */
static SomFamiliarCarry familiar_carry;
static SomFamiliarCarry clone_source_art; /* captured before mutating destination */
static int familiar_is_native_control; /* Mind Control never repaints native art */
/* Necromancy's Raise Dead (player, 2026-11-23: "copy the zombies as thats
 * what Necromancers undead should look like ... depending on what type of
 * creature he revives"): the raised familiar becomes one of the game's own
 * undead. The game re-skins a live monster itself: a new id in +0x180 /
 * +0x1E7 and bit 0 of the request byte +0x1FA make $C1:939C load that id's
 * program, palette ($C8:1000 + id * 30) and art (descriptor $D0:0000 +
 * id * 16, frames streamed from ROM), on any map (som-decomp
 * refresh_actor_graphics_status_and_controller.s, load_actor_map_*.s; the
 * game's own form change, status 0x0800, uses it). Headless 2026-11-23:
 * Zombie 0x12, Beast Zombie 0x35, Ghoul 0x36, Ghost 0x30, Specter 0x09
 * all drew cleanly in a rabite slot (som-decomp numbers monsters in hex). */
#define SOM_UNDEAD_ZOMBIE 0x12u
#define SOM_UNDEAD_BEAST 0x35u
#define SOM_UNDEAD_GHOUL 0x36u
#define SOM_UNDEAD_GHOST 0x30u
#define SOM_UNDEAD_SPECTER 0x09u
static unsigned familiar_undead;          /* 0, or the undead id it wears */
static unsigned som_undead_for(int species){
    switch(species){
    case 0x09:case 0x12:case 0x30:case 0x35:case 0x36:return (unsigned)species;
    /* animals and crawlers: the rotting hound */
    case 0x00:case 0x10:case 0x14:case 0x16:case 0x18:case 0x24:case 0x28:case 0x31:
    case 0x32:case 0x34:case 0x43:case 0x45:case 0x47:case 0x49:case 0x50:
        return SOM_UNDEAD_BEAST;
    /* mushrooms, flowers, drops, pumpkins, vegetables: the ghoul */
    case 0x02:case 0x04:case 0x08:case 0x17:case 0x22:case 0x27:case 0x38:case 0x39:
    case 0x40:case 0x42:
        return SOM_UNDEAD_GHOUL;
    /* flyers: a ghost */
    case 0x01:case 0x13:case 0x15:case 0x29:return SOM_UNDEAD_GHOST;
    /* magic things (books, swords, eyes, imps): a specter */
    case 0x07:case 0x21:case 0x23:case 0x33:case 0x37:case 0x46:case 0x51:
        return SOM_UNDEAD_SPECTER;
    default:return SOM_UNDEAD_ZOMBIE;           /* goblins, thugs, knights, ninjas */
    }
}
static const char *som_undead_name(unsigned id){
    return id==SOM_UNDEAD_BEAST?"Beast Zombie":id==SOM_UNDEAD_GHOUL?"Ghoul":
           id==SOM_UNDEAD_GHOST?"Ghost":id==SOM_UNDEAD_SPECTER?"Specter":"Zombie";
}
/* Turns the familiar in `slot` into its undead form through the game's
 * own refresh (next combat tick). */
static void som_familiar_raise_undead(uint8_t *ram,int slot){
    if(!ram||slot<0||slot>2||!familiar_undead)return;
    uint8_t *o=ram+0xE600u+0x200u*(unsigned)slot;
    o[0x180]=o[0x1E7]=(uint8_t)familiar_undead;
    o[0x011]=0;o[0x01C]=0;o[0x030]=0xFF;
    o[0x1FA]|=0x01u;
    char text[40];snprintf(text,sizeof(text),"^Rise, %s!",som_undead_name(familiar_undead));
    som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] UNDEAD familiar in slot %d rises as %s (id 0x%02X)\n",
            slot+1,som_undead_name(familiar_undead),familiar_undead);
}
static unsigned long familiar_arrived;
#define SOM_FAMILIAR_SETTLE_FRAMES 60ul
static void som_familiar_bind(void);
/* summon_boss: a saved monster (object, its slot's sprite tiles, palette;
 * built on SkyLoft from a BossRecorder capture) comes as a familiar. While
 * SOM_SUMMON_ANYONE_FILE exists anyone may summon (test override for the
 * summoner rule); delete it to restore the rule. */
#define SOM_SUMMON_FILE "/home/apps/retroarch/cores/experimental/som_summon.bin"
#define SOM_SUMMON_ANYONE_FILE "/home/apps/retroarch/cores/experimental/som_ai_summon_anyone"
static const char *summon_file=SOM_SUMMON_FILE;
static const char *summon_anyone_file=SOM_SUMMON_ANYONE_FILE;
static int som_summon_allowed(const uint8_t *ram){
    if(access(summon_anyone_file,F_OK)==0)return 1;
    if(disciplines.selected==SOM_DISC_SUMMONING||disciplines.selected==SOM_DISC_NECRO||
       disciplines.selected==5u)return 1;                  /* Dark Arts: a shadow pact */
    return ram&&som_familiar_summoner(ram+0xE000);
}
static int som_summon_boss(void);
/* Candy without the ring menu. A live trace of a ring Candy use (2026-11-14)
 * showed the menu requesting the heal through the hero's pending-healing
 * byte +0x1F3 (100 = Candy), which the game applies itself on the hero's
 * next update, like pending damage +0x1F1. Inventory bytes at 7E:CC48 are
 * (count*2)<<4 | item id (Candy = 0). Only with 2+ Candies: what the game
 * does with an emptied ring slot is not verified, so the last Candy is
 * left to the ring macro. */
#define SOM_CANDY_HEAL 100u
/* Give an unowned weapon, as the published Pro Action Replay codes do:
 * owned weapons are 7E:CC54 + type (0xFF = not owned; low bits tier, bit 6
 * held by the boy; e.g. 7ECC5548 = Mana Sword). Live 2026-11-14: only the
 * sword (0x40) is owned. Gives tier 0, not equipped; never changes an owned
 * weapon. A full undo snapshot is taken first (undo_spawn). */
static const char *const som_weapon_types[8]={"glove","sword","axe","spear","whip","bow",
    "boomerang","javelin"};
static int som_take_undo(void);
static uint8_t *som_vram(void);
/* Switching weapons without the ring.
 *
 * Why the earlier direct switch garbled the weapon: the game's equip refresh
 * ($C0:EA19 -> $C0:EA41, cidwel/som-decomp) does more than set the hero's
 * weapon type. It also points the hero at that weapon's frame/script tables
 * and expands the weapon's 64 effect tiles from ROM bank $D4 into the hero's
 * WRAM buffer 7E:2000 (boy). The game then streams that buffer into OBJ
 * tiles 0x144+ every frame. Switching without refilling the buffer drew the
 * new weapon's frames with the old weapon's tiles.
 *
 * Everything below is computed from the loaded ROM exactly as $C0:EA41 does
 * (unchanged in Turbo), with Turbo's own converter: Turbo re-encodes bank
 * $D4 as 16 bytes of (a,b) row pairs followed by 8 bytes of c per tile, and
 * its masked converter ($C1:C5D1) writes planes a/b/c & ~(b&c), plane 3 = 0.
 * Verified 2026-11-14 against the live trace: the computed sword buffer
 * equals 7E:2000 byte for byte. At runtime the model must reproduce the
 * game's own buffer and table pointers for the held weapon before any
 * direct switch is allowed (fail closed). */
static const uint8_t *som_rom;
static size_t som_rom_size;
static int weapon_gfx_verified;
static unsigned long weapon_gfx_last_check;
#define SOM_WEAPON_TILES 0x40u
#define SOM_WEAPON_BUF 0x2000u          /* boy's effect-tile buffer */
#define SOM_HERO_EFFECT_BYTES (SOM_WEAPON_TILES*32u)
static const uint8_t *som_rom_at(uint32_t snes,size_t length){
    if(!som_rom||snes<0xC00000u||snes>0xFFFFFFu)return NULL;
    const size_t header=som_rom_size%0x8000u==512u?512u:0u;
    const size_t offset=header+(size_t)(snes-0xC00000u);
    if(offset>som_rom_size||length>som_rom_size-offset)return NULL;
    return som_rom+offset;
}
/* Turbo's plane converters, one per hero ($C1:C599 picks by actor): the
 * boy's masked ($C1:C5D1), the girl's xor ($C1:C731), the sprite's
 * inverted ($C1:C8A9), each into its own buffer 7E:2000 + who*0x1000.
 * Read from the Turbo ROM's code and checked byte for byte against all
 * three live buffers (2026-11-20). */
static int som_expand_effect_tiles_for(uint32_t source,unsigned tiles,uint8_t *out,unsigned who){
    const uint8_t *src=som_rom_at(source,(size_t)tiles*24u);
    if(!src||who>2u||((source&0xFFFFu)+tiles*24u)>0x10000u)return 0;
    for(unsigned t=0;t<tiles;t++){
        const uint8_t *s=src+t*24u;
        uint8_t *o=out+t*32u;
        for(unsigned y=0;y<8u;y++){
            const uint8_t a=s[2u*y],b=s[2u*y+1u],c=s[16u+y];
            const uint8_t keep=(uint8_t)~(b&c);
            if(who==0u){
                o[2u*y]=(uint8_t)(a&keep);o[2u*y+1u]=(uint8_t)(b&keep);
                o[16u+2u*y]=(uint8_t)(c&keep);o[17u+2u*y]=0;
            }else if(who==1u){
                const uint8_t ab=(uint8_t)(a^b);
                o[2u*y]=(uint8_t)~a;o[2u*y+1u]=ab;
                o[16u+2u*y]=(uint8_t)(~c&ab);o[17u+2u*y]=(uint8_t)((a&b)^c);
            }else{
                o[2u*y]=(uint8_t)(a&keep);o[2u*y+1u]=(uint8_t)~b;
                o[16u+2u*y]=(uint8_t)(b^c);o[17u+2u*y]=keep;
            }
        }
    }
    return 1;
}
static int som_expand_effect_tiles(uint32_t source,unsigned tiles,uint8_t *out){
    return som_expand_effect_tiles_for(source,tiles,out,0u);
}
typedef struct {
    const uint8_t *row;                 /* 12-byte weapon row, D0:1000+12*row */
    uint8_t type,graphics;              /* +0x068, D030 (row byte 3) */
    uint16_t f065,f06a,f07e,f02a;
    uint8_t tiles[SOM_HERO_EFFECT_BYTES];
} SomWeaponModel;
/* $C0:EA41 for hero `who` (actor offset who*0x200). */
static int som_weapon_model_for(const uint8_t *hero,unsigned row,SomWeaponModel *m,unsigned who){
    if(!hero||!m||row>=72u||who>2u)return 0;
    const uint8_t *r=som_rom_at(0xD01000u+12u*row,12u);
    if(!r||r[0]>=8u||r[0]!=row/9u)return 0;
    const unsigned graphics=r[3];
    if(graphics>=0xEEu)return 0;        /* Turbo $E4:FE7C: alternate bank E4 */
    const unsigned g=graphics<4u?r[0]:graphics+0x12u;
    const uint8_t *table=som_rom_at(0xD4C000u+4u*g,4u);
    const uint8_t *frames=som_rom_at(0xD0FFD0u+2u*g,2u);
    if(!table||!frames)return 0;
    m->row=r;m->type=r[0];m->graphics=(uint8_t)graphics;
    const uint16_t e077=(uint16_t)(hero[0x077]|hero[0x078]<<8);
    m->f065=(uint16_t)(e077+240u*m->type);
    m->f06a=(uint16_t)(table[2]|table[3]<<8);
    m->f07e=(uint16_t)(0x1040u+(g<8u?g<<8:0u)+(who==2u?0x800u:0u));
    m->f02a=(uint16_t)(frames[0]|frames[1]<<8);
    const uint16_t source=(uint16_t)(24u*(unsigned)(table[0]|table[1]<<8));
    return som_expand_effect_tiles_for(0xD40000u+source,SOM_WEAPON_TILES,m->tiles,who);
}
static int som_weapon_model(const uint8_t *hero,unsigned row,SomWeaponModel *m){
    return som_weapon_model_for(hero,row,m,0u);
}
static uint16_t som_word(const uint8_t *p){return (uint16_t)(p[0]|p[1]<<8);}
static void som_put_word(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
/* Does our model reproduce what the game itself loaded for the held weapon? */
static int som_weapon_gfx_matches(const uint8_t *ram){
    const uint8_t *hero=ram+0xE000u;
    static SomWeaponModel m;
    if(!som_weapon_model(hero,hero[0x1E3],&m)||hero[0x1E4]!=m.type)return 0;
    return hero[0x068]==m.type&&ram[0xD030]==m.graphics&&
        som_word(hero+0x065)==m.f065&&som_word(hero+0x06A)==m.f06a&&
        som_word(hero+0x07E)==m.f07e&&som_word(hero+0x02A)==m.f02a&&
        /* A scaled style blanks the buffer: its saved copy is the game's art. */
        memcmp(weapon_tiles_hidden?weapon_native_tiles:ram+SOM_WEAPON_BUF,m.tiles,
               sizeof(m.tiles))==0;
}
static int som_field_actor_idle(const uint8_t *ram,unsigned who){
    if(!ram||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]||
       ram[0x7D]==1||(ram[0xF1]&0x80))return 0;
    const uint8_t *hero=ram+0xE000u+0x200u*who;
    return hero[0]==1&&!hero[0x1C]&&!hero[0x60]&&!hero[0x19B];
}
static int som_field_idle(const uint8_t *ram){
    return som_field_actor_idle(ram,0u);
}
/* Once per session, while the hero stands in the field. */
static void som_weapon_gfx_verify_frame(const uint8_t *ram){
    if(weapon_gfx_verified||!som_rom||!som_field_idle(ram))return;
    if(weapon_gfx_last_check&&frame_number-weapon_gfx_last_check<30ul)return;
    weapon_gfx_last_check=frame_number;
    if(som_weapon_gfx_matches(ram)){
        weapon_gfx_verified=1;
        fprintf(stderr,"[som-ai] WEAPON art model verified against held %s\n",
                som_weapon_types[ram[0xE1E4]&7u]);
    }
}
/* $CC54+type bits 6-7: who holds that weapon, 0 nobody, 1 the boy, 2 the
 * girl, 3 the sprite (live 2026-11-20: the girl's glove 0x85, the sprite's
 * boomerang 0xC7). One holder per weapon: a class weapon another member
 * holds is shared without taking the mark from them. */
static void som_weapon_hold(uint8_t *owned,unsigned type,unsigned who){
    if(type>=8u||owned[type]==0xFF||(owned[type]&0xC0u))return;
    owned[type]=(uint8_t)(owned[type]|((who+1u)<<6));
}
static void som_weapon_release(uint8_t *owned,unsigned type,unsigned who){
    if(type>=8u||owned[type]==0xFF||((owned[type]>>6)&3u)!=who+1u)return;
    owned[type]&=0x3Fu;
}
/* A row with Str/Agi/Con/Int/Wis bonuses needs the full stat rebuild
 * ($C0:4530); refuse those rather than leave stats half-updated. */
static int som_weapon_row_plain(const uint8_t *r){return r[1]==0&&(r[2]&3u)==0;}
static void som_style_restore_attack(uint8_t *hero);
/* force: re-apply the held weapon's row (its owned tier changed). */
static int som_equip_weapon_row(uint8_t *ram,unsigned type,int force){
    if(!ram||type>=8u)return SOM_EQUIP_REFUSED;
    uint8_t *hero=ram+0xE000u;
    if(hero[0x1E4]==type&&!force)return SOM_EQUIP_OK;
    if(!som_field_idle(ram))return SOM_EQUIP_BUSY;
    uint8_t *owned=ram+0xCC54u;
    const unsigned current=hero[0x1E4];
    if(current>=8u||owned[type]==0xFF||!weapon_gfx_verified)return SOM_EQUIP_REFUSED;
    const unsigned tier=owned[type]&0x0Fu;
    if(tier>8u)return SOM_EQUIP_REFUSED;
    const unsigned row=type*9u+tier;
    const uint8_t *old_row=som_rom_at(0xD01000u+12u*hero[0x1E3],12u);
    static SomWeaponModel m;
    if(!old_row||hero[0x1E3]>=72u||!som_weapon_model(hero,row,&m)||
       !som_weapon_row_plain(old_row)||!som_weapon_row_plain(m.row))
        return SOM_EQUIP_REFUSED;
    const uint8_t *r=m.row;
    som_style_restore_attack(hero);     /* the row difference needs the game's value */
    /* Stats ($C0:4530 for a plain row): power and hit change by the row
     * difference; crit = 3*level + row crit, halved for bow/boomerang/javelin. */
    const uint8_t levels=hero[0x1C0u+type/2u];
    const unsigned level=type%2u?(levels&0x0Fu):(levels>>4);
    int attack=(int)hero[0x198]-(int)old_row[8]+(int)r[8];
    int hit=(int)hero[0x197]-(int)old_row[7]+(int)r[7];
    unsigned crit=3u*level+r[6];
    if(type>4u)crit/=2u;
    hero[0x1E4]=(uint8_t)type;
    hero[0x1E3]=hero[0x1E8]=(uint8_t)row;
    hero[0x198]=(uint8_t)(attack<0?0:attack>255?255:attack);
    hero[0x197]=(uint8_t)(hit<0?0:hit>99?99:hit);
    hero[0x196]=(uint8_t)crit;
    hero[0x19C]=(uint8_t)level;
    hero[0x194]=r[5];hero[0x199]=r[9];hero[0x19A]=r[10];hero[0x1F7]=r[11];
    /* $C0:EA19 / $C0:EA41 */
    hero[0x084]=0;hero[0x00C]=0;
    if(!hero[0x069])hero[0x069]=r[4];   /* weapon palette id: only if unset */
    hero[0x068]=m.type;ram[0xD030]=m.graphics;
    som_put_word(hero+0x065,m.f065);som_put_word(hero+0x06A,m.f06a);
    som_put_word(hero+0x07E,m.f07e);som_put_word(hero+0x02A,m.f02a);
    memcpy(ram+SOM_WEAPON_BUF,m.tiles,sizeof(m.tiles));
    som_weapon_release(owned,current,0u);                     /* no longer held */
    som_weapon_hold(owned,type,0u);                           /* held by the boy */
    fprintf(stderr,"[som-ai] WEAPON equipped without the ring: %s (row %u)\n",
            som_weapon_types[type],row);
    return SOM_EQUIP_OK;
}
static int som_equip_weapon_direct(uint8_t *ram,unsigned type){
    return som_equip_weapon_row(ram,type,0);
}
/* The same switch for a companion (who 1 the girl, 2 the sprite): her own
 * actor fields, effect buffer 7E:2000+who*0x1000 and D030+who*0x40
 * ($C0:EA41 with X = who*0x200), at the tier the party owns. No weapon
 * styles: a companion's attack is the game's row value. */
static int som_ally_idle(const uint8_t *ram,unsigned who){
    const uint8_t *o=ram+0xE000u+0x200u*who;
    return som_field_actor_idle(ram,who)&&(o[0x184]|o[0x185])&&(o[0x182]|o[0x183]);
}
static int som_equip_ally_weapon(uint8_t *ram,unsigned who,unsigned type){
    if(!ram||who<1u||who>2u||type>=8u)return SOM_EQUIP_REFUSED;
    uint8_t *hero=ram+0xE000u+0x200u*who;
    if(hero[0x1E4]==type)return SOM_EQUIP_OK;
    if(!som_ally_idle(ram,who))return SOM_EQUIP_BUSY;
    uint8_t *owned=ram+0xCC54u;
    const unsigned current=hero[0x1E4];
    if(current>=8u||owned[type]==0xFF)return SOM_EQUIP_REFUSED;
    const unsigned tier=owned[type]&0x0Fu;
    if(tier>8u||hero[0x1E3]>=72u)return SOM_EQUIP_REFUSED;
    const unsigned row=type*9u+tier;
    const uint8_t *old_row=som_rom_at(0xD01000u+12u*hero[0x1E3],12u);
    static SomWeaponModel m;
    /* Her model must first rebuild what the game loaded for her (fail closed). */
    if(!old_row||!som_weapon_model_for(hero,hero[0x1E3],&m,who)||
       m.type!=current||hero[0x068]!=m.type||ram[0xD030u+0x40u*who]!=m.graphics||
       som_word(hero+0x07E)!=m.f07e||som_word(hero+0x06A)!=m.f06a||
       memcmp(ram+SOM_WEAPON_BUF+0x1000u*who,m.tiles,sizeof(m.tiles))){
        fprintf(stderr,"[som-ai] ALLY %u weapon art model does not match the game's: refused\n",who);
        return SOM_EQUIP_REFUSED;
    }
    if(!som_weapon_model_for(hero,row,&m,who)||!som_weapon_row_plain(old_row)||
       !som_weapon_row_plain(m.row))return SOM_EQUIP_REFUSED;
    const uint8_t *r=m.row;
    const uint8_t levels=hero[0x1C0u+type/2u];
    const unsigned level=type%2u?(levels&0x0Fu):(levels>>4);
    int attack=(int)hero[0x198]-(int)old_row[8]+(int)r[8];
    int hit=(int)hero[0x197]-(int)old_row[7]+(int)r[7];
    unsigned crit=3u*level+r[6];
    if(type>4u)crit/=2u;
    hero[0x1E4]=(uint8_t)type;
    hero[0x1E3]=hero[0x1E8]=(uint8_t)row;
    hero[0x198]=(uint8_t)(attack<0?0:attack>255?255:attack);
    hero[0x197]=(uint8_t)(hit<0?0:hit>99?99:hit);
    hero[0x196]=(uint8_t)crit;
    hero[0x19C]=(uint8_t)level;
    hero[0x194]=r[5];hero[0x199]=r[9];hero[0x19A]=r[10];hero[0x1F7]=r[11];
    hero[0x084]=0;hero[0x00C]=0;
    if(!hero[0x069])hero[0x069]=r[4];
    hero[0x068]=m.type;ram[0xD030u+0x40u*who]=m.graphics;
    som_put_word(hero+0x065,m.f065);som_put_word(hero+0x06A,m.f06a);
    som_put_word(hero+0x07E,m.f07e);som_put_word(hero+0x02A,m.f02a);
    memcpy(ram+SOM_WEAPON_BUF+0x1000u*who,m.tiles,sizeof(m.tiles));
    som_weapon_release(owned,current,who);
    som_weapon_hold(owned,type,who);
    fprintf(stderr,"[som-ai] ALLY %s equipped %s (row %u)\n",som_party_names[who],
            som_weapon_types[type],row);
    return SOM_EQUIP_OK;
}
static unsigned long equip_notice_frame;
static int som_equip_weapon(uint8_t *ram,unsigned type){
    const int result=som_equip_weapon_direct(ram,type);
    if(result==SOM_EQUIP_REFUSED&&type<8u&&
       (!equip_notice_frame||frame_number-equip_notice_frame>=SOM_MESSAGE_FRAMES)){
        equip_notice_frame=frame_number;
        char message[96];
        snprintf(message,sizeof(message),weapon_gfx_verified?
                 "^%s: select it in the ring|direct switch unsupported":
                 "^%s: weapon art check pending|stand still a moment",som_weapon_types[type]);
        som_overlay_say(&overlay,message,SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] WEAPON switch to %s refused (%s)\n",som_weapon_types[type],
                weapon_gfx_verified?"row needs native rebuild":"art model not verified");
    }
    return result;
}
/* Casting without the ring (EXPERIMENTAL; X/Triangle). Reproduces what the
 * ring menu does when a spell is confirmed (cidwel/som-decomp
 * docs/summon-cast.md section 2, the same steps its validated Sim.cast
 * uses): $C0:731F writes the request into the caster, $C0:001B clears the
 * hit list and stores the magic level, and $C1:88C7 (unchanged in Turbo)
 * starts the cast pose; the game's own animation and effect scripts do the
 * rest, take the MP on the first hit and restore the weapon art ($C0:E9F4)
 * when the hero is released. Not reproduced: the "<spell> Lv." banner, and
 * the spark palette for frames 0-59 (the game loads the cast palette itself
 * at frame 60). */
static unsigned cast_who;                 /* the hero casting now (spell names) */
static const char *som_spell_name(unsigned spell){
    return som_class_spell_label(som_disc_member_class(&disciplines,cast_who),spell);
}
/* 0 Gnome .. 5 Dryad: six spells each; 6 Shade, 7 Lumina: three each. */
static unsigned som_spell_element(unsigned spell){
    return spell<0x24u?spell/6u:spell<0x27u?6u:7u;
}
/* Hostile hero spells target the monster slots 3-5 that are present, on
 * screen and alive ($D0:DA60). Never the familiar (a summoned monster is
 * on our side); the locked-on monster when there is one; otherwise the
 * nearest, with a monster in front of the caster counted at half its
 * distance and one behind her at double, so the spell goes where she
 * looks when two are about as near (player, 2026-11-23). */
static int som_cast_target(const uint8_t *ram,unsigned who){
    const uint8_t *hero=ram+0xE000u+0x200u*who;
    const int hx=(int)som_word(hero+0x20),hy=(int)som_word(hero+0x22);
    const unsigned face=hero[0x10];   /* 0x00 up, 0x01 down, 0x02 right, 0x82 left */
    int best=-1;long best_score=0;
    for(unsigned slot=3;slot<6u;slot++){
        if((int)slot-3==familiar.slot)continue;
        const uint8_t *e=ram+0xE000u+0x200u*slot;
        const int x=(int)som_word(e+0x20),y=(int)som_word(e+0x22);
        if(e[0]!=1||(e[0x191]&0x80u)||x<8||x>=0xF8||y>=0xD8)continue;
        if((int)slot-3==lock_slot)return (int)slot;
        const int dx=x-hx,dy=y-hy;
        const int ahead=face==0x02u?dx:face==0x82u?-dx:face==0x01u?dy:-dy;
        const int side=face==0x02u||face==0x82u?abs(dy):abs(dx);
        long score=(long)(abs(dx)+abs(dy))*2;
        if(ahead>0&&side<=ahead)score/=2;     /* in front, within 45 degrees */
        else if(ahead<0)score*=2;             /* behind */
        if(best<0||score<best_score){best=(int)slot;best_score=score;}
    }
    return best;
}
/* Quick cast (player, 2026-11-22: "a mage casts Fire Ball without the
 * precasting"): a class's B/Y spell skips the pose and spark (frames 0-59)
 * and the spirit's pictures (60 .. spell start), so the spell starts a few
 * frames after the press. The game still does every step itself; the
 * proxy only moves the caster's animation script pointer (+0x16, bank
 * $D1; +0x12 = ticks left of the step) past them (som-decomp
 * summon-cast.md 3, 9: the pose holds on op FD, which starts the spirit
 * with +0x60 = 0x20; the spirit script, +0xFC, ends in FF and then the
 * spell starts). A skip is made only when the ROM bytes parse as those
 * scripts; anything else leaves the ordinary cast running. */
/* Where the cast pose's closing "83 FD" is from ptr on (bank $D1; the 83,
 * restore the colour set, still runs), or 0: only the steps and operations
 * the pose is known to use (8A, A7, 82-87, F9 id p), and FD must follow 83,
 * so a stale pointer into another animation is never moved. */
static unsigned som_cast_pose_fd(unsigned ptr){
    const uint8_t *b=som_rom_at(0xD10000u|ptr,64u);
    if(!b||ptr>0xFFC0u)return 0;
    unsigned last=64u;                          /* the token before this one */
    for(unsigned i=0;i<64u;){
        const uint8_t op=b[i];
        if(op==0xFDu)return last<64u&&b[last]==0x83u?ptr+last:0u;
        last=i;
        if(op<0x80u){i+=2u;continue;}
        if(op==0xF9u){i+=3u;continue;}
        if(op==0x8Au||op==0xA7u||(op>=0x82u&&op<=0x87u)){i++;continue;}
        return 0;
    }
    return 0;
}
/* The FF that ends the spirit script at base (F9 id p, then steps), or 0. */
static unsigned som_cast_spirit_end(unsigned base){
    const uint8_t *b=som_rom_at(0xD10000u|base,64u);
    if(!b||base>0xFFC0u)return 0;
    for(unsigned i=0;i<64u;){
        const uint8_t op=b[i];
        if(op<0x80u){i+=2u;continue;}
        if(op==0xFFu)return base+i;
        if(op==0xF9u){i+=3u;continue;}
        return 0;
    }
    return 0;
}
/* Quick-cast trace (2026-11-22, the skip did not show live and RetroArch
 * keeps no log): each frame of a quick cast appends the caster's cast
 * fields to SOM_QUICK_TRACE, at most SOM_QUICK_TRACE_LINES lines a session. */
#define SOM_QUICK_TRACE "/tmp/flxtr-som-quick-cast.txt"
#define SOM_QUICK_TRACE_LINES 3000u
static unsigned quick_trace_lines;
static const char *quick_trace_file=SOM_QUICK_TRACE;
static void som_quick_trace(unsigned who,const uint8_t *hero,const char *what){
    if(quick_trace_lines>=SOM_QUICK_TRACE_LINES||!quick_trace_file)return;
    FILE *out=fopen(quick_trace_file,"a");
    if(!out)return;
    fprintf(out,"%lu %s ph=%02X 1C=%02X an=%02X p16=%04X t12=%02X pFC=%04X %s\n",frame_number,
            som_party_names[who%SOM_PARTY_MEMBERS],hero[0x60],hero[0x1C],hero[0x11],
            som_word(hero+0x16),hero[0x12],som_word(hero+0xFC),what);
    fclose(out);quick_trace_lines++;
}
static void som_quick_cast_frame(uint8_t *ram){
    if(!ram)return;
    for(unsigned who=0;who<SOM_PARTY_MEMBERS;who++){
        if(!quick_cast_until[who])continue;
        uint8_t *hero=ram+0xE000u+0x200u*who;
        if(frame_number>quick_cast_until[who]||hero[0x1C]!=0x40u||!hero[0x60]){
            som_quick_trace(who,hero,"end");
            quick_cast_until[who]=0;continue;               /* cast over (or never began) */
        }
        const unsigned ptr=som_word(hero+0x16);
        const char *what="";
        if(hero[0x60]==0x20u&&hero[0x11]==0x94u){           /* pose and spark */
            const unsigned fd=som_cast_pose_fd(ptr);
            if(fd&&fd!=ptr){
                hero[0x16]=(uint8_t)fd;hero[0x17]=(uint8_t)(fd>>8);hero[0x12]=0;what="pose skipped";
                fprintf(stderr,"[som-ai] QUICK CAST %s: pose skipped (%04X -> %04X)\n",
                        som_party_names[who],ptr,fd);
            }
        }else if(hero[0x60]==0x80u){                        /* the spirit */
            /* Its pictures run either on the hero's own pointer (+0x16 inside
             * the spirit script) or on the spirit pointer +0xFC itself (the
             * pose waits on FD meanwhile): move whichever is in the script
             * to its FF; the game then starts the spell. */
            const unsigned fc=som_word(hero+0xFC),end=som_cast_spirit_end(fc);
            if(end&&ptr>=fc&&ptr<end){
                hero[0x16]=(uint8_t)end;hero[0x17]=(uint8_t)(end>>8);hero[0x12]=0;
                what="spirit skipped (+0x16)";
            }else if(end&&end!=fc){
                hero[0xFC]=(uint8_t)end;hero[0xFD]=(uint8_t)(end>>8);
                what="spirit skipped (+0xFC)";
            }
            if(*what){
                fprintf(stderr,"[som-ai] QUICK CAST %s: %s\n",som_party_names[who],what);
                som_quick_trace(who,hero,what);
            }
            som_quick_trace(who,hero,"spirit");
            continue;                       /* watched to the end (trace) */
        }
        som_quick_trace(who,hero,what);
    }
}
static int som_cast_spell(uint8_t *ram,unsigned spell){return som_cast_spell_by(ram,0u,spell);}
/* Mage bolt (rule, player 2026-11-22; src/som_mage_bolt.h): every caster
 * (som_disc_caster), boy, girl or sprite, player, AI or the game's ally AI,
 * attacks with one glove punch and a painted ball of her B spell's element.
 * Before the game's frame (after = 0) her glove charge +0x19B is held at 0,
 * so the punch is never a charged one (no kicks); after it (after = 1) a new
 * plain swing (+0x1C 0 -> 0x20) throws the ball, and the balls fly. Damage:
 * 6 + 3/2 hero level (+0x181) + class level, no MP. */
#define SOM_BOLT_BASE_DAMAGE 6u
#define SOM_BOLT_ALLY_GAP 50ul          /* frames between an AI mage's own throws */
#define SOM_BOLT_ALLY_REACH 110         /* px along her facing */
#define SOM_BOLT_ALLY_ACROSS 12         /* px off that line */
#define SOM_GLOVE_JAB 1u                /* glove normal attack 1, the jab (graphics.md 4) */
/* An AI companion mage stays back (keep-back), out of the game's punching
 * range, so she throws from where she stands: a monster in a straight line
 * (up, down, left or right) within the reach, and she turns to it and
 * jabs, the game's own swing (+0x1C 0x20, animation 1, script restart). */
static int som_mage_ally_throw(uint8_t *ram,unsigned who){
    uint8_t *o=ram+0xE000u+0x200u*who;
    if(o[0x2C]||som_pad1_member_in(ram)==who||!som_ally_idle(ram,who)||
       o[0x1ED]||frame_number<mage_ally_next[who])return 0;
    const int x=(int)som_word(o+2),y=(int)som_word(o+4);
    int best=-1;unsigned facing=0;
    for(int slot=0;slot<3;slot++){
        if(slot==familiar.slot)continue;
        const uint8_t *m=ram+SOM_BOLT_MONSTER_BASE+SOM_BOLT_OBJECT*(unsigned)slot;
        if(!som_bolt_monster_alive(m))continue;
        const int dx=(int)som_word(m+2)-x,dy=(int)som_word(m+4)-y;
        int along,across;unsigned f;
        if(abs(dx)>=abs(dy)){along=abs(dx);across=dy;f=dx>=0?0x02u:0x82u;}
        else{along=abs(dy);across=dx;f=dy>=0?0x01u:0x00u;}
        if(along>SOM_BOLT_ALLY_REACH||abs(across)>SOM_BOLT_ALLY_ACROSS)continue;
        if(best<0||along<best){best=along;facing=f;}
    }
    if(best<0)return 0;
    o[0x10]=(uint8_t)facing;o[0x1C]=0x20;o[0x11]=SOM_GLOVE_JAB;o[0x30]=0xFF;
    o[0x06]=o[0x07]=o[0x08]=0;
    mage_ally_next[who]=frame_number+SOM_BOLT_ALLY_GAP;
    return 1;
}
static unsigned som_mage_bolt_damage(const uint8_t *hero,unsigned cls){
    return SOM_BOLT_BASE_DAMAGE+hero[0x181]*3u/2u+
        (cls<SOM_DISCIPLINE_COUNT?disciplines.level[cls]:0u);
}
/* The living hero with the lowest share of her HP (+0x182 of +0x184),
 * or -1 when nobody is hurt (no MP spent on a full party). Revivifier
 * (0x21) only targets a fallen party member. */
static int som_most_hurt_hero(const uint8_t *ram,unsigned spell){
    int best=-1;unsigned long best_share=0;
    if(spell==0x21u){
        for(unsigned k=0;k<SOM_PARTY_MEMBERS;k++){
            const uint8_t *o=ram+0xE000u+0x200u*k;
            if(!som_word(o+0x184)||som_word(o+0x182))continue;
            int pending=0;
            for(unsigned i=0;i<SOM_PARTY_MEMBERS;i++)
                pending|=revivals[i].deadline&&revivals[i].target==k;
            if(!pending)return (int)k;
        }
        return -1;
    }
    for(unsigned k=0;k<SOM_PARTY_MEMBERS;k++){
        const uint8_t *o=ram+0xE000u+0x200u*k;
        const unsigned hp=som_word(o+0x182),max=som_word(o+0x184);
        if(o[0]!=1||!max||!hp||hp>=max)continue;
        const unsigned long share=(unsigned long)hp*1000ul/max;
        if(best<0||share<best_share){best=(int)k;best_share=share;}
    }
    return best;
}
static void som_mage_bolt_frame(uint8_t *ram,int after){
    if(!ram)return;
    const int field=ram[0xE000]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F&&ram[0xD0]==0&&ram[0x7D]!=1;
    const unsigned map=som_word(ram+0xDC)&0x1FFu;
    for(unsigned who=0;who<SOM_PARTY_MEMBERS;who++){
        uint8_t *hero=ram+0xE000u+0x200u*who;
        const unsigned cls=som_disc_member_class(&disciplines,who);
        const int mage=field&&cls<SOM_DISCIPLINE_COUNT&&som_disc_caster(cls)&&
            hero[0]==1&&(hero[0x182]|hero[0x183])&&hero[0x1E4]==0u;
        if(!mage){mage_swing_prev[who]=hero[0x1C];continue;}
        /* No charge, ever (also while B is held): a charged glove is a
         * kick. The kick still came now and then live (2026-11-22). */
        hero[0x19B]=0;
        if(!after)continue;
        if(who)som_mage_ally_throw(ram,who);
        /* One punch, never the kick or the lunge (glove normals 2, 3), on
         * any frame of the swing: the game may pick it after the first. */
        if(hero[0x1C]==0x20u&&(hero[0x11]==2u||hero[0x11]==3u)){
            hero[0x11]=SOM_GLOVE_JAB;hero[0x30]=0xFF;
            fprintf(stderr,"[som-ai] MAGE JAB for %s's kick/lunge\n",som_party_names[who]);
        }
        const int swing=hero[0x1C]==0x20u&&!hero[0x60];
        if(swing&&mage_swing_prev[who]!=0x20u){
            const unsigned spell=som_caster_spell(cls,0u);
            const unsigned element=som_spell_element(spell),damage=som_mage_bolt_damage(hero,cls);
            if(som_bolt_fire(mage_bolts,who,element,(int)som_word(hero+2),(int)som_word(hero+4),
                             hero[0x10],damage,map,frame_number))
                fprintf(stderr,"[som-ai] MAGE BOLT %s %s (%u)\n",som_party_names[who],
                        som_class_spell_label(cls,spell),damage);
        }
        mage_swing_prev[who]=hero[0x1C];
    }
    if(!after)return;
    for(unsigned i=0;i<SOM_BOLTS;i++){
        SomBolt *b=&mage_bolts[i];
        if(!b->active)continue;
        if(!field){b->active=0;continue;}
        const int slot=som_bolt_step(b,ram,map,familiar.slot,frame_number);
        if(slot>=0){
            const uint8_t *m=ram+SOM_BOLT_MONSTER_BASE+SOM_BOLT_OBJECT*(unsigned)slot;
            if(b->who==0u&&disciplines.selected<SOM_DISCIPLINE_COUNT)
                som_xp_give(som_xp_spell_discipline(disciplines.selected),SOM_XP_SPELL,
                            m[0x180],SOM_XPK_SPELL,"mage bolt hit");
        }
    }
}
/* Turbo can charge for the quick-triggered cast without raising its
 * target. Finish revival only when that cast paid MP and left its pose. */
static void som_revival_frame(uint8_t *ram){
    if(!ram)return;
    for(unsigned who=0;who<SOM_PARTY_MEMBERS;who++){
        SomRevival *revival=&revivals[who];
        if(!revival->deadline)continue;
        uint8_t *caster=ram+0xE000u+0x200u*who;
        uint8_t *target=ram+0xE000u+0x200u*revival->target;
        const unsigned max_hp=som_word(target+0x184);
        if(frame_number>revival->deadline||som_word(ram+0xDC)!=revival->map||
           caster[0]!=1||!som_word(caster+0x182)||!max_hp||som_word(target+0x182)){
            revival->deadline=0;
            continue;
        }
        if(caster[0x170]==0x21u&&caster[0x186]+revival->cost<=revival->mp)
            revival->paid=1;
        if(caster[0x60])continue;
        revival->deadline=0;
        if(!revival->paid)continue;
        target[0x191]&=0x7Fu;
        som_put_word(target+0x182,1u);
        som_put_word(target+0x1F1,0u);
        som_put_word(target+0x1F3,(uint16_t)(max_hp/4u));
        fprintf(stderr,"[som-ai] REVIVIFIER restored %s\n",som_party_names[revival->target]);
    }
}
/* who: the caster, 0 the boy, 1 the girl, 2 the sprite. The three heroes run
 * the same cast (som-decomp summon-cast.md 2: same script, other pictures);
 * only the sprite's palette index differs (+0x4F 0xCF) and each has her
 * own effect-tile buffer (7E:2000 + who*0x1000). */
static int som_cast_spell_by(uint8_t *ram,unsigned who,unsigned spell){
    if(!ram||who>2u||spell>=SOM_SPELL_COUNT||!som_rom)return 0;
    if(who?!som_ally_idle(ram,who):!som_field_idle(ram))return 0;   /* busy: no cast */
    cast_who=who;
    uint8_t *hero=ram+0xE000u+0x200u*who;
    const uint8_t *row=som_rom_at(0xD02AD0u+64u*spell,16u);
    /* 0x81 hostile: monster slots; 0x01 ally and 0x00 self: the caster
     * herself (hero slot who, target who+1, as the ring's single-ally choice). */
    /* The 0x40 / 0x10 bits (Defender, Fire Bouquet, Lunar Boost 0x41, Moon
     * Energy 0x51, Burst 0xC1, Dispel 0xD1 in Turbo) read as "may also take
     * everyone": cast on one, as the ring's default single choice
     * (unverified live, 2026-11-22). Remedy's 0xE0 stays refused. */
    const int kind=som_cast_target_kind(row);
    if(kind<0){
        fprintf(stderr,"[som-ai] CAST refused: spell %#04x has unsupported targets\n",spell);
        return 0;
    }
    const int hostile=kind==0x81u;
    char message[64];
    const unsigned cost=row[15];
    if(hero[0x186]<cost){
        snprintf(message,sizeof(message),"^%s: not enough MP|%u/%u",
                 som_spell_name(spell),(unsigned)hero[0x186],cost);
        som_overlay_say(&overlay,message,SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] CAST refused: MP %u < %u\n",(unsigned)hero[0x186],cost);
        return 0;
    }
    /* Heals go to the hero with the lowest share of her HP, and not at
     * all when nobody is hurt; other ally spells to the caster. */
    const int heal=som_bolt_heal_spell(spell);
    const int target=hostile?som_cast_target(ram,who):
        heal?som_most_hurt_hero(ram,spell):(int)who;
    if(target<0){
        snprintf(message,sizeof(message),spell==0x21u?"^%s: no fallen ally":
                 heal?"^%s: nobody is hurt":"^%s: no target",
                 som_spell_name(spell));
        som_overlay_say(&overlay,message,SOM_MESSAGE_FRAMES);
        return 0;
    }
    static uint8_t spark[SOM_HERO_EFFECT_BYTES];
    if(!som_expand_effect_tiles(0xD4BA00u,SOM_WEAPON_TILES,spark))return 0;
    const unsigned element=som_spell_element(spell);
    const uint8_t nibbles=hero[0x1C4u+element/2u];
    unsigned level=element%2u?(nibbles&0x0Fu):(nibbles>>4);
    if(level>7u)level=7u;      /* level 8 needs the game's RNG ($D0:4EAC) */
    /* $C0:731F + $C0:001B */
    hero[0x170]=(uint8_t)spell;hero[0x177]=(uint8_t)element;
    hero[0x176]=1;hero[0x178]=(uint8_t)(target+1);hero[0x179]=hero[0x17A]=0;
    hero[0x172]=hero[0x173]=hero[0x174]=0xFF;
    hero[0x171]=(uint8_t)level;
    /* $C1:88C7 */
    hero[0x111]=(uint8_t)level;hero[0x108]=0;
    hero[0x04F]=who==2u?0xCFu:0xCEu;hero[0x01C]=0x40;hero[0x030]=0xFF;hero[0x011]=0x94;
    hero[0x006]=hero[0x007]=hero[0x008]=0;
    hero[0x060]=0x20;
    revivals[who].deadline=0;
    if(spell==0x21u)
        revivals[who]=(SomRevival){frame_number+420u,(unsigned)target,som_word(ram+0xDC),
                                  hero[0x186],cost,0};
    memcpy(ram+SOM_WEAPON_BUF+0x1000u*who+0x800u,spark,sizeof(spark));   /* $C1:8634 */
    if(hostile&&target>=0){
        const uint8_t *m=ram+0xE600u+0x200u*(unsigned)target;
        xp_spell_slot=target;xp_spell_hp=(uint16_t)(m[0x182]|m[0x183]<<8);
        xp_spell_species=m[0x180];xp_spell_frame=frame_number;
    }
    fprintf(stderr,"[som-ai] CAST %s %s Lv.%u at %s slot %d (MP %u, cost %u)\n",
            som_party_names[who],som_spell_name(spell),level,hostile?"monster":"hero",target,
            (unsigned)hero[0x186],cost);
    return 1;
}
/* Kage Bunshin clones (src/som_bunshin.h). Explosion damage grows with
 * the Ninjutsu discipline level. */
static unsigned som_bunshin_damage(void){
    return 10u+disciplines.level[2]/2u+(unsigned)som_perk(SOM_EFFECT_CLONE_DAMAGE);
}
static unsigned som_hero_place(const uint8_t *hero){return hero[0x00B];}
static void som_bunshin_explode(uint8_t *ram,int x,int y){
    som_bun_boom(bunshin_booms,x,y,0,frame_number);
    const unsigned hit=som_bun_blast(ram,x,y,som_bunshin_damage(),familiar.slot);
    if(hit)som_xp_give(SOM_DISC_NINJUTSU,SOM_XP_CLONE,0xF0u,SOM_XPK_CLONE,"clone explosion");
    fprintf(stderr,"[som-ai] BUNSHIN explosion at %d,%d: %u monster(s), %u damage\n",
            x,y,hit,som_bunshin_damage());
}
/* Swap the hero into a clone. flee: substitution away from a monster at
 * (fx, fy): the clone is used up and the spot he left explodes. Otherwise
 * the clone takes his old spot. */
static int som_bunshin_swap(uint8_t *ram,int flee,int fx,int fy,int dir_x,int dir_y){
    if(!ram)return 0;
    uint8_t *hero=ram+0xE000u+0x200u*bunshin_owner;
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    const int i=som_bun_pick(&bunshin,hx,hy,flee,fx,fy,dir_x,dir_y);
    if(i<0)return 0;
    const int nx=bunshin.x[i],ny=bunshin.y[i];
    hero[2]=(uint8_t)nx;hero[3]=(uint8_t)(nx>>8);
    hero[4]=(uint8_t)ny;hero[5]=(uint8_t)(ny>>8);
    hero[0x006]=hero[0x007]=0;camera_catch_until=frame_number+60ul;
    bunshin.hero_x=nx;bunshin.hero_y=ny;
    bunshin.swap_ready=frame_number+SOM_BUN_SWAP_COOLDOWN;
    som_bun_boom(bunshin_booms,nx,ny,1,frame_number);
    if(flee){
        bunshin.alive[i]=0;
        som_bunshin_explode(ram,hx,hy);
        som_overlay_say(&overlay,"^Kawarimi no Jutsu!",SOM_MESSAGE_FRAMES/2u);
    }else{
        bunshin.x[i]=hx;bunshin.y[i]=hy;
        som_bun_boom(bunshin_booms,hx,hy,1,frame_number);
    }
    som_bun_reform(&bunshin,nx,ny);
    fprintf(stderr,"[som-ai] BUNSHIN %s: hero %d,%d -> clone %d at %d,%d\n",
            flee?"substitution":"swap",hx,hy,i,nx,ny);
    return 1;
}
/* The native cast queue names real actor slots. A decoy has no such
 * slot, so absorb the queued single-target cast and draw its impact at
 * the decoy. The caster keeps its animation and pays the normal MP cost.
 * C8:E6D4 ignores hits once the cast's hit list is full. */
static void som_bunshin_absorb_magic(uint8_t *ram){
    if(bunshin_shunshin)return;
    for(unsigned slot=0;slot<3u;slot++){
        uint8_t *caster=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
        if((int)slot==familiar.slot||!som_bun_monster_alive(caster)||
           (caster[0x140]!=0x44u&&caster[0x60]!=0x20u)||
           caster[0x176]!=1u||caster[0x178]!=bunshin_owner+1u||
           caster[0x179]||caster[0x17A]||
           caster[0x172]!=0xFFu||caster[0x173]!=0xFFu||caster[0x174]!=0xFFu)
            continue;
        const unsigned spell=caster[0x170];
        if(spell>=SOM_SPELL_COUNT)continue;
        const uint8_t *row=som_rom_at(0xD02AD0u+64u*spell,16u);
        if(!row||!(row[8]&0x80u))continue;
        const int x=(int)som_word(caster+2),y=(int)som_word(caster+4);
        int decoy=-1,nearest=0;
        for(unsigned i=0;i<SOM_BUN_COUNT;i++){
            if(!bunshin.alive[i])continue;
            const int distance=abs(bunshin.x[i]-x)+abs(bunshin.y[i]-y);
            if(decoy<0||distance<nearest){decoy=(int)i;nearest=distance;}
        }
        if(decoy<0)return;
        caster[0x172]=0;caster[0x173]=1;caster[0x174]=2;
        caster[0x176]=caster[0x178]=0;
        caster[0x186]=(uint8_t)(caster[0x186]>row[15]?caster[0x186]-row[15]:0);
        bunshin.alive[decoy]=0;
        som_bun_boom(bunshin_booms,bunshin.x[decoy],bunshin.y[decoy],1,frame_number);
        som_overlay_say(&overlay,"^Kage Bunshin|spell absorbed",SOM_MESSAGE_FRAMES/3u);
        fprintf(stderr,"[som-ai] BUNSHIN absorbed %s from enemy slot %u for %s\n",
                som_spell_label(spell),slot,som_party_names[bunshin_owner]);
    }
}
static void som_bunshin_frame(uint8_t *ram){
    if(!ram||!som_bun_alive(&bunshin))return;
    uint8_t *hero=ram+0xE000u+0x200u*bunshin_owner;
    const int field=hero[0]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F&&ram[0xD0]==0;
    const int same_map=som_bun_u16(ram+0xDC)==bunshin.map;
    if(!field||!same_map||frame_number>bunshin.until){
        const int detonate=bunshin_shunshin&&field&&same_map;
        if(same_map)
            for(unsigned i=0;i<SOM_BUN_COUNT;i++)
                if(bunshin.alive[i]){
                    if(detonate)som_bunshin_explode(ram,bunshin.x[i],bunshin.y[i]);
                    else som_bun_boom(bunshin_booms,bunshin.x[i],bunshin.y[i],1,frame_number);
                }
        som_bun_clear(&bunshin);bunshin_shunshin=0;return;
    }
    som_bunshin_absorb_magic(ram);
    if(!som_bun_alive(&bunshin))return;
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    som_bun_follow(&bunshin,ram,hx,hy,som_hero_place(hero));
    for(unsigned i=0;i<SOM_BUN_COUNT;i++){
        if(!bunshin.alive[i]||som_bun_toucher(ram,bunshin.x[i],bunshin.y[i],SOM_BUN_TOUCH_X,
                                              SOM_BUN_TOUCH_Y,familiar.slot)<0)continue;
        bunshin.alive[i]=0;
        som_bunshin_explode(ram,bunshin.x[i],bunshin.y[i]);
        som_overlay_say(&overlay,"^Bunshin Daibakuha!",SOM_MESSAGE_FRAMES/2u);
    }
    if(!som_bun_alive(&bunshin)){bunshin_shunshin=0;return;}
    if(bunshin_shunshin){
        /* Shunshin: a monster at the hero sets off every clone at once. */
        if(som_bun_toucher(ram,hx,hy,SOM_BUN_HERO_TOUCH_X,SOM_BUN_HERO_TOUCH_Y,familiar.slot)<0)
            return;
        for(unsigned i=0;i<SOM_BUN_COUNT;i++)
            if(bunshin.alive[i])som_bunshin_explode(ram,bunshin.x[i],bunshin.y[i]);
        som_bun_clear(&bunshin);bunshin_shunshin=0;
        som_overlay_say(&overlay,"^Shunshin|Daibakuha!",SOM_MESSAGE_FRAMES/2u);
        return;
    }
    if(frame_number<bunshin.swap_ready)return;
    const int toucher=som_bun_toucher(ram,hx,hy,SOM_BUN_HERO_TOUCH_X,SOM_BUN_HERO_TOUCH_Y,
                                      familiar.slot);
    if(toucher>=0){
        const uint8_t *o=ram+SOM_BUN_MONSTER_BASE+(unsigned)toucher*SOM_BUN_OBJECT;
        som_bunshin_swap(ram,1,(int)som_bun_u16(o+2),(int)som_bun_u16(o+4),0,0);
    }
}
static int som_bunshin_booming(void){
    for(unsigned i=0;i<SOM_BUN_BOOMS;i++)
        if(bunshin_booms[i].start&&frame_number-bunshin_booms[i].start<SOM_BUN_BOOM_FRAMES)return 1;
    return 0;
}
/* Clones and explosions on the frame copy; (hero_x, hero_y) = the hero on
 * the base screen. */
static void som_bunshin_draw(uint8_t *pixels,unsigned width,unsigned height,size_t pitch,
                             int hero_x,int hero_y){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size)return;
    const uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE)return;
    const uint8_t *hero=ram+0xE000u+0x200u*bunshin_owner;
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    const uint8_t *vram=backend_retro_get_memory_size(RETRO_MEMORY_VIDEO_RAM)>=SOM_BUN_VRAM_SIZE?
        backend_retro_get_memory_data(RETRO_MEMORY_VIDEO_RAM):NULL;
    if(vram)
        for(unsigned i=0;i<SOM_BUN_COUNT;i++)
            if(bunshin.alive[i])
                som_bun_draw_copy_oam(pixels,width,height,pitch,pixel_format,ram,
                                      oam_shown_valid?oam_shown:ram+0x0800u,vram,bunshin_owner,
                                      bunshin.x[i]-hx,bunshin.y[i]-hy);
    if(kawarimi_log_start){
        const unsigned long age=frame_number-kawarimi_log_start;
        if(age>=SOM_KAWARIMI_LOG_FRAMES||som_bun_u16(ram+0xDC)!=kawarimi_log_map){
            if(som_bun_u16(ram+0xDC)==kawarimi_log_map)
                som_bun_boom(bunshin_booms,kawarimi_log_x,kawarimi_log_y,1,frame_number);
            kawarimi_log_start=0;
        }else som_bun_draw_log(pixels,width,height,pitch,pixel_format,
                               hero_x+(kawarimi_log_x-hx),hero_y+(kawarimi_log_y-hy));
    }
    for(unsigned i=0;i<SOM_BUN_BOOMS;i++){
        SomBoom *b=&bunshin_booms[i];
        if(!b->start)continue;
        const unsigned long age=frame_number-b->start;
        if(age>=SOM_BUN_BOOM_FRAMES){b->start=0;continue;}
        som_bun_draw_boom(pixels,width,height,pitch,pixel_format,
                          hero_x+(b->x-hx),hero_y+(b->y-hy)-8,(unsigned)age,b->smoke);
    }
    for(unsigned i=0;i<SOM_BOLTS;i++)
        if(mage_bolts[i].active)
            som_bolt_draw(pixels,width,height,pitch,pixel_format,&mage_bolts[i],
                          hero_x+(mage_bolts[i].x-hx),hero_y+(mage_bolts[i].y-hy)-10,frame_number);
}
/* Swordsman bash: see SOM_BASH_LUNGE_FRAMES. */
static int som_bash_start_raw(uint8_t *ram){
    if(!ram||bash_stage||evade_active||special_stage)return 0;
    uint8_t *hero=SOM_ACT(ram);
    if(hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||ram[0x7D]==1)return 0;
    /* The run's own start conditions ($02:B6CD). */
    if(hero[0x060]||hero[0x01C]||hero[0x00A]||hero[0x01A]||hero[0x01B]||
       (som_word(hero+0x190)&0xC160u))return 0;
    if(hero[0x1ED]){
        som_overlay_say(&overlay,"^Bash|catch your breath",SOM_MESSAGE_FRAMES/2u);
        return 0;
    }
    const unsigned weapon=bash_kick?0u:1u;           /* glove for the kick, else sword */
    const int equipped=som_actor?som_equip_ally_weapon(ram,som_actor,weapon):
        som_equip_weapon(ram,weapon);
    if(equipped!=SOM_EQUIP_OK)return 0;
    bash_stage=SOM_BASH_LUNGE;bash_started=frame_number;
    som_overlay_say(&overlay,bash_kick?"^Flying kick!":"^Bash!",SOM_MESSAGE_FRAMES/3u);
    fprintf(stderr,"[som-ai] %s lunge\n",bash_kick?"FLYING KICK":"BASH");
    return 1;
}
/* Martial Arts' X: see SOM_FLYKICK_LIFT. Returns 1 when it started. */
static int som_flykick_start_raw(uint8_t *ram){
    if(!ram||som_actor_class()!=SOM_DISC_MARTIAL_ARTS||flykick.active)return 0;
    bash_kick=1;
    if(!som_bash_start(ram)){bash_kick=0;return 0;}
    if(!cartwheel_pack_tried){
        cartwheel_pack_tried=1;
        som_load_pack_file(SOM_CARTWHEEL_PACK_FILE,"Hero moves",&cartwheel_pack,&cartwheel_pack_size);
    }
    uint8_t *hero=SOM_ACT(ram);
    const unsigned id=hero[0x180],facing=hero[0x010];
    const unsigned pack_facing=(facing&0x7Fu)==0x02u?2u:facing==0x01u?1u:0u;
    SomWorkEntry entry;
    if(cartwheel_pack&&id>=0x80u&&id<=0x82u&&
       som_work_find(cartwheel_pack,cartwheel_pack_size,SOM_FLYKICK_KIND+id-0x80u,pack_facing,&entry)){
        memset(&flykick,0,sizeof(flykick));
        flykick.active=1;flykick.slot=som_actor;flykick.kind=id;
        flykick.dir=facing==0x82u?3u:pack_facing;
        flykick.start=frame_number;flykick.entry=entry;flykick.own_palette=1;
        flykick.length=som_work_swing_frames(&entry);
        flykick_hid=!(hero[0x0E]&0x80u);
        hero[0x0E]|=0x80u;
    }
    return 1;
}
static void som_flykick_end(uint8_t *ram){
    if(!flykick.active)return;
    if(ram&&flykick_hid)SOM_ACT(ram)[0x00E]&=(uint8_t)~0x80u;
    flykick.active=0;flykick_hid=0;
}
/* Each frame: the jump arc over the run, landing at the strike; the
 * drawing ends with its frames, or with the finisher it started. */
static void som_flykick_frame(uint8_t *ram){
    if(!flykick.active)return;
    const unsigned long t=frame_number-flykick.start;
    if(bash_stage==SOM_BASH_LUNGE){
        const long l=(long)SOM_BASH_LUNGE_FRAMES,k=(long)t;
        flykick.lift=k>=l?SOM_FLYKICK_LIFT/3:(int)(4l*SOM_FLYKICK_LIFT*k*(l-k)/(l*l));
        if(flykick.lift<SOM_FLYKICK_LIFT/3&&k>l/2)flykick.lift=SOM_FLYKICK_LIFT/3;
    }else flykick.lift=0;
    const int game_over=!ram||SOM_ACT(ram)[0x000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F;
    if(game_over||(!bash_stage&&!special_stage&&t>=flykick.length)||t>180ul)som_flykick_end(ram);
}
static void som_fuma_start(const uint8_t *ram,unsigned slot,unsigned base){
    if(!ram||fuma.active||frame_number<fuma_ready||slot>2u||!base)return;
    if(!cartwheel_pack_tried){
        cartwheel_pack_tried=1;
        som_load_pack_file(SOM_CARTWHEEL_PACK_FILE,"Hero moves",&cartwheel_pack,&cartwheel_pack_size);
    }
    const uint8_t *hero=ram+0xE000u,*m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
    const unsigned id=hero[0x180];
    if(!cartwheel_pack||id<0x80u||id>0x82u)return;
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    const int mx=(int)som_bun_u16(m+2),my=(int)som_bun_u16(m+4);
    int cx=mx,cy=my;
    unsigned facing,dir;
    if(abs(hx-mx)>=abs(hy-my)){                 /* beside it, facing it */
        const int side=hx>=mx?1:-1;
        cx=mx+side*SOM_FUMA_OFFSET;facing=2u;dir=side>0?3u:2u;
    }else{                                      /* above or below it */
        const int side=hy>=my?1:-1;
        cy=my+side*SOM_FUMA_OFFSET;facing=side>0?0u:1u;dir=facing;
    }
    SomWorkEntry entry;
    if(!som_work_find(cartwheel_pack,cartwheel_pack_size,SOM_FUMA_KIND+id-0x80u,facing,&entry))
        return;
    unsigned level=disciplines.level[SOM_DISC_NINJUTSU];
    const unsigned sword=hero[0x1C0]&0x0Fu;
    if(level>sword)level=sword;
    level+=(unsigned)som_perk(SOM_EFFECT_FUMA_LEVEL);         /* Fuma Mastery */
    if(level>8u)level=8u;
    unsigned damage=base*(2u+level)/2u;
    if(damage>0xFFu)damage=0xFFu;
    memset(&fuma,0,sizeof(fuma));
    fuma.active=1;fuma.slot=0;fuma.kind=id;fuma.dir=dir;fuma.entry=entry;fuma.own_palette=1;
    fuma.fixed=1;fuma.wx=cx;fuma.wy=cy;
    fuma.start=frame_number+SOM_FUMA_APPEAR;
    const unsigned long swing=som_work_swing_frames(&entry);
    fuma.length=swing+SOM_FUMA_LINGER;
    fuma_strike_at=fuma.start+swing*2ul/3ul;
    fuma_end_at=fuma.start+fuma.length;
    fuma_target=(int)slot;fuma_target_id=m[0x180];fuma_damage=damage;fuma_level=level;
    fuma_struck=0;
    som_bun_boom(bunshin_booms,cx,cy,1,frame_number);
    char text[48];
    snprintf(text,sizeof(text),"^Fuma|shadow strike level %u",level);
    som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] FUMA clone at %d,%d strikes monster %u: level %u, %u damage "
            "(boomerang hit %u)\n",cx,cy,slot,level,damage,base);
}
/* Each frame: the clone's strike lands two thirds into the swing; then it
 * holds its pose and vanishes in smoke. */
static void som_fuma_frame(uint8_t *ram){
    if(!fuma.active)return;
    const int field=ram&&ram[0xE000]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F&&ram[0xD0]==0;
    if(field&&!fuma_struck&&frame_number>=fuma_strike_at){
        fuma_struck=1;
        uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)fuma_target;
        if(som_bun_monster_alive(m)&&m[0x180]==fuma_target_id){
            /* The boomerang just hit it: Coup de Grace and Last Resort apply. */
            fuma_damage+=som_perk_bonus(0u,(unsigned)fuma_target,fuma_damage,"fuma strike");
            som_monster_hit((unsigned)fuma_target);
            const unsigned pending=m[0x1F1]+fuma_damage;
            m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
            som_xp_give(SOM_DISC_NINJUTSU,SOM_XP_HIT,m[0x180],SOM_XPK_HIT,"fuma strike");
            fprintf(stderr,"[som-ai] FUMA strike: %u damage queued on monster %d\n",
                    fuma_damage,fuma_target);
        }else fprintf(stderr,"[som-ai] FUMA strike missed: monster %d gone\n",fuma_target);
    }
    if(!field||frame_number>=fuma_end_at){
        if(field)som_bun_boom(bunshin_booms,fuma.wx,fuma.wy,1,frame_number);
        fuma.active=0;fuma_target=-1;fuma_ready=frame_number+SOM_FUMA_COOLDOWN;
    }
}
static int som_blink_start_raw(uint8_t *ram,int tx,int ty){
    if(!ram||blink_active||evade_active||bash_stage||special_stage)return 0;
    if(frame_number<blink_ready){
        fprintf(stderr,"[som-ai] BLINK refused: ready in %lu frames\n",blink_ready-frame_number);
        return 0;
    }
    uint8_t *hero=SOM_ACT(ram);
    if(hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||ram[0x7D]==1)return 0;
    const int x0=(int)som_bun_u16(hero+2),y0=(int)som_bun_u16(hero+4);
    if(tx<0||ty<0||abs(tx-x0/16)>SOM_BLINK_RANGE||abs(ty-y0/16)>SOM_BLINK_RANGE){
        fprintf(stderr,"[som-ai] BLINK refused: tile %d,%d out of range\n",tx,ty);
        return 0;
    }
    /* The tile centre first, then nearby spots the hero's box fits in: a
     * tile he walked through can be too narrow at its centre (tile 30,19
     * on map 130 refused again and again, 2026-11-19). */
    static const int near[][2]={{0,0},{0,-4},{0,4},{-4,0},{4,0},{-4,-4},{4,-4},{-4,4},{4,4},
                                {0,-7},{0,7},{-7,0},{7,0}};
    int x=0,y=0,found=0;
    for(unsigned k=0;k<sizeof(near)/sizeof(near[0])&&!found;k++){
        x=tx*16+8+near[k][0];y=ty*16+8+near[k][1];
        found=som_bun_spot_open(ram,x,y,som_hero_place(hero))&&som_blink_spot_ok(ram,x,y);
    }
    if(!found){
        fprintf(stderr,"[som-ai] BLINK refused: tile %d,%d is not open ground\n",tx,ty);
        return 0;
    }
    som_blink_go(ram,x,y);
    som_overlay_say(&overlay,"^Blink",SOM_MESSAGE_FRAMES/3u);
    fprintf(stderr,"[som-ai] BLINK from %d,%d to tile %d,%d (%d,%d)\n",x0,y0,tx,ty,x,y);
    return 1;
}
/* The player's Blink (Ninjutsu: A in the X wait): up to SOM_BLINK_DASH_PX
 * along the held direction (else the facing), stopping before the first
 * ground the hero cannot stand on, so it never crosses a wall. */
static int som_blink_dash_raw(uint8_t *ram,int dx,int dy){
    if(!ram||blink_active||evade_active||bash_stage||special_stage)return 0;
    uint8_t *hero=SOM_ACT(ram);
    if(hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||ram[0x7D]==1)return 0;
    if(frame_number<blink_ready){
        char text[48];
        snprintf(text,sizeof(text),"^Blink|ready in %lus",(blink_ready-frame_number+59ul)/60ul);
        som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/3u);
        return 0;
    }
    (void)som_lock_evasion_direction(&dx,&dy);
    if(!dx&&!dy){
        const unsigned facing=hero[0x010];
        if(facing==0x02u)dx=1;else if(facing==0x82u)dx=-1;else if(facing==0x01u)dy=1;else dy=-1;
    }
    const int x0=(int)som_bun_u16(hero+2),y0=(int)som_bun_u16(hero+4);
    const unsigned place=som_hero_place(hero);
    int reach=0;
    for(int d=8;d<=SOM_BLINK_DASH_PX;d+=8){
        if(!som_bun_spot_open(ram,x0+dx*d,y0+dy*d,place)||
           !som_blink_spot_ok(ram,x0+dx*d,y0+dy*d))break;
        reach=d;
    }
    if(reach<24){
        som_overlay_say(&overlay,"^Blink|no room",SOM_MESSAGE_FRAMES/3u);
        return 0;
    }
    som_blink_go(ram,x0+dx*reach,y0+dy*reach);
    som_overlay_say(&overlay,"^Blink",SOM_MESSAGE_FRAMES/3u);
    fprintf(stderr,"[som-ai] BLINK (X then A) %d px toward %d,%d\n",reach,dx,dy);
    return 1;
}
/* The hidden straight-line travel shared by Blink and the Fuma swap. */
static int som_blink_go(uint8_t *ram,int x,int y){
    uint8_t *hero=SOM_ACT(ram);
    blink_active=1;blink_tx=x;blink_ty=y;
    blink_hid=!(hero[0x0E]&0x80u);
    hero[0x0E]|=0x80u;
    som_bun_boom(bunshin_booms,(int)som_bun_u16(hero+2),(int)som_bun_u16(hero+4),1,frame_number);
    return 1;
}
static int som_fuma_swap(uint8_t *ram){
    if(!ram||!fuma.active||frame_number<fuma.start||blink_active||
       som_actor!=fuma.slot||som_actor_class()!=SOM_DISC_NINJUTSU)return 0;
    uint8_t *hero=SOM_ACT(ram);
    if(hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0)return 0;
    if(!som_bun_spot_open(ram,fuma.wx,fuma.wy,som_hero_place(hero))){
        fprintf(stderr,"[som-ai] FUMA swap refused: the clone stands where the hero cannot\n");
        return 0;
    }
    if(!fuma_struck){fuma_strike_at=frame_number;som_fuma_frame(ram);}
    const int x=fuma.wx,y=fuma.wy;
    fuma.active=0;fuma_target=-1;fuma_ready=frame_number+SOM_FUMA_COOLDOWN;
    som_blink_go(ram,x,y);
    blink_is_swap=1;               /* not a Blink: its cooldown is untouched */
    som_overlay_say(&overlay,"^Fuma|swap",SOM_MESSAGE_FRAMES/3u);
    fprintf(stderr,"[som-ai] FUMA swap: hero takes the clone's place at %d,%d\n",x,y);
    return 1;
}
/* Each frame of a blink: SOM_BLINK_SPEED px per axis toward the landing
 * spot, camera along; smoke and the hero back on arrival. */
static void som_blink_frame(uint8_t *ram){
    if(!blink_active)return;
    uint8_t *hero=ram?SOM_ACT(ram):NULL;
    const int field=ram&&hero[0]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F&&ram[0xD0]==0;
    int x=field?(int)som_bun_u16(hero+2):0,y=field?(int)som_bun_u16(hero+4):0;
    if(field){
        const int dx=blink_tx-x,dy=blink_ty-y;
        const int sx=dx>SOM_BLINK_SPEED?SOM_BLINK_SPEED:dx<-SOM_BLINK_SPEED?-SOM_BLINK_SPEED:dx;
        const int sy=dy>SOM_BLINK_SPEED?SOM_BLINK_SPEED:dy<-SOM_BLINK_SPEED?-SOM_BLINK_SPEED:dy;
        x+=sx;y+=sy;
        hero[2]=(uint8_t)x;hero[3]=(uint8_t)(x>>8);hero[4]=(uint8_t)y;hero[5]=(uint8_t)(y>>8);
        hero[0x006]=hero[0x007]=0;
        som_camera_follow_axis(ram,hero,0u,sx,0);som_camera_follow_axis(ram,hero,1u,sy,0);
        if(x!=blink_tx||y!=blink_ty)return;
        som_blink_bring_party(ram);
        som_bun_boom(bunshin_booms,x,y,1,frame_number);
        fprintf(stderr,"[som-ai] BLINK arrived at %d,%d\n",x,y);
    }
    if(ram&&blink_hid)SOM_ACT(ram)[0x00E]&=(uint8_t)~0x80u;
    if(!blink_is_swap)                         /* Shadow Step shortens it */
        blink_ready=frame_number+SOM_BLINK_COOLDOWN*(unsigned long)(100-som_perk(
            SOM_EFFECT_BLINK_COOLDOWN_CUT))/100ul;
    blink_active=0;blink_hid=0;blink_is_swap=0;
}
static void som_companion_frame(uint8_t *ram){
    if(!companion.active)return;
    companion.draw.active=0;
    if(familiar.slot<0){
        if(!familiar_carry.valid&&!familiar_pending&&!clone_pending_until){
            companion.active=0;
            fprintf(stderr,"[som-ai] COMPANION gone\n");
        }
        return;                                    /* carried across a map */
    }
    if(!ram||ram[0xE000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F)return;
    if(!cartwheel_pack_tried){
        cartwheel_pack_tried=1;
        som_load_pack_file(SOM_CARTWHEEL_PACK_FILE,"Hero moves",&cartwheel_pack,&cartwheel_pack_size);
    }
    if(!cartwheel_pack)return;
    const unsigned slot=3u+(unsigned)familiar.slot;
    uint8_t *o=ram+SOM_NPC_OBJECT(slot);
    o[0x0E]|=0x80u;                                /* the monster sprite stays hidden */
    if(!companion.switched_at||(frame_number%SOM_COMPANION_LOOK_FRAMES==0&&
                                frame_number-companion.switched_at>=SOM_COMPANION_SWITCH_FRAMES)){
        const char *why="";
        const unsigned pick=som_companion_pick(ram,slot,&why);
        const int first=!companion.switched_at;
        if(pick!=companion.cls||first){
            companion.switched_at=frame_number;
            companion.cls=pick;
            som_bun_boom(bunshin_booms,(int)som_bun_u16(o+2),(int)som_bun_u16(o+4),1,frame_number);
            fprintf(stderr,"[som-ai] COMPANION %s (%s)\n",som_discipline_names[pick],why);
        }
    }
    const int x=(int)som_bun_u16(o+2),y=(int)som_bun_u16(o+4);
    const int dx=x-companion.px,dy=y-companion.py;
    if(dx||dy){
        if(frame_number-companion.moved_at>SOM_COMPANION_MOVING)companion.walk_start=frame_number;
        companion.moved_at=frame_number;
        if(abs(dx)>=abs(dy)){companion.facing=2u;companion.dir=dx>0?2u:3u;}
        else{companion.facing=dy>0?1u:0u;companion.dir=companion.facing;}
    }
    companion.px=x;companion.py=y;
    if(familiar.last_strike!=companion.strike_seen){   /* she struck: face the target */
        companion.strike_seen=familiar.last_strike;companion.swing_at=frame_number;
        if(familiar.target>=0){
            const uint8_t *t=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)familiar.target;
            const int tx=(int)som_bun_u16(t+2)-x,ty=(int)som_bun_u16(t+4)-y;
            if(abs(tx)>=abs(ty)){companion.facing=2u;companion.dir=tx>0?2u:3u;}
            else{companion.facing=ty>0?1u:0u;companion.dir=companion.facing;}
        }
    }
    const unsigned weapon=som_disc_weapons[companion.cls][0];
    SomWorkEntry swing,walk;
    const int have_swing=som_work_find(cartwheel_pack,cartwheel_pack_size,
                                       SOM_COMPANION_SWING+weapon,companion.facing,&swing);
    if(!som_work_find(cartwheel_pack,cartwheel_pack_size,SOM_COMPANION_WALK,companion.facing,&walk))
        return;
    SomDisciplines as_class=disciplines;as_class.selected=(uint8_t)companion.cls;
    SomNpcWorkState *d=&companion.draw;
    memset(d,0,sizeof(*d));
    d->active=1;d->slot=slot;d->kind=SOM_COMPANION_WALK;d->dir=companion.dir;
    d->profile=som_disc_visual_profile(&as_class);
    if(have_swing&&frame_number-companion.swing_at<som_work_swing_frames(&swing)){
        d->entry=swing;d->start=companion.swing_at;            /* her class weapon */
    }else if(frame_number-companion.moved_at<=SOM_COMPANION_MOVING){
        d->entry=walk;d->start=companion.walk_start;d->no_rest=1;   /* walking */
    }else{
        d->entry=walk;d->start=frame_number;                    /* standing */
    }
}
/* Before each emulated frame: advance the bash. */
static void som_bash_tick(void){
    if(!bash_stage||!backend_retro_get_memory_data)return;
    uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram){bash_stage=SOM_BASH_NONE;return;}
    uint8_t *hero=SOM_ACT(ram);
    const unsigned long t=frame_number-bash_started;
    if(hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||t>60ul){
        bash_stage=SOM_BASH_NONE;bash_kick=0;return;
    }
    if(bash_stage==SOM_BASH_LUNGE){
        const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
        const int close=som_bun_toucher(ram,hx,hy,SOM_BASH_REACH,SOM_BASH_REACH,familiar.slot)>=0;
        const int stopped=t>=3ul&&!hero[0x006]&&!hero[0x007];   /* a wall */
        if(t>=SOM_BASH_LUNGE_FRAMES||(t>=2ul&&close)||stopped){
            bash_stage=SOM_BASH_RELEASE;bash_started=frame_number;
        }
    }else if(bash_stage==SOM_BASH_RELEASE){
        /* Releasing A loaded the run gauge E1ED; the swing hits at full. */
        hero[0x1ED]=0;
        const unsigned level=bash_kick?som_combo_effective_level():0u;
        const unsigned hits=combo.hits;
        if(level){                 /* the kick lands as the combo finisher */
            bash_stage=SOM_BASH_NONE;bash_kick=0;
            special_from_ai=flykick_from_ai;
            const int finisher=som_special_start(0,level);
            special_from_ai=0;flykick_from_ai=0;
            if(finisher){
                special_chord=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);special_suppress=1;
                char text[64];
                snprintf(text,sizeof(text),"^Flying kick|%u-hit combo, level %u",hits,level);
                som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/2u);
                fprintf(stderr,"[som-ai] FLYING KICK finisher: %u hits, level %u\n",hits,level);
                return;
            }
            bash_kick=1;           /* refused: the plain kick */
        }
        bash_stage=SOM_BASH_STRIKE;bash_started=frame_number;
        som_combo_swing(0);
    }else if(t>=SOM_BASH_STRIKE_FRAMES){
        bash_stage=SOM_BASH_NONE;
        fprintf(stderr,"[som-ai] %s strike\n",bash_kick?"FLYING KICK":"BASH");
        bash_kick=0;flykick_from_ai=0;
    }
}
/* Field play, no event, not casting, not in a pose or power attack, not
 * knocked out or held by a status that stops movement: a dodge or jutsu
 * may start. */
static int som_jutsu_free(const uint8_t *ram){
    const uint8_t *hero=SOM_ACT(ram);
    return !(hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||
             hero[0x060]||hero[0x01C]==0x40||hero[0x01C]==0x80||
             (som_word(hero+0x190)&0xC160u));
}
/* A shadow clone on each side of the ninja, across his facing (clone,
 * ninja, clone): Kage Bunshin (long-range weapon, on A; swap) and Shunshin
 * (short-range weapon, AI; explosive, bunshin_shunshin). */
static int som_clone_jutsu_raw(uint8_t *ram,const char *jutsu){
    uint8_t *hero=SOM_ACT(ram);
    const unsigned made=som_bun_spawn(&bunshin,ram,(int)som_bun_u16(hero+2),
        (int)som_bun_u16(hero+4),hero[0x010],som_hero_place(hero),frame_number);
    if(!made){
        som_overlay_say(&overlay,"^No room|for shadow clones",SOM_MESSAGE_FRAMES/2u);
        return 0;
    }
    for(unsigned i=0;i<SOM_BUN_COUNT;i++)
        if(bunshin.alive[i])som_bun_boom(bunshin_booms,bunshin.x[i],bunshin.y[i],1,frame_number);
    bunshin_owner=som_actor;
    bunshin_shunshin=!strcmp(jutsu,"Shunshin no Jutsu");
    char title[40];snprintf(title,sizeof(title),"^%s",jutsu);
    som_overlay_say(&overlay,title,SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] JUTSU %s: %u clone(s)\n",jutsu,made);
    return 1;
}
/* Cartwheel evade (Martial Arts, Roguecraft; som_disc_dodge .cartwheel):
 * the game's own miss reaction. When a hit on a hero misses, $C0:4E6F
 * stores reaction 0x11 + rnd*3/256 in E1E5 and the hero's next combat tick
 * (within 5 frames) plays pose 0x80|code; 0x93 is the cartwheel (frames
 * 65, 66, 67, 66 upside down, 65: 25 px away from the facing in 25 frames;
 * up/down facings do a handstand). Headless on the real ROM, 2026-11-18:
 * E1E5 = 0x13 with no monster played E01C/E011 = 40/93 and moved the hero
 * 22 px back; the game moves him, so walls and the camera are its own.
 * The combat tick runs on combat maps only ($ED bit 7) and drops the code
 * for the statuses in 0xC570, so the hop is the fallback. */
#define SOM_CARTWHEEL_REACTION 0x13u
#define SOM_CARTWHEEL_POSE 0x93u
#define SOM_CARTWHEEL_FRAMES 30u            /* 25 of pose + tick latency */
#define SOM_CARTWHEEL_CHECK 8u               /* frames to see the pose start */
static int cartwheel_native;
static unsigned cartwheel_fallback;          /* hop frames if the game refuses */
/* Which native dodge plays (its reaction code; pose = code | 0x80), and
 * the facing to give back after a Ukemi: the game's roll travels away from
 * the facing, so a forward Ukemi turns him round for the roll. */
static unsigned native_code;
static int native_restore_facing=-1;
#define SOM_UKEMI_FRAMES 70u                 /* 65 of pose + tick latency */
/* Asks the game for its cartwheel toward (dx, dy): it goes away from the
 * facing, so a sideways one turns the hero to face the other way first. */
/* Native Ukemi pose is not reliably accepted outside the hit-reaction
 * handler. Draw the real tumble on the frontend frame instead of asking
 * the ROM to play the unrelated sideways cartwheel. */
static void som_ukemi_end(uint8_t *ram){
    if(!ukemi_draw.active)return;
    const unsigned o=0xE000u+0x200u*(ukemi_draw.slot<SOM_PARTY_MEMBERS?ukemi_draw.slot:0u);
    if(ram&&ukemi_hid&&som_bun_u16(ram+0xDCu)==ukemi_map&&ram[o]==1u)
        ram[o+0x0Eu]&=(uint8_t)~0x80u;
    ukemi_draw.active=0;ukemi_hid=0;
}
static int som_move_begin_draw(uint8_t *ram,int dx,int dy,unsigned kind_base);
static int som_ukemi_begin_draw(uint8_t *ram,int dx,int dy,unsigned move){
    if(!som_move_begin_draw(ram,dx,dy,SOM_UKEMI_DRAW_KIND))return 0;
    ukemi_draw.pose_duration=ukemi_draw.length/SOM_WORK_TICK_FRAMES;
    ukemi_draw.length=move;
    trail_until=0;
    return 1;
}
static int som_move_begin_draw(uint8_t *ram,int dx,int dy,unsigned kind_base){
    if(!cartwheel_pack_tried){
        cartwheel_pack_tried=1;
        som_load_pack_file(SOM_CARTWHEEL_PACK_FILE,"Hero moves",
                           &cartwheel_pack,&cartwheel_pack_size);
    }
    uint8_t *hero=SOM_ACT(ram);
    const unsigned id=hero[0x180u];
    if(!cartwheel_pack||id<0x80u||id>0x82u)return 0;
    const unsigned facing=dx?2u:(dy>0?1u:0u);
    SomWorkEntry entry;
    if(!som_work_find(cartwheel_pack,cartwheel_pack_size,
                      kind_base+id-0x80u,facing,&entry))return 0;
    memset(&ukemi_draw,0,sizeof(ukemi_draw));
    ukemi_draw.active=1;ukemi_draw.slot=som_actor;ukemi_draw.kind=id;
    ukemi_draw.dir=dx<0?3u:dx>0?2u:dy>0?1u:0u;
    ukemi_draw.entry=entry;ukemi_draw.start=frame_number;
    ukemi_draw.length=som_work_swing_frames(&entry);
    ukemi_draw.own_palette=1;
    ukemi_hid=!(hero[0x0Eu]&0x80u);
    hero[0x0Eu]|=0x80u;
    ukemi_map=som_bun_u16(ram+0xDCu);
    return 1;
}
static void som_ukemi_frame(uint8_t *ram){
    if(!ukemi_draw.active)return;
    if(!ram||!evade_active||SOM_ACT(ram)[0x000]!=1u||
       som_bun_u16(ram+0xDCu)!=ukemi_map||
       frame_number-ukemi_draw.start>=ukemi_draw.length)
        som_ukemi_end(ram);
}
/* Up + A: the game's cartwheel facing up goes down; while its pose plays,
 * each frame's step (and any camera scroll with it) is turned the other
 * way, onto open ground only. */
static int cart_flip_y;
static unsigned cart_last_y,cart_last_cam;
static unsigned som_hero_place(const uint8_t *hero);
/* Walls only (terrain 3, layer switches): the full box check refused
 * ground the hero had just cartwheeled over (headless map 130). */
static int som_cart_ground(const uint8_t *ram,int x,int y,unsigned place){
    if(x<0||y<0)return 0;
    const int layer2=(place&0x80u)!=0;
    const unsigned index=(((unsigned)y>>4)&0x7Fu)*128u+(((unsigned)x>>4)&0x7Fu);
    const unsigned id=ram[(layer2?0x14000u:0x10000u)+index];
    const uint8_t *a=ram+(layer2?0x1BC00u:0x1B800u)+id*4u;
    return !(a[0]&8u)&&(a[0]&7u)!=3u;
}
/* During lock-on the player's D-pad has already been translated into the
 * same tangent used by lock-on walking.  Reuse that world direction for
 * every dodge, so pressing a direction with evade/cartwheel/Blink circles
 * the locked enemy while preserving each discipline's own move and art. */
static int som_lock_evasion_direction(int *dx,int *dy){
    if(!dx||!dy||lock_slot<0||lock_who!=som_actor||!lock_dpad_out)return 0;
    const uint16_t left=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT);
    const uint16_t right=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT);
    const uint16_t up=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_UP);
    const uint16_t down=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN);
    *dx=((lock_dpad_out&right)?1:0)-((lock_dpad_out&left)?1:0);
    *dy=((lock_dpad_out&down)?1:0)-((lock_dpad_out&up)?1:0);
    return *dx||*dy;
}
static void som_cart_flip_after(uint8_t *ram){
    if(!cart_flip_y)return;
    uint8_t *hero=ram?SOM_ACT(ram):NULL;
    if(!hero||!evade_active||!cartwheel_native||hero[0]!=1){cart_flip_y=0;return;}
    if(hero[0x011]!=SOM_CARTWHEEL_POSE){
        if(frame_number-evade_started>SOM_CARTWHEEL_CHECK)cart_flip_y=0;   /* over, or refused */
        cart_last_y=som_word(hero+4);cart_last_cam=som_word(ram+0xAA);
        return;
    }
    const int d=(int)som_word(hero+4)-(int)cart_last_y;
    const int c=(int)som_word(ram+0xAA)-(int)cart_last_cam;
    int ny=(int)cart_last_y;
    if(d){
        const int want=(int)cart_last_y-d;
        if(som_cart_ground(ram,(int)som_word(hero+2),want-4,som_hero_place(hero)))ny=want;
        som_put_word(hero+4,(uint16_t)ny);
    }
    if(c){                        /* the game's scroll undone, then his own step's */
        const int full=(int)som_bun_u16(ram+0xC2);
        const int back=c>full/2?c-full:c<-full/2?c+full:c;
        som_camera_shift(ram,1u,-back,hero[0x00B]&0x80u?1u:0u);
    }
    if(d&&ny!=(int)cart_last_y)som_camera_follow_axis(ram,hero,1u,ny-(int)cart_last_y,1);
    cart_last_y=(unsigned)ny;cart_last_cam=som_word(ram+0xAA);
}
static int som_cartwheel_request(uint8_t *ram,int dx,int dy,unsigned code){
    uint8_t *hero=SOM_ACT(ram);
    if(!(ram[0xED]&0x80u)||hero[0x1E5]!=0xFFu||hero[0x01C]==0x40u)return 0;
    if(som_word(hero+0x190)&0xC570u)return 0;
    /* Face away from where he goes, as for left/right: down + A faces up
     * (it went up before, the wrong way; player, 2026-11-21). */
    if(dx)hero[0x010]=dx>0?0x82u:0x02u;
    else if(dy)hero[0x010]=dy>0?0x00u:0x01u;
    hero[0x1E5]=(uint8_t)code;
    return 1;
}
/* The camera during the proxy's own moves (dodges, Ukemi, Blink; player,
 * 2026-11-23: "they should not make the game glitch"). The game's rules,
 * som-decomp C0:D6C8 / D890 / DA6E, with the numbers of Turbo's own code
 * (its window is tighter than the original's 38-C8, 48-B0):
 *   - no scroll at all with $FF set or with word 010E clear (a fixed room);
 *   - the camera moves by the hero's step only when that step leaves him
 *     outside the window x [5B,A3), y [61,94) and goes further out;
 *   - a partner a pad controls (E02C or E08C) is kept on screen, x [9,F7),
 *     y [20,C2); the AI's partners never hold it back;
 *   - the layer he stands on (E00B bit 7) moves by the step, the other by
 *     its parallax mode 0A80 + 0 / 10 / 20 / 30 (0 still, 1 half, 2 the
 *     same, else double), the second layer only with $B9;
 *   - scroll words wrap at 512.
 * Moving it on every step, both layers alike and in fixed rooms, as before,
 * left the hero off-centre for good and showed ground beyond the room.
 * Never more than 15 px a frame: the game streams one tile row and column
 * per frame. */
#define SOM_CAM_LEFT 0x5B
#define SOM_CAM_RIGHT 0xA3
#define SOM_CAM_TOP 0x61
#define SOM_CAM_BOTTOM 0x94
#define SOM_CAM_PARTNER_LEFT 0x09
#define SOM_CAM_PARTNER_RIGHT 0xF7
#define SOM_CAM_PARTNER_TOP 0x20
#define SOM_CAM_PARTNER_BOTTOM 0xC2
#define SOM_CAM_EDGE 0x18                /* a dodge may go this near the screen's edge */
#define SOM_CAM_CATCH_SPEED 4
static int som_camera_locked(const uint8_t *ram){
    return ram[0xFF]||!(ram[0x10E]|ram[0x10F]);
}
static int som_camera_layer2(const uint8_t *hero){return (hero[0x00B]&0x80u)!=0;}
/* The hero's screen position on one axis (0 x, 1 y), the map being a torus. */
static int som_camera_screen(const uint8_t *ram,const uint8_t *hero,int pos,unsigned axis){
    const unsigned cam=(som_camera_layer2(hero)?0xACu:0xA8u)+2u*axis;
    const int size=(int)som_bun_u16(ram+0xC0u+2u*axis);
    int at=pos-(int)som_bun_u16(ram+cam);
    if(at<0&&size)at+=size;
    return at;
}
static void som_camera_shift_layer(uint8_t *ram,unsigned second,unsigned axis,int d){
    if(!d||(second&&!ram[0xB9]))return;
    const unsigned cam=(second?0xACu:0xA8u)+2u*axis,scroll=(second?0xB4u:0xB0u)+2u*axis;
    const int size=(int)som_bun_u16(ram+(second?0xC4u:0xC0u)+2u*axis);
    int c=(int)som_bun_u16(ram+cam)+d;
    if(size){if(c<0)c+=size;else if(c>=size)c-=size;}
    som_put_word(ram+cam,(uint16_t)c);
    som_put_word(ram+scroll,(uint16_t)((som_bun_u16(ram+scroll)+(unsigned)d)&0x1FFu));
}
/* Moves both layers as the game does for a step of the hero's layer. */
static void som_camera_shift(uint8_t *ram,unsigned axis,int d,unsigned layer2){
    if(!d)return;
    som_camera_shift_layer(ram,layer2,axis,d);
    uint8_t *mode=ram+0xA80u+(layer2?0u:0x20u)+0x10u*axis;
    int mag=abs(d);
    if(mode[0]==0)return;
    if(mode[0]==1u){
        if(mag>1)mag/=2;else{mode[1]++;mag=mode[1]&1;}
    }else if(mode[0]!=2u)mag*=2;
    som_camera_shift_layer(ram,!layer2,axis,d<0?-mag:mag);
}
/* A partner on a pad that a scroll of d would take off the screen. */
static int som_camera_partner_out(const uint8_t *ram,const uint8_t *hero,unsigned axis,int d){
    const int low=axis?SOM_CAM_PARTNER_TOP:SOM_CAM_PARTNER_LEFT;
    const int high=axis?SOM_CAM_PARTNER_BOTTOM:SOM_CAM_PARTNER_RIGHT;
    for(unsigned k=0;k<SOM_PARTY_MEMBERS;k++){
        const uint8_t *o=ram+0xE000u+0x200u*k;
        if(o==hero||!o[0]||(o[0]&0x80u)||!(o[0x02C]|o[0x08C]))continue;
        const int at=som_camera_screen(ram,hero,(int)som_bun_u16(o+2u+2u*axis),axis)-d;
        if(at<low||at>=high)return 1;
    }
    return 0;
}
/* The game's camera for a step d the hero has just taken on one axis. */
static void som_camera_follow_axis(uint8_t *ram,const uint8_t *hero,unsigned axis,int d,int leash){
    if(!d||som_camera_locked(ram))return;
    const int at=som_camera_screen(ram,hero,(int)som_bun_u16(hero+2u+2u*axis),axis);
    const int low=axis?SOM_CAM_TOP:SOM_CAM_LEFT,high=axis?SOM_CAM_BOTTOM:SOM_CAM_RIGHT;
    if(d>0?at<high:at>=low)return;
    if(leash&&som_camera_partner_out(ram,hero,axis,d))return;
    som_camera_shift(ram,axis,d>15?15:d<-15?-15:d,(unsigned)som_camera_layer2(hero));
}
/* May the hero step d on this axis? Not past the screen's edge when the
 * camera may not come along (fixed room, or a partner would be left). */
static int som_camera_step_ok(const uint8_t *ram,const uint8_t *hero,unsigned axis,int d){
    const int size=axis?224:256;
    const int at=som_camera_screen(ram,hero,(int)som_bun_u16(hero+2u+2u*axis)+d,axis);
    const int low=axis?SOM_CAM_TOP:SOM_CAM_LEFT,high=axis?SOM_CAM_BOTTOM:SOM_CAM_RIGHT;
    if(d>0?at<high:at>=low)return 1;
    if(!som_camera_locked(ram)&&!som_camera_partner_out(ram,hero,axis,d))return 1;
    return d>0?at<size-SOM_CAM_EDGE:at>=SOM_CAM_EDGE;
}
/* A jump (a clone swap, Kawarimi) left him outside the window: the camera
 * then walks after him a few px a frame. */
static void som_camera_catch_frame(uint8_t *ram){
    if(!camera_catch_until||!ram)return;
    const uint8_t *hero=SOM_ACT(ram);
    if(frame_number>camera_catch_until||hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||
       som_camera_locked(ram)){camera_catch_until=0;return;}
    int out=0;
    for(unsigned axis=0;axis<2u;axis++){
        const int at=som_camera_screen(ram,hero,(int)som_bun_u16(hero+2u+2u*axis),axis);
        const int low=axis?SOM_CAM_TOP:SOM_CAM_LEFT,high=axis?SOM_CAM_BOTTOM:SOM_CAM_RIGHT;
        int d=at<low?at-low:at>=high?at-high+1:0;
        if(!d)continue;
        if(d>SOM_CAM_CATCH_SPEED)d=SOM_CAM_CATCH_SPEED;else if(d<-SOM_CAM_CATCH_SPEED)d=-SOM_CAM_CATCH_SPEED;
        if(som_camera_partner_out(ram,hero,axis,d))continue;
        som_camera_shift(ram,axis,d,(unsigned)som_camera_layer2(hero));
        out=1;
    }
    if(!out)camera_catch_until=0;
}
/* Blink ends on (x, y): on screen when the camera cannot come along. */
static int som_blink_spot_ok(const uint8_t *ram,int x,int y){
    if(!som_camera_locked(ram))return 1;
    const uint8_t *hero=SOM_ACT(ram);
    const int sx=som_camera_screen(ram,hero,x,0),sy=som_camera_screen(ram,hero,y,1);
    return sx>=SOM_CAM_EDGE&&sx<256-SOM_CAM_EDGE&&sy>=SOM_CAM_EDGE&&sy<224-SOM_CAM_EDGE;
}
/* Blink outran the party: whoever is off the screen when he arrives comes
 * out of the smoke with him (a partner left behind a wall could not follow,
 * and one on a pad would hold the camera). */
static void som_blink_bring_party(uint8_t *ram){
    const uint8_t *hero=SOM_ACT(ram);
    for(unsigned k=0;k<SOM_PARTY_MEMBERS;k++){
        uint8_t *o=ram+0xE000u+0x200u*k;
        if(o==hero||!o[0]||(o[0]&0x80u))continue;
        const int sx=som_camera_screen(ram,hero,(int)som_bun_u16(o+2),0);
        const int sy=som_camera_screen(ram,hero,(int)som_bun_u16(o+4),1);
        if(sx>=SOM_CAM_PARTNER_LEFT&&sx<SOM_CAM_PARTNER_RIGHT&&
           sy>=SOM_CAM_PARTNER_TOP&&sy<SOM_CAM_PARTNER_BOTTOM)continue;
        memcpy(o+2,hero+2,4);
        o[0x00B]=hero[0x00B];o[0x08E]=hero[0x08E];
        o[0x006]=o[0x007]=0;
        fprintf(stderr,"[som-ai] BLINK brought %s along\n",som_party_names[k]);
    }
}
/* A (and the AI's "evade"): the class's plain dodge hop. A ninja with the
 * long-range weapon (chakram/boomerang) casts Kage Bunshin instead, and with clones out A swaps
 * him into one. Holding A arms Kawarimi after the hop, and L2+X is
 * Shunshin (also AI commands "kawarimi" / "shunshin"). */
static int som_evade_start_raw(uint8_t *ram){
    if(!ram||evade_active)return 0;
    if(!som_disc_has_dodge(som_actor_class())){
        som_overlay_say(&overlay,"^No evade|this class cannot dodge",SOM_MESSAGE_FRAMES);
        return 0;
    }
    uint8_t *hero=SOM_ACT(ram);
    if(!som_jutsu_free(ram))return 0;
    const int ninja=som_actor_class()==2u;
    if(ninja&&som_actor==fuma.slot&&fuma.active&&frame_number>=fuma.start)return som_fuma_swap(ram);
    if(ninja&&som_actor==bunshin_owner&&som_bun_alive(&bunshin)&&!bunshin_shunshin)
        return som_bunshin_swap(ram,0,0,0,evade_dir_x,evade_dir_y);
    if(ninja&&som_weapon_long_range(hero[0x1E4])&&
       (!som_bun_alive(&bunshin)||som_actor==bunshin_owner))
        return som_clone_jutsu(ram,"Kage Bunshin no Jutsu");
    const SomDodge dodge=som_disc_dodge(som_actor_class(),hero[0x1E4]);
    if(!dodge.speed)return 0;
    /* The held direction, else back from (or, forward: toward) the facing. */
    int dx=evade_dir_x,dy=evade_dir_y;
    const int orbit=som_lock_evasion_direction(&dx,&dy);
    if(!dx&&!dy){
        const unsigned facing=hero[0x010];
        if(facing==0x02u)dx=1;else if(facing==0x82u)dx=-1;
        else if(facing==0x01u)dy=1;else dy=-1;
        if(!orbit&&!dodge.forward){dx=-dx;dy=-dy;}
    }
    /* Dodge Mastery lengthens the move, not the grace. */
    const unsigned move=dodge.frames+(unsigned)som_perk(SOM_EFFECT_DODGE_FRAMES);
    hop_dx=dx*(int)dodge.speed;hop_dy=dy*(int)dodge.speed;hop_frames_left=move;
    evade_frames=move+SOM_DODGE_GRACE;
    const unsigned facing_before=hero[0x010];
    /* Ukemi classes: the game has no side-view somersault (the cannon
     * flight is the same ball every way), so left and right are the game's
     * own cartwheel is the fallback when the directional roll pack is absent. */
    const int ukemi_class=som_actor_class()==SOM_DISC_NINJUTSU||
                          som_actor_class()==SOM_DISC_BUSHIDO||
                          som_actor_class()==SOM_DISC_MARTIAL_ARTS;    /* monk: Ukemi too */
    const int ukemi_roll=ukemi_class&&som_ukemi_begin_draw(ram,dx,dy,move);
    /* Every cartwheel, forward + A: the
     * game's cartwheel goes away from the facing, so forward it is drawn
     * facing forward instead (he dashes ahead facing where he goes; player,
     * 2026-11-20). */
    /* Left/right while facing up or down too (player, 2026-11-20: "left
     * and right mirrored opposite"): the game's cartwheel only travels away
     * from the facing, so it turned him round and he wheeled away with his
     * back to where he went. Only a backward cartwheel is the game's own. */
    const int backward=(dx>0&&facing_before==0x82u)||(dx<0&&facing_before==0x02u);
    const int forward_cart=!ukemi_roll&&(dodge.cartwheel==SOM_REACT_CARTWHEEL||ukemi_class)&&dx&&!backward&&
        som_move_begin_draw(ram,dx,0,SOM_FORWARD_CART_KIND);
    /* Mirrored to the left (player, 2026-11-21: both the cartwheel classes'
     * and Ukemi's faced the wrong way the other way round). */
    if(forward_cart)ukemi_draw.dir=dx<0?3u:2u;
    /* Up/down (cartwheel classes): always the game's own cartwheel with
     * the hero facing up (its back-view picture, player 2026-11-21). It
     * travels away from the facing, so down plays as it is and up has its
     * motion mirrored after each game frame (som_cart_flip_after). Off
     * combat maps the game refuses it: drawn instead (it was a bare slide). */
    const int vertical_cart=!dx&&dy&&dodge.cartwheel==SOM_REACT_CARTWHEEL;
    int updown_native=0;
    if(vertical_cart&&!forward_cart){
        updown_native=som_cartwheel_request(ram,0,1,SOM_REACT_CARTWHEEL);   /* faces up */
        cart_flip_y=updown_native&&dy<0;
        cart_last_y=som_word(hero+4);cart_last_cam=som_word(ram+0xAA);
    }
    const int updown_cart=vertical_cart&&!forward_cart&&!updown_native&&
        som_move_begin_draw(ram,1,0,SOM_FORWARD_CART_KIND);
    if(updown_cart){
        ukemi_draw.dir=SOM_UPDOWN_CART_DIR;
        hop_dx=0;hop_dy=dy*(int)SOM_SIDE_CART_SPEED;
        hop_frames_left=SOM_SIDE_CART_FRAMES+(unsigned)som_perk(SOM_EFFECT_DODGE_FRAMES);
        evade_frames=hop_frames_left+SOM_DODGE_GRACE;
    }
    /* A cartwheel to the side goes the game's own distance (about 25 px in
     * 25 frames), not the 54-64 px of the old hop (player: too far). */
    const int side_cart=!ukemi_roll&&dx&&(dodge.cartwheel==SOM_REACT_CARTWHEEL||ukemi_class);
    if(side_cart){
        hop_dx=dx*(int)SOM_SIDE_CART_SPEED;hop_dy=0;
        hop_frames_left=SOM_SIDE_CART_FRAMES+(unsigned)som_perk(SOM_EFFECT_DODGE_FRAMES);
        evade_frames=hop_frames_left+SOM_DODGE_GRACE;
    }
    const unsigned side_move=hop_frames_left;
    cartwheel_native=updown_native||(!ukemi_roll&&!forward_cart&&!updown_cart&&!vertical_cart&&
        (dodge.cartwheel==SOM_REACT_CARTWHEEL||(ukemi_class&&dx))&&
        som_cartwheel_request(ram,dx,dy,SOM_REACT_CARTWHEEL));
    if(ukemi_class&&!ukemi_roll&&!dx)
        fprintf(stderr,"[som-ai] UKEMI roll missing: rebuild som_cartwheel_frames.bin\n");
    native_restore_facing=-1;
    if(cartwheel_native){
        native_code=SOM_REACT_CARTWHEEL;
        cartwheel_fallback=side_cart?side_move:move;hop_frames_left=0;
        evade_frames=(native_code==SOM_REACT_UKEMI?SOM_UKEMI_FRAMES:SOM_CARTWHEEL_FRAMES)+
                     (unsigned)som_perk(SOM_EFFECT_DODGE_FRAMES)+SOM_DODGE_GRACE;
        /* Steering it (forward + A: the cartwheel travels away from the
         * facing) turned him round: he faces the old way again after. */
        if(hero[0x010]!=facing_before)native_restore_facing=(int)facing_before;
    }
    /* A drawn pose (tumble, forward / up-down cartwheel) plays to its end:
     * the short move is over sooner, and the hero holds still (the D-pad is
     * hidden while the evade runs) until the last picture. */
    if(ukemi_draw.active&&ukemi_draw.length+1ul>evade_frames)
        evade_frames=(unsigned)ukemi_draw.length+1u;
    hero[0x006]=hero[0x007]=0;
    evade_hp=som_word(hero+0x182);
    evade_active=1;evade_started=frame_number;evade_xp_given=0;
    evade_blink=som_actor_class()==SOM_DISC_MAGIC&&!cartwheel_native;
    if(evade_blink){
        evade_blink_hid=!(hero[0x0E]&0x80u);
        hero[0x0E]|=0x80u;
        som_bun_boom(bunshin_booms,(int)som_bun_u16(hero+2),(int)som_bun_u16(hero+4),1,frame_number);
    }
    char dodge_text[32];snprintf(dodge_text,sizeof(dodge_text),"^%s",
                                 evade_blink?"Blink step":dodge.name);
    som_overlay_say(&overlay,dodge_text,SOM_MESSAGE_FRAMES/2u);
    if(cartwheel_native)
        fprintf(stderr,"[som-ai] EVADE %s as %s: the game's own (pose %02x) toward %d,%d\n",dodge.name,
                som_actor_class()<SOM_DISCIPLINE_COUNT?som_discipline_names[som_actor_class()]:"no class",0x80u|native_code,dx,dy);
    else fprintf(stderr,"[som-ai] EVADE %s as %s: %d,%d px per frame for %u frames\n",
                 dodge.name,som_actor_class()<SOM_DISCIPLINE_COUNT?som_discipline_names[som_actor_class()]:"no class",hop_dx,hop_dy,move);
    return 1;
}
/* AI "kawarimi": arm the substitution for SOM_KAWARIMI_ARM_FRAMES (the
 * classes with a dodge); som_kawarimi_check fires it when a monster touches
 * or hits him before the window closes. */
static int som_kawarimi_arm_raw(uint8_t *ram){
    if(!ram||evade_active||kawarimi_armed_until)return 0;
    /* Kawarimi is a ninja skill (player's design, 2026-11-20). */
    if(som_actor_class()!=SOM_DISC_NINJUTSU)return 0;
    if(!som_jutsu_free(ram))return 0;
    const SomDodge dodge=som_disc_dodge(som_actor_class(),SOM_ACT(ram)[0x1E4]);
    if(!dodge.speed)return 0;
    kawarimi_armed_until=frame_number+SOM_KAWARIMI_ARM_FRAMES;
    som_overlay_say(&overlay,"^" SOM_KAWARIMI_NAME "|ready",SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] KAWARIMI armed as %s for %lu frames\n",
            som_discipline_names[som_actor_class()],SOM_KAWARIMI_ARM_FRAMES);
    return 1;
}
/* AI "shunshin": ninja with a short-range weapon in hand, no clones out. */
static int som_shunshin_start_raw(uint8_t *ram){
    if(!ram||evade_active||som_actor_class()!=2u||!som_weapon_short_range(SOM_ACT(ram)[0x1E4]))return 0;
    if(som_bun_alive(&bunshin)||!som_jutsu_free(ram))return 0;
    return som_clone_jutsu(ram,"Shunshin no Jutsu");
}
/* Before each frame while Kawarimi is armed: a monster touching the hero,
 * or a hit queued on him (E059 mask / pending damage E1F1), makes him
 * vanish in smoke and reappear up to speed * frames px away from the
 * monster (Dodge Mastery lengthens it) on open ground, never through a
 * wall; the hit is dropped and a log stands where he was. He then stays
 * untouchable for the move plus SOM_DODGE_GRACE frames. Returns 1 when it
 * fired. */
static int som_kawarimi_check(uint8_t *ram){
    if(!kawarimi_armed_until||!ram)return 0;
    uint8_t *hero=SOM_ACT(ram);
    if(frame_number>kawarimi_armed_until||evade_active||som_actor_class()>=SOM_DISCIPLINE_COUNT){
        kawarimi_armed_until=0;return 0;
    }
    if(hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0)return 0;
    const int x0=(int)som_bun_u16(hero+2),y0=(int)som_bun_u16(hero+4);
    const int toucher=som_bun_toucher(ram,x0,y0,SOM_BUN_HERO_TOUCH_X,SOM_BUN_HERO_TOUCH_Y,
                                      familiar.slot);
    const int hit=hero[0x059]||hero[0x1F1]||hero[0x1F2];
    if(toucher<0&&!hit)return 0;
    const SomDodge dodge=som_disc_dodge(som_actor_class(),hero[0x1E4]);
    if(!dodge.speed){kawarimi_armed_until=0;return 0;}
    /* Away from the monster on its stronger axis, else back from the facing. */
    int dx=0,dy=0;
    if(toucher>=0){
        const uint8_t *o=ram+SOM_BUN_MONSTER_BASE+(unsigned)toucher*SOM_BUN_OBJECT;
        const int mx=x0-(int)som_bun_u16(o+2),my=y0-(int)som_bun_u16(o+4);
        if(abs(mx)>=abs(my))dx=mx>=0?1:-1;else dy=my>=0?1:-1;
    }else{
        const unsigned facing=hero[0x010];
        if(facing==0x02u)dx=-1;else if(facing==0x82u)dx=1;
        else if(facing==0x01u)dy=-1;else dy=1;
    }
    const unsigned move=dodge.frames+(unsigned)som_perk(SOM_EFFECT_DODGE_FRAMES);
    const unsigned place=som_hero_place(hero);
    int x=x0,y=y0;
    for(unsigned k=0;k<dodge.speed*move;k++){
        if(!som_bun_spot_open(ram,x+dx,y+dy,place))break;
        x+=dx;y+=dy;
    }
    hero[0x059]=0;hero[0x1F1]=hero[0x1F2]=0;
    hero[2]=(uint8_t)x;hero[3]=(uint8_t)(x>>8);hero[4]=(uint8_t)y;hero[5]=(uint8_t)(y>>8);
    hero[0x006]=hero[0x007]=0;camera_catch_until=frame_number+60ul;
    kawarimi_armed_until=0;
    hop_dx=hop_dy=0;hop_frames_left=0;
    evade_frames=move+SOM_DODGE_GRACE;
    evade_hp=som_word(hero+0x182);
    evade_active=1;evade_started=frame_number;evade_xp_given=1;
    kawarimi_log_x=x0;kawarimi_log_y=y0;kawarimi_log_start=frame_number?frame_number:1u;
    kawarimi_log_map=som_bun_u16(ram+0xDC);
    som_bun_boom(bunshin_booms,x0,y0,1,frame_number);
    if(x!=x0||y!=y0)som_bun_boom(bunshin_booms,x,y,1,frame_number);
    som_overlay_say(&overlay,"^" SOM_KAWARIMI_NAME "!",SOM_MESSAGE_FRAMES/2u);
    if(toucher>=0)
        som_xp_give(som_actor_class(),SOM_XP_DODGE,
                    ram[SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)toucher+0x180u],
                    SOM_XPK_DODGE,"kawarimi");
    fprintf(stderr,"[som-ai] KAWARIMI fired (%s): %d,%d -> %d,%d\n",
            toucher>=0?"touched":"hit",x0,y0,x,y);
    return 1;
}
/* Before each frame: carry the dodging hero one step, per axis, onto open
 * ground only (src/som_bunshin.h); a wall ends the travel, not the guard.
 * Records the after-image trail. */
static void som_hop_step(void){
    if(!backend_retro_get_memory_data)return;
    if(lock_slot>=0){
        uint8_t *locked=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        if(locked)som_lock_frame(locked);
    }
    {
        uint8_t *locked=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        if(locked)som_lock_frame2(locked);
    }
    {
        uint8_t *art=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        if(art)som_weapon_mask_frame(art);
    }
    if(holy_until||bomb_at||mind_until||telekinesis.slot>=0||
       mine_live[0]||mine_live[1]||mine_live[2]){
        uint8_t *gear=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        if(gear)som_class_x_frame(gear);
    }
    if(hundred_until){
        uint8_t *fists=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        if(fists)som_hundred_frame(fists);
    }
    if(kawarimi_armed_until){
        uint8_t *armed=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        if(armed)som_kawarimi_check(armed);
    }
    if(camera_catch_until)som_camera_catch_frame(backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    if(!evade_active)return;
    uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram)return;
    uint8_t *hero=SOM_ACT(ram);
    if(hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0){hop_frames_left=0;return;}
    som_evade_shield(ram);
    int x=(int)som_bun_u16(hero+2),y=(int)som_bun_u16(hero+4);
    const unsigned slot=(unsigned)(frame_number%SOM_TRAIL_LENGTH);
    if(cartwheel_native&&cartwheel_fallback&&frame_number-evade_started>=SOM_CARTWHEEL_CHECK){
        if(hero[0x01C]!=0x40u||hero[0x011]!=(0x80u|native_code)){   /* refused or cut */
            if(hero[0x1E5]==native_code)hero[0x1E5]=0xFFu;
            if(native_restore_facing>=0){hero[0x010]=(uint8_t)native_restore_facing;native_restore_facing=-1;}
            cartwheel_native=0;hop_frames_left=cartwheel_fallback;
            evade_frames=(unsigned)(frame_number-evade_started)+cartwheel_fallback+SOM_DODGE_GRACE;
            fprintf(stderr,"[som-ai] EVADE native dodge did not start (pose %02x/%02x): hop\n",
                    hero[0x01C],hero[0x011]);
        }else cartwheel_fallback=0;
    }
    if(!cartwheel_native&&!evade_blink&&!(ukemi_draw.active&&ukemi_draw.pose_duration)){
        if(trail_owner!=som_actor)memset(trail_frame,0,sizeof(trail_frame));
        trail_owner=som_actor;
        trail_x[slot]=x;trail_y[slot]=y;trail_frame[slot]=frame_number;trail_until=frame_number+10u;
    }
    if(!evade_xp_given&&som_actor_class()<SOM_DISCIPLINE_COUNT){
        const int near=som_bun_toucher(ram,x,y,SOM_XP_NEAR_MISS_PX,SOM_XP_NEAR_MISS_PX,familiar.slot);
        if(near>=0){
            evade_xp_given=1;
            som_xp_give(som_actor_class(),SOM_XP_DODGE,
                        ram[0xE600u+0x200u*(unsigned)near+0x180u],SOM_XPK_DODGE,"near miss");
        }
    }
    if(!hop_frames_left)return;
    const unsigned place=som_hero_place(hero);
    int moved=0;
    const int x0=x,y0=y;
    if(hop_dx&&som_bun_spot_open(ram,x+hop_dx,y,place)&&som_camera_step_ok(ram,hero,0u,hop_dx)){x+=hop_dx;moved=1;}
    if(hop_dy&&som_bun_spot_open(ram,x,y+hop_dy,place)&&som_camera_step_ok(ram,hero,1u,hop_dy)){y+=hop_dy;moved=1;}
    hero[2]=(uint8_t)x;hero[3]=(uint8_t)(x>>8);hero[4]=(uint8_t)y;hero[5]=(uint8_t)(y>>8);
    hero[0x006]=hero[0x007]=0;
    som_camera_follow_axis(ram,hero,0u,x-x0,1);som_camera_follow_axis(ram,hero,1u,y-y0,1);
    if(issen_active)som_issen_cut(ram,x,y);
    hop_frames_left=moved?hop_frames_left-1u:0u;
    if(evade_blink&&!hop_frames_left)som_evade_blink_end(ram);
}
static void som_evade_blink_end(uint8_t *ram){
    if(!evade_blink)return;
    evade_blink=0;
    if(!ram)return;
    if(evade_blink_hid)SOM_ACT(ram)[0x00E]&=(uint8_t)~0x80u;
    evade_blink_hid=0;
    som_bun_boom(bunshin_booms,(int)som_bun_u16(SOM_ACT(ram)+0x002),(int)som_bun_u16(SOM_ACT(ram)+0x004),1,frame_number);
}
static void som_issen_cut(uint8_t *ram,int x,int y){
    for(unsigned slot=0;slot<3u;slot++){
        if((int)slot==familiar.slot||(issen_hit&(1u<<slot)))continue;
        uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
        if(!som_bun_monster_alive(m)||
           !som_bun_near((int)som_bun_u16(m+2),(int)som_bun_u16(m+4),x,y,
                         SOM_ISSEN_REACH,SOM_ISSEN_REACH))continue;
        const unsigned damage=16u+2u*disciplines.level[SOM_DISC_BUSHIDO];
        const unsigned pending=m[0x1F1]+damage;
        m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
        issen_hit|=1u<<slot;som_monster_hit((unsigned)slot);
        som_bun_boom(bunshin_booms,(int)som_bun_u16(m+2),(int)som_bun_u16(m+4),1,frame_number);
        fprintf(stderr,"[som-ai] ISSEN cut monster %u for %u\n",slot,damage);
    }
}
static void som_class_damage(uint8_t *ram,unsigned slot,unsigned damage){
    uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
    const unsigned pending=m[0x1F1]+damage;
    m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
    som_monster_hit((unsigned)slot);
    som_bun_boom(bunshin_booms,(int)som_bun_u16(m+2),(int)som_bun_u16(m+4),1,frame_number);
}
/* ---- Companion class moves (src/som_ally_moves.h) ----
 * Each companion has her own class energy and AP (not the boy's), so her
 * techniques never empty his bar. Damage goes through the monster's
 * pending-damage byte like the boy's techniques, without his kill credit
 * (perks are his); heals through the hero's queued-heal byte +0x1F3, as
 * the candy does. */
typedef struct {
    SomEnergy en;
    unsigned long ready,dodge_ready,next_hit;
    int dashing,dodging,target,tx,ty,lx,ly;
    unsigned hits_left,amount,dodge_left,cls;
    int ddx,ddy;
} SomAllyTech;
static SomAllyTech ally_tech[2];
static SomEnergy *som_actor_energy(void){
    return som_actor&&som_actor<SOM_PARTY_MEMBERS?&ally_tech[som_actor-1u].en:&energy;
}
static int ally_tech_ready;
#define SOM_ALLY_DODGE_COOLDOWN 150ul
/* Keep-back (player, 2026-11-22: "no one respects long range"): the game's
 * ally AI walks a bow or caster companion into melee. While a monster is
 * within SOM_ALLY_KEEP_PX (x+y) and she is not swinging or casting, the
 * proxy walks her straight away from it SOM_ALLY_KEEP_SPEED px per frame,
 * faster than the AI brings her in, so she hovers at her range. Not while
 * the player drives her. */
#define SOM_ALLY_KEEP_PX 40
#define SOM_ALLY_KEEP_SPEED 2
static int som_ally_keeps_back(unsigned cls){
    return cls<SOM_DISCIPLINE_COUNT&&
        (!som_weapon_short_range(som_disc_weapons[cls][0])||som_disc_caster(cls));
}
static uint8_t *som_ally_obj(uint8_t *ram,unsigned who){return ram+0xE000u+0x200u*who;}
static int som_ally_alive(const uint8_t *o){
    return o[0]==1&&(o[0x184]|o[0x185])&&(o[0x182]|o[0x183]);
}
static void som_ally_hurt(uint8_t *ram,unsigned slot,unsigned damage){
    uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
    const unsigned pending=m[0x1F1]+damage;
    m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
    som_bun_boom(bunshin_booms,(int)som_bun_u16(m+2),(int)som_bun_u16(m+4),1,frame_number);
}
static void som_ally_heal(uint8_t *o,unsigned amount){
    const unsigned queued=o[0x1F3]+amount;
    o[0x1F3]=(uint8_t)(queued>0xFFu?0xFFu:queued);
}
/* One step of a carried move (dash or dodge): px toward the goal, only onto
 * open ground. 0 when blocked or there. */
static int som_ally_step(uint8_t *ram,uint8_t *o,int dx,int dy){
    const int x=(int)som_bun_u16(o+2),y=(int)som_bun_u16(o+4);
    if(!dx&&!dy)return 0;
    if(!som_bun_spot_open(ram,x+dx,y+dy,som_hero_place(o)))return 0;
    som_put_word(o+2,(uint16_t)(x+dx));som_put_word(o+4,(uint16_t)(y+dy));
    return 1;
}
static int som_rescue_threat(const uint8_t *ram,int x,int y,unsigned place){
    int nearest=SOM_TK_RANGE+1;
    for(unsigned slot=0;slot<3u;slot++){
        const uint8_t *enemy=ram+SOM_BUN_MONSTER_BASE+slot*SOM_BUN_OBJECT;
        if((int)slot==familiar.slot||!som_bun_monster_alive(enemy)||
           (enemy[0x0B]&0x80u)!=(place&0x80u))continue;
        const int distance=abs((int)som_word(enemy+2)-x)+abs((int)som_word(enemy+4)-y);
        if(distance<nearest)nearest=distance;
    }
    return nearest;
}
static int som_rescue_path_open(const uint8_t *ram,int x,int y,int nx,int ny,unsigned place){
    const int dx=nx-x,dy=ny-y;
    const int steps=(abs(dx)+abs(dy)+3)/4;
    for(int step=1;step<steps;step++)
        if(!som_bun_spot_open(ram,x+dx*step/steps,y+dy*step/steps,place))return 0;
    return 1;
}
static int som_telekinesis_rescue(uint8_t *ram,unsigned who,int automatic){
    if(!som_tk_field(ram)||frame_number<rescue_ready[who])return 0;
    const uint8_t *caster=som_ally_obj(ram,who);
    if(!som_ally_alive(caster)||caster[0x60]||caster[0x1C]||
       (som_word(caster+0x190)&0xC160u))return 0;
    const int cx=(int)som_word(caster+2),cy=(int)som_word(caster+4);
    const unsigned place=caster[0x0B];
    int picked=-1,land_x=0,land_y=0;
    unsigned lowest=101;
    for(unsigned k=0;k<SOM_PARTY_MEMBERS;k++){
        const uint8_t *ally=som_ally_obj(ram,k);
        if(k==who||!som_ally_alive(ally)||ally[0x60]||
           (ally[0x0B]&0x83u)!=(place&0x83u))continue;
        const unsigned pct=som_word(ally+0x182)*100u/som_word(ally+0x184);
        if(pct>=lowest||(automatic&&pct>60u))continue;
        const int x=(int)som_word(ally+2),y=(int)som_word(ally+4);
        const int danger=som_rescue_threat(ram,x,y,place);
        if(danger>40||abs(x-cx)+abs(y-cy)>SOM_TK_RANGE)continue;
        static const int spots[][2]={{-28,0},{28,0},{0,-28},{0,28},
                                     {-20,-20},{20,-20},{-20,20},{20,20}};
        int safest=47;
        for(unsigned i=0;i<sizeof(spots)/sizeof(spots[0]);i++){
            const int nx=cx+spots[i][0],ny=cy+spots[i][1];
            const int threat=som_rescue_threat(ram,nx,ny,place);
            if(threat<=safest||threat<danger+24||
               !som_bun_spot_open(ram,nx,ny,place)||
               !som_rescue_path_open(ram,x,y,nx,ny,place))continue;
            int occupied=0;
            for(unsigned j=0;j<SOM_PARTY_MEMBERS;j++){
                const uint8_t *other=som_ally_obj(ram,j);
                if(j!=k&&som_ally_alive(other)&&
                   som_bun_near(nx,ny,(int)som_word(other+2),(int)som_word(other+4),12,10))
                    occupied=1;
            }
            if(occupied)continue;
            safest=threat;picked=(int)k;lowest=pct;land_x=nx;land_y=ny;
        }
    }
    if(picked<0)return 0;
    uint8_t *ally=som_ally_obj(ram,(unsigned)picked);
    som_bun_boom(bunshin_booms,(int)som_word(ally+2),(int)som_word(ally+4),1,frame_number);
    som_put_word(ally+2,(uint16_t)land_x);
    som_put_word(ally+4,(uint16_t)land_y);
    ally[0x06]=ally[0x07]=ally[0x08]=0;
    som_bun_boom(bunshin_booms,land_x,land_y,1,frame_number);
    rescue_ready[who]=frame_number+SOM_TK_COOLDOWN;
    char message[48];
    snprintf(message,sizeof(message),"^Telekinesis|%s rescued",som_party_names[picked]);
    som_overlay_say(&overlay,message,SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] TELEKINESIS %s blinked %s out of danger\n",
            som_party_names[who],som_party_names[picked]);
    return 1;
}
static int som_clamp_step(int d,int limit){return d>limit?limit:d<-limit?-limit:d;}
static int som_try_revivifier(uint8_t *ram,unsigned who,int switch_class){
    const uint8_t *row=som_rom_at(0xD02AD0u+64u*0x21u,16u);
    uint8_t *caster=som_ally_obj(ram,who);
    if(!row||!som_ally_alive(caster)||caster[0x186]<row[15]||
       (som_word(caster+0x190)&0xC1E0u)||revivals[who].deadline||
       som_most_hurt_hero(ram,0x21u)<0)return 0;
    uint8_t *selected=who?&disciplines.party[who-1u]:&disciplines.selected;
    const uint8_t previous=*selected;
    if(previous!=SOM_DISC_CLERIC&&!switch_class)return 0;
    /* Use Cleric for this cast without replacing the player's saved class. */
    *selected=SOM_DISC_CLERIC;
    const int started=som_cast_spell_by(ram,who,0x21u);
    *selected=previous;
    if(started){
        som_overlay_say(&overlay,"^Cleric|Revivifier",SOM_MESSAGE_FRAMES/3u);
        if(previous!=SOM_DISC_CLERIC)
            fprintf(stderr,"[som-ai] %s switched to Cleric to revive an ally\n",som_party_names[who]);
    }
    return started;
}
static void som_ally_tech_one(uint8_t *ram,unsigned who){
    SomAllyTech *t=&ally_tech[who-1u];
    uint8_t *o=som_ally_obj(ram,who);
    const unsigned cls=som_disc_member_class(&disciplines,who);
    const int manual=som_pad2_member(ram)==who;
    const int manual_move=manual&&(pad2_combo_actor==who||t->dashing||t->hits_left);
    if(cls>=SOM_DISCIPLINE_COUNT||!som_ally_alive(o)||(!manual_move&&(manual||o[0x2C]!=0))||
       som_pad1_member_in(ram)==who){
        if(cls<SOM_DISCIPLINE_COUNT&&som_ally_alive(o)){     /* the player drives her: */
            const int x=(int)som_bun_u16(o+2),y=(int)som_bun_u16(o+4);   /* her bar refills */
            som_energy_tick(&t->en,cls,disciplines.level[cls],x==t->lx&&y==t->ly);
            t->lx=x;t->ly=y;
        }
        if(t->dashing)o[0x0E]&=(uint8_t)~0x80u;
        t->dashing=t->dodging=0;t->hits_left=0;return;
    }
    if(cls!=t->cls){
        if(t->dashing)o[0x0E]&=(uint8_t)~0x80u;
        t->cls=cls;t->hits_left=0;t->dashing=t->dodging=0;
    }
    if(o[0x60]||o[0x1C]==0x40||o[0x1C]==0x80)return;
    if(!manual&&cls==SOM_DISC_CLERIC&&som_try_revivifier(ram,who,0))return;
    const SomAllyMove *mv=&som_ally_moves[cls];
    const unsigned level=disciplines.level[cls];
    const int x=(int)som_bun_u16(o+2),y=(int)som_bun_u16(o+4);
    som_energy_tick(&t->en,cls,level,x==t->lx&&y==t->ly);
    t->lx=x;t->ly=y;
    const int ordered=!manual&&frame_number<ally_order.until&&
        (som_word(ram+0xDC)&0x1FFu)==ally_order.map;
    const int support=ordered&&ally_order.slot==SOM_ALLY_SUPPORT_SLOT;
    if(support){
        if(t->dashing)o[0x0E]&=(uint8_t)~0x80u;
        t->hits_left=0;t->dashing=t->dodging=0;
        if(mv->kind!=SOM_AM_HEAL&&mv->kind!=SOM_AM_HEAL_ALL)return;
    }
    if(!manual&&cls==SOM_DISC_PSIONICS&&som_energy_can(&t->en,cls,10u,0u)&&
       som_telekinesis_rescue(ram,who,1)){
        som_energy_take(&t->en,cls,10u,0u);
        return;
    }
    if(t->dodging){
        const SomDodge d=som_disc_dodge(cls,0u);
        if(!t->dodge_left||!som_ally_step(ram,o,t->ddx*(int)d.speed,t->ddy*(int)d.speed))t->dodging=0;
        else t->dodge_left--;
        return;
    }
    if(t->dashing){
        const int dx=som_clamp_step(t->tx-x,SOM_ALLY_DASH_SPEED),dy=som_clamp_step(t->ty-y,SOM_ALLY_DASH_SPEED);
        if(som_ally_step(ram,o,dx,dy))return;
        t->dashing=0;                                     /* there, or blocked: cut */
        const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)t->target;
        if(t->target>=0&&som_bun_monster_alive(m)&&
           som_bun_near((int)som_bun_u16(m+2),(int)som_bun_u16(m+4),x,y,40,40))
            som_ally_hurt(ram,(unsigned)t->target,t->amount);
        o[0x0E]&=(uint8_t)~0x80u;                          /* seen again */
        return;
    }
    if(t->hits_left){
        if(frame_number<t->next_hit)return;
        const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)t->target;
        if(t->target<0||!som_bun_monster_alive(m)||
           !som_bun_near((int)som_bun_u16(m+2),(int)som_bun_u16(m+4),x,y,mv->reach+16,mv->reach+16)){
            t->hits_left=0;return;
        }
        som_ally_hurt(ram,(unsigned)t->target,t->amount);
        t->hits_left--;t->next_hit=frame_number+SOM_ALLY_HIT_GAP;
        return;
    }
    /* The scene: nearest monster, how many in reach, the most hurt member. */
    int nearest=-1,best=1<<30;unsigned crowd=0;
    for(unsigned slot=0;slot<3u;slot++){
        if((int)slot==familiar.slot)continue;
        const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
        if(!som_bun_monster_alive(m))continue;
        const int d=abs((int)som_bun_u16(m+2)-x)+abs((int)som_bun_u16(m+4)-y);
        if(d<best){best=d;nearest=(int)slot;}
        if(d<=mv->reach)crowd++;
    }
    unsigned worst=100u;int worst_who=-1;
    for(unsigned k=0;k<SOM_PARTY_MEMBERS;k++){
        const uint8_t *p=som_ally_obj(ram,k);
        if(!som_ally_alive(p))continue;
        const unsigned hp=som_word(p+0x182),max=som_word(p+0x184);
        const unsigned pct=max?hp*100u/max:100u;
        if(pct<worst){worst=pct;worst_who=(int)k;}
    }
    /* Her evade: ranged and caster classes keep their distance; a melee
     * class only when badly hurt (the game's AI must stand close to hit). */
    const unsigned own_pct=som_word(o+0x184)?som_word(o+0x182)*100u/som_word(o+0x184):100u;
    if(!manual&&!support&&nearest>=0&&best<=SOM_ALLY_DODGE_NEAR&&som_ally_dodges(cls)&&frame_number>=t->dodge_ready&&
       (!som_weapon_short_range(som_disc_weapons[cls][0])||som_disc_caster(cls)||own_pct<40u)&&
       som_energy_can(&t->en,cls,0u,SOM_ALLY_DODGE_AP)){
        const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)nearest;
        const int ax=x-(int)som_bun_u16(m+2),ay=y-(int)som_bun_u16(m+4);
        if(abs(ax)>=abs(ay)){t->ddx=ax>=0?1:-1;t->ddy=0;}else{t->ddx=0;t->ddy=ay>=0?1:-1;}
        t->dodging=1;t->dodge_left=som_disc_dodge(cls,0u).frames;
        t->dodge_ready=frame_number+SOM_ALLY_DODGE_COOLDOWN;
        som_energy_take(&t->en,cls,0u,SOM_ALLY_DODGE_AP);
        fprintf(stderr,"[som-ai] ALLY %s %s\n",som_party_names[who],som_disc_dodge(cls,0u).name);
        return;
    }
    if(!manual&&!support&&!(cls==SOM_DISC_CLERIC&&worst<SOM_ALLY_HEAL_BELOW_PCT+15u)&&
       nearest>=0&&best<SOM_ALLY_KEEP_PX&&som_ally_keeps_back(cls)&&
       o[0x1C]==0&&o[0x60]==0&&som_pad1_member_in(ram)!=who){
        const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)nearest;
        const int ax=x-(int)som_bun_u16(m+2),ay=y-(int)som_bun_u16(m+4);
        const int sx=ax>=0?SOM_ALLY_KEEP_SPEED:-SOM_ALLY_KEEP_SPEED;
        const int sy=ay>=0?SOM_ALLY_KEEP_SPEED:-SOM_ALLY_KEEP_SPEED;
        /* The axis she is closer on first, then the other (a wall behind). */
        if(abs(ax)<=abs(ay)?(som_ally_step(ram,o,0,sy)||som_ally_step(ram,o,sx,0)):
                            (som_ally_step(ram,o,sx,0)||som_ally_step(ram,o,0,sy)))
            return;
    }
    if(ordered&&!support&&(int)ally_order.slot!=familiar.slot){
        const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*ally_order.slot;
        if(som_bun_monster_alive(m)&&m[0x180]==ally_order.identity){
            const int distance=abs((int)som_bun_u16(m+2)-x)+abs((int)som_bun_u16(m+4)-y);
            if(distance<=mv->reach){nearest=(int)ally_order.slot;best=distance;}
        }
    }
    if(frame_number<t->ready||!som_ally_move_fits(mv,nearest>=0?best:-1,worst,crowd)||
       !som_energy_can(&t->en,cls,mv->energy,0u))return;
    t->amount=som_ally_move_amount(mv,level);
    const uint8_t *m=nearest>=0?ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)nearest:NULL;
    const int mx=m?(int)som_bun_u16(m+2):x,my=m?(int)som_bun_u16(m+4):y;
    switch(mv->kind){
    case SOM_AM_AREA:
        for(unsigned slot=0;slot<3u;slot++){
            const uint8_t *e=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
            if((int)slot!=familiar.slot&&som_bun_monster_alive(e)&&
               som_bun_near((int)som_bun_u16(e+2),(int)som_bun_u16(e+4),x,y,mv->reach,mv->reach))
                som_ally_hurt(ram,slot,t->amount);
        }
        som_bun_boom(bunshin_booms,x,y,0,frame_number);
        break;
    case SOM_AM_MULTI:
        t->target=nearest;t->hits_left=mv->hits;t->next_hit=frame_number;
        break;
    case SOM_AM_DASH:{                     /* to the far side, else the near side */
        const int sx=mx>=x?1:-1,sy=my>=y?1:-1;
        const int horizontal=abs(mx-x)>=abs(my-y);
        int gx=horizontal?mx+sx*20:mx,gy=horizontal?my:my+sy*20;
        if(!som_bun_spot_open(ram,gx,gy,som_hero_place(o))){
            gx=horizontal?mx-sx*20:mx;gy=horizontal?my:my-sy*20;
        }
        t->target=nearest;t->tx=gx;t->ty=gy;t->dashing=1;
        o[0x0E]|=0x80u;                    /* a blur: hidden while she travels */
        som_bun_boom(bunshin_booms,x,y,1,frame_number);
        break;}
    case SOM_AM_BOLT:
        som_bun_blast(ram,mx,my,t->amount,familiar.slot);
        som_bun_boom(bunshin_booms,mx,my,0,frame_number);
        break;
    case SOM_AM_RANGED:{
        unsigned shots=0;
        for(unsigned slot=0;slot<3u&&shots<3u;slot++){
            const uint8_t *e=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
            if((int)slot!=familiar.slot&&som_bun_monster_alive(e)&&
               abs((int)som_bun_u16(e+2)-x)+abs((int)som_bun_u16(e+4)-y)<=mv->reach){
                som_ally_hurt(ram,slot,t->amount);shots++;
            }
        }
        break;}
    case SOM_AM_HEAL:
        if(worst_who<0)return;
        som_ally_heal(som_ally_obj(ram,(unsigned)worst_who),t->amount);
        som_bun_boom(bunshin_booms,(int)som_bun_u16(som_ally_obj(ram,(unsigned)worst_who)+2),
                     (int)som_bun_u16(som_ally_obj(ram,(unsigned)worst_who)+4),0,frame_number);
        break;
    case SOM_AM_HEAL_ALL:
        for(unsigned k=0;k<SOM_PARTY_MEMBERS;k++){
            uint8_t *member=som_ally_obj(ram,k);
            if(som_ally_alive(member)&&som_word(member+0x182)<som_word(member+0x184))
                som_ally_heal(member,t->amount);
        }
        som_bun_boom(bunshin_booms,x,y,0,frame_number);
        break;
    default:return;
    }
    som_energy_take(&t->en,cls,mv->energy,0u);
    t->ready=frame_number+SOM_ALLY_MOVE_COOLDOWN;
    char text[48];
    snprintf(text,sizeof(text),"^%s|%s",som_party_names[who],mv->name);
    som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/3u);
    fprintf(stderr,"[som-ai] ALLY %s %s (%s %u)\n",som_party_names[who],mv->name,
            mv->kind>=SOM_AM_HEAL?"heal":"damage",t->amount);
}
static void som_ally_tech_frame(uint8_t *ram){
    if(!ram)return;
    if(ally_order.until&&(som_word(ram+0xDC)&0x1FFu)!=ally_order.map)
        ally_order.until=0;
    if(!ally_tech_ready){
        for(unsigned i=0;i<2u;i++){memset(&ally_tech[i],0,sizeof(ally_tech[i]));
            som_energy_init(&ally_tech[i].en);ally_tech[i].target=-1;ally_tech[i].cls=SOM_DISCIPLINE_NONE;}
        ally_tech_ready=1;
    }
    if(ram[0xE000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||ram[0x7D]==1)return;
    if(ring_ai_enabled&&som_pad1_member_in(ram)==0u&&disciplines.selected==SOM_DISC_PSIONICS&&
       som_energy_can(&energy,SOM_DISC_PSIONICS,10u,0u)&&som_telekinesis_rescue(ram,0u,1))
        som_energy_take(&energy,SOM_DISC_PSIONICS,10u,0u);
    for(unsigned who=1;who<SOM_PARTY_MEMBERS;who++)som_ally_tech_one(ram,who);
    if(ring_ai_enabled){
        const unsigned driver=som_pad1_member_in(ram);
        if(!som_try_revivifier(ram,driver,1)){
            for(unsigned who=0;who<SOM_PARTY_MEMBERS;who++)
                if(who!=driver&&!som_ally_obj(ram,who)[0x2C]&&som_try_revivifier(ram,who,1))break;
        }
    }
}
static int som_class_move_raw(uint8_t *ram){
    const unsigned d=som_actor_class();
    if(!ram||evade_active||blink_active||hundred_until)return 0;
    const char *name=d==SOM_DISC_SWORD?"Whirlwind":d==SOM_DISC_ROGUE?"Shadowstep":
                     d==SOM_DISC_HUNTER?"Grand Cross":d==SOM_DISC_ARTIFICER?"Mine":"Volley";
    if(d==SOM_DISC_ARTIFICER){                 /* a mine where he stands */
        uint8_t *hero=SOM_ACT(ram);
        unsigned free_slot=SOM_MINES;
        for(unsigned i=0;i<SOM_MINES;i++)if(!mine_live[i]){free_slot=i;break;}
        if(free_slot==SOM_MINES||frame_number<class_move_ready){
            som_overlay_say(&overlay,free_slot==SOM_MINES?"^Mines|all set":"^Mine|not ready",
                            SOM_MESSAGE_FRAMES/3u);
            return 0;
        }
        mine_x[free_slot]=(int)som_bun_u16(hero+2);mine_y[free_slot]=(int)som_bun_u16(hero+4);
        mine_map[free_slot]=som_bun_u16(ram+0xDC);mine_live[free_slot]=1;
        class_move_ready=frame_number+SOM_MINE_COOLDOWN;
        som_bun_boom(bunshin_booms,mine_x[free_slot],mine_y[free_slot],1,frame_number);
        som_overlay_say(&overlay,"^Mine set",SOM_MESSAGE_FRAMES/3u);
        fprintf(stderr,"[som-ai] MINE set at %d,%d\n",mine_x[free_slot],mine_y[free_slot]);
        return 1;
    }
    if(frame_number<class_move_ready){
        char text[40];snprintf(text,sizeof(text),"^%s|not ready",name);
        som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/3u);return 0;
    }
    if(!som_jutsu_free(ram))return 0;
    uint8_t *hero=SOM_ACT(ram);
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    unsigned hit=0;
    if(d==SOM_DISC_SWORD){
        hit=som_bun_blast(ram,hx,hy,14u+2u*disciplines.level[SOM_DISC_SWORD],familiar.slot);
        for(unsigned slot=0;slot<3u;slot++)if(monster_hit_frame[slot]!=frame_number&&(int)slot!=familiar.slot){
            const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
            if(som_bun_monster_alive(m)&&som_bun_near((int)som_bun_u16(m+2),(int)som_bun_u16(m+4),
                                                      hx,hy,SOM_BUN_BLAST_X,SOM_BUN_BLAST_Y))
                som_monster_hit((unsigned)slot);
        }
        static const int ring[4][2]={{-20,0},{20,0},{0,-20},{0,20}};
        for(unsigned k=0;k<4u;k++)som_bun_boom(bunshin_booms,hx+ring[k][0],hy+ring[k][1],1,frame_number);
        class_move_ready=frame_number+SOM_WHIRL_COOLDOWN;
    }else if(d==SOM_DISC_HUNTER){              /* Grand Cross: all around */
        for(unsigned slot=0;slot<3u;slot++){
            if((int)slot==familiar.slot)continue;
            const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
            if(!som_bun_monster_alive(m)||
               !som_bun_near((int)som_bun_u16(m+2),(int)som_bun_u16(m+4),hx,hy,
                             SOM_CROSS_RANGE,SOM_CROSS_RANGE))continue;
            som_class_damage(ram,slot,20u+3u*disciplines.level[SOM_DISC_HUNTER]);
            hit++;
        }
        for(int k=-2;k<=2;k++){
            som_bun_boom(bunshin_booms,hx+k*24,hy,0,frame_number);
            if(k)som_bun_boom(bunshin_booms,hx,hy+k*24,0,frame_number);
        }
        class_move_ready=frame_number+SOM_CROSS_COOLDOWN;
    }else if(d==SOM_DISC_ROGUE){
        int best=-1;long best_d=0;
        for(unsigned slot=0;slot<3u;slot++){
            if((int)slot==familiar.slot)continue;
            const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
            if(!som_bun_monster_alive(m))continue;
            const long dist=labs((long)som_bun_u16(m+2)-hx)+labs((long)som_bun_u16(m+4)-hy);
            if(dist<=SOM_SHADOW_RANGE&&(best<0||dist<best_d)){best=(int)slot;best_d=dist;}
        }
        if(best<0){som_overlay_say(&overlay,"^Shadowstep|no target",SOM_MESSAGE_FRAMES/3u);return 0;}
        const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)best;
        const int mx=(int)som_bun_u16(m+2),my=(int)som_bun_u16(m+4);
        int bx=mx,by=my;
        if(abs(mx-hx)>=abs(my-hy))bx+=mx>=hx?SOM_SHADOW_BEHIND:-SOM_SHADOW_BEHIND;
        else by+=my>=hy?SOM_SHADOW_BEHIND:-SOM_SHADOW_BEHIND;
        if(!som_bun_spot_open(ram,bx,by,som_hero_place(hero))){
            som_overlay_say(&overlay,"^Shadowstep|no room behind it",SOM_MESSAGE_FRAMES/3u);return 0;
        }
        som_blink_go(ram,bx,by);
        backstab_until=frame_number+SOM_BACKSTAB_FRAMES;
        class_move_ready=frame_number+SOM_SHADOW_COOLDOWN;
        hit=1;
    }else{
        const unsigned facing=hero[0x010];
        int fx=0,fy=0;
        if(facing==0x02u)fx=1;else if(facing==0x82u)fx=-1;else if(facing==0x01u)fy=1;else fy=-1;
        for(unsigned slot=0;slot<3u;slot++){
            if((int)slot==familiar.slot)continue;
            const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
            if(!som_bun_monster_alive(m))continue;
            const int ox=(int)som_bun_u16(m+2)-hx,oy=(int)som_bun_u16(m+4)-hy;
            const int ahead=ox*fx+oy*fy,side=abs(fx?oy:ox);
            if(ahead<=0||ahead>SOM_VOLLEY_RANGE||side>16+ahead/3)continue;
            som_class_damage(ram,slot,10u+disciplines.level[SOM_DISC_ARCHER]);
            hit++;
        }
        class_move_ready=frame_number+SOM_VOLLEY_COOLDOWN;
    }
    char text[48];snprintf(text,sizeof(text),"^%s!",name);
    som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] %s: %u target(s)\n",name,hit);
    return 1;
}
/* L2+X (monk): Hundred Fists. */
static int som_hundred_start_raw(uint8_t *ram){
    if(!ram||som_actor_class()!=SOM_DISC_MARTIAL||hundred_until||evade_active)return 0;
    if(frame_number<hundred_ready){
        som_overlay_say(&overlay,"^Hundred Fists|not ready",SOM_MESSAGE_FRAMES/3u);return 0;
    }
    if(!som_jutsu_free(ram))return 0;
    hundred_until=frame_number+SOM_HUNDRED_FRAMES;
    hundred_ready=frame_number+SOM_HUNDRED_COOLDOWN;
    som_overlay_say(&overlay,"^Hundred Fists!",SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] HUNDRED FISTS\n");
    return 1;
}
static void som_hundred_frame(uint8_t *ram){
    uint8_t *hero=SOM_ACT(ram);
    if(frame_number>=hundred_until||hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0){
        hundred_until=0;return;
    }
    if((hundred_until-frame_number)%SOM_HUNDRED_TICK)return;
    const unsigned facing=hero[0x010];
    int dx=0,dy=0;
    if(facing==0x02u)dx=1;else if(facing==0x82u)dx=-1;else if(facing==0x01u)dy=1;else dy=-1;
    const int fx=(int)som_bun_u16(hero+2)+dx*SOM_HUNDRED_AHEAD,
              fy=(int)som_bun_u16(hero+4)+dy*SOM_HUNDRED_AHEAD;
    for(unsigned slot=0;slot<3u;slot++){
        if((int)slot==familiar.slot)continue;
        uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
        if(!som_bun_monster_alive(m)||
           !som_bun_near((int)som_bun_u16(m+2),(int)som_bun_u16(m+4),fx,fy,
                         SOM_HUNDRED_REACH,SOM_HUNDRED_REACH))continue;
        const unsigned damage=3u+disciplines.level[SOM_DISC_MARTIAL]/2u;
        const unsigned pending=m[0x1F1]+damage;
        m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
        som_monster_hit((unsigned)slot);
    }
}
/* L2+X (samurai): Issen, the dash-cut. */
static int som_issen_start_raw(uint8_t *ram){
    if(!ram||som_actor_class()!=SOM_DISC_BUSHIDO||evade_active)return 0;
    if(frame_number<issen_ready){
        som_overlay_say(&overlay,"^Issen|not ready",SOM_MESSAGE_FRAMES/3u);return 0;
    }
    if(!som_jutsu_free(ram))return 0;
    uint8_t *hero=SOM_ACT(ram);
    const unsigned facing=hero[0x010];
    int dx=0,dy=0;
    if(facing==0x02u)dx=1;else if(facing==0x82u)dx=-1;else if(facing==0x01u)dy=1;else dy=-1;
    hop_dx=dx*SOM_ISSEN_SPEED;hop_dy=dy*SOM_ISSEN_SPEED;hop_frames_left=SOM_ISSEN_FRAMES;
    evade_frames=SOM_ISSEN_FRAMES+SOM_DODGE_GRACE;cartwheel_native=0;cartwheel_fallback=0;
    hero[0x006]=hero[0x007]=0;
    evade_hp=som_word(hero+0x182);
    evade_active=1;evade_started=frame_number;evade_xp_given=0;
    issen_active=1;issen_hit=0;issen_ready=frame_number+SOM_ISSEN_COOLDOWN;
    som_overlay_say(&overlay,"^Issen!",SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] ISSEN dash-cut toward %d,%d\n",dx,dy);
    return 1;
}
/* Untouchable without the Barrel status (0x0400 is what item 10, the
 * Barrel, toggles: it shows the hero as a barrel). Before and after every
 * emulated frame of the dodge the hit mask E059 (bit per attacking monster)
 * and the pending damage E1F1/E1F2 are cleared before the combat tick can
 * apply them (som-decomp hit-reactions.md 1-2, rom-combat.md 4.1), and any
 * HP lost anyway is put back. */
static void som_evade_shield(uint8_t *ram){
    uint8_t *hero=SOM_ACT(ram);
    hero[0x059]=0;hero[0x1F1]=hero[0x1F2]=0;
    const unsigned hp=som_word(hero+0x182);
    if(hp>evade_hp)evade_hp=hp;                       /* healed meanwhile */
    else if(hp<evade_hp&&hp){
        hero[0x182]=(uint8_t)evade_hp;hero[0x183]=(uint8_t)(evade_hp>>8);
        fprintf(stderr,"[som-ai] EVADE blocked %u damage\n",evade_hp-hp);
    }
}
/* After each frame: shield, and end the dodge by frame count (the move
 * plus SOM_DODGE_GRACE). */
static void som_evade_tick(void){
    if(!evade_active||!backend_retro_get_memory_data)return;
    uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram)return;
    som_evade_shield(ram);
    if(frame_number-evade_started<evade_frames&&SOM_ACT(ram)[0x000]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F)
        return;
    som_evade_blink_end(ram);
    if(native_restore_facing>=0){SOM_ACT(ram)[0x010]=(uint8_t)native_restore_facing;native_restore_facing=-1;}
    som_ukemi_end(ram);
    evade_active=0;hop_frames_left=0;cartwheel_native=0;issen_active=0;
}
/* After-images of the dodge, 4 and 8 frames behind, on even frames only
 * (a flicker reads as translucent; the boy's palette has no colour math). */
static void som_trail_draw(uint8_t *pixels,unsigned width,unsigned height,size_t pitch){
    if(!trail_until||frame_number>trail_until||(frame_number&1u)||!backend_retro_get_memory_data)return;
    const uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||backend_retro_get_memory_size(RETRO_MEMORY_VIDEO_RAM)<SOM_BUN_VRAM_SIZE)return;
    const uint8_t *vram=backend_retro_get_memory_data(RETRO_MEMORY_VIDEO_RAM);
    const uint8_t *hero=ram+0xE000u+0x200u*trail_owner;
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    for(unsigned back=8u;back>=4u;back-=4u){
        if(frame_number<back)continue;
        const unsigned slot=(unsigned)((frame_number-back)%SOM_TRAIL_LENGTH);
        if(!trail_frame[slot]||trail_frame[slot]!=frame_number-back)continue;
        const int dx=trail_x[slot]-hx,dy=trail_y[slot]-hy;
        if(dx||dy)som_bun_draw_copy_oam(pixels,width,height,pitch,pixel_format,ram,
                                        oam_shown_valid?oam_shown:ram+0x0800u,vram,trail_owner,dx,dy);
    }
}
/* Charge the game's actual weapon stage through its normal B input. */
static int som_special_start(unsigned which,unsigned stage){
    if(which>1||som_actor_class()>=SOM_DISCIPLINE_COUNT)return 0;
    return som_special_start_weapon(som_disc_weapons[som_actor_class()][which],stage,0);
}
/* The Monk's glove: every power level, whatever the game has it learned
 * (player, 2026-11-20). E19C is the held weapon's learned level; raised
 * while the monk holds the glove, given back otherwise. */
#define SOM_MONK_GLOVE_LEVELS 8u
static int monk_glove_raised=-1;
static unsigned monk_glove_who;           /* whose level is raised */
static void som_monk_glove_restore(uint8_t *ram){
    uint8_t *o=ram+0xE000u+0x200u*monk_glove_who;
    if(monk_glove_raised>=0&&o[0x1E4]==0u&&o[0x19C]==SOM_MONK_GLOVE_LEVELS)
        o[0x19C]=(uint8_t)monk_glove_raised;
    monk_glove_raised=-1;
}
static void som_monk_glove(uint8_t *ram){
    if(!ram)return;
    if(monk_glove_raised>=0&&monk_glove_who!=som_actor)som_monk_glove_restore(ram);
    /* Plain field play only: menus, dialogs and the inn's save (events,
     * 7E:00D0 set) see the learned level, so it is never saved raised. */
    const int monk_glove=som_actor_class()==SOM_DISC_MARTIAL_ARTS&&SOM_ACT(ram)[0x1E4]==0u&&
                         SOM_ACT(ram)[0x000]==1u&&ram[0xE4]==1u&&ram[0xE6]==0x0Fu&&ram[0xD0]==0u;
    if(monk_glove){
        if(SOM_ACT(ram)[0x19C]<SOM_MONK_GLOVE_LEVELS){
            if(monk_glove_raised<0){monk_glove_raised=SOM_ACT(ram)[0x19C];monk_glove_who=som_actor;}
            SOM_ACT(ram)[0x19C]=SOM_MONK_GLOVE_LEVELS;
        }
    }else if(monk_glove_raised>=0)som_monk_glove_restore(ram);
}
static int som_special_start_weapon(unsigned weapon,unsigned stage,int quiet){
    if(stage<1||stage>SOM_SEALS_MAX||special_stage||bash_stage||
       (ring_ai_enabled&&!special_from_ai)||som_manual_disciplines||
       som_actor_class()>=SOM_DISCIPLINE_COUNT||
       !backend_retro_get_memory_data)return 0;
    uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||SOM_ACT(ram)[0x000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||
       ram[0xD0]!=0||ram[0x7D]==1||SOM_ACT(ram)[0x1ED]!=0)return 0;
    if(som_actor&&!som_ally_idle(ram,som_actor))return 0;
    const int equipped=som_actor?som_equip_ally_weapon(ram,som_actor,weapon):
        som_equip_weapon(ram,weapon);
    if(equipped!=SOM_EQUIP_OK)return 0;
    som_monk_glove(ram);
    if(!SOM_ACT(ram)[0x19C]||SOM_ACT(ram)[0x19B]!=0){
        fprintf(stderr,"[som-ai] SPECIAL refused: %s needs charge level %u\n",
                som_weapon_types[weapon],stage);
        return 0;
    }
    if(SOM_ACT(ram)[0x19C]<stage&&quiet)stage=SOM_ACT(ram)[0x19C];   /* a chain finisher: the top learned */
    if(SOM_ACT(ram)[0x19C]<stage){                    /* only the levels learned */
        char text[64];
        snprintf(text,sizeof(text),"^%s level %u|not learned: level %u",
                 som_weapon_types[weapon],stage,(unsigned)SOM_ACT(ram)[0x19C]);
        som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES);
        stage=SOM_ACT(ram)[0x19C];
    }
    special_stage=stage;special_weapon=weapon;special_started=frame_number;
    som_combo_clear();
    fprintf(stderr,"[som-ai] SPECIAL charging %s level %u\n",
            som_weapon_types[weapon],stage);
    return 1;
}
/* The B string's weapon by class: the monk's glove, the samurai's katana
 * (player, 2026-11-23: "MONK IT TOTALLY BEST ... SAME RECIPE BUT FOR
 * SAMURAI"; sword power attacks follow the glove's rule, E011 = 4 * stage
 * + class, som-decomp weapons-melee.md 4.1). SOM_NO_WEAPON: no string. */
static unsigned som_string_weapon(unsigned cls){
    return cls==SOM_DISC_MARTIAL_ARTS?0u:cls==SOM_DISC_BUSHIDO?1u:
           cls==SOM_DISC_ARCHERY?5u:
           som_blade_string(cls)?som_disc_weapons[cls][0]:SOM_NO_WEAPON;
}
/* Martial Arts' B: a Ninja Storm string with Tekken commands (player,
 * 2026-11-23: "somewhere between Naruto Ninja Storm B+B+B+B+B+B and
 * Tekken's ->-> + B"; command moves are free, "the command is the
 * penalty"). The glove's moves are the game's own: the normal swings
 * (E01C = 0x20, E011 = variant 0-3) and the power attacks of levels 1-7,
 * started the way the game's release does it ($C0:B21C, som-decomp
 * glove-attacks.md 3, 4.1: E01C = 0x80, E006 = E007 = 0, E011 = 4 * level +
 * distance class 0 near / 1 mid / 2 far, 2.2), with no charge.
 *
 *   B B B B   four quick strikes (swing variants 1, 0, 3, 2)
 *   B         the level-1 flurry
 *   B         the finisher, by the direction held at the press:
 *             none level 3, toward the enemy 4, away 2, sideways 5
 *   toward, toward + B   level 7      away, toward + B   level 6
 *
 * Directions are taken against the nearest monster (the facing when none):
 * pressing away turns the hero in SoM, so "back" cannot be the facing. A
 * command needs its two D-pad presses within SOM_MONK_CMD_GAP frames and B
 * within SOM_MONK_CMD_GAP of the second; it starts at once from standing or
 * cancels a string move. A B during a move is kept and starts the next one
 * the frame it ends (the game drops presses during a swing).
 * SOM_MONK_CHAIN_RESET frames after a move without a press start the
 * string over. Damage stage E19B (x(2s+4)/4, 5.2): strikes 0, flurry 1,
 * finishers 2, commands 3. */
#define SOM_MONK_CHAIN_RESET 90ul
#define SOM_MONK_CMD_GAP 15ul
#define SOM_MONK_STRIKES 4u
enum{SOM_MONK_TOWARD=1,SOM_MONK_AWAY=2,SOM_MONK_SIDE=3};
static const uint8_t som_monk_strike_variant[SOM_MONK_STRIKES]={1,0,3,2};
static unsigned monk_chain_step,monk_chain_who;
static int monk_chain_active,monk_chain_queued,monk_chain_queued_cmd;
static unsigned long monk_chain_end;
static unsigned monk_dir_last,monk_dir_prev;       /* SOM_MONK_* of the last D-pad presses */
static unsigned long monk_dir_last_at,monk_dir_prev_at;
/* The nearest monster's direction from the hero as a unit step on its
 * larger axis (the facing when no monster is near), and the near / mid /
 * far class the game uses for the variant. */
static unsigned som_monk_enemy(const uint8_t *ram,const uint8_t *o,int *ex,int *ey){
    const int hx=(int)som_word(o+2),hy=(int)som_word(o+4);
    unsigned best=3u;long best_d=-1;
    const unsigned f=o[0x010];
    *ex=f==0x02u?1:f==0x82u?-1:0;*ey=*ex?0:(f==0x01u?1:-1);
    for(unsigned k=0;k<3u;k++){
        const uint8_t *m=ram+0xE600u+0x200u*k;
        if(m[0]!=1||m[0x180]>=0x80||(int)k==familiar.slot)continue;
        const int dx=(int)som_word(m+2)-hx,dy=(int)som_word(m+4)-hy;
        const unsigned cx=abs(dx)<26?1u:abs(dx)<42?2u:3u,cy=abs(dy)<24?1u:abs(dy)<40?2u:3u;
        const unsigned c=cx>cy?cx:cy;
        if(c<best)best=c;
        const long d=labs((long)dx)+labs((long)dy);
        if(d<=160&&(best_d<0||d<best_d)){
            best_d=d;
            if(abs(dx)>=abs(dy)){*ex=dx>=0?1:-1;*ey=0;}else{*ex=0;*ey=dy>=0?1:-1;}
        }
    }
    return best-1u;
}
/* "Toward" is fixed at the first D-pad press of a run of presses (the
 * nearest enemy, else the facing as it was then): pressing away turns the
 * hero, so with no enemy near the second "away" read as "toward" (headless
 * 2026-11-23). SOM_MONK_REF_FRAMES after the last press it is taken anew. */
#define SOM_MONK_REF_FRAMES 45ul
static int monk_ref_x,monk_ref_y;
static unsigned long monk_ref_at;
static void som_monk_ref(const uint8_t *ram,const uint8_t *o,int *ex,int *ey){
    if(monk_ref_at&&frame_number-monk_ref_at<=SOM_MONK_REF_FRAMES){*ex=monk_ref_x;*ey=monk_ref_y;return;}
    (void)som_monk_enemy(ram,o,ex,ey);
}
static unsigned som_monk_dir_of(const uint8_t *ram,const uint8_t *o,uint16_t dirs){
    const int x=(dirs&(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT))?1:(dirs&(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT))?-1:0;
    const int y=(dirs&(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN))?1:(dirs&(1u<<RETRO_DEVICE_ID_JOYPAD_UP))?-1:0;
    if(!x&&!y)return 0;
    int ex,ey;som_monk_ref(ram,o,&ex,&ey);
    const int along=x*ex+y*ey;
    return along>0?SOM_MONK_TOWARD:along<0?SOM_MONK_AWAY:SOM_MONK_SIDE;
}
/* Each poll: remember the D-pad presses (for the commands). */
static void som_monk_chain_dirs(const uint8_t *ram,unsigned who,uint16_t pressed_dirs){
    if(!ram||!pressed_dirs)return;
    const uint8_t *o=ram+0xE000u+0x200u*who;
    if(!monk_ref_at||frame_number-monk_ref_at>SOM_MONK_REF_FRAMES)
        (void)som_monk_enemy(ram,o,&monk_ref_x,&monk_ref_y);
    monk_ref_at=frame_number;
    const unsigned d=som_monk_dir_of(ram,o,pressed_dirs);
    if(!d)return;
    monk_dir_prev=monk_dir_last;monk_dir_prev_at=monk_dir_last_at;
    monk_dir_last=d;monk_dir_last_at=frame_number;
}
/* The command the last two D-pad presses make with a B now: 7 toward,
 * toward; 6 away, toward; 9 away, away (the archer's); 0 none. */
#define SOM_CMD_AWAY_AWAY 9u
static unsigned som_monk_command(void){
    if(!monk_dir_last||!monk_dir_prev||frame_number-monk_dir_last_at>SOM_MONK_CMD_GAP||
       monk_dir_last_at-monk_dir_prev_at>SOM_MONK_CMD_GAP)return 0;
    if(monk_dir_last==SOM_MONK_AWAY)return monk_dir_prev==SOM_MONK_AWAY?SOM_CMD_AWAY_AWAY:0u;
    if(monk_dir_last!=SOM_MONK_TOWARD)return 0;
    return monk_dir_prev==SOM_MONK_TOWARD?7u:monk_dir_prev==SOM_MONK_AWAY?6u:0u;
}
/* Plays one move: a strike (level 0, swing variant) or a power attack. */
static void som_monk_play(uint8_t *ram,uint8_t *o,unsigned level,unsigned variant,unsigned stage,
                          const char *what){
    const unsigned cls=som_monk_enemy(ram,o,&(int){0},&(int){0});
    o[0x01C]=level?0x80u:0x20u;
    o[0x011]=(uint8_t)(level?4u*level+cls:variant);
    o[0x19B]=(uint8_t)stage;
    o[0x006]=o[0x007]=0;o[0x030]=0xFF;
    if(level){
        char text[32];snprintf(text,sizeof(text),"^%s|level %u",what,level);
        som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/4u);
    }
    fprintf(stderr,"[som-ai] MONK %s level %u (animation %u)\n",what,level,(unsigned)o[0x011]);
}
static int som_monk_ready(const uint8_t *ram,const uint8_t *o,unsigned who,int cancel){
    if(o[0]!=1||o[0x1E4]!=som_string_weapon(som_disc_member_class(&disciplines,who))||
       o[0x061]||o[0x060]||(o[0x191]&0x80u)||
       ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||ram[0x7D]==1)return 0;
    if(cancel)return monk_chain_active&&monk_chain_who==who&&(o[0x01C]==0x20u||o[0x01C]==0x80u);
    return !o[0x01C]&&(who?som_ally_idle(ram,who):som_field_idle(ram));
}
/* The archer's string (player, 2026-11-23: "archer needs an overhaul";
 * javelin moves in it "would be nice"). The bow has one shot animation for
 * every stage; the stage E19B at the launch (op FB, frame 21) sets how many
 * arrows fly: 0-2 one, 3-5 two, 6-8 three (som-decomp weapons-ranged.md
 * 3.1). The javelin has a script per stage, some with a lunge and a jump
 * (5.2). So a move holds E19B at its projectile stage until the launch,
 * then sets its own damage stage (the game keeps E19B while the volley
 * flies, x(2s+4)/4). The launcher fires nothing while an earlier volley
 * still flies (E061), so a move waits until the last volley will be gone
 * by its launch. Every archer move first turns to the nearest enemy.
 *
 *   B B B      shots (1 arrow)          B   Double Shot (2 arrows)
 *   B          finisher: none Triple Shot (3), toward Javelin Leap (stage 4,
 *              2 javelins), away Retreat Shot (hop back, 2 arrows),
 *              sideways Javelin Step (stage 3, 2 javelins)
 *   toward, toward + B   Javelin Strike (stage 8, 3 javelins)
 *   away, away + B       Falcon Retreat (hop back, 3 arrows)
 *   away, toward + B     Triple Shot */
typedef struct {
    uint8_t weapon,level,arrows,damage,hop,launch,free;   /* frames from the start */
    const char *name;
} SomArcherMove;
enum{SOM_ARC_SHOT,SOM_ARC_DOUBLE,SOM_ARC_TRIPLE,SOM_ARC_LEAP,SOM_ARC_STEP,
     SOM_ARC_RETREAT,SOM_ARC_FALCON,SOM_ARC_STRIKE};
static const SomArcherMove som_archer_moves[]={
    {5,0,0,0,0,21,43,"shot"},
    {5,3,3,1,0,21,55,"Double Shot"},
    {5,6,6,2,0,21,69,"Triple Shot"},
    {7,4,3,2,0,41,82,"Javelin Leap"},
    {7,3,3,1,0,16,56,"Javelin Step"},
    {5,3,3,1,1,21,55,"Retreat Shot"},
    {5,6,6,2,1,21,69,"Falcon Retreat"},
    {7,8,6,3,0,46,100,"Javelin Strike"},
};
#define SOM_ARCHER_SHOTS 3u
#define SOM_ARCHER_LATENCY 5u     /* the first animation step comes 0-4 frames late */
static int archer_hop_move=-1,archer_moving=-1;
static uint16_t monk_chain_held_dirs;      /* defined with som_monk_chain_press */
static unsigned long archer_launch_at,archer_free_at;
static int som_archer_class(unsigned who){
    return som_disc_member_class(&disciplines,who)==SOM_DISC_ARCHERY;
}
/* The hero can take the next move now (the move's launch comes after the
 * last volley is gone). */
static int som_archer_ready(const uint8_t *ram,const uint8_t *o,unsigned who,unsigned move){
    if(o[0]!=1||o[0x01C]||o[0x060]||(o[0x191]&0x80u)||!(o[0x182]|o[0x183])||
       ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||ram[0x7D]==1)return 0;
    if(o[0x1E4]!=5u&&o[0x1E4]!=7u)return 0;
    if(who&&!som_ally_idle(ram,who)&&o[0x19B]==0)return 0;
    return !o[0x061]||frame_number+som_archer_moves[move].launch>=archer_free_at;
}
static void som_archer_face(const uint8_t *ram,uint8_t *o){
    /* The nearest live enemy; with none in 160 px, "toward" as the command
     * was read (the hero has turned round pressing away). */
    int near=0;
    for(unsigned k=0;k<3u;k++){
        const uint8_t *m=ram+0xE600u+0x200u*k;
        if(m[0]!=1||m[0x180]>=0x80||(int)k==familiar.slot||(som_word(m+0x190)&0x8000u))continue;
        near|=labs((long)som_word(m+2)-(long)som_word(o+2))+
              labs((long)som_word(m+4)-(long)som_word(o+4))<=160;
    }
    int ex,ey;
    if(near)(void)som_monk_enemy(ram,o,&ex,&ey);else som_monk_ref(ram,o,&ex,&ey);
    o[0x010]=ex>0?0x02u:ex<0?0x82u:ey>0?0x01u:0x00u;
}
/* Starts `move` (after its hop, if it has one). */
static int som_archer_play(uint8_t *ram,unsigned who,unsigned move,int hop_done){
    uint8_t *o=ram+0xE000u+0x200u*who;
    const SomArcherMove *m=&som_archer_moves[move];
    if(m->hop&&!hop_done&&who==som_actor){
        int ex,ey;(void)som_monk_enemy(ram,o,&ex,&ey);
        const int keep_x=evade_dir_x,keep_y=evade_dir_y;
        evade_dir_x=-ex;evade_dir_y=-ey;
        const int hopped=som_evade_start_raw(ram);
        evade_dir_x=keep_x;evade_dir_y=keep_y;
        if(hopped){
            archer_hop_move=(int)move;
            fprintf(stderr,"[som-ai] ARCHER %s: hop back\n",m->name);
            return 1;
        }
    }
    if(!som_archer_ready(ram,o,who,move))return 0;
    if(o[0x1E4]!=m->weapon){
        const int r=who?som_equip_ally_weapon(ram,who,m->weapon):som_equip_weapon(ram,m->weapon);
        if(r==SOM_EQUIP_BUSY)return 0;                                     /* wait */
        if(r!=SOM_EQUIP_OK){
            if(m->weapon!=5u)return som_archer_play(ram,who,SOM_ARC_TRIPLE,1);   /* no javelin */
            return 0;
        }
    }
    som_archer_face(ram,o);
    const unsigned cls=som_monk_enemy(ram,o,&(int){0},&(int){0});
    o[0x01C]=m->level?0x80u:0x20u;
    o[0x011]=(uint8_t)(m->level?4u*m->level+cls:0u);
    o[0x19B]=m->arrows;
    o[0x006]=o[0x007]=0;o[0x030]=0xFF;
    archer_moving=(int)move;
    archer_launch_at=frame_number+m->launch+SOM_ARCHER_LATENCY;
    archer_free_at=frame_number+m->free+SOM_ARCHER_LATENCY;
    if(move!=SOM_ARC_SHOT){
        char text[32];snprintf(text,sizeof(text),"^%s",m->name);
        som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/4u);
    }
    fprintf(stderr,"[som-ai] ARCHER %s (%s stage %u, animation %u) at frame %lu, step %u\n",m->name,
            m->weapon==7u?"javelin":"bow",(unsigned)m->level,(unsigned)o[0x011],frame_number,
            monk_chain_step);
    return 1;
}
/* The archer's next move of the string, or the command `cmd`. */
static int som_archer_start(uint8_t *ram,unsigned who,uint16_t held_dirs,unsigned cmd){
    uint8_t *o=ram+0xE000u+0x200u*who;
    if(frame_number-monk_chain_end>SOM_MONK_CHAIN_RESET||monk_chain_who!=who)monk_chain_step=0;
    unsigned move,next=monk_chain_step+1u;
    if(cmd){
        move=cmd==7u?SOM_ARC_STRIKE:cmd==SOM_CMD_AWAY_AWAY?SOM_ARC_FALCON:SOM_ARC_TRIPLE;
        next=0;
    }else if(monk_chain_step<SOM_ARCHER_SHOTS)move=SOM_ARC_SHOT;
    else if(monk_chain_step==SOM_ARCHER_SHOTS)move=SOM_ARC_DOUBLE;
    else{
        const unsigned d=som_monk_dir_of(ram,o,held_dirs);
        move=d==SOM_MONK_TOWARD?SOM_ARC_LEAP:d==SOM_MONK_AWAY?SOM_ARC_RETREAT:
             d==SOM_MONK_SIDE?SOM_ARC_STEP:SOM_ARC_TRIPLE;
        next=0;
    }
    if(!som_archer_play(ram,who,move,0))return 0;
    monk_chain_step=next;
    if(cmd)monk_dir_last=monk_dir_prev=0;
    monk_chain_who=who;monk_chain_active=1;monk_chain_queued=0;monk_chain_queued_cmd=0;
    som_combo_clear();
    return 1;
}
/* Each frame of an archer move: its hop, its projectile stage, its end. */
static void som_archer_frame(uint8_t *ram){
    uint8_t *o=ram+0xE000u+0x200u*monk_chain_who;
    if(archer_hop_move>=0){
        if(evade_active)return;
        const int move=archer_hop_move;archer_hop_move=-1;
        if(!som_archer_play(ram,monk_chain_who,(unsigned)move,1)){
            monk_chain_active=0;monk_chain_end=frame_number;
        }
        return;
    }
    if(archer_moving>=0){
        const SomArcherMove *m=&som_archer_moves[archer_moving];
        if(frame_number<=archer_launch_at)o[0x19B]=m->arrows;          /* the count, at launch */
        else if(frame_number==archer_launch_at+1u&&o[0x061])o[0x19B]=m->damage;
    }
    if(o[0]==1&&(o[0x01C]==0x80u||o[0x01C]==0x20u)&&ram[0xE4]==1&&ram[0xD0]==0)return;
    if(archer_moving>=0&&o[0x01C]==0&&!o[0x061])o[0x19B]=0;           /* no volley out */
    if(archer_moving>=0)monk_chain_end=frame_number;     /* the move ended now */
    archer_moving=-1;
    if(monk_chain_queued&&som_archer_class(monk_chain_who)){
        if(som_archer_start(ram,monk_chain_who,monk_chain_held_dirs,(unsigned)monk_chain_queued_cmd))
            return;
        if(o[0x061]&&!o[0x01C])return;                                  /* waits for the volley */
    }
    monk_chain_active=0;monk_chain_end=frame_number;
    monk_chain_queued=0;monk_chain_queued_cmd=0;
}
/* The mixed-weapon strings (player, 2026-11-23: "Ninja, Swordman, WeaponFLow
 * and Rogue need this too"): one table per class, each move with its own
 * weapon, played like the monk's (a swing variant, or the power attack of a
 * level) and, for the bow and javelin, like the archer's (stage until the
 * launch, then the damage stage). hop: the class's dodge first, away from
 * (1) or toward (2) the enemy. extra: a sword wave (a painted light bolt),
 * the rogue's Sleep Flower (its MP; the move itself when it cannot be
 * cast), Shunshin or Kawarimi before the cut (their Chakra; the cut alone
 * when it cannot be paid). */
enum{SOM_BX_NONE,SOM_BX_WAVE,SOM_BX_SLEEP,SOM_BX_SHUNSHIN,SOM_BX_KAWARIMI};
#define SOM_BLADE_STEPS 5u
#define SOM_BLADE_WAIT 60ul          /* a kept press waits this long for a weapon swap */
#define SOM_BLADE_WAVE_FRAME 8ul
#define SOM_SLEEP_FLOWER 0x1Eu
typedef struct {
    uint8_t weapon,level,variant,arrows,damage,hop,extra,launch,free;
    const char *name;
} SomBladeMove;
typedef struct {
    unsigned cls;
    SomBladeMove step[SOM_BLADE_STEPS];
    SomBladeMove finish[4];                  /* none, toward, away, sideways */
    SomBladeMove toward,away;                /* toward, toward + B; away, toward + B */
} SomBladeString;
static const SomBladeString som_blade_strings[]={
    {SOM_DISC_SWORD,
     {{1,0,1,0,0,0,0,0,0,NULL},{1,0,0,0,0,0,0,0,0,NULL},{1,0,3,0,0,0,0,0,0,NULL},
      {1,0,2,0,0,0,0,0,0,NULL},{2,1,0,1,1,0,0,0,0,"Axe Cleave"}},
     {{1,3,0,2,2,0,SOM_BX_WAVE,0,0,"Saber Wave"},{1,4,0,2,2,0,0,0,0,"Thrust"},
      {1,2,0,2,2,1,0,0,0,"Step-Back Cut"},{1,5,0,2,2,0,0,0,0,"Wide Sweep"}},
     {1,7,0,3,3,0,0,0,0,"Bash Cut"},{1,6,0,3,3,0,0,0,0,"Parry Cut"}},
    {SOM_DISC_NINJUTSU,
     {{0,0,1,0,0,0,0,0,0,NULL},{0,0,0,0,0,0,0,0,0,NULL},{1,0,0,0,0,0,0,0,0,NULL},
      {0,0,3,0,0,0,0,0,0,NULL},{0,1,0,1,1,0,0,0,0,"Taijutsu"}},
     {{6,0,0,0,1,0,0,0,0,"Chakram"},{1,4,0,2,2,0,0,0,0,"Dash Cut"},
      {6,0,0,0,1,1,0,0,0,"Retreat Chakram"},{1,5,0,2,2,0,0,0,0,"Shadow Spin"}},
     {1,7,0,3,3,0,SOM_BX_SHUNSHIN,0,0,"Shunshin Cut"},
     {0,6,0,3,3,0,SOM_BX_KAWARIMI,0,0,"Kawarimi Fist"}},
    {SOM_DISC_WEAPON_FLOW,
     {{1,0,1,0,0,0,0,0,0,NULL},{2,0,0,0,0,0,0,0,0,NULL},{3,0,0,0,0,0,0,0,0,NULL},
      {4,0,0,0,0,0,0,0,0,NULL},{7,3,0,3,1,0,0,16,56,"Javelin Step"}},
     {{2,3,0,2,2,0,0,0,0,"Axe Breaker"},{7,4,0,3,2,0,0,41,82,"Javelin Leap"},
      {4,2,0,2,2,0,0,0,0,"Whip Guard"},{3,5,0,2,2,0,0,0,0,"Spear Wheel"}},
     {3,7,0,3,3,0,0,0,0,"Spear Charge"},{5,6,0,6,2,0,0,21,69,"Triple Shot"}},
    {SOM_DISC_ROGUE,
     {{1,0,0,0,0,0,0,0,0,NULL},{1,0,1,0,0,0,0,0,0,NULL},{1,0,0,0,0,0,0,0,0,NULL},
      {1,0,2,0,0,0,0,0,0,NULL},{5,3,0,3,1,0,0,21,55,"Quick Shot"}},
     {{1,3,0,2,2,0,SOM_BX_SLEEP,0,0,"Cruel Cut"},{1,4,0,2,2,0,0,0,0,"Backstab"},
      {5,3,0,3,1,1,0,21,55,"Cartwheel Shot"},{1,5,0,2,2,0,0,0,0,"Side Stab"}},
     {1,7,0,3,3,2,0,0,0,"Slip Past"},{5,6,0,6,2,1,0,21,69,"Smoke Hop"}},
};
static const SomBladeMove *blade_move,*blade_hop_move;
static unsigned long blade_launch_at,blade_free_at,blade_started,blade_wait_at;
static const SomBladeString *som_blade_of(unsigned cls){
    for(unsigned i=0;i<sizeof(som_blade_strings)/sizeof(som_blade_strings[0]);i++)
        if(som_blade_strings[i].cls==cls)return &som_blade_strings[i];
    return NULL;
}
static int som_blade_string(unsigned cls){return som_blade_of(cls)!=NULL;}
/* A B style on another weapon (the ninja's Kusarigama) keeps its own B. */
static int som_string_off(unsigned cls){
    if(!som_blade_string(cls))return 0;
    const SomWeaponStyle *st=som_style_of(&styles,cls,NULL);
    return st&&st->weapon!=som_disc_weapons[cls][0];
}
/* 1 started, 0 not now (try again), -1 never. */
static int som_blade_play(uint8_t *ram,unsigned who,const SomBladeMove *m,int cancel,int hop_done){
    uint8_t *o=ram+0xE000u+0x200u*who;
    if(o[0]!=1||o[0x060]||(o[0x191]&0x80u)||!(o[0x182]|o[0x183])||
       ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||ram[0x7D]==1)return -1;
    const int cancelled=cancel&&o[0x01C]&&monk_chain_active&&monk_chain_who==who&&
        o[0x1E4]==m->weapon&&!m->hop&&!m->extra&&!o[0x061];
    if(!cancelled&&(o[0x01C]||(who?!som_ally_idle(ram,who):!som_field_idle(ram))))return 0;
    if(o[0x061]&&(!m->launch||frame_number+m->launch<blade_free_at))return 0;
    if(m->hop&&!hop_done&&who==som_actor){
        int ex,ey;(void)som_monk_enemy(ram,o,&ex,&ey);
        const int keep_x=evade_dir_x,keep_y=evade_dir_y,sign=m->hop==1u?-1:1;
        evade_dir_x=sign*ex;evade_dir_y=sign*ey;
        const int hopped=som_evade_start_raw(ram);
        evade_dir_x=keep_x;evade_dir_y=keep_y;
        if(hopped){blade_hop_move=m;blade_wait_at=frame_number;return 1;}
    }
    unsigned extra=m->extra;
    if(extra==SOM_BX_SLEEP&&som_cast_spell_by(ram,who,SOM_SLEEP_FLOWER)){
        blade_move=m;blade_started=frame_number;blade_launch_at=0;return 1;
    }
    if(o[0x1E4]!=m->weapon){
        const int r=who?som_equip_ally_weapon(ram,who,m->weapon):som_equip_weapon(ram,m->weapon);
        if(r==SOM_EQUIP_BUSY)return 0;
        if(r!=SOM_EQUIP_OK)return -1;
    }
    if(extra==SOM_BX_SHUNSHIN&&who==som_actor)(void)som_shunshin_start(ram);
    if(extra==SOM_BX_KAWARIMI&&who==som_actor)(void)som_kawarimi_arm(ram);
    som_archer_face(ram,o);
    const unsigned cls=som_monk_enemy(ram,o,&(int){0},&(int){0});
    o[0x01C]=m->level?0x80u:0x20u;
    o[0x011]=(uint8_t)(m->level?4u*m->level+cls:m->variant);
    o[0x19B]=m->arrows;
    o[0x006]=o[0x007]=0;o[0x030]=0xFF;
    blade_move=m;blade_started=frame_number;
    blade_launch_at=m->launch?frame_number+m->launch+SOM_ARCHER_LATENCY:0;
    blade_free_at=m->launch?frame_number+m->free+SOM_ARCHER_LATENCY:0;
    if(m->name){
        char text[32];snprintf(text,sizeof(text),"^%s",m->name);
        som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/4u);
    }
    fprintf(stderr,"[som-ai] STRING %s: %s (weapon %u level %u, animation %u)\n",
            som_discipline_names[som_disc_member_class(&disciplines,who)],m->name?m->name:"strike",
            (unsigned)m->weapon,(unsigned)m->level,(unsigned)o[0x011]);
    return 1;
}
static int som_blade_start(uint8_t *ram,unsigned who,uint16_t held_dirs,unsigned cmd){
    const SomBladeString *str=som_blade_of(som_disc_member_class(&disciplines,who));
    uint8_t *o=ram+0xE000u+0x200u*who;
    if(!str)return 0;
    if(cmd==SOM_CMD_AWAY_AWAY)cmd=0;
    if(frame_number-monk_chain_end>SOM_MONK_CHAIN_RESET||monk_chain_who!=who)monk_chain_step=0;
    const SomBladeMove *m;unsigned next=monk_chain_step+1u;
    if(cmd){m=cmd==7u?&str->toward:&str->away;next=0;}
    else if(monk_chain_step<SOM_BLADE_STEPS)m=&str->step[monk_chain_step];
    else{m=&str->finish[som_monk_dir_of(ram,o,held_dirs)];next=0;}
    if(!monk_chain_active)blade_move=blade_hop_move=NULL;      /* none left from another class */
    int r=som_blade_play(ram,who,m,cmd!=0u,0);
    if(r<0&&o[0x1E4]!=m->weapon&&(o[0x1E4]<5u)){
        /* The weapon is not to be had: the same swing with the one in hand. */
        static SomBladeMove helds[2];
        SomBladeMove *const heldp=&helds[p2_context?1:0];
#define held (*heldp)
        held=*m;held.weapon=o[0x1E4];
        if(held.launch){held.launch=held.free=0;held.arrows=held.damage;}
        held.hop=held.extra=0;
        r=som_blade_play(ram,who,&held,0,1);
#undef held
    }
    if(r<=0)return 0;
    monk_chain_step=next;
    if(cmd)monk_dir_last=monk_dir_prev=0;
    monk_chain_who=who;monk_chain_active=1;monk_chain_queued=0;monk_chain_queued_cmd=0;
    som_combo_clear();
    return 1;
}
static void som_blade_frame(uint8_t *ram){
    uint8_t *o=ram+0xE000u+0x200u*monk_chain_who;
    if(blade_hop_move){
        if(evade_active)return;
        const SomBladeMove *m=blade_hop_move;blade_hop_move=NULL;
        const int r=som_blade_play(ram,monk_chain_who,m,0,1);
        if(r==0&&frame_number-blade_wait_at<=SOM_BLADE_WAIT){blade_hop_move=m;return;}
        if(r<=0){monk_chain_active=0;monk_chain_end=frame_number;}
        return;
    }
    if(blade_move){
        if(blade_launch_at){
            if(frame_number<=blade_launch_at)o[0x19B]=blade_move->arrows;
            else if(frame_number==blade_launch_at+1u&&o[0x061])o[0x19B]=blade_move->damage;
        }else if(blade_move->extra==SOM_BX_WAVE&&frame_number==blade_started+SOM_BLADE_WAVE_FRAME)
            (void)som_bolt_fire(mage_bolts,monk_chain_who,7u,(int)som_word(o+2),(int)som_word(o+4),
                                o[0x010],som_mage_bolt_damage(o,som_disc_member_class(&disciplines,monk_chain_who)),
                                som_word(ram+0xDC)&0x1FFu,frame_number);
    }
    if(o[0]==1&&(o[0x01C]==0x80u||o[0x01C]==0x20u)&&ram[0xE4]==1&&ram[0xD0]==0)return;
    if(blade_move){
        if(!o[0x01C]&&!o[0x061])o[0x19B]=0;
        monk_chain_end=blade_wait_at=frame_number;blade_move=NULL;
    }
    if(monk_chain_queued&&som_blade_string(som_disc_member_class(&disciplines,monk_chain_who))){
        if(som_blade_start(ram,monk_chain_who,monk_chain_held_dirs,(unsigned)monk_chain_queued_cmd))return;
        /* The arrow still flies or the swap must wait: the press is kept. */
        if(o[0]==1&&!(o[0x191]&0x80u)&&ram[0xE4]==1&&ram[0xD0]==0&&
           (!o[0x01C]||o[0x01C]==0x20u||o[0x01C]==0x80u)&&frame_number-blade_wait_at<=SOM_BLADE_WAIT)return;
    }
    if(!o[0x01C]&&!o[0x061])o[0x19B]=0;
    monk_chain_active=0;monk_chain_end=frame_number;
    monk_chain_queued=0;monk_chain_queued_cmd=0;
}
/* The next move of the string, or the command `cmd` (6 / 7). */
static int som_monk_chain_start(uint8_t *ram,unsigned who,uint16_t held_dirs,unsigned cmd){
    if(som_archer_class(who))return som_archer_start(ram,who,held_dirs,cmd);
    if(som_blade_string(som_disc_member_class(&disciplines,who)))
        return som_blade_start(ram,who,held_dirs,cmd);
    if(cmd==SOM_CMD_AWAY_AWAY)cmd=0;                   /* the archer's command */
    uint8_t *o=ram+0xE000u+0x200u*who;
    if(!som_monk_ready(ram,o,who,cmd!=0u)&&!som_monk_ready(ram,o,who,0))return 0;
    if(frame_number-monk_chain_end>SOM_MONK_CHAIN_RESET||monk_chain_who!=who)monk_chain_step=0;
    if(cmd){
        const int samurai=som_disc_member_class(&disciplines,who)==SOM_DISC_BUSHIDO;
        som_monk_play(ram,o,cmd,0,3,cmd==7u?(samurai?"Tsubame Gaeshi":"Rising Chi"):
                                         (samurai?"Hyakuretsu":"Fist Storm"));
        monk_chain_step=0;
        monk_dir_last=monk_dir_prev=0;
    }else if(monk_chain_step<SOM_MONK_STRIKES){
        som_monk_play(ram,o,0,som_monk_strike_variant[monk_chain_step],0,"strike");
        monk_chain_step++;
    }else if(monk_chain_step==SOM_MONK_STRIKES){
        som_monk_play(ram,o,1,0,1,som_disc_member_class(&disciplines,who)==SOM_DISC_BUSHIDO?
                      "Renzoku":"flurry");
        monk_chain_step++;
    }else{
        const unsigned d=som_monk_dir_of(ram,o,held_dirs);
        const unsigned level=d==SOM_MONK_TOWARD?4u:d==SOM_MONK_AWAY?2u:d==SOM_MONK_SIDE?5u:3u;
        som_monk_play(ram,o,level,0,2,"finisher");
        monk_chain_step=0;
    }
    monk_chain_who=who;monk_chain_active=1;monk_chain_queued=0;monk_chain_queued_cmd=0;
    som_combo_clear();
    return 1;
}
/* A move is playing, or the string is under way and may still go on. */
static int som_monk_in_string(unsigned who){
    return monk_chain_who==who&&(monk_chain_active||
        (monk_chain_step>0u&&frame_number-monk_chain_end<=SOM_MONK_CHAIN_RESET));
}
/* B pressed: a command at once (cancelling a string move), otherwise the
 * next move now, or queued behind the one playing. */
static uint16_t monk_chain_held_dirs;
static int som_monk_chain_press(uint8_t *ram,unsigned who,uint16_t held_dirs){
    if(!ram)return 0;
    unsigned cmd=som_monk_command();
    if(cmd==SOM_CMD_AWAY_AWAY&&!som_archer_class(who))cmd=0;
    monk_chain_held_dirs=held_dirs;
    if(cmd&&som_monk_chain_start(ram,who,held_dirs,cmd))return 1;
    if(monk_chain_active&&monk_chain_who==who){
        monk_chain_queued=1;monk_chain_queued_cmd=(int)cmd;return 1;
    }
    if(som_monk_chain_start(ram,who,held_dirs,cmd))return 1;
    /* The archer's volley still flies (or a weapon swap must wait): the
     * press is kept and fired when it can be. */
    const uint8_t *o=ram+0xE000u+0x200u*who;
    if(som_archer_class(who)&&o[0]==1&&(o[0x061]||o[0x01C])){
        monk_chain_who=who;monk_chain_active=1;monk_chain_queued=1;monk_chain_queued_cmd=(int)cmd;
        return 1;
    }
    return 0;
}
/* Each frame: notice the end of a move and start a queued one. */
static void som_monk_chain_frame(uint8_t *ram){
    if(!ram||!monk_chain_active)return;
    if(som_archer_class(monk_chain_who)){som_archer_frame(ram);return;}
    if(som_blade_string(som_disc_member_class(&disciplines,monk_chain_who))){som_blade_frame(ram);return;}
    uint8_t *o=ram+0xE000u+0x200u*monk_chain_who;
    if(o[0]==1&&(o[0x01C]==0x80u||o[0x01C]==0x20u)&&ram[0xE4]==1&&ram[0xD0]==0)return;
    monk_chain_active=0;monk_chain_end=frame_number;
    if(!o[0x01C])o[0x19B]=0;               /* the stage only lives for the move */
    if(monk_chain_queued&&som_string_weapon(som_disc_member_class(&disciplines,monk_chain_who))!=SOM_NO_WEAPON)
        som_monk_chain_start(ram,monk_chain_who,monk_chain_held_dirs,(unsigned)monk_chain_queued_cmd);
    monk_chain_queued=0;monk_chain_queued_cmd=0;
}
static void som_string_swap(int reset){
    static __typeof__(monk_chain_step) p2_monk_chain_step;
    static __typeof__(monk_chain_who) p2_monk_chain_who=SOM_PARTY_MEMBERS;
    static __typeof__(monk_chain_active) p2_monk_chain_active;
    static __typeof__(monk_chain_queued) p2_monk_chain_queued;
    static __typeof__(monk_chain_queued_cmd) p2_monk_chain_queued_cmd;
    static __typeof__(monk_chain_end) p2_monk_chain_end;
    static __typeof__(monk_dir_last) p2_monk_dir_last;
    static __typeof__(monk_dir_prev) p2_monk_dir_prev;
    static __typeof__(monk_dir_last_at) p2_monk_dir_last_at;
    static __typeof__(monk_dir_prev_at) p2_monk_dir_prev_at;
    static __typeof__(monk_ref_x) p2_monk_ref_x;
    static __typeof__(monk_ref_y) p2_monk_ref_y;
    static __typeof__(monk_ref_at) p2_monk_ref_at;
    static __typeof__(archer_hop_move) p2_archer_hop_move=-1;
    static __typeof__(archer_moving) p2_archer_moving=-1;
    static __typeof__(archer_launch_at) p2_archer_launch_at;
    static __typeof__(archer_free_at) p2_archer_free_at;
    static __typeof__(monk_chain_held_dirs) p2_monk_chain_held_dirs;
    static __typeof__(blade_move) p2_blade_move;
    static __typeof__(blade_hop_move) p2_blade_hop_move;
    static __typeof__(blade_launch_at) p2_blade_launch_at;
    static __typeof__(blade_free_at) p2_blade_free_at;
    static __typeof__(blade_started) p2_blade_started;
    static __typeof__(blade_wait_at) p2_blade_wait_at;
    if(reset){
        p2_monk_chain_step=0;
        p2_monk_chain_who=SOM_PARTY_MEMBERS;
        p2_monk_chain_active=0;
        p2_monk_chain_queued=0;
        p2_monk_chain_queued_cmd=0;
        p2_monk_chain_end=0;
        p2_monk_dir_last=0;
        p2_monk_dir_prev=0;
        p2_monk_dir_last_at=0;
        p2_monk_dir_prev_at=0;
        p2_monk_ref_x=0;
        p2_monk_ref_y=0;
        p2_monk_ref_at=0;
        p2_archer_hop_move=-1;
        p2_archer_moving=-1;
        p2_archer_launch_at=0;
        p2_archer_free_at=0;
        p2_monk_chain_held_dirs=0;
        p2_blade_move=0;
        p2_blade_hop_move=0;
        p2_blade_launch_at=0;
        p2_blade_free_at=0;
        p2_blade_started=0;
        p2_blade_wait_at=0;
        return;
    }
    {__typeof__(monk_chain_step) t=monk_chain_step;monk_chain_step=p2_monk_chain_step;p2_monk_chain_step=t;}
    {__typeof__(monk_chain_who) t=monk_chain_who;monk_chain_who=p2_monk_chain_who;p2_monk_chain_who=t;}
    {__typeof__(monk_chain_active) t=monk_chain_active;monk_chain_active=p2_monk_chain_active;p2_monk_chain_active=t;}
    {__typeof__(monk_chain_queued) t=monk_chain_queued;monk_chain_queued=p2_monk_chain_queued;p2_monk_chain_queued=t;}
    {__typeof__(monk_chain_queued_cmd) t=monk_chain_queued_cmd;monk_chain_queued_cmd=p2_monk_chain_queued_cmd;p2_monk_chain_queued_cmd=t;}
    {__typeof__(monk_chain_end) t=monk_chain_end;monk_chain_end=p2_monk_chain_end;p2_monk_chain_end=t;}
    {__typeof__(monk_dir_last) t=monk_dir_last;monk_dir_last=p2_monk_dir_last;p2_monk_dir_last=t;}
    {__typeof__(monk_dir_prev) t=monk_dir_prev;monk_dir_prev=p2_monk_dir_prev;p2_monk_dir_prev=t;}
    {__typeof__(monk_dir_last_at) t=monk_dir_last_at;monk_dir_last_at=p2_monk_dir_last_at;p2_monk_dir_last_at=t;}
    {__typeof__(monk_dir_prev_at) t=monk_dir_prev_at;monk_dir_prev_at=p2_monk_dir_prev_at;p2_monk_dir_prev_at=t;}
    {__typeof__(monk_ref_x) t=monk_ref_x;monk_ref_x=p2_monk_ref_x;p2_monk_ref_x=t;}
    {__typeof__(monk_ref_y) t=monk_ref_y;monk_ref_y=p2_monk_ref_y;p2_monk_ref_y=t;}
    {__typeof__(monk_ref_at) t=monk_ref_at;monk_ref_at=p2_monk_ref_at;p2_monk_ref_at=t;}
    {__typeof__(archer_hop_move) t=archer_hop_move;archer_hop_move=p2_archer_hop_move;p2_archer_hop_move=t;}
    {__typeof__(archer_moving) t=archer_moving;archer_moving=p2_archer_moving;p2_archer_moving=t;}
    {__typeof__(archer_launch_at) t=archer_launch_at;archer_launch_at=p2_archer_launch_at;p2_archer_launch_at=t;}
    {__typeof__(archer_free_at) t=archer_free_at;archer_free_at=p2_archer_free_at;p2_archer_free_at=t;}
    {__typeof__(monk_chain_held_dirs) t=monk_chain_held_dirs;monk_chain_held_dirs=p2_monk_chain_held_dirs;p2_monk_chain_held_dirs=t;}
    {__typeof__(blade_move) t=blade_move;blade_move=p2_blade_move;p2_blade_move=t;}
    {__typeof__(blade_hop_move) t=blade_hop_move;blade_hop_move=p2_blade_hop_move;p2_blade_hop_move=t;}
    {__typeof__(blade_launch_at) t=blade_launch_at;blade_launch_at=p2_blade_launch_at;p2_blade_launch_at=t;}
    {__typeof__(blade_free_at) t=blade_free_at;blade_free_at=p2_blade_free_at;p2_blade_free_at=t;}
    {__typeof__(blade_started) t=blade_started;blade_started=p2_blade_started;p2_blade_started=t;}
    {__typeof__(blade_wait_at) t=blade_wait_at;blade_wait_at=p2_blade_wait_at;p2_blade_wait_at=t;}
}
/* Bushido's X (and the AI's iai_<3|4>): the katana's charge level
 * straight away, one per SOM_IAI_COOLDOWN_FRAMES. */
static int som_iai_start_raw(unsigned level){
    if(som_actor_class()!=SOM_DISC_BUSHIDO)return 0;
    if(frame_number<iai_ready){
        som_overlay_say(&overlay,"^Iaijutsu|not ready",SOM_MESSAGE_FRAMES/3u);
        return 0;
    }
    if(!som_special_start(0,level))return 0;
    iai_ready=frame_number+(unsigned long)SOM_IAI_COOLDOWN_FRAMES*
              (unsigned long)(100-som_perk(SOM_EFFECT_IAI_COOLDOWN_CUT))/100ul;   /* Iai Focus */
    special_chord=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);special_suppress=1;
    char text[40];snprintf(text,sizeof(text),"^Iaijutsu|level %u",special_stage);
    som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] IAI level %u\n",special_stage);
    return 1;
}
static void som_special_tick(void){
    if(!special_stage||!backend_retro_get_memory_data)return;
    uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||SOM_ACT(ram)[0x000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||
       ram[0xD0]!=0||SOM_ACT(ram)[0x1E4]!=special_weapon||
       frame_number-special_started>SOM_SPECIAL_TIMEOUT){
        fprintf(stderr,"[som-ai] SPECIAL abort: state changed or charge timeout\n");
        special_stage=0;
        kusarigama_attack_active=kusarigama_attack_released=kusarigama_swing_seen=0;
        return;
    }
    /* Skip the hold: once the swing is over and the gauge runs ($C0:B330:
     * E01C, E061, E1ED clear; E01A counting), set the gauge word E01A to
     * the finished stage (counter parked at 44, stage in E01B = E19B),
     * as 90 frames per stage of holding would (som-decomp rom-combat.md
     * 11.3, glove-attacks.md). Releasing B then starts that power attack. */
    if(SOM_ACT(ram)[0x19B]<special_stage&&(SOM_ACT(ram)[0x01A]||SOM_ACT(ram)[0x01B])&&
       !SOM_ACT(ram)[0x01C]&&!SOM_ACT(ram)[0x061]&&!SOM_ACT(ram)[0x1ED]){
        SOM_ACT(ram)[0x01A]=0x2C;SOM_ACT(ram)[0x01B]=(uint8_t)special_stage;SOM_ACT(ram)[0x19B]=(uint8_t)special_stage;
    }
    if(SOM_ACT(ram)[0x19B]>=special_stage){
        fprintf(stderr,"[som-ai] SPECIAL release %s level %u\n",
                som_weapon_types[special_weapon],special_stage);
        if(kusarigama_attack_active)kusarigama_attack_released=1;
        special_stage=0; /* B releases on next emulated frame */
    }
}
/* Next owned weapon after the current one (Y while playing). */
static int som_cycle_weapon(uint8_t *ram){
    if(!ram)return 0;
    const unsigned current=ram[0xE1E4];
    for(unsigned step=1;step<8u;step++){
        const unsigned type=(current+step)%8u;
        if(ram[0xCC54u+type]!=0xFF)return som_equip_weapon(ram,type)==SOM_EQUIP_OK;
    }
    return 0;
}
static int som_give_weapon(uint8_t *ram,unsigned type){
    if(!ram||type>=8u)return 0;
    if(ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0){
        fprintf(stderr,"[som-ai] WEAPON refused: not in field play\n");return 0;
    }
    uint8_t *owned=ram+0xCC54u+type;
    if(*owned!=0xFF){
        fprintf(stderr,"[som-ai] WEAPON refused: %s already owned (%#04x)\n",
                som_weapon_types[type],(unsigned)*owned);return 0;
    }
    if(!som_take_undo())return 0;
    *owned=0x00;
    som_overlay_say(&overlay,"^A new weapon appears!",SOM_MESSAGE_FRAMES);
    fprintf(stderr,"[som-ai] WEAPON given: %s (tier 0); undo with undo_spawn\n",
            som_weapon_types[type]);
    return 1;
}
/* Grant both class weapons only when absent (0xFF); never overwrite an
 * owned weapon's tier. No save file is modified directly. */
static unsigned som_class_supply(uint8_t *ram,unsigned discipline){
    if(!ram||discipline>=SOM_DISCIPLINE_COUNT||ram[0xE000]!=1||
       ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||ram[0x7D]==1)return 0;
    unsigned given=0;
    for(unsigned k=0;k<SOM_STYLES_PER_CLASS;k++){      /* a style's own weapon (Kusarigama) */
        const unsigned type=som_weapon_styles[som_style_index(discipline,k)].weapon;
        if(type<8u&&ram[0xCC54u+type]==0xFF){
            ram[0xCC54u+type]=0;given++;
            fprintf(stderr,"[som-ai] CLASS granted missing %s tier 0\n",som_weapon_types[type]);
        }
    }
    {                                               /* the B chain's weapons (Weapon Flow) */
        const uint8_t *steps;const unsigned n=som_b_chain(discipline,&steps);
        for(unsigned k=0;k<n;k++)
            if(steps[k]<8u&&ram[0xCC54u+steps[k]]==0xFF){
                ram[0xCC54u+steps[k]]=0;given++;
                fprintf(stderr,"[som-ai] CLASS granted missing %s tier 0\n",som_weapon_types[steps[k]]);
            }
    }
    for(unsigned i=0;i<3;i++){
        unsigned type=i<2u?som_disc_weapons[discipline][i]:
            discipline==SOM_DISC_NINJUTSU?0u:som_disc_third_weapon(discipline);
        if(type<8u&&ram[0xCC54u+type]==0xFF){
            ram[0xCC54u+type]=0;
            given++;
            fprintf(stderr,"[som-ai] CLASS granted missing %s tier 0\n",som_weapon_types[type]);
        }
    }
    return given;
}
static void som_class_supply_frame(void){
    if((!class_supply_pending&&!class_equip_pending)||
       disciplines.selected>=SOM_DISCIPLINE_COUNT||
       !backend_retro_get_memory_data||!backend_retro_get_memory_size||
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE)return;
    uint8_t *ram=(uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||ram[0xE000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||
       ram[0xD0]!=0||ram[0x7D]==1)return;
    if(class_supply_pending){
        unsigned n=som_class_supply(ram,disciplines.selected);
        class_supply_pending=0;
        if(n)som_overlay_say(&overlay,"^Class weapons unlocked!",SOM_MESSAGE_FRAMES);
    }
    if(class_equip_pending&&(!class_last_equip_attempt||
                             frame_number-class_last_equip_attempt>=120ul)){
        class_last_equip_attempt=frame_number;
        unsigned primary=som_disc_weapons[disciplines.selected][0];
        const int result=som_equip_weapon_direct(ram,primary);
        /* Retry while the hero is busy or the art check is still pending. */
        if(result==SOM_EQUIP_OK||(result==SOM_EQUIP_REFUSED&&weapon_gfx_verified))
            class_equip_pending=0;
    }
}
/* Each companion holds her class's B weapon (casters: bare hands), given
 * once per class choice and session like the boy's; the player may change
 * it in the ring afterwards. Missing class weapons are granted at tier 0. */
static void som_ally_equip_frame(uint8_t *ram){
    if(!ram||!(ally_equip_pending&6u)||!som_rom||
       (ally_equip_tried&&frame_number-ally_equip_tried<60ul))return;
    if(ram[0xE000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||ram[0x7D]==1)return;
    ally_equip_tried=frame_number;
    for(unsigned who=1;who<SOM_PARTY_MEMBERS;who++){
        if(!(ally_equip_pending&(1u<<who)))continue;
        const uint8_t *o=ram+0xE000u+0x200u*who;
        const unsigned cls=som_disc_member_class(&disciplines,who);
        if(cls>=SOM_DISCIPLINE_COUNT){ally_equip_pending&=~(1u<<who);continue;}
        if(o[0]!=1||!(o[0x184]|o[0x185]))continue;          /* not joined yet: later */
        unsigned type=som_disc_weapons[cls][0];
        const SomWeaponStyle *st=som_disc_caster(cls)?NULL:som_style_of(&styles,cls,NULL);
        if(st)type=st->weapon;                  /* her style's weapon (Kusarigama: whip) */
        som_class_supply(ram,cls);
        const int result=som_equip_ally_weapon(ram,who,type);
        if(result!=SOM_EQUIP_BUSY)ally_equip_pending&=~(1u<<who);
    }
}
/* ---- Class weapon styles (src/som_weapon_styles.h) ----
 * The class's B weapon is pinned at its top plain tier (som_style_top_tier:
 * the highest row with no stat bonus, which the direct equip can apply),
 * and while a style weapon is in hand its attack +0x198 is the style's:
 * the game's own value minus the row's attack plus som_style_attack. The
 * game rebuilds +0x198 on level-ups and ring equips; a value other than
 * the one written means it did, so the base is read again. */
#define SOM_STYLE_FILE "/home/apps/retroarch/cores/experimental/som_ai_weapon_styles"
static const char *style_file=SOM_STYLE_FILE;
static int style_dirty,style_carry,style_atk_active;
static uint8_t style_written_atk,style_game_atk;
static unsigned long style_saved_frame,style_swing_frame;
static int style_swing_prev,style_cut_pending;
static uint16_t style_cut_hp[3];
static uint8_t style_cut_id[3];
static void som_styles_save(void){
    char line[1400],temp[300];
    if(!som_styles_format(&styles,line,sizeof(line)))return;
    snprintf(temp,sizeof(temp),"%s.tmp",style_file);
    FILE *out=fopen(temp,"w");
    if(!out)return;
    int ok=fprintf(out,"%s\n",line)>0;
    if(fclose(out)!=0)ok=0;
    if(ok)rename(temp,style_file);else unlink(temp);
    style_dirty=0;style_saved_frame=frame_number;
}
static void som_styles_load(void){
    som_styles_init(&styles);
    FILE *in=fopen(style_file,"r");
    if(!in)return;
    char line[1400];
    if(fgets(line,sizeof(line),in)&&!som_styles_parse(&styles,line)){
        som_styles_init(&styles);
        fprintf(stderr,"[som-ai] STYLES file unreadable; starting fresh\n");
    }
    fclose(in);
}
/* The game's name of weapon `type` at `grade`: the ROM's name block (event
 * texts 0x800+, bank $CA; weapons from entry 0x32, som-decomp
 * tools/names.py). Cached; "" without the ROM. */
static const char *som_weapon_name(unsigned type,unsigned grade){
    static char names[72][24];
    static uint8_t done[72];
    if(type>=8u||grade>8u)return "";
    const unsigned i=type*9u+grade;
    if(done[i])return names[i];
    const unsigned ev=0x800u+0x32u+i;
    const uint8_t *p=som_rom_at(0xCA0000u+(ev-0x400u)*2u,2u);
    if(!p)return "";
    const uint8_t *t=som_rom_at(0xCA0000u+(unsigned)(p[0]|p[1]<<8),23u);
    if(!t)return "";
    unsigned n=0;
    for(;n<23u&&t[n];n++){
        const uint8_t c=t[n];
        names[i][n]=c==0x80u?' ':c>=0x81u&&c<=0x9Au?(char)(c-0x20u):
                    c>=0x9Bu&&c<=0xB4u?(char)(c-0x5Au):c>=0xB5u&&c<=0xBEu?(char)(c-0x85u):
                    c==0xBFu?'.':c==0xC2u?'\'':'?';
    }
    names[i][n]=0;done[i]=1;
    return names[i];
}
/* The grade a class weapon is pinned at: the class's choice, or the
 * highest grade below it the direct equip can really apply, art model
 * included (Wingbow and Coral Spear passed the row check but their art did
 * not build, so B/Y switching was refused, 2026-11-20). 0xFF: none. */
static unsigned som_class_weapon_grade(uint8_t *hero,unsigned cls,unsigned slot,unsigned type){
    static uint8_t cache[SOM_DISCIPLINE_COUNT][3];
    static uint8_t known[SOM_DISCIPLINE_COUNT][3];
    if(cls>=SOM_DISCIPLINE_COUNT||slot>2u||som_class_arms[cls][slot].grade>8u)return 0xFFu;
    if(known[cls][slot])return cache[cls][slot];
    unsigned found=0xFFu;
    static SomWeaponModel m;
    for(unsigned g=som_class_arms[cls][slot].grade+1u;g-->0u;){
        const uint8_t *r=som_rom_at(0xD01000u+12u*(type*9u+g),12u);
        if(r&&r[0]==type&&som_weapon_row_plain(r)&&som_weapon_model(hero,type*9u+g,&m)){
            found=g;break;
        }
    }
    if(found!=som_class_arms[cls][slot].grade)
        fprintf(stderr,"[som-ai] CLASS %s: %s grade %u cannot be switched to directly; using %u\n",
                som_discipline_titles[cls],som_weapon_types[type],
                (unsigned)som_class_arms[cls][slot].grade,found);
    cache[cls][slot]=(uint8_t)found;known[cls][slot]=1;
    return found;
}
/* Highest tier of `type` whose row the direct equip can apply. */
static unsigned som_style_top_tier(unsigned type){
    for(unsigned t=9u;t-->0u;){
        const uint8_t *r=som_rom_at(0xD01000u+12u*(type*9u+t),12u);
        if(r&&r[0]==type&&som_weapon_row_plain(r))return t;
    }
    return 0;
}
static unsigned som_style_row_attack(unsigned type,unsigned tier){
    const uint8_t *r=som_rom_at(0xD01000u+12u*(type*9u+tier),12u);
    return r?r[8]:0u;
}
/* Put the game's own attack back (before a direct equip, or with no style). */
static void som_style_restore_attack(uint8_t *hero){
    if(style_atk_active&&hero[0x198]==style_written_atk)hero[0x198]=style_game_atk;
    style_atk_active=0;
}
static const SomWeaponStyle *som_style_held(const uint8_t *ram,unsigned *index){
    if(!ram||disciplines.selected>=SOM_DISCIPLINE_COUNT)return NULL;
    const SomWeaponStyle *st=som_style_of(&styles,disciplines.selected,index);
    return st&&ram[0xE1E4]==st->weapon?st:NULL;
}
/* who: the hero whose class style turns (the player's buttons: the one on pad 1). */
static void som_style_cycle(int to,unsigned who){
    if(who>=SOM_PARTY_MEMBERS)who=0;
    const unsigned cls=som_disc_member_class(&disciplines,who);
    if(cls>=SOM_DISCIPLINE_COUNT)return;
    if(who){ally_equip_pending|=1u<<who;ally_equip_tried=0;}   /* her style's weapon */
    styles.selected[cls]=(uint8_t)(to>=0?(unsigned)to%SOM_STYLES_PER_CLASS:
                                   (styles.selected[cls]+1u)%SOM_STYLES_PER_CLASS);
    unsigned index;
    const SomWeaponStyle *st=som_style_of(&styles,cls,&index);
    char text[64];
    if(som_disc_caster(cls))                       /* a spell set, not a weapon */
        snprintf(text,sizeof(text),"^%s|B %s Y %s",st->name,
                 som_class_spell_label(cls,som_caster_spell(cls,0)),
                 som_class_spell_label(cls,som_caster_spell(cls,1)));
    else snprintf(text,sizeof(text),"^%s%s%s|Lv%u %s",who?som_party_names[who]:"",who?" ":"",
             st->name,(unsigned)styles.level[index],
             st->speed_pct>100u?"fast":st->reach_bonus>0||st->power_pct>100u?"heavy":"balanced");
    som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] STYLE %s %s: %s Lv%u (speed %u%%, power %u%%, reach +%d)\n",
            som_party_names[who],som_discipline_names[cls],st->name,(unsigned)styles.level[index],
            (unsigned)st->speed_pct,(unsigned)st->power_pct,(int)st->reach_bonus);
    som_styles_save();
}
static void som_style_give(unsigned species){
    unsigned index;
    uint8_t *ram=backend_retro_get_memory_data?
        (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL;
    const SomWeaponStyle *st=som_style_held(ram,&index);
    if(!st)return;
    (void)species;
    if(som_style_award(&styles,index,SOM_STYLE_XP_HIT)){
        char text[64];
        snprintf(text,sizeof(text),"^%s|level %u",st->name,(unsigned)styles.level[index]);
        som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] STYLE %s level %u\n",st->name,(unsigned)styles.level[index]);
        som_styles_save();
    }else style_dirty=1;
}
/* Before the game's frame: pin the tier, write the attack. */
static void som_style_frame(uint8_t *ram){
    if(!ram||!som_rom)return;
    uint8_t *hero=ram+0xE000u;
    const int field=ram[0xE000]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F&&ram[0xD0]==0&&ram[0x7D]!=1;
    if(!field)return;
    unsigned index;
    const SomWeaponStyle *any=disciplines.selected<SOM_DISCIPLINE_COUNT?
        som_style_of(&styles,disciplines.selected,&index):NULL;
    (void)any;
    const unsigned cls=disciplines.selected;
    if(cls<SOM_DISCIPLINE_COUNT&&weapon_gfx_verified){
        for(unsigned type=0;type<8u;type++){
            const unsigned slot=som_class_arm_slot(cls,type);
            if(slot>2u)continue;
            const unsigned grade=som_class_weapon_grade(hero,cls,slot,type);
            uint8_t *owned=ram+0xCC54u+type;
            if(grade>8u||*owned==0xFF||(*owned&0x0Fu)==grade||
               (hero[0x1E4]==type&&!som_field_idle(ram)))continue;
            *owned=(uint8_t)((*owned&0xF0u)|grade);
            fprintf(stderr,"[som-ai] CLASS %s: %s pinned at grade %u (%s)\n",
                    som_discipline_titles[cls],som_weapon_types[type],grade,
                    som_weapon_name(type,grade));
            if(hero[0x1E4]==type)som_equip_weapon_row(ram,type,1);   /* the new art */
        }
    }
    /* A caster holds no weapon: the glove goes on as soon as he is free. */
    static unsigned long caster_equip_tried;
    if(cls<SOM_DISCIPLINE_COUNT&&som_disc_caster(cls)&&hero[0x1E4]!=0u&&
       ram[0xCC54u]!=0xFF&&!ring_ai_enabled&&frame_number-caster_equip_tried>=60ul&&
       som_field_idle(ram)){
        caster_equip_tried=frame_number;
        if(som_equip_weapon_direct(ram,0u)==SOM_EQUIP_OK)
            fprintf(stderr,"[som-ai] CLASS %s: bare hands (glove)\n",som_discipline_titles[cls]);
    }
    const SomWeaponStyle *st=som_style_held(ram,&index);
    SomWeaponStyle arm;unsigned arm_level=0;
    if(!st&&cls<SOM_DISCIPLINE_COUNT){
        /* The Y (or X) weapon: class-set stats at the class level. */
        const unsigned slot=som_class_arm_slot(cls,hero[0x1E4]);
        if(slot>0u&&slot<3u&&som_class_arms[cls][slot].grade<=8u){
            arm=(SomWeaponStyle){som_weapon_types[hero[0x1E4]],hero[0x1E4],0,100,
                                 som_class_arms[cls][slot].power_pct,0};
            arm_level=disciplines.level[cls]<SOM_STYLE_MAX_LEVEL?disciplines.level[cls]:
                      SOM_STYLE_MAX_LEVEL;
            st=&arm;
        }
    }
    if(!st){som_style_restore_attack(hero);return;}
    const uint8_t *row=som_rom_at(0xD01000u+12u*hero[0x1E3],12u);
    if(!row||hero[0x1E3]>=72u)return;
    const unsigned game=style_atk_active&&hero[0x198]==style_written_atk?style_game_atk:hero[0x198];
    const unsigned base=game>row[8]?game-row[8]:0u;
    const unsigned want=base+som_style_attack(st,arm_level?arm_level:styles.level[index],
        som_style_row_attack(st->weapon,0u),som_style_row_attack(st->weapon,som_style_top_tier(st->weapon)));
    const uint8_t value=(uint8_t)(want>255u?255u:want);
    if(!style_atk_active||hero[0x198]!=value){
        style_game_atk=(uint8_t)game;style_written_atk=value;hero[0x198]=value;
        if(!style_atk_active)
            fprintf(stderr,"[som-ai] STYLE %s Lv%u attack %u (game %u)\n",st->name,
                    arm_level?arm_level:(unsigned)styles.level[index],(unsigned)value,game);
        style_atk_active=1;
    }
}
/* After the game's frame: the cooldown at the style's speed, and the long
 * cut a heavy style adds past the game's reach. */
static void som_style_after(uint8_t *ram){
    if(!ram)return;
    if(lock_slot>=0)som_lock_frame(ram);
    som_lock_frame2(ram);
    uint8_t *hero=ram+0xE000u;
    unsigned index;
    const SomWeaponStyle *st=som_style_held(ram,&index);
    const int field=ram[0xE000]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F&&ram[0xD0]==0;
    if(!st||!field){style_carry=0;style_cut_pending=0;style_swing_prev=0;return;}
    if(st->speed_pct!=100u&&!special_stage)
        hero[0x1ED]=som_style_cooldown(hero[0x1ED],st->speed_pct,&style_carry);
    const int swinging=ram[0xE01C]!=0;
    if(swinging&&!style_swing_prev&&st->reach_bonus>0&&som_weapon_short_range(st->weapon)){
        style_swing_frame=frame_number;style_cut_pending=1;
        for(unsigned i=0;i<3u;i++){
            const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*i;
            style_cut_hp[i]=som_word(m+0x182);style_cut_id[i]=m[0x180];
        }
    }
    style_swing_prev=swinging;
    if(!style_cut_pending||frame_number-style_swing_frame<SOM_STYLE_CUT_FRAMES)return;
    style_cut_pending=0;
    static const int native[4]={24,28,28,34};      /* som_weapon_ranges.py */
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    for(unsigned i=0;i<3u;i++){
        if((int)i==familiar.slot)continue;
        uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*i;
        if(!som_bun_monster_alive(m)||m[0x180]!=style_cut_id[i]||
           som_word(m+0x182)<style_cut_hp[i])continue;     /* the game's swing got it */
        if(!som_style_in_cut((int)som_bun_u16(m+2)-hx,(int)som_bun_u16(m+4)-hy,hero[0x010],
                             native[st->weapon],st->reach_bonus))continue;
        unsigned damage=perk_last_hit_damage?perk_last_hit_damage:hero[0x198]/2u+1u;
        const unsigned pending=m[0x1F1]+damage;
        m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
        som_monster_hit((unsigned)i);
        fprintf(stderr,"[som-ai] STYLE %s long cut: %u on monster %u\n",st->name,damage,i);
        break;
    }
}
/* ---- Samurai's throw (player's design, 2026-11-19) ----
 * A defensive move: holding block (R2), press the D-pad away from the
 * facing with a monster in contact, and the samurai grabs it and throws it
 * over himself: the monster flies SOM_THROW_FRAMES to the spot
 * SOM_THROW_PX behind him (open ground only, else no throw), lands for
 * pending damage +0x1F1 and is dazed (its attack cooldown +0x1ED held).
 * The game has no grab: the decomp's glove power attacks are all strikes
 * (som-decomp docs/glove-attacks.md), so the proxy moves the monster. */
#define SOM_THROW_FRAMES 12ul
#define SOM_THROW_PX 26
#define SOM_THROW_COOLDOWN 150ul
#define SOM_THROW_DAZE 90u
static int throw_slot=-1,throw_x0,throw_y0,throw_x1,throw_y1;
static unsigned long throw_start,throw_ready;
static unsigned throw_damage;
static int som_throw_back_pressed(const uint8_t *ram,uint16_t pressed){
    unsigned back;
    switch(SOM_ACT(ram)[0x010]){
    case 0x00u: back=RETRO_DEVICE_ID_JOYPAD_DOWN;break;
    case 0x01u: back=RETRO_DEVICE_ID_JOYPAD_UP;break;
    case 0x02u: back=RETRO_DEVICE_ID_JOYPAD_LEFT;break;
    case 0x82u: back=RETRO_DEVICE_ID_JOYPAD_RIGHT;break;
    default: return 0;
    }
    return (pressed&(1u<<back))!=0;
}
static int som_throw_start_raw(uint8_t *ram){
    if(!ram||throw_active||frame_number<throw_ready||som_actor_class()!=SOM_DISC_BUSHIDO||
       SOM_ACT(ram)[0x000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0)return 0;
    const uint8_t *hero=SOM_ACT(ram);
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    const int slot=som_bun_toucher(ram,hx,hy,SOM_BUN_HERO_TOUCH_X+8,SOM_BUN_HERO_TOUCH_Y+8,
                                   familiar.slot);
    if(slot<0){fprintf(stderr,"[som-ai] THROW refused: no monster in reach\n");return 0;}
    uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)slot;
    const int mx=(int)som_bun_u16(m+2),my=(int)som_bun_u16(m+4);
    int tx=hx,ty=hy;
    if(abs(mx-hx)>=abs(my-hy))tx=hx+(mx>=hx?-SOM_THROW_PX:SOM_THROW_PX);
    else ty=hy+(my>=hy?-SOM_THROW_PX:SOM_THROW_PX);
    if(!som_bun_spot_open(ram,tx,ty,m[0x0B])){
        fprintf(stderr,"[som-ai] THROW refused: no open ground behind\n");return 0;
    }
    throw_active=1;throw_slot=slot;throw_start=frame_number;
    throw_x0=mx;throw_y0=my;throw_x1=tx;throw_y1=ty;
    throw_damage=hero[0x198]/2u+disciplines.level[SOM_DISC_BUSHIDO]/2u+4u;
    throw_ready=frame_number+SOM_THROW_COOLDOWN;
    som_overlay_say(&overlay,"^Throw!",SOM_MESSAGE_FRAMES/3u);
    fprintf(stderr,"[som-ai] THROW monster %d from %d,%d to %d,%d\n",slot,mx,my,tx,ty);
    return 1;
}
static void som_throw_frame(uint8_t *ram){
    if(!throw_active)return;
    if(!ram||throw_slot<0||SOM_ACT(ram)[0x000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F){throw_active=0;return;}
    uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)throw_slot;
    if(!som_bun_monster_alive(m)){throw_active=0;return;}
    const unsigned long t=frame_number-throw_start;
    const long k=(long)(t>SOM_THROW_FRAMES?SOM_THROW_FRAMES:t),n=(long)SOM_THROW_FRAMES;
    const int x=throw_x0+(int)((throw_x1-throw_x0)*k/n),y=throw_y0+(int)((throw_y1-throw_y0)*k/n);
    m[2]=(uint8_t)x;m[3]=(uint8_t)(x>>8);m[4]=(uint8_t)y;m[5]=(uint8_t)(y>>8);
    if(m[0x1ED]<SOM_THROW_DAZE)m[0x1ED]=SOM_THROW_DAZE;
    if(t>=SOM_THROW_FRAMES){
        const unsigned pending=m[0x1F1]+throw_damage;
        m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
        som_monster_hit((unsigned)throw_slot);
        som_xp_give(SOM_DISC_BUSHIDO,SOM_XP_HIT,m[0x180],SOM_XPK_HIT,"throw");
        fprintf(stderr,"[som-ai] THROW landed: %u on monster %d\n",throw_damage,throw_slot);
        throw_active=0;
    }
}
/* Item ring records $7E:CC48 + slot (11 slots, 0xFF = empty): low nibble
 * item id, high nibble twice the count (Turbo carries up to 7). Takes one
 * of item id; the last one empties its slot and the later slots move up,
 * so the ring keeps no gap. Returns the count left, or -1 without one. */
static int som_item_take(uint8_t *ram,unsigned id){
    for(unsigned slot=0;slot<11u;slot++){
        uint8_t *item=ram+0xCC48u+slot;
        if(*item==0xFF||(*item&0x0Fu)!=id)continue;
        const unsigned code=*item>>4;
        if(code<2u||code>14u||(code&1u))return -1;      /* not a verified count */
        if(code>2u){*item=(uint8_t)(*item-0x20u);return (int)(code/2u-1u);}
        for(unsigned k=slot;k+1u<11u;k++)ram[0xCC48u+k]=ram[0xCC48u+k+1u];
        ram[0xCC48u+10u]=0xFF;
        return 0;
    }
    return -1;
}
#define SOM_ITEM_CANDY 0u
/* Turbo's own order (its name table, 2026-11-23, and som-decomp
 * items-shops.md): 0 Candy, 1 Chocolate, 2 Royal Jam, 3 Walnut, 4 Herb,
 * 5 Cup of Wishes, 6 Magic Rope, 7 Drum. Id 1 for the Herb took Chocolate. */
#define SOM_ITEM_HERB 4u
#define SOM_ITEM_ROPE 6u
#define SOM_STATUS_POISON 0x2000u
static int som_item_field(const uint8_t *ram,const char *what){
    if(ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||ram[0xE000]!=1){
        fprintf(stderr,"[som-ai] %s refused: not in field play\n",what);return 0;
    }
    return 1;
}
static int som_use_candy(uint8_t *ram,unsigned who){
    if(!ram||who>=SOM_PARTY_MEMBERS||!som_item_field(ram,"CANDY"))return 0;
    uint8_t *hero=som_ally_obj(ram,who);
    const unsigned hp=som_word(hero+0x182),max_hp=som_word(hero+0x184);
    if(!som_ally_alive(hero)||hp>max_hp){
        fprintf(stderr,"[som-ai] CANDY refused: %s HP %u/%u\n",som_party_names[who],hp,max_hp);
        return 0;
    }
    const int left=som_item_take(ram,SOM_ITEM_CANDY);
    if(left<0){fprintf(stderr,"[som-ai] CANDY refused: no Candy\n");return 0;}
    som_ally_heal(hero,SOM_CANDY_HEAL);
    fprintf(stderr,"[som-ai] CANDY for %s: %d left, +%u HP queued\n",
            som_party_names[who],left,SOM_CANDY_HEAL);
    return 1;
}
/* "use_herb": Medical Herb on the hero when he is poisoned (status word
 * +0x190 bit 0x2000, som-decomp hit-reactions.md): clears the poison and
 * takes one Herb. Refused when he is not poisoned. */
static int som_use_herb(uint8_t *ram){
    if(!ram||!som_item_field(ram,"HERB"))return 0;
    const unsigned status=ram[0xE190]|ram[0xE191]<<8;
    if(!(status&SOM_STATUS_POISON)){fprintf(stderr,"[som-ai] HERB refused: not poisoned\n");return 0;}
    const int left=som_item_take(ram,SOM_ITEM_HERB);
    if(left<0){fprintf(stderr,"[som-ai] HERB refused: no Medical Herb\n");return 0;}
    ram[0xE191]=(uint8_t)(ram[0xE191]&~(SOM_STATUS_POISON>>8));
    fprintf(stderr,"[som-ai] HERB cured poison without the ring: %d left\n",left);
    return 1;
}
/* "use_rope" (player, 2026-11-23: "AI should use rope to get out ... just
 * load from address from decomp"): the Magic Rope without the ring. The
 * game allows it where $011F bit 0x40 is set and remembers the way out of
 * the dungeon in $0108; script command 0D ($C1:EAB4) changes to map event
 * 0x800 + $0108. The event VM is started on a 0D 00 (leave, END) of the
 * game's own ROM as $C1:E77B / E88D start an event, so the fade, the load
 * and the release are the game's. The rope is not used up (nor by the
 * game: $C0:7212 leaves the count of item 6). */
#define SOM_ROPE_SCRIPT 0xCA90F7u
#define SOM_ROPE_ALLOWED 0x40u
static int som_use_rope(uint8_t *ram){
    if(!ram||!som_item_field(ram,"ROPE"))return 0;
    const uint8_t *script=som_rom_at(SOM_ROPE_SCRIPT,2u);
    int owned=0;
    for(unsigned slot=0;slot<11u;slot++)
        owned|=ram[0xCC48u+slot]!=0xFF&&(ram[0xCC48u+slot]&0x1Fu)==SOM_ITEM_ROPE&&ram[0xCC48u+slot]>=0x20u;
    const unsigned out=som_word(ram+0x108);
    const char *why=!owned?"no Magic Rope":!(ram[0x11F]&SOM_ROPE_ALLOWED)?"not allowed on this map":
        !out||out>=0x400u?"no way out remembered":ram[0xFF]||(ram[0xF1]&0x80u)||ram[0x7D]==1?"game busy":
        !script||script[0]!=0x0D||script[1]!=0x00?"script bytes differ":
        !som_jutsu_free(ram)||evade_active||blink_active||bash_stage||special_stage||throw_active?"hero busy":NULL;
    if(why){fprintf(stderr,"[som-ai] ROPE refused: %s\n",why);return 0;}
    for(unsigned who=0;who<SOM_PARTY_MEMBERS;who++)
        ram[0xE006u+0x200u*who]=ram[0xE007u+0x200u*who]=0;
    for(unsigned pad=0;pad<3u;pad++)
        if(ram[0xD9u+pad]&(1u<<som_pad1_member_in(ram)))ram[0xD9u+pad]|=0x80u;
    ram[0xCC]=ram[0xCD]=ram[0xCE]=0;
    ram[0xD1]=(uint8_t)SOM_ROPE_SCRIPT;ram[0xD2]=(uint8_t)(SOM_ROPE_SCRIPT>>8);
    ram[0xD3]=(uint8_t)(SOM_ROPE_SCRIPT>>16);
    ram[0xD0]=1;
    som_overlay_say(&overlay,"^Magic Rope",SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] ROPE out of map %u by transition %03x\n",som_word(ram+0xDC)&0x1FFu,out);
    return 1;
}
/* Read-only diagnostic: the core's video RAM (sprite tiles live there, loaded
 * per map for that map's monsters) to SOM_VRAM_FILE for analysis. */
/* Read-only diagnostic: every frame for SOM_PAL_TRACE_FRAMES, the low WRAM
 * (7E:0000-1FFF, where an upload request flag would live) and the hero's
 * palette shadow, to find what makes the game send palettes to CGRAM (it
 * does so only during effects such as dash and block). Written to
 * SOM_PAL_TRACE_FILE when done. */
#define SOM_PAL_TRACE_FILE "/tmp/flxtr-som-paltrace.bin"
#define SOM_PAL_TRACE_FRAMES 480u          /* 8 s */
#define SOM_PAL_TRACE_LOW 0x2000u
static const char *pal_trace_file=SOM_PAL_TRACE_FILE;
static uint8_t *pal_trace;
static unsigned pal_trace_frames;
static int pal_trace_on;
static void som_pal_trace_frame(const uint8_t *ram){
    if(!pal_trace_on||!ram)return;
    /* low WRAM + palette shadow 7EDE00-DFFF + hero object 7EE000-E1FF */
    const size_t record=SOM_PAL_TRACE_LOW+0x400u;
    if(!pal_trace){
        pal_trace=malloc(record*SOM_PAL_TRACE_FRAMES);
        if(!pal_trace){pal_trace_on=0;return;}
    }
    uint8_t *at=pal_trace+record*pal_trace_frames;
    memcpy(at,ram,SOM_PAL_TRACE_LOW);
    memcpy(at+SOM_PAL_TRACE_LOW,ram+0xDE00u,0x200u);
    memcpy(at+SOM_PAL_TRACE_LOW+0x200u,ram+0xE000u,0x200u);
    if(++pal_trace_frames<SOM_PAL_TRACE_FRAMES)return;
    pal_trace_on=0;
    char temp[256];snprintf(temp,sizeof(temp),"%s.tmp",pal_trace_file);
    FILE *out=fopen(temp,"wb");
    if(out){
        int ok=fwrite(pal_trace,record,SOM_PAL_TRACE_FRAMES,out)==SOM_PAL_TRACE_FRAMES;
        if(fclose(out)!=0)ok=0;
        if(ok)rename(temp,pal_trace_file);else unlink(temp);
        fprintf(stderr,"[som-ai] PALETTE TRACE written (%u frames)\n",SOM_PAL_TRACE_FRAMES);
    }
}
#define SOM_VRAM_FILE "/tmp/flxtr-som-vram.bin"
static const char *vram_file=SOM_VRAM_FILE;
static void som_vram_dump(void){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size)return;
    const uint8_t *vram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_VIDEO_RAM);
    size_t size=backend_retro_get_memory_size(RETRO_MEMORY_VIDEO_RAM);
    if(!vram||!size){fprintf(stderr,"[som-ai] VRAM dump: the core does not expose VRAM\n");return;}
    char temp[256];snprintf(temp,sizeof(temp),"%s.tmp",vram_file);
    FILE *out=fopen(temp,"wb");
    if(!out)return;
    int ok=fwrite(vram,1,size,out)==size;
    if(fclose(out)!=0)ok=0;
    if(ok&&rename(temp,vram_file)==0)
        fprintf(stderr,"[som-ai] VRAM dump: %lu bytes at frame %lu\n",(unsigned long)size,frame_number);
    else unlink(temp);
}
/* Observe native weapon-ring graphics loading without altering RAM/VRAM.
 * Capture before native weapon change, then 12 frames after. The snapshots
 * include complete WRAM and VRAM so we can find DMA/upload state changes. */
#define SOM_WEAPON_TRACE_DIR "/tmp"
#define SOM_WEAPON_TRACE_FRAMES 12u
static int weapon_trace_on,weapon_trace_busy,weapon_trace_has_prev;
static unsigned weapon_trace_wait,weapon_trace_from,weapon_trace_to;
static uint8_t *weapon_trace_prev_ram,*weapon_trace_prev_vram;
static uint8_t *weapon_trace_before_ram,*weapon_trace_before_vram;
/* Separate per-frame snapshots: keep previous field state for switch detection
 * while recording all frames, including ring/menu frames. */
static uint8_t *weapon_trace_frame_ram,*weapon_trace_frame_vram;
static int weapon_trace_frame_valid;
static unsigned weapon_trace_frame_count,weapon_trace_vram_dumps;
static char weapon_trace_recent_paths[32][128];
static FILE *weapon_trace_timeline;
#define SOM_WEAPON_TRACE_MAX_FRAMES 480u
#define SOM_WEAPON_TRACE_MAX_VRAM_DUMPS 32u
static void som_weapon_trace_timeline_stop(void){
    if(weapon_trace_timeline){fclose(weapon_trace_timeline);weapon_trace_timeline=NULL;}
}
static void som_weapon_trace_timeline_reset(void){
    som_weapon_trace_timeline_stop();
    weapon_trace_frame_valid=0;weapon_trace_frame_count=0;weapon_trace_vram_dumps=0;
    memset(weapon_trace_recent_paths,0,sizeof(weapon_trace_recent_paths));
}
static void som_weapon_trace_timeline_frame(const uint8_t *ram,const uint8_t *vram){
    if(!weapon_trace_frame_ram||!weapon_trace_frame_vram)return;
    if(!weapon_trace_timeline){
        weapon_trace_timeline=fopen("/tmp/flxtr-weapon-timeline.txt","w");
        if(!weapon_trace_timeline)return;
        fprintf(weapon_trace_timeline,
                "frame game_mode ring_state weapon wr_change vr_change vram_snapshot\n");
    }
    unsigned wr=0,vr=0;
    if(weapon_trace_frame_valid){
        for(unsigned i=0;i<SOM_WRAM_SIZE;i++)wr+=ram[i]!=weapon_trace_frame_ram[i];
        for(unsigned i=0;i<0x10000u;i++)vr+=vram[i]!=weapon_trace_frame_vram[i];
    }
    char filename[128]="-";
    if(weapon_trace_frame_valid&&vr){
        /* Retain the most recent 32 VRAM changes, not the first 32.
         * Weapon graphics may load well before the equipment byte changes. */
        unsigned slot=weapon_trace_vram_dumps%SOM_WEAPON_TRACE_MAX_VRAM_DUMPS;
        snprintf(filename,sizeof(filename),"/tmp/flxtr-weapon-frame-%03u.vram",
                 weapon_trace_frame_count);
        FILE *out=fopen(filename,"wb");
        if(out){
            int ok=fwrite(vram,1,0x10000u,out)==0x10000u;
            if(fclose(out)!=0)ok=0;
            if(!ok){unlink(filename);strcpy(filename,"-");}
            else{
                if(weapon_trace_recent_paths[slot][0])
                    unlink(weapon_trace_recent_paths[slot]);
                snprintf(weapon_trace_recent_paths[slot],
                         sizeof(weapon_trace_recent_paths[slot]),"%s",filename);
                weapon_trace_vram_dumps++;
            }
        }else strcpy(filename,"-");
    }
    fprintf(weapon_trace_timeline,"%u %02X %02X %u %u %u %s\n",
            weapon_trace_frame_count,ram[0xE4],ram[0xD0],
            ram[0xE1E4],wr,vr,filename);
    if(weapon_trace_frame_valid&&vr){
        fprintf(weapon_trace_timeline,"VRAM tiles changed:");
        for(unsigned t=0;t<2048u;t++)
            if(memcmp(vram+t*32u,weapon_trace_frame_vram+t*32u,32u))
                fprintf(weapon_trace_timeline," %03X",t);
        fprintf(weapon_trace_timeline,"\n");
    }
    memcpy(weapon_trace_frame_ram,ram,SOM_WRAM_SIZE);
    memcpy(weapon_trace_frame_vram,vram,0x10000u);
    weapon_trace_frame_valid=1;
    weapon_trace_frame_count++;
    fflush(weapon_trace_timeline);
}
static void som_weapon_trace_reset(void){
    weapon_trace_busy=weapon_trace_has_prev=0;weapon_trace_wait=0;
    som_weapon_trace_timeline_reset();
}
static int som_weapon_trace_allocate(void){
    if(!weapon_trace_prev_ram)weapon_trace_prev_ram=malloc(SOM_WRAM_SIZE);
    if(!weapon_trace_prev_vram)weapon_trace_prev_vram=malloc(0x10000u);
    if(!weapon_trace_before_ram)weapon_trace_before_ram=malloc(SOM_WRAM_SIZE);
    if(!weapon_trace_before_vram)weapon_trace_before_vram=malloc(0x10000u);
    if(!weapon_trace_frame_ram)weapon_trace_frame_ram=malloc(SOM_WRAM_SIZE);
    if(!weapon_trace_frame_vram)weapon_trace_frame_vram=malloc(0x10000u);
    return weapon_trace_prev_ram&&weapon_trace_prev_vram&&
           weapon_trace_before_ram&&weapon_trace_before_vram&&
           weapon_trace_frame_ram&&weapon_trace_frame_vram;
}
static void som_weapon_trace_dump(const uint8_t *before_ram,
                                  const uint8_t *before_vram,
                                  const uint8_t *after_ram,
                                  const uint8_t *after_vram){
    static const char *const names[4]={"ram-before","vram-before","ram-after","vram-after"};
    const uint8_t *sources[4]={before_ram,before_vram,after_ram,after_vram};
    const size_t lengths[4]={SOM_WRAM_SIZE,0x10000u,SOM_WRAM_SIZE,0x10000u};
    char path[160];
    for(unsigned i=0;i<4;i++){
        snprintf(path,sizeof(path),SOM_WEAPON_TRACE_DIR "/flxtr-weapon-%s.bin",names[i]);
        FILE *out=fopen(path,"wb");
        if(!out)continue;
        int ok=fwrite(sources[i],1,lengths[i],out)==lengths[i];
        if(fclose(out)!=0)ok=0;
        if(!ok)unlink(path);
    }
    unsigned changes_ram=0,changes_vram=0;
    for(size_t i=0;i<SOM_WRAM_SIZE;i++)changes_ram+=before_ram[i]!=after_ram[i];
    for(size_t i=0;i<0x10000u;i++)changes_vram+=before_vram[i]!=after_vram[i];
    snprintf(path,sizeof(path),SOM_WEAPON_TRACE_DIR "/flxtr-weapon-trace.txt");
    FILE *report=fopen(path,"w");
    if(report){
        fprintf(report,"native weapon switch: %s (%u) -> %s (%u)\\n",
                som_weapon_types[weapon_trace_from],weapon_trace_from,
                som_weapon_types[weapon_trace_to],weapon_trace_to);
        fprintf(report,"WRAM changed bytes: %u; VRAM changed bytes: %u\\n",
                changes_ram,changes_vram);
        fprintf(report,"WRAM byte changes (first 512, hex offsets):\\n");
        unsigned printed=0;
        for(size_t i=0;i<SOM_WRAM_SIZE&&printed<512u;i++)
            if(before_ram[i]!=after_ram[i]){
                fprintf(report,"%05lX %02X -> %02X\\n",
                        (unsigned long)i,before_ram[i],after_ram[i]);printed++;
            }
        fprintf(report,"VRAM changed 32-byte tile blocks (hex tile IDs):\\n");
        for(unsigned t=0;t<2048u;t++)
            if(memcmp(before_vram+t*32u,after_vram+t*32u,32u))
                fprintf(report,"%03X ",t);
        fprintf(report,"\\n");
        fclose(report);
    }
    fprintf(stderr,"[som-ai] WEAPON TRACE native %s -> %s; "
                   "RAM %u VRAM %u changed bytes; /tmp/flxtr-weapon-*\\n",
            som_weapon_types[weapon_trace_from],
            som_weapon_types[weapon_trace_to],changes_ram,changes_vram);
    weapon_trace_on=0;weapon_trace_busy=0;
    som_weapon_trace_timeline_stop();
}
static void som_weapon_trace_frame(void){
    if(!weapon_trace_on||!backend_retro_get_memory_data||
       !backend_retro_get_memory_size||
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE||
       backend_retro_get_memory_size(RETRO_MEMORY_VIDEO_RAM)<0x10000u||
       !som_weapon_trace_allocate())return;
    const uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    const uint8_t *vram=backend_retro_get_memory_data(RETRO_MEMORY_VIDEO_RAM);
    if(!ram||!vram)return;
    som_weapon_trace_timeline_frame(ram,vram);
    if(weapon_trace_frame_count>=SOM_WEAPON_TRACE_MAX_FRAMES){
        fprintf(stderr,"[som-ai] WEAPON TRACE timed out after %u frames\n",
                weapon_trace_frame_count);
        weapon_trace_on=0;som_weapon_trace_timeline_stop();return;
    }
    /* Preserve the last field-play frame across the native ring menu.
     * The actual equip byte may change while the menu is open, so comparing
     * consecutive frames would miss that transition entirely. */
    if(ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xE000]!=1)return;
    if(weapon_trace_busy){
        if(++weapon_trace_wait>=SOM_WEAPON_TRACE_FRAMES){
            som_weapon_trace_dump(weapon_trace_before_ram,weapon_trace_before_vram,
                                  ram,vram);
            weapon_trace_has_prev=0;
        }
        return;
    }
    if(weapon_trace_has_prev&&ram[0xE4]==1&&ram[0xE6]==0x0F&&
       ram[0xE000]==1&&ram[0xE1E4]<8u&&
       weapon_trace_prev_ram[0xE1E4]<8u&&
       ram[0xE1E4]!=weapon_trace_prev_ram[0xE1E4]){
        weapon_trace_from=weapon_trace_prev_ram[0xE1E4];
        weapon_trace_to=ram[0xE1E4];
        memcpy(weapon_trace_before_ram,weapon_trace_prev_ram,SOM_WRAM_SIZE);
        memcpy(weapon_trace_before_vram,weapon_trace_prev_vram,0x10000u);
        weapon_trace_busy=1;weapon_trace_wait=0;
        fprintf(stderr,"[som-ai] WEAPON TRACE native switch observed, "
                       "recording 12 more frames\\n");
    }else{
        memcpy(weapon_trace_prev_ram,ram,SOM_WRAM_SIZE);
        memcpy(weapon_trace_prev_vram,vram,0x10000u);
        weapon_trace_has_prev=1;
    }
}
static void som_familiar_dismiss(void);
static uint8_t *undo_state;
static size_t undo_size;
static int undo_ready;
static int som_monster_live(const uint8_t *object){
    uint16_t status=(uint16_t)(object[0x190]|object[0x191]<<8);
    return object[0]==1&&object[0x180]<0x80&&!(status&0x8000u);
}
/* Full emulator snapshot for undo_spawn, taken right before a write. */
static int som_take_undo(void){
    if(!backend_retro_serialize_size||!backend_retro_serialize)return 0;
    size_t size=backend_retro_serialize_size();
    if(size>undo_size){
        uint8_t *grown=realloc(undo_state,size);
        if(!grown){fprintf(stderr,"[som-ai] SPAWN refused: no memory for undo\n");return 0;}
        undo_state=grown;undo_size=size;
    }
    if(!size||!backend_retro_serialize(undo_state,size)){
        fprintf(stderr,"[som-ai] SPAWN refused: undo snapshot failed\n");return 0;
    }
    undo_ready=1;
    return 1;
}
static int som_clone_enemy(void){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size||
       !backend_retro_serialize_size||!backend_retro_serialize)return 0;
    uint8_t *ram=(uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE){
        fprintf(stderr,"[som-ai] SPAWN refused: WRAM unavailable\n");return 0;
    }
    int source=-1,target=-1;
    for(unsigned slot=0;slot<3;slot++){
        uint8_t *object=ram+SOM_MONSTER_BASE+slot*SOM_OBJECT_SIZE;
        if(som_monster_live(object)){if(source<0)source=(int)slot;}
        else if(object[0]!=1&&target<0)target=(int)slot;
    }
    clone_no_slot=0;
    if(source<0){fprintf(stderr,"[som-ai] SPAWN refused: no live monster to copy\n");return 0;}
    /* Every slot busy (map 130 refills a slot the frame its rabite dies,
     * so a summon used to wait 10 s for nothing): a familiar takes over
     * the farthest other monster of the source's own kind. Its slot holds
     * that kind's art already, so the copy draws exactly like the source
     * (headless 2026-11-23). Not a boss. */
    if(target<0&&familiar_pending){
        const uint8_t *src=ram+SOM_MONSTER_BASE+(unsigned)source*SOM_OBJECT_SIZE;
        const long hx=(long)som_bun_u16(ram+0xE002u),hy=(long)som_bun_u16(ram+0xE004u);
        long far=-1;
        for(unsigned slot=0;slot<3;slot++){
            const uint8_t *o=ram+SOM_MONSTER_BASE+slot*SOM_OBJECT_SIZE;
            if((int)slot==source||!som_monster_live(o)||o[0x180]!=src[0x180]||o[0x180]>=0x57u)
                continue;
            const long d=labs((long)som_bun_u16(o+2u)-hx)+labs((long)som_bun_u16(o+4u)-hy);
            if(d>far){far=d;target=(int)slot;}
        }
        if(target>=0)fprintf(stderr,"[som-ai] SPAWN takes over monster slot %d (%ld px away)\n",
                             target+1,far);
    }
    if(target<0){clone_no_slot=1;return 0;}
    if(!som_take_undo())return 0;
    uint8_t *from=ram+SOM_MONSTER_BASE+(unsigned)source*SOM_OBJECT_SIZE;
    /* The destination's VRAM is stale until the core uploads new tiles.
     * Capture the currently visible source monster before overwriting it. */
    clone_source_art.has_vram=0;
    familiar_is_native_control=0;
    if(familiar_pending){
        som_familiar_save(&clone_source_art,ram,source);
        const uint8_t *source_vram=som_vram();
        if(source_vram)
            som_familiar_capture_vram(&clone_source_art,ram,source_vram,from,
                                      som_bun_u16(ram+0xDCu));
    }
    uint8_t *to=ram+SOM_MONSTER_BASE+(unsigned)target*SOM_OBJECT_SIZE;
    uint8_t keep[sizeof(som_slot_fields)][2];
    for(unsigned i=0;i<sizeof(som_slot_fields);i++){
        keep[i][0]=to[som_slot_fields[i]];keep[i][1]=to[som_slot_fields[i]+1];
    }
    memcpy(to,from,SOM_OBJECT_SIZE);
    for(unsigned i=0;i<sizeof(som_slot_fields);i++){
        to[som_slot_fields[i]]=keep[i][0];to[som_slot_fields[i]+1]=keep[i][1];
    }
    memcpy(ram+SOM_MONSTER_GFX+(unsigned)target*SOM_MONSTER_GFX_SIZE,
           ram+SOM_MONSTER_GFX+(unsigned)source*SOM_MONSTER_GFX_SIZE,SOM_MONSTER_GFX_SIZE);
    memcpy(ram+SOM_MONSTER_PALETTE+(unsigned)target*SOM_MONSTER_PALETTE_SIZE,
           ram+SOM_MONSTER_PALETTE+(unsigned)source*SOM_MONSTER_PALETTE_SIZE,
           SOM_MONSTER_PALETTE_SIZE);
    uint16_t x=(uint16_t)(to[2]|to[3]<<8);x=(uint16_t)(x+32u);
    to[2]=(uint8_t)x;to[3]=(uint8_t)(x>>8);
    last_clone_target=target;
    fprintf(stderr,"[som-ai] SPAWN copied monster id %u from 7E%04X to 7E%04X; undo with undo_spawn\n",
        (unsigned)from[0x180],SOM_MONSTER_BASE+(unsigned)source*SOM_OBJECT_SIZE,
        SOM_MONSTER_BASE+(unsigned)target*SOM_OBJECT_SIZE);
    return 1;
}
/* Living world (EXPERIMENTAL, opt-in): one native NPC walk per command file
 * write. Refused unless the ROM routines were verified at load, the enable
 * marker exists and som_npc_walk_refusal() accepts the live state. */
static const char *npc_command_file=SOM_NPC_COMMAND_FILE;
static const char *npc_enable_file=SOM_NPC_ENABLE_FILE;
static int npc_rom_verified;
static unsigned long npc_last_walk_frame;
static int npc_have_walked;
#define SOM_NPC_MIN_GAP_FRAMES 8u
/* Working villagers (src/som_npc_work.h): frames from the pack file. */
static const char *npc_work_pack_file=SOM_WORK_PACK_FILE;
static uint8_t *npc_work_pack;
static size_t npc_work_pack_size;
static int npc_work_pack_tried;
static SomNpcWorkState npc_work;
/* Reads a whole frame pack (at most 16 MiB) into memory. */
static void som_load_pack_file(const char *path,const char *what,uint8_t **pack,size_t *size){
    FILE *f=fopen(path,"rb");
    if(!f){fprintf(stderr,"[som-ai] %s: no frame pack %s\n",what,path);return;}
    if(fseek(f,0,SEEK_END)==0){
        const long n=ftell(f);
        if(n>10&&n<(16l<<20)&&fseek(f,0,SEEK_SET)==0&&(*pack=malloc((size_t)n))){
            if(fread(*pack,1,(size_t)n,f)==(size_t)n)*size=(size_t)n;
            else{free(*pack);*pack=NULL;}
        }
    }
    fclose(f);
    fprintf(stderr,"[som-ai] %s: frame pack %s (%lu bytes)\n",what,
        *pack?"loaded":"unreadable",(unsigned long)*size);
}
static void som_npc_work_load_pack(void){
    if(npc_work_pack_tried)return;
    npc_work_pack_tried=1;
    som_load_pack_file(npc_work_pack_file,"NPC work",&npc_work_pack,&npc_work_pack_size);
}
static uint8_t *som_npc_ram(size_t *size){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size)return NULL;
    *size=backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    return (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
}
static int som_npc_work_command(const char *line){
    size_t size=0;
    uint8_t *ram=som_npc_ram(&size);
    if(!strcmp(line,"npc_work_stop")){
        if(npc_work.active&&ram&&size==SOM_WRAM_SIZE){
            som_npc_work_end(&npc_work,ram);
            fprintf(stderr,"[som-ai] NPC WORK stopped\n");
        }
        return 1;
    }
    SomNpcWork work;
    if(!som_npc_parse_work(line,&work)){
        fprintf(stderr,"[som-ai] NPC work refused: malformed command\n");return 0;
    }
    if(!npc_rom_verified){fprintf(stderr,"[som-ai] NPC work refused: ROM routines not verified\n");return 0;}
    if(access(npc_enable_file,F_OK)!=0){fprintf(stderr,"[som-ai] NPC work refused: living world not enabled\n");return 0;}
    if(npc_work.active){fprintf(stderr,"[som-ai] NPC work refused: another villager is working\n");return 0;}
    if(!ram||size!=SOM_WRAM_SIZE)return 0;
    som_npc_work_load_pack();
    SomWorkEntry entry;
    const char *refusal=som_npc_work_refusal(ram,size,&work,npc_work_pack,npc_work_pack_size,&entry);
    if(refusal){fprintf(stderr,"[som-ai] NPC work refused: %s\n",refusal);return 0;}
    som_npc_work_begin(&npc_work,ram,&work,&entry,frame_number);
    fprintf(stderr,"[som-ai] NPC WORK map %u slot %u kind %02X at (%u,%u) dir %u, %u swings (%lu frames)\n",
        work.map,work.slot,work.kind,work.x,work.y,work.dir,work.cycles,npc_work.length);
    return 1;
}
static void som_npc_work_frame(void){
    if(!npc_work.active)return;
    size_t size=0;
    uint8_t *ram=som_npc_ram(&size);
    if(!ram||size!=SOM_WRAM_SIZE){npc_work.active=0;return;}
    const char *why=som_npc_work_tick(&npc_work,ram,frame_number);
    if(why)fprintf(stderr,"[som-ai] NPC WORK ended: %s\n",why);
}
static int som_npc_walk_command(const char *line){
    SomNpcWalk walk;
    if(!som_npc_parse_walk(line,&walk)){
        fprintf(stderr,"[som-ai] NPC refused: malformed command\n");return 0;
    }
    if(npc_work.active&&npc_work.slot==walk.slot){
        fprintf(stderr,"[som-ai] NPC refused: that villager is working\n");return 0;
    }
    if(!npc_rom_verified){fprintf(stderr,"[som-ai] NPC refused: ROM routines not verified\n");return 0;}
    if(access(npc_enable_file,F_OK)!=0){fprintf(stderr,"[som-ai] NPC refused: living world not enabled\n");return 0;}
    if(npc_have_walked&&frame_number<npc_last_walk_frame+SOM_NPC_MIN_GAP_FRAMES){
        fprintf(stderr,"[som-ai] NPC refused: rate limit\n");return 0;
    }
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size)return 0;
    uint8_t *ram=(uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t size=backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    const char *refusal=som_npc_walk_refusal(ram,size,&walk);
    if(refusal){fprintf(stderr,"[som-ai] NPC refused: %s\n",refusal);return 0;}
    som_npc_apply_walk(ram,&walk);
    npc_have_walked=1;npc_last_walk_frame=frame_number;
    fprintf(stderr,"[som-ai] NPC WALK map %u slot %u kind %02X from (%u,%u) dir %u for %u frames\n",
        walk.map,walk.slot,walk.kind,walk.x,walk.y,walk.dir,walk.frames);
    return 1;
}
static void som_general_raid_command(void);
static void som_check_npc_command(void){
    if(!test_enabled||!game_loaded||!on_turbo||frame_number%SOM_TRIGGER_POLL_FRAMES)return;
    if(access(npc_command_file,F_OK)!=0)return;
    char line[96]={0};
    FILE *input=fopen(npc_command_file,"r");
    if(!input)return;
    int have=fgets(line,sizeof(line),input)!=NULL;
    fclose(input);
    if(unlink(npc_command_file)!=0||!have)return;
    line[strcspn(line,"\r\n")]=0;
    if(!strncmp(line,"npc_work",8))som_npc_work_command(line);
    else if(!strcmp(line,"general_raid"))som_general_raid_command();
    else som_npc_walk_command(line);
}
static int som_undo_spawn(void){
    if(!undo_ready||!backend_retro_unserialize){
        fprintf(stderr,"[som-ai] UNDO refused: no spawn snapshot\n");return 0;
    }
    int ok=backend_retro_unserialize(undo_state,undo_size);
    undo_ready=0;
    familiar.slot=familiar.target=-1;familiar_pending=0;   /* memory is pre-clone */
    familiar_carry.valid=0;
    som_tk_reset(&telekinesis);
    unlink(familiar_file);
    fprintf(stderr,ok?"[som-ai] UNDO restored the pre-spawn snapshot\n":
                      "[som-ai] UNDO failed to restore the snapshot\n");
    return ok;
}
static void som_start_action(SomAction action){
    if(action==SOM_ACT_SNAPSHOT){som_snapshot();return;}
    /* A caster's attack is the glove punch like anyone's: the mage bolt
     * rides on it (som_mage_bolt_frame), AI or player. */
    if(action==SOM_ACT_ATTACK)som_combo_swing(0);
    /* The AI's attack runs the class's B chain only in a fight (a monster
     * within 80 px): cutting grass it keeps the sword it took out (the
     * chain swapped it for the ninja's glove, which cuts nothing, 3 misses
     * per patch, 2026-11-20). */
    uint8_t *chain_ram=backend_retro_get_memory_data?
        (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL;
    if(action==SOM_ACT_ATTACK&&som_taijutsu_peek()!=SOM_NO_WEAPON&&chain_ram&&
       som_bun_toucher(chain_ram,(int)som_bun_u16(chain_ram+0xE002),(int)som_bun_u16(chain_ram+0xE004),
                       80,80,familiar.slot)>=0){
        uint8_t *ram=chain_ram;
        const unsigned want=som_taijutsu_peek();
        if(ram&&som_equip_weapon_direct(ram,want)==SOM_EQUIP_OK){
            som_taijutsu_advance();
            fprintf(stderr,"[som-ai] TAIJUTSU %s (AI)\n",som_weapon_types[want]);
        }
    }
    active_action=action;
    action_frames=som_action_frames(action);
    armed=1;cancelled=0;completed=0;
    start_frame=frame_number;injected_queries=0;
    fprintf(stderr,"[som-ai] TRIGGERED %s at frame %lu for %u frames\n",
        som_action_name(action),start_frame,action_frames);
}
/* File commands are polled from the emulation thread.
 * A new command may replace an unfinished hold; physical input still wins. */
static void som_auto_tick(void){
    static const SomAction sequence[]={SOM_ACT_RIGHT,SOM_ACT_ATTACK,
        SOM_ACT_DOWN,SOM_ACT_ATTACK,SOM_ACT_LEFT,SOM_ACT_ATTACK,
        SOM_ACT_UP,SOM_ACT_ATTACK};
    if(!autonomous_mode)return;
    if(frame_number>=auto_deadline){
        autonomous_mode=0;
        fprintf(stderr,"[som-ai] AUTO COMPLETE after limited demo\n");
        return;
    }
    if(armed)return;
    /* Delay between actions allows attack recovery and visible stop frames. */
    if(frame_number<start_frame+action_frames+18)return;
    som_start_action(sequence[auto_step%8u]);
    auto_step++;
}
static int som_handle_command(const char *command){
    if(!command||!*command||!test_enabled||!game_loaded||!on_turbo)return 0;
    if(!strcmp(command,"p2_ai_on")||!strcmp(command,"p2_ai_off")){
        const int want=!strcmp(command,"p2_ai_on");
        if(gambits.player2!=want){gambits.player2=want;som_p2ai_switched();}
        return 1;
    }
    if(!strcmp(command,"camera_on")){unlink(camera_off_file);camera_off=0;return 1;}
    if(!strcmp(command,"camera_off")){
        FILE *off=fopen(camera_off_file,"w");
        if(off)fclose(off);
        camera_off=1;
        return 1;
    }
    if(!strcmp(command,"ai_on")){
        if(!som_write_ring_ai_marker())return 0;
        ring_ai_enabled=1;ring_ai_grace_until=frame_number+30;
        fprintf(stderr,"[som-ai] MANUAL AI START signal ready%s\n",
                direct_transport?" in process":" on disk");
        return 1;
    }
    if(!strcmp(command,"ai_off")){
        ally_order.until=0;
        ring_ai_enabled=0;ring_ai_pending=0;armed=0;cancelled=1;gambits.open=0;
        if(!direct_transport)unlink(ring_ai_file);
        idle_started=1;
        fprintf(stderr,"[som-ai] MANUAL AI STOP\n");
        return 1;
    }
    if(!strcmp(command,"auto_on")){
        autonomous_mode=1;auto_step=0;auto_deadline=frame_number+600;
        start_frame=frame_number>50?frame_number-50:0;action_frames=0;
        fprintf(stderr,"[som-ai] AUTO START: bounded 600-frame movement/attack demo\n");
        return 1;
    }
    if(!strcmp(command,"clone_enemy")||!strcmp(command,"summon_familiar")){
        familiar_pending=!strcmp(command,"summon_familiar");
        if(familiar_pending&&(familiar.slot>=0||familiar_carry.valid)){
            familiar_pending=0;
            fprintf(stderr,"[som-ai] FAMILIAR refused: one is already with you\n");
            return 0;
        }
        if(familiar_pending){
            const uint8_t *ram=backend_retro_get_memory_data?
                (const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL;
            if(!som_summon_allowed(ram)){
                familiar_pending=0;
                som_overlay_say(&overlay,"^Only summoners can summon",SOM_MESSAGE_FRAMES);
                fprintf(stderr,"[som-ai] FAMILIAR refused: the leader is not a summoner\n");
                return 0;
            }
        }
        if(som_clone_enemy()){som_familiar_bind();return 1;}
        if(clone_no_slot){
            clone_pending_until=frame_number+SOM_CLONE_WAIT_FRAMES;
            fprintf(stderr,"[som-ai] SPAWN waiting up to 10 s for a free monster slot\n");
        }else familiar_pending=0;
        return clone_no_slot;
    }
    if(!strcmp(command,"dismiss_familiar")){som_familiar_dismiss();return 1;}
    if(!strcmp(command,"party_release")){ally_order.until=0;return 1;}
    if(!strncmp(command,"party_order_",12)){
        unsigned map,slot,identity;char tail;
        if(sscanf(command+12,"%3u_%1u_%3u%c",&map,&slot,&identity,&tail)!=3||
           map>0x1FFu||slot>SOM_ALLY_SUPPORT_SLOT||identity>=0x80u)return 0;
        const uint8_t *ram=backend_retro_get_memory_data?
            backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL;
        if(!ram||(som_word(ram+0xDC)&0x1FFu)!=map)return 0;
        ally_order.map=map;ally_order.slot=slot;ally_order.identity=identity;
        ally_order.until=frame_number+SOM_ALLY_ORDER_FRAMES;
        return 1;
    }
    {   /* ally_class_<who 1|2>_<class>: the girl's or the sprite's class. */
        unsigned who=0,cls=0;char tail;
        if(!strncmp(command,"ally_class_",11)&&sscanf(command+11,"%1u_%2u%c",&who,&cls,&tail)==2&&
           who>=1u&&who<=2u&&cls<SOM_DISCIPLINE_COUNT){
            if(!som_disc_may(&disciplines,who,cls))return 0;
            disciplines.party[who-1u]=(uint8_t)cls;disciplines.who=(uint8_t)who;
            som_party_class_saved();
            return 1;
        }
    }
    {
        unsigned cls=som_shinobi[3];char tail;
        if((!strcmp(command,"companion")||
            (!strncmp(command,"companion_",10)&&sscanf(command+10,"%2u%c",&cls,&tail)==1))&&
           cls<SOM_DISCIPLINE_COUNT){
            companion.cls=cls;
            if(companion.active)return 1;               /* a new class for her */
            memset(&companion,0,sizeof(companion));companion.cls=cls;companion.active=1;
            if(familiar.slot>=0||familiar_carry.valid)return 1;    /* she takes it over */
            familiar_pending=1;
            fprintf(stderr,"[som-ai] COMPANION as %s\n",som_discipline_names[cls]);
            if(som_clone_enemy()){som_familiar_bind();return 1;}
            if(clone_no_slot){
                clone_pending_until=frame_number+SOM_CLONE_WAIT_FRAMES;
                return 1;
            }
            familiar_pending=0;companion.active=0;
            fprintf(stderr,"[som-ai] COMPANION refused: no monster to take the shape of\n");
            return 0;
        }
    }
    /* The AI's dodge: the same evade as the player's A. Kawarimi and
     * Shunshin are AI-only jutsu (not on any button yet). */
    if(!strcmp(command,"telekinesis")||!strcmp(command,"pull_chest"))
        return som_telekinesis_cast(backend_retro_get_memory_data?
            (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL);
    /* evade_up/down/left/right: the AI's dodge in a direction (an escape
     * goes along its route, not back into the monster behind it). */
    if(!strncmp(command,"evade_",6)){
        const char *d=command+6;
        const int dx=!strcmp(d,"right")?1:!strcmp(d,"left")?-1:0;
        const int dy=!strcmp(d,"down")?1:!strcmp(d,"up")?-1:0;
        if(!dx&&!dy)return 0;
        evade_dir_x=dx;evade_dir_y=dy;
        const int ok=som_evade_start(backend_retro_get_memory_data?
            (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL);
        evade_dir_x=evade_dir_y=0;
        return ok;
    }
    if(!strcmp(command,"evade"))
        return som_evade_start(backend_retro_get_memory_data?
            (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL);
    {
        int tx,ty;char tail;
        if(!strncmp(command,"blink_",6)&&sscanf(command+6,"%3d_%3d%c",&tx,&ty,&tail)==2)
            return som_blink_start(backend_retro_get_memory_data?
                (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL,tx,ty);
    }
    /* Headless tests only (FLXTR_SOM_TEST_COMMANDS=1, set by
     * tools/som_headless/run.py; never in the live game): set a discipline's
     * level, e.g. test_level_2_9 = Ninjutsu 9, to exercise the perks. */
    {
        const char *tests=getenv("FLXTR_SOM_TEST_COMMANDS");
        unsigned d,l;char tail;
        if(tests&&!strcmp(tests,"1")&&!strncmp(command,"test_level_",11)&&
           sscanf(command+11,"%2u_%3u%c",&d,&l,&tail)==2&&d<SOM_DISCIPLINE_COUNT&&l>=1u&&l<=100u){
            disciplines.level[d]=(uint16_t)l;
            fprintf(stderr,"[som-ai] TEST %s level %u\n",som_discipline_names[d],l);
            return 1;
        }
    }
    if(!strcmp(command,"fuma_swap"))
        return som_fuma_swap(backend_retro_get_memory_data?
            (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL);
    if(!strcmp(command,"kawarimi"))
        return som_kawarimi_arm(backend_retro_get_memory_data?
            (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL);
    if(!strcmp(command,"shunshin"))
        return som_shunshin_start(backend_retro_get_memory_data?
            (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL);
    if(!strcmp(command,"summon_boss"))return som_summon_boss();
    if(!strcmp(command,"vram_dump")){som_vram_dump();return 1;}
    if(!strcmp(command,"weapon_trace")){
        if(!som_weapon_trace_allocate())return 0;
        weapon_trace_on=1;som_weapon_trace_reset();
        som_overlay_say(&overlay,"WEAPON TRACE ON|Equip via native ring",SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] WEAPON TRACE armed; equip via native ring\n");
        return 1;
    }
    /* The AI picks the discipline that fits (bridge som_class_director). */
    if(!strncmp(command,"class_",6)){
        char *end=NULL;
        const unsigned long n=!strcmp(command+6,"none")?SOM_DISCIPLINE_NONE:
            strtoul(command+6,&end,10);
        if(n!=SOM_DISCIPLINE_NONE&&(!end||*end||end==command+6||n>=SOM_DISCIPLINE_COUNT))return 0;
        if(!som_disc_may(&disciplines,0u,(unsigned)n))return 0;
        if(disciplines.selected!=n){
            disciplines.selected=(uint8_t)n;
            som_disc_save();som_class_selected();
            if(gambits.debug_messages&&n<SOM_DISCIPLINE_COUNT){
                char text[64];
                snprintf(text,sizeof(text),"^%s|AI chose this class",som_discipline_names[n]);
                som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES);
            }
            fprintf(stderr,"[som-ai] DISCIPLINE selected=%lu by the AI (saved)\n",n);
        }
        return 1;
    }
    /* Cast a spell from the AI leader's class. */
    if(!strncmp(command,"cast_",5)&&som_rom&&backend_retro_get_memory_data){
        uint8_t *ram=(uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        const unsigned d=disciplines.selected;
        const int want_attack=!strcmp(command+5,"attack"),want_heal=!strcmp(command+5,"heal");
        if(!strcmp(command+5,"revive"))
            return ram&&som_try_revivifier(ram,0u,ring_ai_enabled);
        if(!want_attack&&!want_heal&&strcmp(command+5,"buff"))return 0;
        for(unsigned i=0;i<som_class_spell_count(d);i++){
            const unsigned spell=som_class_spell(d,i);
            const uint8_t *row=som_rom_at(0xD02AD0u+64u*spell,16u);
            if(!row)continue;
            const int heal=spell==0x0Bu;    /* Cure Water */
            if(want_attack?row[8]==0x81u:want_heal?heal:(row[8]!=0x81u&&!som_bolt_heal_spell(spell)))
                return som_cast_spell(ram,spell);
        }
        return 0;
    }
    /* flykick: Martial Arts' flying kick for the AI (as X/Triangle). */
    if(!strcmp(command,"flykick")){
        uint8_t *ram=backend_retro_get_memory_data?
            backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL;
        flykick_from_ai=1;
        const int ok=som_flykick_start(ram);
        if(!ok)flykick_from_ai=0;
        return ok;
    }
    /* style_<0-2> / style_next: the class's B weapon style (fast,
     * balanced, heavy). throw: Samurai's grab and throw (AI). */
    if(!strncmp(command,"style_",6)){
        if(disciplines.selected>=SOM_DISCIPLINE_COUNT)return 0;
        if(!strcmp(command+6,"next")){som_style_cycle(-1,0);return 1;}
        if(command[6]>='0'&&command[6]<'0'+(int)SOM_STYLES_PER_CLASS&&!command[7]){
            som_style_cycle(command[6]-'0',0);return 1;
        }
        return 0;
    }
    if(!strcmp(command,"throw"))
        return som_throw_start(backend_retro_get_memory_data?
            (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL);
    /* iai_3 / iai_4: Bushido's Iaijutsu for the AI (same cooldown). */
    if(!strcmp(command,"iai_3")||!strcmp(command,"iai_4")){
        special_from_ai=1;
        const int ok=som_iai_start(command[4]=='4'?SOM_IAI_HELD_LEVEL:SOM_IAI_LEVEL);
        special_from_ai=0;
        return ok;
    }
    /* special_<1-8>: the primary weapon's charge level, without holding. */
    if(!strncmp(command,"special_",8)&&command[8]>='1'&&command[8]<='8'&&!command[9])
    {
        special_from_ai=1;
        const int ok=som_special_start(0,(unsigned)(command[8]-'0'));
        special_from_ai=0;
        return ok;
    }
    if(!strncmp(command,"equip_",6)){
        for(unsigned t=0;t<8u;t++)
            if(!strcmp(command+6,som_weapon_types[t]))
                return som_equip_weapon(backend_retro_get_memory_data?
                    (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL,t);
    }
    /* Tool-only: apply the preserved device-wide levels to the save in play. */
    if(!strcmp(command,"progress_claim_legacy"))return som_prog_claim_legacy();
    if(!strcmp(command,"next_weapon"))
        return som_cycle_weapon(backend_retro_get_memory_data?
            (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL);
    if(!strncmp(command,"give_",5)){
        for(unsigned t=0;t<8u;t++)
            if(!strcmp(command+5,som_weapon_types[t]))
                return som_give_weapon(backend_retro_get_memory_data?
                    (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL,t);
    }
    if(!strcmp(command,"use_candy"))return som_use_candy(som_live_wram(),0u);
    if(!strcmp(command,"use_candy_1"))return som_use_candy(som_live_wram(),1u);
    if(!strcmp(command,"use_candy_2"))return som_use_candy(som_live_wram(),2u);
    if(!strcmp(command,"use_rope"))return som_use_rope(som_live_wram());
    if(!strcmp(command,"use_herb"))
        return som_use_herb(backend_retro_get_memory_data?
            (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL);
    if(!strcmp(command,"pal_trace")){
        pal_trace_frames=0;pal_trace_on=1;
        som_overlay_say(&overlay,"Recording 8 s: move now!",SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] PALETTE TRACE started\n");
        return 1;
    }
    if(!strcmp(command,"undo_spawn"))return som_undo_spawn();
    if(!strcmp(command,"auto_off")){
        autonomous_mode=0;armed=0;
        fprintf(stderr,"[som-ai] AUTO STOP\n");
        return 1;
    }
    if(autonomous_mode){autonomous_mode=0;armed=0;}
    SomAction action=som_parse_action(command);
    if(action==SOM_ACT_NONE){fprintf(stderr,"[som-ai] unknown action %s\n",command);return 0;}
    som_start_action(action);
    return 1;
}
static void som_check_trigger(void){
    if(!test_enabled||!game_loaded||!on_turbo||
       frame_number%SOM_TRIGGER_POLL_FRAMES)return;
    /* A newer action file command replaces an unfinished hold, so the
     * planner can steer continuously instead of tap-wait-tap. */
    if(!armed&&trigger_file&&access(trigger_file,F_OK)==0){
        if(unlink(trigger_file)==0){som_start_action(SOM_ACT_RIGHT);return;}
        fprintf(stderr,"[som-ai] could not consume legacy trigger\n");
    }
    if(access(action_file,F_OK)!=0)return;
    char command[40]={0};
    FILE *input=fopen(action_file,"r");
    if(!input)return;
    int have=fgets(command,sizeof(command),input)!=NULL;
    fclose(input);
    if(unlink(action_file)!=0)return;
    if(!have)return;
    command[strcspn(command,"\r\n")]=0;
    som_handle_command(command);
}

/* Show one message from the SkyLoft bridge. '|' starts the second line. */
static void som_check_message(void){
    if(!test_enabled||!game_loaded||!on_turbo||frame_number%SOM_MESSAGE_POLL_FRAMES)return;
    if(access(message_file,F_OK)!=0)return;
    char text[96]={0};
    FILE *input=fopen(message_file,"r");
    if(!input)return;
    int have=fgets(text,sizeof(text),input)!=NULL;
    fclose(input);
    unlink(message_file);
    if(have){
        const char *content=text+(text[0]=='^');
        /* Bridge status messages are debug by default. A leading ^ changes
         * their visual style, but not their category; ordinary ^ speech is
         * always retained. */
        if(!gambits.debug_messages&&!strncmp(content,"AI:",3))return;
    }
    if(have&&som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES))
        fprintf(stderr,"[som-ai] MESSAGE %s\n",text);
}
RETRO_API bool flxtr_som_ai_command(const char *command){
    return som_handle_command(command)!=0;
}
RETRO_API bool flxtr_som_ai_message(const char *text){
    return test_enabled&&game_loaded&&on_turbo&&text&&
           som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES)!=0;
}
RETRO_API bool flxtr_som_ai_enabled(void){return ring_ai_enabled!=0;}
RETRO_API unsigned flxtr_som_ai_familiar(void){
    return familiar.slot>=0?(unsigned)familiar.slot+1u:0u;
}
RETRO_API void flxtr_som_ai_gambits(char output[SOM_GAMBIT_COUNT+1]){
    if(output)som_gambit_format(&gambits,output);
}
/* The hero's position on the base 256x224 screen (feet), from read-only
 * WRAM. Best source: his own sprite pieces in the OAM shadow (centre x =
 * pieces' middle, feet 3 lines above their bottom; measured against
 * E002/E004 on the Steam Link), which stays right when the camera stops at
 * a room or map edge. The camera offset this implies is remembered for the
 * frames he blinks (no pieces); without one, the old guess: a camera
 * centred on him, clamped to the map size (7E:00C0/00C2). Field mode only. */
static int som_cam_valid;
static long som_cam_x,som_cam_y;
static unsigned som_cam_map;
static int som_hero_screen(int *x,int *y){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size)return 0;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE)return 0;
    if(ram[0xE000]!=1||ram[0x00E4]!=1||ram[0x00E6]!=0x0F||ram[0x00D0]!=0)return 0;
    long w=ram[0xC0]|ram[0xC1]<<8,h=ram[0xC2]|ram[0xC3]<<8;
    long hx=ram[0xE002]|ram[0xE003]<<8,hy=ram[0xE004]|ram[0xE005]<<8;
    if(w<256||h<224||w>4096||h>4096)return 0;
    const unsigned map=som_bun_u16(ram+0xDC);
    if(som_cam_valid&&som_cam_map!=map)som_cam_valid=0;
    int min_x=1000,max_x=-1000,max_y=-1000;
    for(unsigned i=0;i<128u;i++){
        const uint8_t *e=ram+0x0800u+4u*i;
        if(!som_bun_hero_piece(e,0u))continue;
        int px=(int)(e[0]|(((ram[0x0A00u+i/4u]>>(2u*(i%4u)))&1u)<<8));
        if(px>=256)px-=512;
        if(px<min_x)min_x=px;
        if(px>max_x)max_x=px;
        if((int)e[1]>max_y)max_y=e[1];
    }
    if(max_y>=0){
        const long sx=(min_x+max_x+16)/2,sy=max_y+16-3;
        som_cam_x=hx-sx;som_cam_y=hy-sy;som_cam_valid=1;som_cam_map=map;
        *x=(int)sx;*y=(int)sy;
        return 1;
    }
    if(som_cam_valid){*x=(int)(hx-som_cam_x);*y=(int)(hy-som_cam_y);return 1;}
    hx%=w;hy%=h;
    long cam_x=hx-128,cam_y=hy-112;
    if(cam_x<0)cam_x=0;
    if(cam_x>w-256)cam_x=w-256;
    if(cam_y<0)cam_y=0;
    if(cam_y>h-224)cam_y=h-224;
    *x=(int)(hx-cam_x);*y=(int)(hy-cam_y);
    return 1;
}
/* Frame timing (src/som_perf.h): off unless FLXTR_SOM_PERF=1 or the marker
 * exists. PERF_NOW() is 0 while off, and every PERF_* use checks it. */
#define SOM_PERF_FILE "/tmp/flxtr-som-perf.json"
#define SOM_PERF_MARKER "/tmp/flxtr-som-perf-enable"
#ifndef SOM_PERF_DISABLED
static const char *perf_file=SOM_PERF_FILE,*perf_marker=SOM_PERF_MARKER;
static int perf_on,perf_env=-1;
static SomPerfWindow perf;
static uint64_t perf_last_start,perf_window_start,perf_cb_us;
static unsigned long perf_seq,perf_check;
static struct rusage perf_ru;
#define PERF_NOW() (perf_on?som_perf_now_us():0u)
#define PERF_ADD(phase,t0) do{if(t0)som_perf_add(&perf,(phase),som_perf_now_us()-(t0));}while(0)
static void som_perf_reset(uint64_t now){
    const uint32_t nominal=perf.nominal_us;
    memset(&perf,0,sizeof(perf));perf.nominal_us=nominal;
    perf_window_start=now;getrusage(RUSAGE_SELF,&perf_ru);
}
static double som_perf_tv(const struct timeval *a,const struct timeval *b){
    return (double)(a->tv_sec-b->tv_sec)+(double)(a->tv_usec-b->tv_usec)/1e6;
}
static void som_perf_flush(uint64_t now){
    struct rusage ru;getrusage(RUSAGE_SELF,&ru);
    perf.window_s=(double)(now-perf_window_start)/1e6;
    static char text[4096];
    const int n=som_perf_json(&perf,text,sizeof(text),++perf_seq,
        som_perf_tv(&ru.ru_utime,&perf_ru.ru_utime),som_perf_tv(&ru.ru_stime,&perf_ru.ru_stime),
        ru.ru_minflt-perf_ru.ru_minflt,ru.ru_majflt-perf_ru.ru_majflt,
        ru.ru_nvcsw-perf_ru.ru_nvcsw,ru.ru_nivcsw-perf_ru.ru_nivcsw);
    char temp[300];snprintf(temp,sizeof(temp),"%s.tmp",perf_file);
    int fd=n>0?open(temp,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0644):-1;
    if(fd>=0){
        const int ok=write(fd,text,(size_t)n)==n;
        if(close(fd)==0&&ok)rename(temp,perf_file);else unlink(temp);
    }
    som_perf_reset(now);
}
/* Start of retro_run: enable check, frame interval, window flush. */
static uint64_t som_perf_frame_begin(unsigned long frame){
    if(perf_env<0){const char *e=getenv("FLXTR_SOM_PERF");perf_env=e&&e[0]=='1';}
    if(frame>=perf_check){
        perf_check=frame+SOM_PERF_WINDOW;
        const int want=perf_env||access(perf_marker,F_OK)==0;
        if(want&&!perf_on){perf_last_start=0;som_perf_reset(som_perf_now_us());}
        perf_on=want;
    }
    if(!perf_on)return 0;
    const uint64_t now=som_perf_now_us();
    if(!perf.nominal_us&&backend_retro_get_system_av_info){
        struct retro_system_av_info av;memset(&av,0,sizeof(av));
        backend_retro_get_system_av_info(&av);
        if(av.timing.fps>1.0)perf.nominal_us=(uint32_t)(1e6/av.timing.fps+0.5);
    }
    if(perf_last_start)som_perf_interval(&perf,now-perf_last_start);
    perf_last_start=now;
    if(perf.frames>=SOM_PERF_WINDOW)som_perf_flush(now);
    perf.frames++;
    perf_cb_us=0;
    return now;
}
static void som_perf_frame_end(uint64_t t0,uint64_t core_us){
    if(!t0)return;
    const uint64_t run=som_perf_now_us()-t0;
    som_perf_add(&perf,SOM_PERF_RUN,run);
    som_perf_add(&perf,SOM_PERF_CORE,core_us>perf_cb_us?core_us-perf_cb_us:0);
    som_perf_add(&perf,SOM_PERF_PROXY,run>core_us?run-core_us:0);
}
#else
#define PERF_NOW() 0u
#define PERF_ADD(phase,t0) ((void)(t0))
static uint64_t perf_cb_us;
static uint64_t som_perf_frame_begin(unsigned long frame){(void)frame;return 0;}
static void som_perf_frame_end(uint64_t t0,uint64_t core_us){(void)t0;(void)core_us;}
#endif
/* Opt-in scheduling priority. Measured live on the Steam Link (2026-11-15,
 * 2 x 60 s each): RetroArch's own threads at nice -5 instead of 0 ran the game
 * at 50.1 instead of 48.7 frames/s with the AI decision rate unchanged
 * (3.10 vs 2.97/s). Off unless SOM_PRIORITY_FILE holds -10..-1. Applied at
 * frames 1 and 600 (RetroArch starts its video thread after load_game) to
 * threads named retroarch.exec that are still at nice 0; the SCHED_RR audio
 * thread and helper threads keep theirs. Removing the file takes effect at
 * the next launch. */
#define SOM_PRIORITY_FILE "/home/apps/retroarch/cores/experimental/som_ai_priority"
static const char *priority_file=SOM_PRIORITY_FILE;
static const char *priority_tasks="/proc/self/task";
static int som_priority_wanted(void){
    FILE *in=fopen(priority_file,"r");
    if(!in)return 0;
    int value=0;
    if(fscanf(in,"%d",&value)!=1)value=0;
    fclose(in);
    return value>=-10&&value<=-1?value:0;
}
/* Returns how many threads were changed. */
static int som_priority_apply(int value){
    if(!value)return 0;
    DIR *dir=opendir(priority_tasks);
    if(!dir)return 0;
    int changed=0;
    struct dirent *entry;
    while((entry=readdir(dir))!=NULL){
        char *end=NULL;long tid=strtol(entry->d_name,&end,10);
        if(tid<=0||!end||*end)continue;
        char path[300],name[32]={0};
        snprintf(path,sizeof(path),"%s/%ld/comm",priority_tasks,tid);
        FILE *in=fopen(path,"r");
        if(!in)continue;
        if(!fgets(name,sizeof(name),in))name[0]=0;
        fclose(in);
        name[strcspn(name,"\n")]=0;
        if(strcmp(name,"retroarch.exec"))continue;
        errno=0;
        const int now=getpriority(PRIO_PROCESS,(id_t)tid);
        if(errno||now!=0)continue;
        if(setpriority(PRIO_PROCESS,(id_t)tid,value)==0)changed++;
    }
    closedir(dir);
    return changed;
}
static void som_priority_frame(unsigned long frame){
    if(frame!=1u&&frame!=600u)return;
    const int value=som_priority_wanted();
    const int changed=som_priority_apply(value);
    if(changed)fprintf(stderr,"[som-ai] PRIORITY nice %d on %d RetroArch thread(s)\n",value,changed);
}
/* Soundtrack mixing. The Steam Link has one CPU core that the emulator
 * already fills, so the music is never decoded while it plays: the bridge
 * decodes each arrangement once, at the lowest priority, into a raw stereo
 * s16 file at the core's rate (`greenlink-audio --pcm`), then names that file
 * in SOM_MUSIC_PLAY. Every 30 frames the proxy checks that control file;
 * before each frame it reads what fits into a ~1 s ring (at most 16 KiB, a
 * page-cache read) and loops the file at its end. Every audio batch of the
 * core leaves with the music added. No control file, or an empty ring: the
 * batch passes through untouched. The core's sample rate is published in
 * SOM_MUSIC_RATE_FILE for the bridge. */
#define SOM_MUSIC_PLAY "/tmp/flxtr-som-music.play"
#define SOM_MUSIC_RATE_FILE "/tmp/flxtr-som-audio-rate"
static const char *music_play_file=SOM_MUSIC_PLAY,*music_rate_file=SOM_MUSIC_RATE_FILE;
static retro_audio_sample_batch_t frontend_audio_batch;
static SomMusicRing music_ring;
static int music_fd=-1;
static char music_path[512];
static unsigned long music_check_frame;
static size_t som_audio_batch(const int16_t *data,size_t frames){
    if(!frontend_audio_batch)return frames;
    const uint64_t t0=PERF_NOW();
#ifndef SOM_PERF_DISABLED
    if(t0){
        perf.audio_frames+=frames;
        if(music_fd>=0&&music_ring.count<frames)perf.music_short+=frames-music_ring.count;
    }
#endif
    if(!music_ring.count||!data){
        const size_t r=frontend_audio_batch(data,frames);
        if(t0){const uint64_t d=som_perf_now_us()-t0;PERF_ADD(SOM_PERF_AUDIO,t0);perf_cb_us+=d;}
        return r;
    }
    static int16_t mixed[4096u*2u];
    size_t done=0;
    uint64_t mix_us=0,out_us=0;
    while(done<frames){
        const size_t n=frames-done<4096u?frames-done:4096u;
        const uint64_t m0=PERF_NOW();
        const size_t used=som_mix_apply(&music_ring,mixed,data+2u*done,n);
        (void)used;
        const uint64_t m1=PERF_NOW();
        frontend_audio_batch(mixed,n);
        if(m0){mix_us+=m1-m0;out_us+=som_perf_now_us()-m1;}
#ifndef SOM_PERF_DISABLED
        if(m0)perf.music_frames+=used;
#endif
        done+=n;
    }
#ifndef SOM_PERF_DISABLED
    if(t0){
        som_perf_add(&perf,SOM_PERF_MIX,mix_us);som_perf_add(&perf,SOM_PERF_AUDIO,out_us);
        perf_cb_us+=som_perf_now_us()-t0;
    }
#endif
    return frames;
}
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb){
    load_backend();
    frontend_audio_batch=cb;
    backend_retro_set_audio_sample_batch(som_audio_batch);
}
static void som_music_stop(void){
    if(music_fd>=0){close(music_fd);music_fd=-1;}
    music_path[0]=0;
    som_mix_reset(&music_ring,music_ring.gain?music_ring.gain:SOM_MIX_GAIN_DEFAULT);
}
/* Follow the control file: a new path switches tracks at once, no file
 * stops the music. */
static void som_music_follow(void){
    char want[512]={0};
    FILE *in=fopen(music_play_file,"r");
    if(in){if(!fgets(want,sizeof(want),in))want[0]=0;fclose(in);}
    want[strcspn(want,"\r\n")]=0;
    if(!want[0]){if(music_path[0]){som_music_stop();fprintf(stderr,"[som-ai] MUSIC mix stopped\n");}return;}
    if(!strcmp(want,music_path)&&music_fd>=0)return;
    struct stat st;
    if(stat(want,&st)!=0||!S_ISREG(st.st_mode)||st.st_size<4){
        fprintf(stderr,"[som-ai] MUSIC mix: %s is not a ready PCM file\n",want);
        som_music_stop();snprintf(music_path,sizeof(music_path),"%s",want);
        return;
    }
    som_music_stop();
    music_fd=open(want,O_RDONLY);
    if(music_fd<0)return;
    snprintf(music_path,sizeof(music_path),"%s",want);
    fprintf(stderr,"[som-ai] MUSIC mixing %s (%ld s)\n",want,
            (long)(st.st_size/4/(32040)));
}
static void som_music_pump(void){
    if(frame_number>=music_check_frame){music_check_frame=frame_number+30u;som_music_follow();}
    if(music_fd<0)return;
    uint8_t chunk[16384];
    const unsigned room=som_mix_free_bytes(&music_ring);
    if(room<4096u)return;                        /* keep reads few and large */
    ssize_t got=read(music_fd,chunk,room<sizeof(chunk)?room:sizeof(chunk));
    if(got==0){                                  /* end of the arrangement: loop */
        lseek(music_fd,0,SEEK_SET);music_ring.partial_bytes=0;
        got=read(music_fd,chunk,room<sizeof(chunk)?room:sizeof(chunk));
    }
    if(got>0)som_mix_push_bytes(&music_ring,chunk,(size_t)got);
    else if(got<0){close(music_fd);music_fd=-1;}
}
static void som_music_publish_rate(void){
    if(!backend_retro_get_system_av_info)return;
    struct retro_system_av_info av;memset(&av,0,sizeof(av));
    backend_retro_get_system_av_info(&av);
    if(av.timing.sample_rate<8000.0||av.timing.sample_rate>96000.0)return;
    char temp[256];snprintf(temp,sizeof(temp),"%s.tmp",music_rate_file);
    FILE *out=fopen(temp,"w");
    if(!out)return;
    fprintf(out,"%ld\n",(long)(av.timing.sample_rate+0.5));
    if(fclose(out)==0)rename(temp,music_rate_file);else unlink(temp);
}
#define SOM_COMBO_LABEL_RISE_PX 40   /* counter top, above the hero's head */
#define SOM_COMBO_LABEL_RISE_PX 40   /* counter top, above the hero's head */
/* Generals (src/som_general.h, EXPERIMENTAL): named elite monsters with a
 * rank and a memory, on only while the marker file exists. */
#define SOM_GENERAL_FILE "/home/apps/retroarch/cores/experimental/som_generals.txt"
#define SOM_GENERAL_ENABLE_FILE "/home/apps/retroarch/cores/experimental/som_ai_generals_enable"
static const char *general_file=SOM_GENERAL_FILE;
static const char *general_enable_file=SOM_GENERAL_ENABLE_FILE;
/* Raids have their own opt-in: the first native-combat raid (2026-11-17)
 * left the screen red; off until that is understood. */
#define SOM_RAID_ENABLE_FILE "/home/apps/retroarch/cores/experimental/som_ai_raids_enable"
static const char *raid_enable_file=SOM_RAID_ENABLE_FILE;
static int raids_on;
static SomGeneral generals[SOM_GEN_MAX];
static SomGeneralLive generals_live[SOM_GEN_MAX];
static unsigned general_count;
static int generals_on;
static const char *const som_general_events[]={"","ELECTED","DEFEATED","WON","ESCAPED",
    "BACKING AWAY","STANDS ITS GROUND","VANISHED (not a fight; no record)"};
/* Raids (src/som_raid.h): the general carried into Potos; champions. */
static SomRaid raid;
static int raid_forced;
static unsigned raid_visit_map=0xFFFFu;
static int raid_tried;                  /* this visit already had its chance */
static unsigned long raid_field_since;  /* first frame of field mode on this map */
static int som_raid_has_frames(unsigned kind,void *ctx){
    (void)ctx;
    SomWorkEntry entry;
    return npc_work_pack&&som_work_find(npc_work_pack,npc_work_pack_size,kind,2u,&entry);
}
static void som_general_raid_command(void){
    if(!generals_on||!general_count||!raids_on){fprintf(stderr,"[som-ai] RAID refused: raids not enabled\n");return;}
    if(!raid.captured[0]){fprintf(stderr,"[som-ai] RAID refused: %s not captured yet (visit map %u first)\n",
                                  generals[0].name,generals[0].home_map);return;}
    raid_forced=1;
    fprintf(stderr,"[som-ai] RAID forced: starts on this map when there is room\n");
}
/* Captures the general and one of its pack on the home map for a raid. */
static void som_raid_capture(uint8_t *ram,const SomGeneral *g,const SomGeneralLive *l){
    if(l->slot<0||l->away||frame_number-l->elected_at<SOM_RAID_CAPTURE_DELAY)return;
    if(raid.captured[0]&&frame_number-raid.captured_at<SOM_RAID_CAPTURE_EVERY)return;
    const int slots[SOM_RAID_RAIDERS]={l->slot,-1};
    int pack=-1;
    for(int s=0;s<3;s++){
        const uint8_t *o=ram+SOM_GEN_MONSTER_BASE+(unsigned)s*SOM_GEN_OBJECT;
        if(s!=l->slot&&s!=familiar.slot&&som_gen_alive(o)&&o[0x180]==g->species){pack=s;break;}
    }
    for(unsigned k=0;k<SOM_RAID_RAIDERS;k++){
        const int slot=k?pack:slots[0];
        if(slot<0)continue;
        som_familiar_save(&raid.carry[k],ram,slot);
        if(!raid.captured[k])
            fprintf(stderr,"[som-ai] RAID captured %s from slot %d (object, tile buffer, palette)\n",
                k?"pack Rabite":g->name,slot);
        raid.captured[k]=raid.carry[k].valid;
    }
    raid.captured_at=frame_number;
}
static void som_raid_frame(uint8_t *ram){
    SomGeneral *g=&generals[0];SomGeneralLive *l=&generals_live[0];
    const unsigned map=som_gen_map(ram);
    if(map==g->home_map)som_raid_capture(ram,g,l);
    if(raid.active){
        const int end=som_raid_tick(&raid,ram,frame_number);
        if(end!=SOM_RAID_GOING){
            fprintf(stderr,"[som-ai] RAID on map %u %s\n",raid.map,
                    end==SOM_RAID_REPELLED?"REPELLED: every raider is down":"ended: the hero left");
            if(npc_work.active)som_npc_work_end(&npc_work,ram);
            return;
        }
        if(raid.strike_target>=0&&frame_number>=raid.strike_at){
            const int target=raid.strike_target;
            if(som_raid_strike(&raid,ram))
                fprintf(stderr,"[som-ai] CHAMPION slot %u hits raider slot %d for %u\n",
                        raid.strike_npc,target,SOM_RAID_CHAMPION_DAMAGE);
        }
        if(!npc_work.active&&raid.strike_target<0){
            unsigned npc,dir;int target;
            som_npc_work_load_pack();
            if(som_raid_champion(&raid,ram,som_raid_has_frames,NULL,&npc,&dir,&target)){
                const uint8_t *v=ram+SOM_NPC_OBJECT(npc);
                SomNpcWork w={map,npc,v[0x3D],som_npc_u16(ram,SOM_NPC_OBJECT(npc)+2u),
                              som_npc_u16(ram,SOM_NPC_OBJECT(npc)+4u),dir,1u};
                SomWorkEntry entry;
                const char *why=som_npc_work_refusal_ex(ram,SOM_WRAM_SIZE,&w,npc_work_pack,
                                                        npc_work_pack_size,&entry,1);
                if(!why){
                    som_npc_work_begin(&npc_work,ram,&w,&entry,frame_number);
                    raid.strike_target=target;raid.strike_npc=npc;
                    raid.strike_at=frame_number+som_work_swing_frames(&entry)/2u;
                    fprintf(stderr,"[som-ai] CHAMPION slot %u kind %02X swings at raider slot %d\n",
                            npc,w.kind,target);
                }
            }
        }
        return;
    }
    if(map!=raid_visit_map){raid_visit_map=map;raid_tried=0;raid_field_since=0;}
    if(!som_gen_field(ram)){raid_field_since=0;return;}
    if(!raid_field_since)raid_field_since=frame_number;
    if(frame_number-raid_field_since<SOM_RAID_SETTLE_FRAMES)return;   /* the town sets up first */
    if(raid_tried&&!raid_forced)return;        /* one try per visit */
    /* A forced raid (general_raid) may hit any map but the home map. */
    if((raid_forced?map==g->home_map:map!=SOM_RAID_TOWN_MAP)||!raid.captured[0])return;
    raid_tried=1;                              /* the town is up: this is the try */
    if(!raid_forced&&(g->rank<SOM_RAID_MIN_RANK||
       (raid.have_ended&&frame_number-raid.ended_at<SOM_RAID_GAP_FRAMES)))return;
    raid_forced=0;
    const unsigned placed=som_raid_start(&raid,ram);
    if(!placed){fprintf(stderr,"[som-ai] RAID refused: no free monster slot, OBJ palette 5-7 or open spot near the hero\n");return;}
    if(raid.slot[0]>=0){
        som_gen_reset_live(l);
        l->map=map;l->slot=raid.slot[0];l->away=1;l->done=0;l->elected_at=frame_number;
        l->prev_hp=som_gen_u16(ram+SOM_GEN_MONSTER_BASE+(unsigned)raid.slot[0]*SOM_GEN_OBJECT+0x182u);
        l->hero_was_down=som_gen_u16(ram+0xE182)==0u;
        l->max_hp=som_gen_u16(ram+SOM_GEN_MONSTER_BASE+(unsigned)raid.slot[0]*SOM_GEN_OBJECT+0x184u);
    }
    fprintf(stderr,"[som-ai] RAID %s R%u attacks map %u with %u raider(s): slots %d/%d, palettes %u/%u; combat on (ED %02X)\n",
            g->name,g->rank,map,placed,raid.slot[0],raid.slot[1],raid.palette[0],raid.palette[1],ram[0xED]);
}
static void som_generals_load(void){
    generals_on=access(general_enable_file,F_OK)==0;
    general_count=0;
    for(unsigned i=0;i<SOM_GEN_MAX;i++)som_gen_reset_live(&generals_live[i]);
    if(!generals_on)return;
    char text[2048]={0};
    FILE *f=fopen(general_file,"r");
    if(f){size_t n=fread(text,1,sizeof(text)-1,f);text[n]=0;fclose(f);}
    general_count=som_gen_parse(text,generals,SOM_GEN_MAX);
    if(!general_count)general_count=som_gen_defaults(&generals[0]);
    som_raid_reset(&raid);raid_forced=0;raid_visit_map=0xFFFFu;raid_tried=0;raid_field_since=0;
    raids_on=access(raid_enable_file,F_OK)==0;
    fprintf(stderr,"[som-ai] GENERALS on: %u (%s R%u, %u encounters); raids %s\n",general_count,
        generals[0].name,generals[0].rank,generals[0].encounters,raids_on?"on":"off");
}
static void som_generals_save(void){
    char text[2048];
    const size_t n=som_gen_format(generals,general_count,text,sizeof(text));
    char temp[256];snprintf(temp,sizeof(temp),"%s.tmp",general_file);
    FILE *f=fopen(temp,"w");
    if(!f)return;
    const int ok=fwrite(text,1,n,f)==n;
    if(fclose(f)!=0||!ok||rename(temp,general_file)!=0)
        fprintf(stderr,"[som-ai] GENERALS could not save %s\n",general_file);
}
static void som_generals_frame(void){
    if(!generals_on||!game_loaded||!on_turbo||!backend_retro_get_memory_data||
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE)return;
    uint8_t *ram=(uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    for(unsigned i=0;i<general_count;i++){
        SomGeneral *g=&generals[i];SomGeneralLive *l=&generals_live[i];
        int event=som_gen_elect(g,l,ram,frame_number,familiar.slot);
        if(event==SOM_GEN_NONE)event=som_gen_tick(g,l,ram,frame_number);
        if(event==SOM_GEN_NONE)continue;
        static const char *const weapons[8]={"glove","sword","axe","spear","whip","bow",
            "boomerang","javelin"};
        const char *nemesis=g->nemesis>=0?weapons[g->nemesis]:"none";
        if(event==SOM_GEN_ELECTED)
            fprintf(stderr,"[som-ai] GENERAL %s R%u ELECTED in slot %d on map %u: HP %u, bite every %u frames, nemesis %s\n",
                g->name,g->rank,l->slot,g->home_map,l->prev_hp,som_gen_cooldown_cap(g->rank),nemesis);
        else if(event==SOM_GEN_BACKING||event==SOM_GEN_STANDING)
            fprintf(stderr,"[som-ai] GENERAL %s %s (the boy holds the %s that beat it; bite %s)\n",
                g->name,som_general_events[event],nemesis,event==SOM_GEN_BACKING?"recharging":"ready or out of reach");
        else{
            fprintf(stderr,"[som-ai] GENERAL %s %s: now R%u, %u encounters, %u defeats, %u wins, %u escapes, nemesis %s\n",
                g->name,som_general_events[event],g->rank,g->encounters,g->defeats,g->wins,g->escapes,nemesis);
            som_generals_save();
        }
    }
    if(general_count&&(raids_on||raid.active))som_raid_frame(ram);
}
/* Screen position of an on-field general's label (base pixels), 0 if none. */
static int som_general_label_at(const uint8_t *ram,unsigned i,int *x,int *y){
    const SomGeneralLive *l=&generals_live[i];
    if(!generals_on||l->slot<0||!ram)return 0;
    const uint8_t *o=ram+SOM_GEN_MONSTER_BASE+(unsigned)l->slot*SOM_GEN_OBJECT;
    const unsigned cam=(o[0x0B]&0x80u)?0xACu:0xA8u;
    *x=(int16_t)(som_gen_u16(o+2)-som_gen_u16(ram+cam));
    *y=(int16_t)(som_gen_u16(o+4)-som_gen_u16(ram+cam+2u))-o[0x45]-30;
    return *x>=0&&*x<256&&*y>=0&&*y<224;
}
static void som_class_guide_draw(uint8_t *pixels,unsigned width,unsigned height,size_t pitch){
    const unsigned sx=width>=512?2u:1u,sy=height>=400?2u:1u;
    if(width<256u*sx||height<224u*sy)return;
    const unsigned cls=som_disc_cursor_class(&disciplines);
    const SomClassGuide *guide=&som_class_guide[cls];
    const SomWeaponStyle *style=som_style_of(&styles,cls,NULL);
    const uint32_t white=som_overlay_color(pixel_format,255,255,255);
    const uint32_t gold=som_overlay_color(pixel_format,255,223,143);
    char line[42];

    som_status_texture_draw(&classes_background,pixels,pitch,pixel_format,sx,sy);
    som_disc_window(pixels,width,height,pitch,pixel_format,4,8,248,39);
    som_disc_window(pixels,width,height,pitch,pixel_format,4,50,248,78);
    som_disc_window(pixels,width,height,pitch,pixel_format,4,131,248,66);
    snprintf(line,sizeof(line),"%s  Lv%u",som_discipline_names[cls],(unsigned)disciplines.level[cls]);
    som_overlay_label(pixels,width,height,pitch,pixel_format,line,12,17,0,gold);
    som_overlay_label(pixels,width,height,pitch,pixel_format,guide->role,12,32,0,white);
    if(som_disc_caster(cls)){
        snprintf(line,sizeof(line),"Bare hands / %s",style->name);
        som_overlay_label(pixels,width,height,pitch,pixel_format,line,12,59,0,gold);
        for(unsigned slot=0;slot<2u;slot++){
            snprintf(line,sizeof(line),"%s %s",slot?"Y":"B Bolt",
                     som_class_spell_label(cls,som_caster_spell(cls,slot)));
            som_overlay_label(pixels,width,height,pitch,pixel_format,line,12,73+(int)slot*14,0,white);
        }
    }else{
        snprintf(line,sizeof(line),"B %s  Power %u%%",style->name,(unsigned)style->power_pct);
        som_overlay_label(pixels,width,height,pitch,pixel_format,line,12,59,0,gold);
        snprintf(line,sizeof(line),"  Speed %u%%  Reach %+d px",(unsigned)style->speed_pct,
                 (int)style->reach_bonus);
        som_overlay_label(pixels,width,height,pitch,pixel_format,line,12,73,0,white);
        for(unsigned slot=1;slot<3u&&som_class_arms[cls][slot].grade<=8u;slot++){
            snprintf(line,sizeof(line),"%c %s  Power %u%%",slot==1u?'Y':'X',
                     som_custom_weapon_name(&styles,cls,slot),(unsigned)som_class_arms[cls][slot].power_pct);
            som_overlay_label(pixels,width,height,pitch,pixel_format,line,12,87+(int)(slot-1u)*14,0,white);
        }
    }
    snprintf(line,sizeof(line),"%s cap %u  |  Base stats unchanged",
             som_energy_name(cls),som_energy_max(disciplines.level[cls]));
    som_overlay_label(pixels,width,height,pitch,pixel_format,line,12,115,0,white);
    const uint8_t *ram=som_live_wram();
    const unsigned partner=som_pad2_member(ram);
    const int pad2_guide=partner<SOM_PARTY_MEMBERS&&ram[0x1808]==partner;
    som_overlay_label(pixels,width,height,pitch,pixel_format,
                      pad2_guide?"Player 2 Moves":"Moves & Combos",12,140,0,gold);
    if(pad2_guide){
        const unsigned role=som_disc_x_role(cls);
        snprintf(line,sizeof(line),"X %s",role==SOM_X_SPELLS?"Cast / hold to cycle":
                 role==SOM_X_THIRD_WEAPON?"Attack with the bow":partner>0u?som_ally_moves[cls].name:"No companion technique");
        som_overlay_label(pixels,width,height,pitch,pixel_format,line,12,154,0,white);
        if(partner>0u){
            snprintf(line,sizeof(line),"L2+X  %s",som_ally_moves[cls].name);
            som_overlay_label(pixels,width,height,pitch,pixel_format,line,12,166,0,white);
        }
        som_overlay_label(pixels,width,height,pitch,pixel_format,"R1 Style   R2 Guard",
                          12,178,0,white);
        som_overlay_label(pixels,width,height,pitch,pixel_format,"L2+R1/L1 Change class",
                          12,190,0,white);
    }else{
        for(unsigned i=0;i<3u;i++)
            som_overlay_label(pixels,width,height,pitch,pixel_format,guide->moves[i],12,154+(int)i*12,0,white);
    }
    som_overlay_label(pixels,width,height,pitch,pixel_format,"Up/Dn Class    A or Select Back",
                      128,206,1,white);

    const unsigned kind=cls==SOM_DISC_MARTIAL?SOM_FLYKICK_KIND:
        cls==SOM_DISC_NINJUTSU||cls==SOM_DISC_BUSHIDO?SOM_UKEMI_DRAW_KIND:
        cls==SOM_DISC_ROGUE||cls==SOM_DISC_SWORD?SOM_FORWARD_CART_KIND:0u;
    if(!kind||pad2_guide)return;
    if(!cartwheel_pack_tried){
        cartwheel_pack_tried=1;
        som_load_pack_file(SOM_CARTWHEEL_PACK_FILE,"Hero moves",&cartwheel_pack,&cartwheel_pack_size);
    }
    SomNpcWorkState preview={0};
    const unsigned who=disciplines.who%SOM_PARTY_MEMBERS;
    const unsigned facing=2u;
    if(!som_work_find(cartwheel_pack,cartwheel_pack_size,kind+who,facing,&preview.entry))return;
    preview.active=1;
    preview.no_rest=1;
    preview.profile=som_disc_class_profile(cls);
    const char *move=kind==SOM_FLYKICK_KIND?"Flying Kick":kind==SOM_UKEMI_DRAW_KIND?"Ukemi":"Cartwheel";
    som_overlay_label(pixels,width,height,pitch,pixel_format,move,217,73,1,gold);
    som_npc_work_draw_at(&preview,NULL,frame_number/2ul,221,106,pixels,width,height,pitch,pixel_format);
}

static void som_video_refresh_inner(const void *data,unsigned width,unsigned height,size_t pitch);
/* The MUSIC page, drawn like the CLASSES page. */
static void som_juke_draw(uint8_t *pixels,unsigned width,unsigned height,size_t pitch,int format){
    const unsigned sx=width>=512?2u:1u,sy=height>=400?2u:1u;
    const unsigned bw=200u*sx,bh=(SOM_JUKE_LINES*10u+8u)*sy;
    if(!pixels||bw+12u*sx>width||bh+12u*sy>height)return;
    const unsigned x0=6u*sx,y0=6u*sy;
    som_overlay_fill(pixels,pitch,format,x0,y0,bw,bh,som_overlay_color(format,245,245,255));
    som_overlay_fill(pixels,pitch,format,x0+sx,y0+sy,bw-2u*sx,bh-2u*sy,
                     som_overlay_color(format,30,18,60));
    char out[33];
    for(unsigned line=0;line<SOM_JUKE_LINES;line++){
        if(!som_juke_line(&jukebox,line,out))break;
        const uint32_t ink=line==1u?som_overlay_color(format,255,214,96):
                           som_overlay_color(format,255,255,255);
        for(unsigned i=0;out[i];i++){
            const unsigned char c=(unsigned char)out[i];
            if(c<32u||c>126u)continue;
            const uint8_t *glyph=som_overlay_font[c-32u];
            for(unsigned row=0;row<7u;row++)for(unsigned col=0;col<5u;col++){
                if(!(glyph[row]&(0x10u>>col)))continue;
                som_overlay_fill(pixels,pitch,format,x0+(5u+i*6u+col)*sx,
                                 y0+(4u+line*10u+row)*sy,sx,sy,ink);
            }
        }
    }
}
static uint64_t perf_video_us;
static void som_frontend_video(const void *data,unsigned width,unsigned height,size_t pitch){
    const uint64_t t0=PERF_NOW();
    frontend_video(data,width,height,pitch);
    if(t0){perf_video_us=som_perf_now_us()-t0;}
}
static void som_video_refresh(const void *data,unsigned width,unsigned height,size_t pitch){
    const uint64_t t0=PERF_NOW();
    perf_video_us=0;
    som_video_refresh_inner(data,width,height,pitch);
#ifndef SOM_PERF_DISABLED
    if(t0){
        const uint64_t all=som_perf_now_us()-t0;
        som_perf_add(&perf,SOM_PERF_VIDEO,perf_video_us);
        som_perf_add(&perf,SOM_PERF_OVERLAY,all>perf_video_us?all-perf_video_us:0);
        perf_cb_us+=all;
    }
#else
    (void)t0;
#endif
}
static int som_portal_place_raw(uint8_t *ram){
    if(!ram||som_actor_class()!=SOM_DISC_ARTIFICER||!som_jutsu_free(ram))return 0;
    const uint8_t *hero=SOM_ACT(ram);
    const unsigned map=som_bun_u16(ram+0xDCu),layer=(hero[0x0B]&0x80u)?1u:0u;
    if(portals.count&&(portals.map!=map||portals.layer!=layer))portals.count=0;
    if(portals.count>=2)portals.count=0;                 /* a third: a new pair */
    const int i=portals.count;
    portals.x[i]=(int)som_bun_u16(hero+2);portals.y[i]=(int)som_bun_u16(hero+4);
    portals.map=map;portals.layer=layer;portals.count=i+1;portals.armed=0;
    som_bun_boom(bunshin_booms,portals.x[i],portals.y[i],1,frame_number);
    som_overlay_say(&overlay,i?"^Portal B|step in to travel":"^Portal A|hold X again for B",
                    SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] PORTAL %c at %d,%d map %u\n",i?'B':'A',portals.x[i],portals.y[i],map);
    return 1;
}
/* Each frame: a hero who steps into one portal comes out of the other. He
 * must have left both first (no bouncing back and forth). */
static void som_portal_frame(uint8_t *ram){
    if(!ram||!portals.count)return;
    const uint8_t *hero=SOM_ACT(ram);
    if(som_bun_u16(ram+0xDCu)!=portals.map||SOM_ACT(ram)[0x000]!=1){portals.count=0;return;}
    if(portals.count<2||blink_active||evade_active||ram[0xD0]!=0)return;
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    int inside=-1,far=1;
    for(int i=0;i<2;i++){
        const int d=abs(hx-portals.x[i])>abs(hy-portals.y[i])?abs(hx-portals.x[i]):abs(hy-portals.y[i]);
        if(d<=SOM_PORTAL_ENTER_PX)inside=i;
        if(d<SOM_PORTAL_REARM_PX)far=0;
    }
    if(far){portals.armed=1;return;}
    if(inside<0||!portals.armed||((hero[0x0B]&0x80u)?1u:0u)!=portals.layer)return;
    const int o=1-inside;
    portals.armed=0;
    som_blink_go(ram,portals.x[o],portals.y[o]);
    blink_is_swap=1;                              /* not a Blink: no cooldown */
    fprintf(stderr,"[som-ai] PORTAL travel %c -> %c\n",inside?'B':'A',o?'B':'A');
}
/* The game's HUD lifted SOM_HUD_LIFT_PX (player, 2026-11-20: room below
 * for the class's weapon name and energy). In plain field play only: the
 * HP numbers are on BG3, whose vertical scroll the NMI takes from 7E:0040
 * (C0C170; 0 in the field, menus set it themselves), and the portraits are
 * HUD sprites (tiles 0x1E0-0x1FF near the bottom) in the OAM buffer
 * 7E:0800 the NMI uploads. Applied at the frame's end; the next frame shows
 * it. Dialogs and events (7E:00D0 set) and menus get the HUD back as it was. */
#define SOM_HUD_LIFT_PX 26u
#define SOM_HUD_LEGACY_LIFT_PX 10u
static int hud_lifted;
static void som_hud_lift(uint8_t *ram,int menu){
    const int field=ram&&!menu&&ram[0xE000]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F&&
                    ram[0xD0]==0&&ram[0x7D]!=1;
    if(!field){
        if(hud_lifted&&ram&&ram[0x40]==SOM_HUD_LIFT_PX&&ram[0x41]==0)ram[0x40]=0;
        hud_lifted=0;
        return;
    }
    /* Upgrade the previous 10 px HUD position in an existing save state;
     * leave any unrelated game scroll untouched. */
    if(ram[0x40]!=0&&ram[0x40]!=SOM_HUD_LEGACY_LIFT_PX&&
       !(hud_lifted&&ram[0x40]==SOM_HUD_LIFT_PX))return;
    const int upgrading_legacy=ram[0x40]==SOM_HUD_LEGACY_LIFT_PX;
    ram[0x40]=SOM_HUD_LIFT_PX;ram[0x41]=0;
    hud_lifted=1;
    for(unsigned i=0;i<128u;i++){
        uint8_t *e=ram+0x0800u+4u*i;
        const unsigned tile=e[2]|((unsigned)(e[3]&1u)<<8);
        if(tile>=0x1E0u&&tile<=0x1FFu&&e[1]<0xE0u){
            if(upgrading_legacy&&e[1]>=186u&&e[1]<196u)
                e[1]=(uint8_t)(e[1]-(SOM_HUD_LIFT_PX-SOM_HUD_LEGACY_LIFT_PX));
            else if(e[1]>=196u)e[1]=(uint8_t)(e[1]-SOM_HUD_LIFT_PX);
        }
    }
}
/* Party panels, Seiken Densetsu 3 style (player, 2026-11-20): under each
 * member's HUD portrait (tile 0x1E8 + 2*who in palette who + 1, live OAM
 * 2026-11-20: x 8 / 88 / 168), in that member's 80 px column:
 *   class title below the portrait and the native HP/MP rows;
 *   the weapon (boy: class weapon or spell set; companions: their class
 *   move), from the portrait's left edge under it;
 *   a gauge: the class energy bar and the AP pips, no text.
 * The boy draws nothing without a class; so does a companion. */
#define SOM_PANEL_COLUMN 78        /* px: from the portrait to the next one */
#define SOM_PANEL_BAR 40u
static void som_panel_gauge(unsigned width,unsigned height,size_t pitch,int x,int y,
                            unsigned have,unsigned max,unsigned ap){
    const unsigned sx=width>=512?2u:1u,sy=height>=400?2u:1u;
    const unsigned bw=SOM_PANEL_BAR,bh=3u,fill=max?bw*(have>max?max:have)/max:0u;
    if(x<1||y<1||(unsigned)(x+(int)bw+4+(int)SOM_AP_MAX*5)*sx>=width||(unsigned)(y+(int)bh+2)*sy>=height)return;
    const uint32_t edge=som_overlay_color(pixel_format,16,16,40);
    som_overlay_fill(overlay_frame,pitch,pixel_format,(unsigned)(x-1)*sx,(unsigned)(y-1)*sy,
                     (bw+2u)*sx,(bh+2u)*sy,edge);
    if(fill)som_overlay_fill(overlay_frame,pitch,pixel_format,(unsigned)x*sx,(unsigned)y*sy,
                             fill*sx,bh*sy,som_overlay_color(pixel_format,70,170,255));
    for(unsigned k=0;k<SOM_AP_MAX;k++){
        const unsigned px=(unsigned)(x+(int)bw+4+(int)k*5);
        som_overlay_fill(overlay_frame,pitch,pixel_format,(px-1u)*sx,(unsigned)(y-1)*sy,5u*sx,5u*sy,edge);
        som_overlay_fill(overlay_frame,pitch,pixel_format,px*sx,(unsigned)y*sy,3u*sx,3u*sy,
                         k<ap?som_overlay_color(pixel_format,255,214,96):
                              som_overlay_color(pixel_format,70,70,90));
    }
}
static void som_panel_text(char *out,size_t cap,const char *text,unsigned limit){
    size_t n=0;
    for(;text&&text[n]&&n+1u<cap&&n<limit;n++)out[n]=text[n];
    out[n]=0;
}
static void som_party_panels(uint8_t *frame,unsigned width,unsigned height,size_t pitch,
                             const uint8_t *ram){
    (void)frame;
    if(!ram)return;
    for(unsigned who=0;who<SOM_PARTY_MEMBERS;who++){
        const unsigned cls=som_disc_member_class(&disciplines,who);
        int px,py;
        if(cls>=SOM_DISCIPLINE_COUNT||!som_hud_member_portrait(ram,who,&px,&py))continue;
        char line[24];
        const unsigned chars=(unsigned)(SOM_PANEL_COLUMN/6);
        const int class_y=py+SOM_HUD_LABEL_DY;
        som_panel_text(line,sizeof(line),som_discipline_titles[cls],chars);
        for(char *c=line;*c;c++)if(*c>='a'&&*c<='z')*c=(char)(*c-32);
        som_overlay_label(overlay_frame,width,height,pitch,pixel_format,line,
                          px,class_y,0,
                          som_overlay_color(pixel_format,255,255,255));
        /* Line 2. */
        char second[48]="";uint32_t ink=som_overlay_color(pixel_format,255,214,96);
        if(who==0u&&som_disc_caster(cls)){
            snprintf(second,sizeof(second),"%s/%s",som_class_spell_label(cls,som_caster_spell(cls,0)),
                     som_class_spell_label(cls,som_caster_spell(cls,1)));
            ink=som_overlay_color(pixel_format,170,200,255);
        }else if(who==0u){
            const unsigned type=ram[0xE1E4];
            if(type<8u&&ram[0xCC54u+type]!=0xFF)
                snprintf(second,sizeof(second),"%s",
                         som_custom_weapon_name(&styles,cls,som_class_arm_slot(cls,type)));
        }else{
            snprintf(second,sizeof(second),"%s",som_ally_moves[cls].name);
            ink=som_overlay_color(pixel_format,170,230,200);
        }
        som_panel_text(line,sizeof(line),second,chars);
        if(line[0])som_overlay_label(overlay_frame,width,height,pitch,pixel_format,line,
                                     px,class_y+8,0,ink);
        /* Line 3: the gauge. */
        const unsigned max=som_energy_max(disciplines.level[cls]);
        const SomEnergy *en=who==0u?&energy:&ally_tech[who-1u].en;
        const int fresh=who&&!ally_tech_ready;
        const unsigned have=fresh||!en->started[cls]?max:en->energy[cls];
        som_panel_gauge(width,height,pitch,px+1,class_y+17,have,max,
                        fresh?SOM_AP_MAX:en->ap);
    }
}
static void som_video_refresh_inner(const void *data,unsigned width,unsigned height,size_t pitch){
    if(!frontend_video)return;
    /* NULL repeats the previous (already decorated) frame; (void*)-1 is a
     * hardware-rendered frame that has no CPU pixels to draw on. */
    const int menu=som_manual_disciplines||
        (ring_ai_enabled&&(gambits.open||disciplines.open));
    /* Class title next to the HUD portrait, under the HP numbers. Around the
     * hero, field mode only (som_hero_screen): combo counter, shadow clones,
     * explosions and dodge after-images. */
    if(game_loaded&&on_turbo&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE)
        som_hud_lift((uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM),menu);
    /* The party panels (som_party_panels) need the game's HUD on screen. */
    const char *class_title=menu?NULL:"";
    int hud_x=-1,hud_y=-1;
    if(class_title&&!(backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE&&
       som_hud_member_portrait((const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM),
                               0u,&hud_x,&hud_y)))class_title=NULL;
    char combo_text[24];som_combo_text_at(&combo,som_combo_effective_level(),combo_text);
    if(menu)combo_text[0]=0;
    const int clones=!menu&&(som_bun_alive(&bunshin)||som_bunshin_booming()||som_bolts_active(mage_bolts)||
                             kawarimi_log_start||frame_number<=trail_until);
    int hero_x=-1,hero_y=-1;
    const int hero_drawn=(combo_text[0]||clones)&&som_hero_screen(&hero_x,&hero_y);
    if(backend_retro_get_memory_data)
        som_weapon_mask_pick((const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    const int worker=(npc_work.active||flykick.active||weapon_masked.active||ukemi_draw.active||companion.draw.active||
                      (fuma.active&&frame_number>=fuma.start))&&!menu;
    int general_x=-1,general_y=-1;
    char general_text[40]={0};
    if(!menu&&general_count&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE&&
       som_general_label_at((const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM),
                            0,&general_x,&general_y))
        som_gen_label(&generals[0],general_text,sizeof(general_text));
    int lock_x=-1,lock_y=-1,lock2_x=-1,lock2_y=-1;
    if(!menu){
        (void)som_lock_marker(&lock_x,&lock_y);
        som_lock_swap(0);(void)som_lock_marker(&lock2_x,&lock2_y);som_lock_swap(0);
    }
    const int draw_classes=som_manual_disciplines&&!jukebox.open&&!act_ai&&
        (height==224u||height==448u)&&(width==256u||width==512u);
    const uint8_t *ring_owner_ram=som_live_wram();
    const uint8_t *cam_ram=NULL,*cam_vram=NULL;
    static uint8_t cam_mask[SOM_CAM_W*SOM_CAM_H];
    static uint32_t cam_copy[SOM_CAM_W*SOM_CAM_H];
    static SomCamPixel cam_text[SOM_CAM_W*SOM_CAM_H];
    unsigned cam_texts=0;
    const unsigned cam_bytes=pixel_format==1?4u:2u;
    if(camera_off<0)camera_off=access(camera_off_file,F_OK)==0;
    if(!camera_off&&game_loaded&&on_turbo&&test_enabled&&ring_owner_ram&&data&&
       width==SOM_CAM_W&&height==SOM_CAM_H&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_VIDEO_RAM)>=0x10000u){
        cam_ram=ring_owner_ram;
        cam_vram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_VIDEO_RAM);
        const int shot=som_cam_frame(&camera,cam_ram,som_pad1_member_in(cam_ram),
                                     menu||portals.count!=0,
                                     !camera_govern.off);
        {   /* the time since the last frame belongs to how that one was drawn */
            static int was_hd;
            static float was_ms;
            struct timespec t;
            clock_gettime(CLOCK_MONOTONIC,&t);
            const float ms=(float)(t.tv_sec%1000)*1000.0f+(float)(t.tv_nsec/10000)/100.0f;
            if(som_cam_govern(&camera_govern,was_hd,ms-was_ms))
                fprintf(stderr,"[som-ai] CAMERA hd off: %d -> %d ms/100 per frame\n",
                        (int)(camera_govern.base*100.0f),(int)(camera_govern.hd*100.0f));
            was_ms=ms;was_hd=camera.hd;
            if(camera_govern.hd_n==300u&&camera.hd&&!camera_govern.off){
                camera_govern.hd_n++;
                fprintf(stderr,"[som-ai] CAMERA hd fine: %d -> %d ms/100 per frame\n",
                        (int)(camera_govern.base*100.0f),(int)(camera_govern.hd*100.0f));
            }
        }
        if(shot!=camera_shot){
            camera_shot=shot;
            fprintf(stderr,"[som-ai] CAMERA %s\n",som_shot_names[shot]);
        }
        if(!(cam_ram[0xD0]&0x80u))camera.window=0;
    }else som_cam_reset(&camera);
    const int cam_on=cam_vram&&!menu&&som_cam_active(&camera);
    /* also with the camera off: the HUD lift itself hides the box */
    const uint8_t *box_vram=!menu&&data&&width==SOM_CAM_W&&height==SOM_CAM_H&&hud_lifted&&ring_owner_ram&&
        backend_retro_get_memory_data&&backend_retro_get_memory_size(RETRO_MEMORY_VIDEO_RAM)>=0x10000u?
        (const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_VIDEO_RAM):NULL;
    const int box_up=box_vram&&som_cam_box_up(ring_owner_ram,box_vram);
    const int cam_look=cam_vram&&!menu&&(cam_on||(cam_ram[0xD0]&0x80u));
    const int classes_icon=!menu&&on_turbo&&som_ring_on_classes(ring_owner_ram)&&
        ring_owner_ram[0x1808]==som_pad1_member_in(ring_owner_ram);
    if((!overlay.frames_left&&!menu&&!hero_drawn&&!class_title&&!worker&&!general_text[0]&&lock_x<0&&lock2_x<0&&
        !classes_icon&&!cam_look&&!box_up)||(!data&&!draw_classes)||data==RETRO_HW_FRAME_BUFFER_VALID||
       (pixel_format!=0&&pixel_format!=1&&pixel_format!=2)){
        som_frontend_video(data,width,height,pitch);
        return;
    }
    /* Status uses 512 horizontal pixels even when the underlying ring is
     * 256 pixels wide. Render its background at that resolution directly. */
    const unsigned source_width=width;
    if(draw_classes)width=512u;
    const size_t row=(size_t)width*(pixel_format==1?4u:2u);
    if(!draw_classes&&row>pitch){som_frontend_video(data,width,height,pitch);return;}
    const size_t size=(size_t)height*row;
    if(size>overlay_frame_size){
        uint8_t *grown=realloc(overlay_frame,size);
        if(!grown){som_frontend_video(data,source_width,height,pitch);return;}
        overlay_frame=grown;overlay_frame_size=size;
    }
    if(draw_classes){
        if(class_guide_open)som_class_guide_draw(overlay_frame,width,height,row);
        else som_disc_draw(overlay_frame,width,height,row,pixel_format,&disciplines,&classes_background);
        som_frontend_video(overlay_frame,width,height,row);
        return;
    }
    /* Copy only the visible pixels from the core's padded scanlines. */
    if(row==pitch)memcpy(overlay_frame,data,size);
    else for(unsigned y=0;y<height;y++)
        memcpy(overlay_frame+(size_t)y*row,(const uint8_t*)data+(size_t)y*pitch,row);
    pitch=row;
    if(box_up)som_cam_box_erase(ring_owner_ram,overlay_frame,pitch,cam_bytes);
    if(cam_look)
        cam_texts=som_cam_save(&camera,cam_ram,cam_vram,oam_shown_valid?oam_shown:NULL,overlay_frame,
                               pitch,cam_bytes,cam_mask,cam_text,SOM_CAM_W*SOM_CAM_H);
    if(cam_on)som_cam_rub(cam_mask,overlay_frame,pitch,cam_bytes);
    if(overlay.speech_bubble&&!hero_drawn)som_hero_screen(&hero_x,&hero_y);
    if(worker){
        size_t ram_size=0;
        const uint8_t *ram=som_npc_ram(&ram_size);
        if(ram&&ram_size==SOM_WRAM_SIZE){
            if(npc_work.active)
                som_npc_work_draw(&npc_work,ram,frame_number,overlay_frame,width,height,pitch,
                                  pixel_format);
            if(flykick.active)
                som_npc_work_draw(&flykick,ram,frame_number,overlay_frame,width,height,pitch,
                                  pixel_format);
            if(weapon_masked.active)
                som_weapon_mask_draw(&weapon_masked,ram,overlay_frame,width,height,
                                     pitch,pixel_format);
            if(ukemi_draw.active)
                som_npc_work_draw(&ukemi_draw,ram,frame_number,overlay_frame,width,height,pitch,
                                  pixel_format);
            if(fuma.active&&frame_number>=fuma.start)
                som_npc_work_draw(&fuma,ram,frame_number,overlay_frame,width,height,pitch,
                                  pixel_format);
            if(companion.draw.active)
                som_npc_work_draw(&companion.draw,ram,frame_number,overlay_frame,width,height,
                                  pitch,pixel_format);
        }
    }
    if(hero_drawn){
        if(clones){
            som_trail_draw(overlay_frame,width,height,pitch);
            som_bunshin_draw(overlay_frame,width,height,pitch,hero_x,hero_y);
        }
    }
    if(cam_on){
        som_cam_clamp(&camera);
        const size_t hd_row=2u*SOM_CAM_W*cam_bytes,hd_size=hd_row*2u*SOM_CAM_H;
        uint8_t *hd=NULL;
        if(camera.hd&&hd_size>overlay_frame_size){
            hd=realloc(overlay_frame,hd_size);
            if(hd){overlay_frame=hd;overlay_frame_size=hd_size;}
        }
        if(camera.hd&&overlay_frame_size>=hd_size){
            memcpy(cam_copy,overlay_frame,size);
            som_cam_scale_hd(&camera,(const uint8_t*)cam_copy,pitch,overlay_frame,cam_bytes);
            som_cam_restore_hd(overlay_frame,pitch,cam_bytes,cam_text,cam_texts);
            width=2u*SOM_CAM_W;height=2u*SOM_CAM_H;pitch=hd_row;
        }else{
            som_cam_scale(&camera,overlay_frame,(uint8_t*)cam_copy,pitch,cam_bytes);
            som_cam_restore(overlay_frame,cam_bytes,cam_text,cam_texts);
        }
        som_cam_point(&camera,&hero_x,&hero_y);
        som_cam_point(&camera,&lock_x,&lock_y);
        som_cam_point(&camera,&lock2_x,&lock2_y);
        som_cam_point(&camera,&general_x,&general_y);
    }
    if(lock_x>=0)                                 /* the lock-on marker, bobbing */
        som_overlay_label(overlay_frame,width,height,pitch,pixel_format,"V",
                          lock_x,lock_y-(int)((frame_number/8u)&1u),1,
                          som_overlay_color(pixel_format,255,220,64));
    if(lock2_x>=0)                                /* player 2's */
        som_overlay_label(overlay_frame,width,height,pitch,pixel_format,"V",
                          lock2_x,lock2_y-(int)((frame_number/8u)&1u),1,
                          som_overlay_color(pixel_format,255,220,64));
    if(general_text[0])
        som_overlay_label(overlay_frame,width,height,pitch,pixel_format,general_text,
                          general_x,general_y,1,
                          generals[0].rank>=3u?som_overlay_color(pixel_format,255,80,64):
                                               som_overlay_color(pixel_format,255,214,64));
    if(hero_drawn){
        if(combo_text[0])
            som_overlay_label(overlay_frame,width,height,pitch,pixel_format,combo_text,
                              hero_x,hero_y-SOM_COMBO_LABEL_RISE_PX,1,
                              som_overlay_color(pixel_format,255,208,64));
    }
    if(box_up)som_cam_box_draw(ring_owner_ram,box_vram,overlay_frame,pitch,pixel_format,width/SOM_CAM_W);
    if(class_title)som_party_panels(overlay_frame,width,height,pitch,
        (const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    if(classes_icon)
        som_ring_classes_title(overlay_frame,width,height,pitch,pixel_format);
    /* Artificer portals: a ring at each (A blue, B orange). */
    if(!menu&&portals.count&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE){
        const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        const unsigned sx=width>=512?2u:1u,sy=height>=400?2u:1u;
        const unsigned cam=portals.layer?0xACu:0xA8u;
        static const int ring[][2]={{-7,0},{-6,-2},{-5,-3},{-3,-4},{0,-4},{3,-4},{5,-3},{6,-2},{7,0},
                                    {6,2},{5,3},{3,4},{0,4},{-3,4},{-5,3},{-6,2}};
        const int pulse=(int)((frame_number/6ul)%2ul);
        for(int i=0;i<portals.count;i++){
            const int cx=(int16_t)(portals.x[i]-(int)som_bun_u16(ram+cam));
            const int cy=(int16_t)(portals.y[i]-(int)som_bun_u16(ram+cam+2u))-2;
            const uint32_t ink=i?som_overlay_color(pixel_format,255,150,40):
                                 som_overlay_color(pixel_format,80,200,255);
            for(unsigned k=0;k<sizeof(ring)/sizeof(ring[0]);k++){
                const int px=cx+ring[k][0]*(pulse?9:8)/8,py=cy+ring[k][1];
                if(px<1||py<1||(unsigned)(px+2)*sx>=width||(unsigned)(py+2)*sy>=height)continue;
                som_overlay_fill(overlay_frame,pitch,pixel_format,(unsigned)px*sx,(unsigned)py*sy,
                                 2u*sx,sy,ink);
            }
        }
    }
    if(overlay.frames_left){
        som_overlay_draw(overlay_frame,width,height,pitch,pixel_format,&overlay,hero_x,hero_y);
        overlay.frames_left--;
    }
    if(menu){
        if(act_ai)som_gambit_draw(overlay_frame,width,height,pitch,pixel_format,&gambits);
        else if(jukebox.open)som_juke_draw(overlay_frame,width,height,pitch,pixel_format);
        else if(class_guide_open)
            som_class_guide_draw(overlay_frame,width,height,pitch);
        else if(som_manual_disciplines)
            som_disc_draw(overlay_frame,width,height,pitch,pixel_format,&disciplines,&classes_background);
        else som_gambit_draw(overlay_frame,width,height,pitch,pixel_format,&gambits);
    }
    som_frontend_video(overlay_frame,width,height,pitch);
}
/* Music-free mixing (our core only): Secret of Mana's music is never
 * synthesised, effects keep playing (patches/snes9x2005-flxtr-music-voices). */
static void (*backend_skip_music)(int);
#define SOM_SKIP_MUSIC_MODE 2   /* only effect-owned, effect-keyed voices */
/* Song names (src/som_songs.h): shown when the game starts a song. */
#define SOM_SONG_FILE "/home/apps/retroarch/cores/experimental/som_song_names.tsv"
static const char *song_file=SOM_SONG_FILE;
static SomSongNames song_names;
static unsigned song_last=0x100u;
static void som_song_frame(const uint8_t *ram){
    if(!ram)return;
    const unsigned id=ram[0x1E05];
    if(id==song_last||id>=SOM_SONG_COUNT)return;
    song_last=id;
    char fallback[16],text[64];
    const char *name=som_song_name(&song_names,id,fallback);
    snprintf(text,sizeof(text),"Now playing|%s",name);
    som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] SONG 0x%02X %s (map %u)\n",id,name,
            (unsigned)((ram[0xDC]|ram[0xDD]<<8)&0x1FFu));
}
static bool som_environment(unsigned cmd,void *data){
    bool handled=environment_callback?environment_callback(cmd,data):false;
    if(handled&&cmd==RETRO_ENVIRONMENT_SET_PIXEL_FORMAT&&data)
        pixel_format=(int)*(const enum retro_pixel_format*)data;
    return handled;
}

static void load_backend(void){
    if(backend)return;
    const char *path=getenv("FLXTR_AI_BASE_CORE");
    if(!path||!*path)path=access(SOM_PROXY_FLXTR_CORE,R_OK)==0?SOM_PROXY_FLXTR_CORE:SOM_PROXY_BASE_CORE;
    fprintf(stderr,"[som-ai] SNES core %s\n",path);
    /* Never allow loading this proxy into itself. */
    if(strstr(path,"som_ai_proxy")){fputs("[som-ai] recursive core path refused\n",stderr);abort();}
    backend=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if(!backend){fprintf(stderr,"[som-ai] dlopen failed: %s\n",dlerror());abort();}
    *(void**)(&backend_skip_music)=dlsym(backend,"retro_flxtr_skip_music_voices");
#define LOAD_VIDEO() do { \
    *(void**)(&backend_set_video_refresh)=dlsym(backend,"retro_set_video_refresh"); \
    if(!backend_set_video_refresh){fputs("[som-ai] backend lacks retro_set_video_refresh\n",stderr);abort();} \
} while(0)
#define LOAD(name) do { \
    *(void**)(&backend_##name)=dlsym(backend,#name); \
    if(!backend_##name){fprintf(stderr,"[som-ai] backend lacks %s\n",#name);abort();} \
} while(0)
    LOAD_VIDEO();LOAD(retro_set_audio_sample);
    LOAD(retro_set_audio_sample_batch);LOAD(retro_set_controller_port_device);
    LOAD(retro_reset);LOAD(retro_serialize_size);LOAD(retro_serialize);
    LOAD(retro_unserialize);LOAD(retro_cheat_reset);LOAD(retro_cheat_set);
    LOAD(retro_load_game_special);LOAD(retro_get_region);
    LOAD(retro_get_memory_data);LOAD(retro_get_memory_size);
    LOAD(retro_api_version);LOAD(retro_get_system_info);LOAD(retro_get_system_av_info);
#undef LOAD
#undef LOAD_VIDEO
#define LOAD_DIRECT(name) do { \
    *(void**)(&backend_##name)=dlsym(backend,"retro_"#name); \
    if(!backend_##name){fprintf(stderr,"[som-ai] missing retro_%s\n",#name);abort();} \
} while(0)
    LOAD_DIRECT(set_environment);LOAD_DIRECT(set_input_poll);LOAD_DIRECT(set_input_state);
    LOAD_DIRECT(init);LOAD_DIRECT(deinit);LOAD_DIRECT(load_game);LOAD_DIRECT(unload_game);LOAD_DIRECT(run);
#undef LOAD_DIRECT
}
RETRO_API void retro_set_environment(retro_environment_t cb){
    load_backend();environment_callback=cb;backend_set_environment(som_environment);
}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb){
    load_backend();frontend_video=cb;backend_set_video_refresh(som_video_refresh);
}
RETRO_API void retro_set_input_poll(retro_input_poll_t cb){
    load_backend();physical_poll=cb;backend_set_input_poll(som_input_poll);
}
RETRO_API void retro_set_input_state(retro_input_state_t cb){
    load_backend();frontend_state=cb;physical_state=som_lead_state;backend_set_input_state(som_input_state);
}
RETRO_API void retro_init(void){
    load_backend();backend_init();
}
RETRO_API void retro_deinit(void){
    if(backend_deinit)backend_deinit();
    /* RetroArch may call get_system_info again; do not unload an active
     * backend out from under its borrowed system-info string pointers. */
    game_loaded=0;armed=0;
    unlink(wram_file);unlink(wram_temp_file);unlink(hot_file);unlink(ring_ai_file);
    (void)environment_callback;
}
/* EXPERIMENTAL map-record patches, applied to the proxy's in-memory copy of
 * the ROM before the core sees it (the .smc file is never modified). Lines:
 *   patch <offset> <expected> <new>     (hex; offset without a copier header)
 * A line applies only if the ROM byte equals <expected>, so a different ROM
 * build is never altered. Used to turn a map's monster record into a Rabite,
 * which the game then spawns natively (drawing, AI, death and drops). */
#ifndef SOM_ROM_PATCH_FILE
#define SOM_ROM_PATCH_FILE "/home/apps/retroarch/cores/experimental/som_ai_rom_patch.txt"
#endif
static const char *rom_patch_file=SOM_ROM_PATCH_FILE;
static uint8_t *patched_rom;
static size_t som_apply_rom_patches(uint8_t *rom,size_t size){
    FILE *input=fopen(rom_patch_file,"r");
    if(!input)return 0;
    size_t header=size%0x8000u==512u?512u:0u,applied=0;
    char line[96];
    while(fgets(line,sizeof(line),input)){
        unsigned long offset;unsigned expected,value;
        if(sscanf(line,"patch %lx %x %x",&offset,&expected,&value)!=3)continue;
        if(expected>0xFFu||value>0xFFu||offset+header>=size){
            fprintf(stderr,"[som-ai] ROM PATCH skipped (out of range): %s",line);continue;
        }
        uint8_t *byte=rom+header+offset;
        if(*byte!=expected){
            fprintf(stderr,"[som-ai] ROM PATCH skipped at %06lX: found %02X, expected %02X\n",
                offset,(unsigned)*byte,expected);continue;
        }
        *byte=(uint8_t)value;applied++;
        fprintf(stderr,"[som-ai] ROM PATCH %06lX %02X -> %02X (memory only)\n",
            offset,expected,value);
    }
    fclose(input);
    return applied;
}
/* Turbo truncates its fixed configuration descriptors to six. Enable the
 * existing seventh descriptor and let the game handle projection and rotation. */
static int som_enable_classes_ring(uint8_t *rom,size_t size){
    const size_t header=size%0x8000u==512u?512u:0u;
    const size_t builder=header+0x10D1AEu,entries=header+0x18FF08u;
    const size_t icon_base=header+0x128400u,icon_size=96u;
    const uint8_t original_icon=0xACu,classes_icon=(uint8_t)SOM_RING_CLASSES_DESCRIPTOR;
    const uint8_t empty_icon[96]={0};
    const uint8_t expected_code[]={0xAD,0x28,0x18,0xAA,0xA9,0x06,0x00,0x48,0xBF,0x60,0xFD,0xD8};
    const uint8_t expected_entries[]={
        0x09,0x90,0xA9,0x8F,0xAA,0x90,0xAB,0x91,0xAD,0x93,0xAE,0x94,0xAC,0x92
    };
    if(size<entries+sizeof(expected_entries)||
       memcmp(rom+builder,expected_code,sizeof(expected_code))||
       memcmp(rom+entries,expected_entries,sizeof(expected_entries))||
       memcmp(rom+icon_base+classes_icon*icon_size,empty_icon,sizeof(empty_icon))||
       !som_status_texture_load(&classes_background,rom+header,size-header))return 0;
    memcpy(rom+icon_base+classes_icon*icon_size,rom+icon_base+original_icon*icon_size,icon_size);
    rom[entries+sizeof(expected_entries)-2u]=classes_icon;
    rom[builder+5u]=SOM_RING_CONFIG_ENTRIES;
    return 1;
}
RETRO_API bool retro_load_game(const struct retro_game_info *game){
    load_backend();
    frame_number=0;classes_ring_enabled=0;ring_hidden=0;start_ring_held=0;start_ring_until=0;cancelled=0;completed=0;armed=0;on_turbo=0;som_cam_valid=0;
    active_action=SOM_ACT_NONE;action_frames=0;som_have_previous_menu=0;
    autonomous_mode=0;auto_step=0;auto_deadline=0;
    lead_swapped=0;pad2_raw=pad2_pressed=pad2_wanted=0;pad2_start_block=0;pad2_actor=pad2_combo_actor=SOM_PARTY_MEMBERS;
    pad2_guard_hp=0;pad2_x_tapped=0;pad2_spell_class=SOM_DISCIPLINE_NONE;
    lock_slot=-1;lock_who=SOM_PARTY_MEMBERS;lock_dpad_out=0;auto_target_ready=0;
    som_lock_swap(1);som_string_swap(1);pad2_string_waits=0;pad2_seals=0;
    last_physical_buttons=0;x_tap_armed=x_l2_chord=0;x_cast_at=0;last_attack_frame=0;som_combo_clear();
    special_stage=0;special_weapon=0;special_started=0;
    kusarigama_attack_active=kusarigama_attack_released=kusarigama_swing_seen=0;
    kusarigama_attack_who=0;kusarigama_attack_started=0;
    evade_active=evade_a_hidden=0;evade_hp=0;evade_frames=0;trail_until=0;
    cartwheel_native=0;cartwheel_fallback=0;flykick.active=0;flykick_hid=0;bash_kick=0;
    memset(&ukemi_draw,0,sizeof(ukemi_draw));ukemi_hid=0;ukemi_map=0;
    som_energy_init(&energy);
    memset(&portals,0,sizeof(portals));portal_x_used=0;monk_glove_raised=-1;
    som_tk_reset(&telekinesis);telekinesis_ready=0;memset(rescue_ready,0,sizeof(rescue_ready));
    memset(trail_frame,0,sizeof(trail_frame));seal_mode=0;seal_count=0;x_cycled=0;
    som_bun_clear(&bunshin);bunshin_shunshin=0;memset(bunshin_booms,0,sizeof(bunshin_booms));
    memset(mage_bolts,0,sizeof(mage_bolts));memset(mage_swing_prev,0,sizeof(mage_swing_prev));memset(mage_ally_next,0,sizeof(mage_ally_next));kawarimi_log_start=0;kawarimi_armed_until=0;
    evade_dir_x=evade_dir_y=0;bash_stage=SOM_BASH_NONE;bash_started=0;
    hop_dx=hop_dy=0;hop_frames_left=0;
    special_chord=0;special_suppress=0;
    y_weapon_swap=y_weapon_blocked=b_weapon_blocked=x_spell_blocked=0;x_weapon_swap=0;iai_ready=0;
    combo_input_held=0;combo_hold_start=0;combo_hold_tried=0;
    memset(combo_hp,0,sizeof(combo_hp));memset(combo_ids,0,sizeof(combo_ids));
    memset(combo_live,0,sizeof(combo_live));
    test_enabled=0;trigger_file=NULL;start_frame=0;injected_queries=0;
    wram_export_error_logged=0;
    memset(&overlay,0,sizeof(overlay));
    undo_ready=0;
    ring_ai_selector_open=0;ring_ai_pending=0;ring_ai_enabled=0;
    ring_ai_initial_cell=0;ring_ai_grace_until=0;
    switch_holding=switch_spoiled=0;switch_hold_start=0;
    som_disc_init(&disciplines);som_manual_disciplines=0;class_guide_open=0;menu_hold_until_release=0;menu_was_open=0;
    som_disc_load();
    som_styles_load();style_atk_active=0;style_carry=0;style_cut_pending=0;style_swing_prev=0;
    style_dirty=0;style_b_hidden=0;throw_active=0;throw_slot=-1;throw_ready=0;
    taijutsu_step=0;taijutsu_want=SOM_NO_WEAPON;taijutsu_last=0;
    memset(&xp_decay,0,sizeof(xp_decay));xp_dirty=0;xp_saved_frame=0;xp_spell_slot=-1;
    som_prog_preserve_legacy();
    som_mix_reset(&music_ring,SOM_MIX_GAIN_DEFAULT);
    som_music_stop();music_check_frame=0;
    {const char *perks=getenv("FLXTR_SOM_PERKS");perks_enabled=!(perks&&!strcmp(perks,"0"));}
    prog_rom=game&&game->data&&game->size?som_prog_hash((const uint8_t*)game->data,game->size):0;
    som_prog_fresh(&prog,prog_rom);prog_baseline=0;prog_was_field=0;prog_session_first=1;
    memset(prog_seen,0,sizeof(prog_seen));
    /* A previously selected class also receives missing weapons after load. */
    som_class_selected();
    ally_equip_pending=6u;ally_equip_tried=0;
    ally_order.until=0;ally_tech_ready=0;memset(revivals,0,sizeof(revivals));
    weapon_trace_on=0;som_weapon_trace_reset();
    memset(member_pal_have,0,sizeof(member_pal_have));
    som_gambit_load();som_music_genre_load();
    som_generals_load();
    last_input_frame=frame_number;idle_started=0;
    const char *direct=getenv("FLXTR_SOM_AI_DIRECT");
    direct_transport=direct&&direct[0]=='1'&&direct[1]=='\0';
    const char *path=game?game->path:NULL;
    som_auto_state_path_set(path);
    on_turbo=som_rom_is_turbo(path);
    free(patched_rom);patched_rom=NULL;
    struct retro_game_info patched_game;
    const struct retro_game_info *load_info=game;
    npc_have_walked=0;npc_last_walk_frame=0;
    npc_rom_verified=on_turbo&&game&&game->data&&
        som_npc_rom_verified((const uint8_t*)game->data,game->size);
    if(on_turbo)fprintf(stderr,"[som-ai] living-world NPC routines %s\n",
        npc_rom_verified?"verified":"NOT verified; npc_walk disabled");
    if(on_turbo&&game&&game->data&&game->size&&(patched_rom=malloc(game->size))){
        memcpy(patched_rom,game->data,game->size);
        const size_t map_patches=som_apply_rom_patches(patched_rom,game->size);
        classes_ring_enabled=som_enable_classes_ring(patched_rom,game->size);
        if(map_patches||classes_ring_enabled){
            patched_game=*game;patched_game.data=patched_rom;load_info=&patched_game;
        }
    }
    som_rom=on_turbo&&patched_rom?patched_rom:NULL;
    som_rom_size=som_rom?game->size:0;
    weapon_gfx_verified=0;weapon_gfx_last_check=0;equip_notice_frame=0;
    game_loaded=backend_load_game(load_info);
    if(game_loaded&&on_turbo)som_music_publish_rate();
    som_song_load(&song_names,song_file);song_last=0x100u;
    if(backend_skip_music){
        backend_skip_music(game_loaded&&on_turbo?SOM_SKIP_MUSIC_MODE:0);
        if(game_loaded&&on_turbo)
            fprintf(stderr,"[som-ai] MUSIC not synthesised (effects only) to save CPU\n");
    }else if(game_loaded&&on_turbo)
        fprintf(stderr,"[som-ai] MUSIC skip unavailable: stock SNES core\n");
    if(game_loaded&&on_turbo&&net_pad.fd<0){
        if(som_net_pad_open(&net_pad,SOM_NET_PAD_PORT))
            fprintf(stderr,"[som-ai] NETPAD player 2 listening on UDP %u (Tailscale only)\n",
                    SOM_NET_PAD_PORT);
        else fprintf(stderr,"[som-ai] NETPAD unavailable: UDP %u not bound\n",SOM_NET_PAD_PORT);
    }
    /* Selecting this experimental core is explicit opt-in. No injection
     * occurs until the separate one-shot trigger is consumed. A
     * launcher-provided environment variable is not reliable across the
     * greenlink -> greenlink-catalog -> RetroArch process boundary. */
    test_enabled=game_loaded&&on_turbo;
    if(game_loaded&&backend_retro_get_memory_size)
        fprintf(stderr,"[som-ai] VRAM exposed by the core: %lu bytes\n",(unsigned long)
                backend_retro_get_memory_size(RETRO_MEMORY_VIDEO_RAM));
    if(test_enabled){
        trigger_file=getenv("FLXTR_SOM_AI_TRIGGER_FILE");
        if(!trigger_file || !*trigger_file) trigger_file=SOM_TRIGGER_DEFAULT;
        /* Stale files from prior sessions must never cause a new takeover. */
        if(unlink(trigger_file)!=0 && access(trigger_file,F_OK)==0){
            fprintf(stderr,"[som-ai] cannot clear stale trigger; disabled\n");
            test_enabled=0;
        }
        if(test_enabled&&!direct_transport){
            unlink(action_file);unlink(message_file);unlink(record_marker);
            unlink(npc_command_file);
            recording=0;
            unlink(wram_file);unlink(wram_temp_file);unlink(hot_file);unlink(ring_ai_file);
            fprintf(stderr,"[som-ai] READY: legacy %s; actions via %s; read-only WRAM via %s\n",
                trigger_file,action_file,wram_file);
        }else if(test_enabled)fprintf(stderr,"[som-ai] READY: Android in-process transport\n");
    }else fprintf(stderr,"[som-ai] disabled (Turbo filename missing or game failed to load)\n");
    return game_loaded;
}
/* Full auto-state on exit. RetroArch 1.7.6 writes savestates as a task,
 * 4096 bytes per task tick (tasks/task_save.c SAVE_STATE_CHUNK), and quits
 * right after queueing the exit auto-save: every .state.auto on the Steam
 * Link was its first 4096 bytes and never loaded. The proxy serializes the
 * whole state when the game is unloaded (no frame runs in between, so it
 * is the same moment), writes it beside the auto-state and a detached
 * helper renames it into place once RetroArch has exited, after any
 * partial write of its own. FLXTR_SOM_AUTO_STATE=0 turns this off. */
/* retro_serialize*: the core's state, then the progression trailer. A state
 * of exactly the core's size (older states) loads as before and keeps the
 * progression in memory. */
RETRO_API size_t retro_serialize_size(void){
    load_backend();
    const size_t core=backend_retro_serialize_size?backend_retro_serialize_size():0;
    return core?core+SOM_PROG_TRAILER_SIZE:0;
}
RETRO_API bool retro_serialize(void *data,size_t size){
    load_backend();
    const size_t core=backend_retro_serialize_size?backend_retro_serialize_size():0;
    if(!core||!data||size<core)return false;
    if(!backend_retro_serialize(data,core))return false;
    if(size>=core+SOM_PROG_TRAILER_SIZE){
        som_prog_from_disc(&prog,&disciplines);
        som_prog_trailer_write(&prog,(uint8_t*)data+core);
    }
    return true;
}
RETRO_API bool retro_unserialize(const void *data,size_t size){
    ally_order.until=0;
    memset(revivals,0,sizeof(revivals));
    memset(rescue_ready,0,sizeof(rescue_ready));
    load_backend();
    const size_t core=backend_retro_serialize_size?backend_retro_serialize_size():0;
    if(core&&data&&size==core+SOM_PROG_TRAILER_SIZE){
        if(!backend_retro_unserialize(data,core))return false;
        SomProgress loaded;
        if(som_prog_trailer_read((const uint8_t*)data+core,&loaded)==SOM_PROG_OK){
            if(disciplines.classless&&!loaded.classless)som_disc_init_keep();
            prog=loaded;som_prog_to_disc(&prog,&disciplines);
            prog_session_first=0;prog_was_field=1;
            fprintf(stderr,"[som-ai] PROGRESS restored from the save state (slot %u)\n",
                    prog.slot<SOM_PROG_SLOTS?prog.slot+1u:0u);
        }else fprintf(stderr,"[som-ai] PROGRESS trailer of the save state unreadable\n");
        return true;
    }
    return backend_retro_unserialize&&backend_retro_unserialize(data,size);
}
static char som_auto_state_path[1024];
static void som_auto_state_path_set(const char *rom){
    som_auto_state_path[0]=0;
    const char *off=getenv("FLXTR_SOM_AUTO_STATE");
    if(!rom||!*rom||(off&&!strcmp(off,"0")))return;
    const char *slash=strrchr(rom,'/'),*dot=strrchr(rom,'.');
    size_t stem=dot&&(!slash||dot>slash)?(size_t)(dot-rom):strlen(rom);
    if(stem+12u>=sizeof(som_auto_state_path))return;
    memcpy(som_auto_state_path,rom,stem);
    memcpy(som_auto_state_path+stem,".state.auto",12u);
}
static int som_save_auto_state(void){
    if(!game_loaded||!som_auto_state_path[0]||!backend_retro_serialize_size||
       !backend_retro_serialize)return 0;
    const size_t size=retro_serialize_size();
    void *data=size?malloc(size):NULL;
    if(!data)return 0;
    char temp[sizeof(som_auto_state_path)+16];
    snprintf(temp,sizeof(temp),"%s.flxtr-full",som_auto_state_path);
    int ok=retro_serialize(data,size);
    FILE *out=ok?fopen(temp,"wb"):NULL;
    ok=out&&fwrite(data,1,size,out)==size;
    if(out){ok=fflush(out)==0&&ok;if(ok)fsync(fileno(out));ok=fclose(out)==0&&ok;}
    free(data);
    if(!ok){
        unlink(temp);
        fprintf(stderr,"[som-ai] AUTO-STATE not written: %s\n",temp);
        return 0;
    }
    const pid_t parent=getpid();
    const pid_t child=fork();
    if(child==0){
        if(fork()!=0)_exit(0);              /* the grandchild is reparented */
        setsid();
        const struct timespec tenth={0,100000000L};
        for(unsigned i=0;i<300u&&kill(parent,0)==0;i++)nanosleep(&tenth,NULL);   /* <= 30 s */
        rename(temp,som_auto_state_path);
        _exit(0);
    }
    if(child>0)waitpid(child,NULL,0);
    else rename(temp,som_auto_state_path);  /* no fork: install now */
    fprintf(stderr,"[som-ai] AUTO-STATE %lu bytes -> %s (after RetroArch exits)\n",
            (unsigned long)size,som_auto_state_path);
    return 1;
}
RETRO_API void retro_unload_game(void){
    if(npc_work.active){
        size_t ram_size=0;
        uint8_t *ram=som_npc_ram(&ram_size);
        som_npc_work_end(&npc_work,ram&&ram_size==SOM_WRAM_SIZE?ram:NULL);
    }
    som_save_auto_state();
    som_xp_flush(1);
    som_net_pad_close(&net_pad);net_pad_was_active=0;
    armed=0;autonomous_mode=0;game_loaded=0;
    ring_ai_enabled=0;ring_ai_pending=0;
    weapon_gfx_verified=0;
    unlink(wram_file);unlink(wram_temp_file);unlink(hot_file);unlink(ring_ai_file);unlink(message_file);
    unlink(familiar_file);familiar.slot=familiar.target=-1;familiar_pending=0;
    familiar_carry.valid=0;familiar_undead=0;last_kill_species=-1;
    som_tk_reset(&telekinesis);telekinesis_ready=0;memset(rescue_ready,0,sizeof(rescue_ready));
    weapon_trace_on=0;som_weapon_trace_reset();
    unlink(npc_command_file);
    memset(&overlay,0,sizeof(overlay));
    undo_ready=0;
    if(backend_unload_game)backend_unload_game();
}
static void som_familiar_publish(void){
    if(familiar.slot<0){unlink(familiar_file);return;}
    FILE *out=fopen(familiar_file,"w");
    if(!out)return;
    fprintf(out,"%d\n",familiar.slot+1);
    fclose(out);
}
static void som_familiar_bind(void){
    if(!familiar_pending||last_clone_target<0)return;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    familiar_pending=0;
    familiar.slot=last_clone_target;familiar.target=-1;familiar.last_strike=0;
    familiar.map=ram?(unsigned)(ram[0xDC]|ram[0xDD]<<8):0;
    if(ram){
        som_familiar_tint((uint8_t*)ram+SOM_MONSTER_PALETTE+
                          (unsigned)familiar.slot*SOM_MONSTER_PALETTE_SIZE);
        /* 7E:DFA0 + slot * 0x20 is palette row 5 + slot's shadow; the
         * screen shows 7E:0700 + row * 32 (547ff5e). A native-controlled
         * monster is never repainted, so its tint only showed after a hit
         * flash copied the shadow over (headless 2026-11-23: stayed yellow). */
        if(familiar_is_native_control)
            memcpy((uint8_t*)ram+SOM_MIND_WORKING_ROW(familiar.slot),
                   ram+SOM_MONSTER_PALETTE+(unsigned)familiar.slot*SOM_MONSTER_PALETTE_SIZE,
                   SOM_MONSTER_PALETTE_SIZE);
        som_familiar_save(&familiar_carry,ram,familiar.slot);
        /* Translate the original source monster's captured graphics into
         * the destination slot; never capture the just-cloned target's VRAM. */
        if(!familiar_is_native_control&&clone_source_art.has_vram){
            familiar_carry.has_vram=1;
            familiar_carry.tile_count=clone_source_art.tile_count;
            memcpy(familiar_carry.tile_id,clone_source_art.tile_id,
                   sizeof(familiar_carry.tile_id));
            memcpy(familiar_carry.tile_data,clone_source_art.tile_data,
                   sizeof(familiar_carry.tile_data));
            memcpy(familiar_carry.source_frame_tile,clone_source_art.source_frame_tile,
                   sizeof(familiar_carry.source_frame_tile));
        }
        if(!familiar_is_native_control)
            som_familiar_reapply(&familiar_carry,(uint8_t*)ram,som_vram());
        fprintf(stderr,"[som-ai] FAMILIAR source art: %u native VRAM tiles\n",
                familiar_carry.tile_count);
    }
    som_familiar_publish();
    som_overlay_say(&overlay,"^A familiar joins you!",SOM_MESSAGE_FRAMES);
    fprintf(stderr,"[som-ai] FAMILIAR bound to monster slot %d\n",familiar.slot+1);
    if(familiar_undead)som_familiar_raise_undead((uint8_t*)ram,familiar.slot);
}
static int som_summon_boss(void){
    if(familiar.slot>=0||familiar_carry.valid){
        fprintf(stderr,"[som-ai] SUMMON refused: a familiar is already with you\n");return 0;
    }
    const uint8_t *ram=backend_retro_get_memory_data?
        (const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL;
    if(!som_summon_allowed(ram)){
        som_overlay_say(&overlay,"^Only summoners can summon",SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] SUMMON refused: the leader is not a summoner\n");return 0;
    }
    FILE *in=fopen(summon_file,"rb");
    if(!in){fprintf(stderr,"[som-ai] SUMMON refused: no %s\n",summon_file);return 0;}
    static SomFamiliarCarry loaded;
    memset(&loaded,0,sizeof(loaded)); /* old packages have no VRAM capture */
    loaded.painted_slot=-1;
    size_t got=fread(loaded.object,1,sizeof(loaded.object),in);
    got+=fread(loaded.tiles,1,sizeof(loaded.tiles),in);
    got+=fread(loaded.palette,1,sizeof(loaded.palette),in);
    int extra=fgetc(in)!=EOF;
    fclose(in);
    if(got!=sizeof(loaded.object)+sizeof(loaded.tiles)+sizeof(loaded.palette)||extra||
       loaded.object[0]!=1||loaded.object[0x180]>=0x80){
        fprintf(stderr,"[som-ai] SUMMON refused: %s is not a monster package\n",summon_file);
        return 0;
    }
    if(!som_take_undo())return 0;
    som_familiar_tint(loaded.palette);
    loaded.valid=1;
    familiar_carry=loaded;familiar_arrived=0;familiar_pending=0;
    som_overlay_say(&overlay,"^A summon answers your call!",SOM_MESSAGE_FRAMES);
    fprintf(stderr,"[som-ai] SUMMON monster id %u is coming; undo with undo_spawn\n",
            (unsigned)loaded.object[0x180]);
    return 1;
}
static uint8_t *som_vram(void){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size||
       backend_retro_get_memory_size(RETRO_MEMORY_VIDEO_RAM)<0x10000)return NULL;
    return (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_VIDEO_RAM);
}
/* Runs the familiar each field frame; carries it to the next map. */
static void som_familiar_frame(void){
    if((familiar.slot<0&&!familiar_carry.valid)||!backend_retro_get_memory_data)return;
    uint8_t *ram=(uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE)return;
    if(ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0){            /* field only */
        familiar_arrived=0;
        return;
    }
    if(familiar.slot<0){                                          /* carried */
        if(!familiar_arrived)familiar_arrived=frame_number;
        if(frame_number-familiar_arrived<SOM_FAMILIAR_SETTLE_FRAMES)return;
        /* A summoned familiar takes the shape of a monster of this map: the
         * clone draws with art this map loaded. Remapping the home map's
         * tiles into a slot of another map drew a shadow and stray pixels
         * (headless 2026-11-23, map 130 -> 131). Where no monster lives
         * (towns) it waits, unseen, for the next map that has one. */
        if(familiar_carry.has_vram){
            if((frame_number-familiar_arrived)%30u)return;
            int live=0;
            for(unsigned k=0;k<3u;k++)
                live|=som_monster_live(ram+SOM_MONSTER_BASE+k*SOM_OBJECT_SIZE)&&
                      ram[SOM_MONSTER_BASE+k*SOM_OBJECT_SIZE+0x180u]<0x57u;
            if(!live)return;
            familiar_carry.valid=0;familiar_pending=1;
            if(som_clone_enemy()){
                som_familiar_bind();
                som_overlay_say(&overlay,"^Your familiar|takes a new shape",SOM_MESSAGE_FRAMES/2u);
                fprintf(stderr,"[som-ai] FAMILIAR re-formed on map %u (slot %d)\n",
                        familiar.map,familiar.slot+1);
                return;
            }
            familiar_pending=0;familiar_carry.valid=1;     /* try again later */
            return;
        }
        int slot=som_familiar_restore(&familiar_carry,ram,som_vram());
        if(slot<0)return;                                         /* wait for a slot */
        familiar.slot=slot;familiar.target=-1;familiar.last_strike=0;
        familiar.map=(unsigned)(ram[0xDC]|ram[0xDD]<<8);
        som_familiar_publish();
        fprintf(stderr,"[som-ai] FAMILIAR followed to map %u (slot %d)\n",familiar.map,slot+1);
        return;
    }
    const unsigned long strike_before=familiar.last_strike;
    int state=som_familiar_tick(&familiar,ram,frame_number);
    if(state==SOM_FAMILIAR_HERE&&familiar.last_strike!=strike_before&&familiar.target>=0)
        som_xp_give(SOM_DISC_SUMMONING,SOM_XP_FAMILIAR,
                    ram[0xE600u+0x200u*(unsigned)familiar.target+0x180u],SOM_XPK_FAMILIAR,
                    "familiar strike");
    if(state==SOM_FAMILIAR_HERE&&!familiar_is_native_control&&!familiar_undead)
        som_familiar_reapply(&familiar_carry,ram,som_vram()); /* own slot only */
    if(state==SOM_FAMILIAR_HERE){
        if(frame_number%60u==0)
            som_familiar_update_object(&familiar_carry,ram,familiar.slot);
        return;
    }
    som_familiar_publish();
    if(state==SOM_FAMILIAR_LEFT_MAP){
        familiar_arrived=0;
        if(familiar_is_native_control){
            familiar_carry.valid=0;
            familiar_is_native_control=0;
            mind_slot=-1;mind_until=0;
        }
        fprintf(stderr,"[som-ai] FAMILIAR follows the hero to the next map\n");
    }else{
        familiar_carry.valid=0;familiar_is_native_control=0;familiar_undead=0;
        fprintf(stderr,"[som-ai] FAMILIAR gone (died)\n");
    }
}
static void som_familiar_dismiss(void){
    if(familiar.slot<0||!backend_retro_get_memory_data)return;
    uint8_t *ram=(uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(ram){
        uint8_t *me=ram+SOM_MONSTER_BASE+(unsigned)familiar.slot*SOM_OBJECT_SIZE;
        me[0x182]=1;me[0x183]=0;me[0x1F1]=0xFF;    /* ends through its own death */
    }
    familiar.slot=familiar.target=-1;familiar_pending=0;familiar_carry.valid=0;familiar_undead=0;
    som_familiar_publish();
    fprintf(stderr,"[som-ai] FAMILIAR dismissed\n");
}
/* A clone request made while every slot was busy fires on the first frame
 * a slot is free (the game refills slots about 3 s after a kill). */
static void som_clone_tick(void){
    if(!clone_pending_until)return;
    if(frame_number>clone_pending_until){
        clone_pending_until=0;
        fprintf(stderr,"[som-ai] SPAWN refused: no monster slot came free\n");
        return;
    }
    if(som_clone_enemy()){clone_pending_until=0;som_familiar_bind();return;}
    if(!clone_no_slot){clone_pending_until=0;familiar_pending=0;}
}
/* Use floors from the original species record. WRAM contains the current
 * values on both fresh spawns and save-state loads, so repeated frames
 * and loading a scaled save cannot compound the bonuses. */
static void som_enemy_scale(uint8_t *enemy,const uint8_t *base,unsigned party_level){
    const unsigned level=party_level<97u?party_level+2u:98u;
    const unsigned delta=level>base[0]?level-base[0]:0u;
    if(enemy[0x181]<level)enemy[0x181]=(uint8_t)level;

    const unsigned native_hp=som_word(base+1);
    const unsigned current_max=som_word(enemy+0x184);
    const unsigned hp=som_word(enemy+0x182);
    unsigned target_hp=native_hp+(native_hp+2u)/3u+7u*delta;
    if(target_hp>9999u)target_hp=9999u;
    if(current_max&&hp&&hp<=current_max&&target_hp>current_max){
        som_put_word(enemy+0x182,(uint16_t)((hp*target_hp+current_max-1u)/current_max));
        som_put_word(enemy+0x184,(uint16_t)target_hp);
    }

    /* Combat recalculates attack as strength plus the active weapon row.
     * Raise strength too so switching attack animations keeps the bonus. */
    unsigned strength=base[4]+4u+delta*2u;
    if(strength>99u)strength=99u;
    const uint8_t *weapon=som_rom_at(0xD01000u+12u*enemy[0x1E3],12u);
    if(weapon&&enemy[0x1E3]>=72u){
        unsigned attack=strength+weapon[8];
        if(attack>255u)attack=255u;
        if(enemy[0x198]<attack)enemy[0x198]=(uint8_t)attack;
    }
    if(enemy[0x188]<strength)enemy[0x188]=(uint8_t)strength;
    for(unsigned stat=0;stat<2u;stat++){
        unsigned mind=base[6u+stat]+3u+delta;
        if(mind>99u)mind=99u;
        if(enemy[0x18Bu+stat]<mind)enemy[0x18Bu+stat]=(uint8_t)mind;
    }
    const unsigned defense=som_word(base+9)+2u+delta/2u;
    const unsigned magic_defense=som_word(base+12)+2u+delta/2u;
    if(som_word(enemy+0x1A5)<defense) som_put_word(enemy+0x1A5,(uint16_t)defense);
    if(som_word(enemy+0x1A8)<magic_defense) som_put_word(enemy+0x1A8,(uint16_t)magic_defense);
}

static void som_enemy_scaling_frame(void){
    uint8_t *ram=som_field_ram();
    if(!ram||!som_rom)return;
    unsigned party_level=0;
    for(unsigned who=0;who<SOM_PARTY_MEMBERS;who++){
        const uint8_t *hero=ram+0xE000u+0x200u*who;
        if(hero[0]==1u&&som_word(hero+0x184)&&hero[0x181]<=98u&&hero[0x181]>party_level)
            party_level=hero[0x181];
    }
    for(unsigned slot=0;slot<3u;slot++){
        uint8_t *enemy=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
        const unsigned species=enemy[0x180];
        if(species>=0x54u||(int)slot==familiar.slot||!som_bun_monster_alive(enemy)||
           !som_word(enemy+0x182)||!som_word(enemy+0x184))continue;
        int general=0;
        for(unsigned i=0;i<general_count;i++)
            if(generals_live[i].slot==(int)slot&&
               generals_live[i].map==(som_word(ram+0xDC)&0x1FFu))general=1;
        if(general)continue;
        const uint8_t *base=som_rom_at(0xD01C00u+29u*species,29u);
        if(base&&som_word(base+1)&&base[0]<=98u)som_enemy_scale(enemy,base,party_level);
    }
}
RETRO_API void retro_run(void){
    if(!backend_run)load_backend();
    const uint64_t perf_t0=som_perf_frame_begin(frame_number);
    som_priority_frame(frame_number);
    if(!direct_transport){
        som_check_trigger();
        som_check_npc_command();
        som_check_message();
    }
    som_npc_work_frame();
    som_clone_tick();
    som_familiar_frame();
    if(test_enabled&&on_turbo)som_enemy_scaling_frame();
    som_generals_frame();
    /* No automatic hero VRAM caching: old monster-style snapshots garbled art. */
    if(test_enabled&&on_turbo&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE)
        som_weapon_gfx_verify_frame((const uint8_t*)
            backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    if(test_enabled&&on_turbo){
        som_class_supply_frame();
        som_ally_equip_frame(backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
        som_quick_cast_frame(backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
        som_mage_bolt_frame(backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM),0);
    }
    if(test_enabled&&on_turbo&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE){
        uint8_t *ram=(uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        som_style_frame(ram);
        som_throw_frame(ram);
    }
    if(test_enabled&&on_turbo)som_infinite_mp_frame();
    if(test_enabled&&on_turbo&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE)
        som_song_frame((const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    if(game_loaded&&on_turbo&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE)
        som_hero_palette_frame((uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    if(test_enabled&&on_turbo&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE)
        som_bunshin_frame((uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    if(test_enabled&&on_turbo){
        const uint16_t dpad=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_UP)|(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)|
                                       (1u<<RETRO_DEVICE_ID_JOYPAD_LEFT)|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));
        if(disciplines.selected<SOM_DISCIPLINE_COUNT)
            som_energy_tick(&energy,disciplines.selected,disciplines.level[disciplines.selected],
                            !(last_physical_buttons&dpad));
        som_bash_tick();
        if(fuma.active)som_fuma_frame(backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
        if(blink_active)som_blink_frame(backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
        if(portals.count)som_portal_frame(backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
        if(monk_glove_raised>=0||disciplines.selected==SOM_DISC_MARTIAL_ARTS)
            som_monk_glove(backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
        if(companion.active)som_companion_frame(backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
        som_ally_tech_frame(backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
        if(flykick.active&&backend_retro_get_memory_data)
            som_flykick_frame(backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    }
    if(test_enabled&&on_turbo)som_hop_step();
    if(test_enabled&&on_turbo&&ukemi_draw.active&&backend_retro_get_memory_data)
        som_ukemi_frame((uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    if(test_enabled&&on_turbo){
        const uint64_t t=PERF_NOW();
        som_music_pump();
        PERF_ADD(SOM_PERF_PUMP,t);
    }
    som_xp_flush(0);
    if(pal_trace_on&&backend_retro_get_memory_data)
        som_pal_trace_frame((const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    if(test_enabled&&on_turbo&&frame_number%SOM_MESSAGE_POLL_FRAMES==0u)
        recording=access(record_marker,F_OK)==0;
    som_auto_tick();
    if(armed&&!cancelled&&!completed&&game_loaded &&
       frame_number==start_frame+action_frames){
        completed=1;armed=0;
        fprintf(stderr,"[som-ai] COMPLETE %s at frame %lu; button sampled %lu times\n",
                som_action_name(active_action),frame_number,injected_queries);
    }
    const uint64_t perf_core=PERF_NOW();
    guard_hp=0;
    if(backend_retro_get_memory_data&&backend_retro_get_memory_size&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE){
        const uint8_t *shadow=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        if(shadow){memcpy(oam_shown,shadow+0x0800u,sizeof(oam_shown));oam_shown_valid=1;}
    }
    backend_run();
    if(on_turbo)som_auto_target_frame(som_live_wram());
    som_guard_finish();
    som_pad2_guard_finish();
    if(test_enabled&&on_turbo)som_revival_frame(som_live_wram());
    const uint64_t perf_core_us=perf_core?som_perf_now_us()-perf_core:0;
    if(test_enabled&&on_turbo)som_combo_frame();
    if(test_enabled&&on_turbo&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE)
        som_mage_bolt_frame((uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM),1);
    if(test_enabled&&on_turbo&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE)
        som_style_after((uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    if(style_dirty&&frame_number-style_saved_frame>=3600ul)som_styles_save();
    if(test_enabled&&on_turbo&&backend_retro_get_memory_data)
        som_perks_frame((uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    if(test_enabled&&on_turbo&&frame_number%30u==0u){som_prog_frame();som_unlock_frame(som_live_wram());}
    if(test_enabled&&on_turbo)som_special_tick();
    if(test_enabled&&on_turbo&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE)
    {
        uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        som_monk_chain_frame(ram);
        som_string_swap(0);
        if(monk_chain_active&&monk_chain_who<SOM_PARTY_MEMBERS)
            som_pad2_string(ram,monk_chain_who,0,0);
        som_string_swap(0);
    }
    if(test_enabled&&on_turbo&&cart_flip_y)
        som_cart_flip_after((uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    if(test_enabled&&on_turbo)som_evade_tick();
    if(test_enabled&&on_turbo)som_weapon_trace_frame();
    if(test_enabled&&on_turbo)som_ring_ai_tick();
    som_idle_tick();
    if(!direct_transport&&test_enabled&&on_turbo&&
       frame_number%(ring_ai_enabled||armed?SOM_WRAM_EXPORT_FRAMES:SOM_WRAM_IDLE_EXPORT_FRAMES)==0u){
        const uint64_t t=PERF_NOW();
        const int exported=som_export_wram();
        PERF_ADD(SOM_PERF_WRAM,t);
        if(!exported&&!wram_export_error_logged){
            wram_export_error_logged=1;
            fprintf(stderr,"[som-ai] WRAM export unavailable; live observer disabled\n");
        }
    }
    som_perf_frame_end(perf_t0,perf_core_us);
    frame_number++;
}

/* ---- New classes' X moves (see SOM_HOLY_AHEAD) ---------------------------- */
static void som_ahead(const uint8_t *hero,int distance,int *x,int *y){
    const unsigned facing=hero[0x010];
    int dx=0,dy=0;
    if(facing==0x02u)dx=1;else if(facing==0x82u)dx=-1;else if(facing==0x01u)dy=1;else dy=-1;
    *x=(int)som_bun_u16(hero+2)+dx*distance;*y=(int)som_bun_u16(hero+4)+dy*distance;
}
/* Acquires a *chest actor*, not a loose object ID or an NPC, leaving
 * its native opening, traps and rewards completely untouched. */
static int som_telekinesis_cast_raw(uint8_t *ram){
    if(som_actor_class()!=SOM_DISC_PSIONICS||!ram||!som_jutsu_free(ram))return 0;
    if(som_telekinesis_rescue(ram,som_actor,0))return 1;
    if(telekinesis.slot>=0){
        som_overlay_say(&overlay,"^Telekinesis|already pulling",SOM_MESSAGE_FRAMES/3u);
        return 0;
    }
    if(frame_number<telekinesis_ready){
        som_overlay_say(&overlay,"^Telekinesis|recharging",SOM_MESSAGE_FRAMES/3u);
        return 0;
    }
    if(!som_tk_begin_by(&telekinesis,ram,frame_number,som_actor)){
        som_overlay_say(&overlay,"^Telekinesis|no reachable chest",SOM_MESSAGE_FRAMES/2u);
        return 0;
    }
    telekinesis_ready=frame_number+SOM_TK_COOLDOWN;
    const uint8_t *chest=ram+0xE000u+0x200u*(unsigned)telekinesis.slot;
    som_bun_boom(bunshin_booms,(int)som_bun_u16(chest+2u),
                 (int)som_bun_u16(chest+4u),1,frame_number);
    som_overlay_say(&overlay,"^Telekinesis!|pulling treasure",SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] TELEKINESIS pulling chest actor %d, map %u\\n",
            telekinesis.slot,telekinesis.map);
    return 1;
}
static void som_mind_release(uint8_t *ram){
    if(mind_slot<0)return;
    if(ram&&familiar.slot==mind_slot){
        memcpy(ram+SOM_MONSTER_PALETTE+(unsigned)mind_slot*SOM_MONSTER_PALETTE_SIZE,mind_palette,
               SOM_MONSTER_PALETTE_SIZE<sizeof(mind_palette)?SOM_MONSTER_PALETTE_SIZE:sizeof(mind_palette));
        memcpy(ram+SOM_MIND_WORKING_ROW(mind_slot),mind_palette,
               SOM_MONSTER_PALETTE_SIZE<sizeof(mind_palette)?SOM_MONSTER_PALETTE_SIZE:sizeof(mind_palette));
        familiar.slot=familiar.target=-1;familiar_carry.valid=0;
        som_familiar_publish();
        som_overlay_say(&overlay,"^The mind is free again",SOM_MESSAGE_FRAMES/2u);
    }
    fprintf(stderr,"[som-ai] MIND CONTROL ends (monster slot %d)\n",mind_slot+1);
    mind_slot=-1;mind_until=0;familiar_is_native_control=0;
}
static int som_class_x_raw(uint8_t *ram){
    const unsigned d=som_actor_class();
    if(!ram||d>=SOM_DISCIPLINE_COUNT)return 0;
    /* The binding classes: X again lets the bound one go (no cooldown). */
    const int binder=d==5u||d==SOM_DISC_SUMMONING||d==SOM_DISC_NECRO||d==SOM_DISC_PSIONICS;
    if(binder&&(familiar.slot>=0||familiar_carry.valid||mind_slot>=0)){
        if(mind_slot>=0)som_mind_release(ram);
        else som_familiar_dismiss();
        x_move_ready=frame_number+30ul;                 /* not straight back on the same press */
        som_overlay_say(&overlay,"^Released",SOM_MESSAGE_FRAMES/3u);
        fprintf(stderr,"[som-ai] BIND released by X\n");
        return 1;
    }
    if(frame_number<x_move_ready){
        som_overlay_say(&overlay,"^Not ready",SOM_MESSAGE_FRAMES/3u);return 0;
    }
    if(!som_jutsu_free(ram))return 0;
    uint8_t *hero=SOM_ACT(ram);
    if(d==SOM_DISC_HUNTER){
        som_ahead(hero,SOM_HOLY_AHEAD,&holy_x,&holy_y);
        holy_until=frame_number+SOM_HOLY_FRAMES;x_move_ready=frame_number+SOM_HOLY_COOLDOWN;
        som_bun_boom(bunshin_booms,holy_x,holy_y,0,frame_number);
        som_overlay_say(&overlay,"^Holy Water!",SOM_MESSAGE_FRAMES/2u);
        fprintf(stderr,"[som-ai] HOLY WATER at %d,%d\n",holy_x,holy_y);
        return 1;
    }
    if(d==SOM_DISC_ARTIFICER){
        if(bomb_at){som_overlay_say(&overlay,"^Bomb|already set",SOM_MESSAGE_FRAMES/3u);return 0;}
        som_ahead(hero,SOM_BOMB_AHEAD,&bomb_x,&bomb_y);
        bomb_at=frame_number+SOM_BOMB_FUSE;x_move_ready=frame_number+SOM_BOMB_COOLDOWN;
        som_bun_boom(bunshin_booms,bomb_x,bomb_y,1,frame_number);
        som_overlay_say(&overlay,"^Bomb!|1 s fuse",SOM_MESSAGE_FRAMES/2u);
        fprintf(stderr,"[som-ai] BOMB set at %d,%d\n",bomb_x,bomb_y);
        return 1;
    }
    if(familiar.slot>=0||familiar_carry.valid){
        som_overlay_say(&overlay,"^One ally at a time",SOM_MESSAGE_FRAMES/3u);return 0;
    }
    if(d==5u||d==SOM_DISC_SUMMONING){                   /* a familiar: summon / shadow pact */
        familiar_undead=0;
        familiar_pending=1;
        x_move_ready=frame_number+SOM_RAISE_COOLDOWN;
        som_overlay_say(&overlay,d==5u?"^Shadow Pact!":"^Summon!",SOM_MESSAGE_FRAMES/2u);
        fprintf(stderr,"[som-ai] %s\n",d==5u?"SHADOW PACT":"SUMMON");
        if(som_clone_enemy()){som_familiar_bind();return 1;}
        if(clone_no_slot){clone_pending_until=frame_number+SOM_CLONE_WAIT_FRAMES;return 1;}
        familiar_pending=0;
        som_overlay_say(&overlay,"^Nothing to bind",SOM_MESSAGE_FRAMES/3u);
        return 0;
    }
    if(d==SOM_DISC_NECRO){
        if(!last_kill_frame||frame_number-last_kill_frame>SOM_RAISE_WINDOW){
            som_overlay_say(&overlay,"^Raise Dead|no fresh corpse",SOM_MESSAGE_FRAMES/3u);return 0;
        }
        familiar_pending=1;
        familiar_undead=som_undead_for(last_kill_species);
        x_move_ready=frame_number+SOM_RAISE_COOLDOWN;
        som_overlay_say(&overlay,"^Rise!",SOM_MESSAGE_FRAMES/2u);
        fprintf(stderr,"[som-ai] RAISE DEAD (species 0x%02X -> %s)\n",
                last_kill_species<0?0u:(unsigned)last_kill_species,som_undead_name(familiar_undead));
        if(som_clone_enemy()){som_familiar_bind();return 1;}
        if(clone_no_slot){clone_pending_until=frame_number+SOM_CLONE_WAIT_FRAMES;return 1;}
        familiar_pending=0;
        return 0;
    }
    if(d==SOM_DISC_PSIONICS){
        const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
        int best=-1;long best_d=0;
        for(unsigned slot=0;slot<3u;slot++){
            const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
            if((int)slot==familiar.slot||!som_bun_monster_alive(m)||m[0x180]>=0x57u)continue;   /* no bosses */
            const long dist=labs((long)som_bun_u16(m+2)-hx)+labs((long)som_bun_u16(m+4)-hy);
            if(dist<=SOM_MIND_RANGE&&(best<0||dist<best_d)){best=(int)slot;best_d=dist;}
        }
        if(best<0){som_overlay_say(&overlay,"^Mind Control|no mind near",SOM_MESSAGE_FRAMES/3u);return 0;}
        memcpy(mind_palette,ram+SOM_MONSTER_PALETTE+(unsigned)best*SOM_MONSTER_PALETTE_SIZE,
               SOM_MONSTER_PALETTE_SIZE<sizeof(mind_palette)?SOM_MONSTER_PALETTE_SIZE:sizeof(mind_palette));
        familiar_is_native_control=1;clone_source_art.has_vram=0;
        familiar_pending=1;last_clone_target=best;familiar_undead=0;
        som_familiar_bind();
        mind_slot=best;mind_until=frame_number+SOM_MIND_FRAMES;
        x_move_ready=frame_number+SOM_MIND_COOLDOWN;
        som_overlay_say(&overlay,"^Mind Control!|it fights for you",SOM_MESSAGE_FRAMES/2u);
        fprintf(stderr,"[som-ai] MIND CONTROL: monster slot %d for %lu frames\n",best+1,SOM_MIND_FRAMES);
        return 1;
    }
    return 0;
}
static void som_class_x_frame(uint8_t *ram){
    if(telekinesis.slot>=0){
        const int slot=telekinesis.slot;
        const int result=som_tk_step(&telekinesis,ram,frame_number);
        if(result==1&&frame_number%10u==0u){
            const uint8_t *o=ram+0xE000u+0x200u*(unsigned)slot;
            som_bun_boom(bunshin_booms,(int)som_bun_u16(o+2u),
                         (int)som_bun_u16(o+4u),1,frame_number);
        }
        if(result==2){
            som_overlay_say(&overlay,"^Treasure in reach",SOM_MESSAGE_FRAMES/2u);
            fprintf(stderr,"[som-ai] TELEKINESIS chest delivered to pickup range\\n");
        }else if(result<0)fprintf(stderr,"[som-ai] TELEKINESIS stopped (obstacle or event)\\n");
    }
    const unsigned map=som_bun_u16(ram+0xDC);
    const int field=SOM_ACT(ram)[0x000]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F&&ram[0xD0]==0;
    if(holy_until){
        if(frame_number>=holy_until||!field)holy_until=0;
        else if(!((holy_until-frame_number)%SOM_HOLY_TICK)){
            som_bun_boom(bunshin_booms,holy_x,holy_y,0,frame_number);
            for(unsigned slot=0;slot<3u;slot++){
                if((int)slot==familiar.slot)continue;
                const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
                if(som_bun_monster_alive(m)&&
                   som_bun_near((int)som_bun_u16(m+2),(int)som_bun_u16(m+4),holy_x,holy_y,
                                SOM_HOLY_REACH,SOM_HOLY_REACH))
                    som_class_damage(ram,slot,5u+disciplines.level[SOM_DISC_HUNTER]);
            }
        }
    }
    if(bomb_at&&frame_number>=bomb_at){
        bomb_at=0;
        if(field){
            const unsigned hit=som_bun_blast(ram,bomb_x,bomb_y,
                                             18u+2u*disciplines.level[SOM_DISC_ARTIFICER],familiar.slot);
            som_bun_boom(bunshin_booms,bomb_x,bomb_y,0,frame_number);
            som_bun_boom(bunshin_booms,bomb_x-12,bomb_y-8,0,frame_number);
            som_bun_boom(bunshin_booms,bomb_x+12,bomb_y+8,0,frame_number);
            fprintf(stderr,"[som-ai] BOMB went off: %u monster(s)\n",hit);
        }
    }
    for(unsigned i=0;i<SOM_MINES;i++){
        if(!mine_live[i])continue;
        if(mine_map[i]!=map){mine_live[i]=0;continue;}          /* left behind */
        if(!field)continue;
        if(som_bun_toucher(ram,mine_x[i],mine_y[i],SOM_MINE_TRIGGER,SOM_MINE_TRIGGER,
                           familiar.slot)<0){
            if(!(frame_number%60ul))som_bun_boom(bunshin_booms,mine_x[i],mine_y[i],1,frame_number);
            continue;
        }
        const unsigned hit=som_bun_blast(ram,mine_x[i],mine_y[i],
                                         24u+2u*disciplines.level[SOM_DISC_ARTIFICER],familiar.slot);
        som_bun_boom(bunshin_booms,mine_x[i],mine_y[i],0,frame_number);
        mine_live[i]=0;
        fprintf(stderr,"[som-ai] MINE went off: %u monster(s)\n",hit);
    }
    if(mind_until&&(frame_number>=mind_until||familiar.slot!=mind_slot))som_mind_release(ram);
}

/* ---- Lock-on targeting (see SOM_LOCK_RANGE) ------------------------------- */
/* Strafing. The held D-pad splits into a part toward / away from the
 * target and a part across (round it). With an across part the game is
 * given the walking direction along the circle (8-way: its own walk
 * animation and wall stops), and after its frame the hero is pulled back
 * to the radius, at most SOM_LOCK_PULL px a frame, so the walk bends into
 * a circle. The way round is kept while the direction stays held (one
 * held direction circles all the way); a press mostly toward / away is a
 * plain walk, and the radius is then wherever he stands (never under
 * SOM_LOCK_MIN_RADIUS). */
#define SOM_LOCK_PULL 3
#define SOM_LOCK_MIN_RADIUS 20
static int lock_keep_radius,lock_orbit_sign,lock_last_x,lock_last_y;
static unsigned lock_stuck;               /* frames the circling walk went nowhere */
static uint16_t lock_orbit_dirs;
static int som_isqrt(long v){
    long r=0;while((r+1)*(r+1)<=v)r++;return (int)r;
}
static void som_lock_steer(uint8_t *ram,uint16_t dirs){
    lock_orbiting=0;lock_keep_radius=0;lock_dpad_out=0;
    if(!dirs){lock_orbit_dirs=0;lock_orbit_sign=0;}
    if(!ram||lock_slot<0||ring_ai_enabled||evade_active||!dirs)return;
    if(lock_who>=SOM_PARTY_MEMBERS)return;
    const uint8_t *hero=ram+0xE000u+0x200u*lock_who;
    if(hero[0]!=1||hero[0x01C])return;                /* busy: the game ignores the pad */
    const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)lock_slot;
    const long vx=(long)som_bun_u16(m+2)-(long)som_bun_u16(hero+2);
    const long vy=(long)som_bun_u16(m+4)-(long)som_bun_u16(hero+4);
    const int dist=som_isqrt(vx*vx+vy*vy);
    if(dist<4)return;
    const int x=(dirs&(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT))?1:(dirs&(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT))?-1:0;
    const int y=(dirs&(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN))?1:(dirs&(1u<<RETRO_DEVICE_ID_JOYPAD_UP))?-1:0;
    const long ux=vx*256/dist,uy=vy*256/dist;        /* toward the target, x256 */
    /* The way round is chosen when a direction is pressed and kept while it
     * stays held, so one held direction goes all the way round; a press
     * mostly toward / away is a plain walk that changes the radius. */
    if(dirs!=lock_orbit_dirs||!lock_orbit_sign){
        const long r=x*ux+y*uy,t=-x*uy+y*ux;          /* toward / round, x256 */
        lock_orbit_dirs=dirs;
        lock_orbit_sign=labs(t)>=labs(r)?(t>0?1:-1):0;
        if(!lock_orbit_sign)return;
    }
    const int ts=lock_orbit_sign;
    const long ox=-uy*ts,oy=ux*ts;                    /* along the circle */
    const long ax=labs(ox),ay=labs(oy);
    uint16_t out=0;
    if(ax*5>ay*2)out|=(uint16_t)(1u<<(ox>0?RETRO_DEVICE_ID_JOYPAD_RIGHT:RETRO_DEVICE_ID_JOYPAD_LEFT));
    if(ay*5>ax*2)out|=(uint16_t)(1u<<(oy>0?RETRO_DEVICE_ID_JOYPAD_DOWN:RETRO_DEVICE_ID_JOYPAD_UP));
    /* Blocked on the diagonal (a tree line): slide on one axis, then the
     * other, so he goes along the obstacle instead of standing still. */
    if(lock_stuck>=2u&&(out&(out-1u))){
        const uint16_t h=(uint16_t)(out&((1u<<RETRO_DEVICE_ID_JOYPAD_LEFT)|(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT)));
        const int x_first=ax>=ay;
        out=((lock_stuck/8u)%2u==0u)==x_first?h:(uint16_t)(out&~h);
    }
    lock_dpad_out=out;lock_orbiting=1;lock_keep_radius=1;
}
/* After the game's frame: keep the radius while circling, else take it. */
static void som_lock_orbit_after(uint8_t *ram){
    if(lock_who>=SOM_PARTY_MEMBERS)return;
    uint8_t *hero=ram+0xE000u+0x200u*lock_who;
    const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)lock_slot;
    const long vx=(long)som_bun_u16(m+2)-(long)som_bun_u16(hero+2);
    const long vy=(long)som_bun_u16(m+4)-(long)som_bun_u16(hero+4);
    const int dist=som_isqrt(vx*vx+vy*vy);
    if(!lock_orbiting||!lock_keep_radius||!lock_radius){
        lock_radius=dist<SOM_LOCK_MIN_RADIUS?SOM_LOCK_MIN_RADIUS:dist;return;
    }
    if(dist<4||hero[0x01C]||evade_active)return;
    const int px=(int)som_bun_u16(hero+2),py=(int)som_bun_u16(hero+4);
    lock_stuck=px==lock_last_x&&py==lock_last_y?lock_stuck+1u:0u;
    lock_last_x=px;lock_last_y=py;
    int pull=dist-lock_radius;                        /* + : too far, step in */
    if(!pull)return;
    if(pull>SOM_LOCK_PULL)pull=SOM_LOCK_PULL;else if(pull<-SOM_LOCK_PULL)pull=-SOM_LOCK_PULL;
    const int hx=(int)som_bun_u16(hero+2)+(int)(vx*pull/dist);
    const int hy=(int)som_bun_u16(hero+4)+(int)(vy*pull/dist);
    if(!som_bun_spot_open(ram,hx,hy,som_hero_place(hero)))return;
    hero[2]=(uint8_t)hx;hero[3]=(uint8_t)(hx>>8);
    hero[4]=(uint8_t)hy;hero[5]=(uint8_t)(hy>>8);
}
static void som_lock_swap(int reset){
    static __typeof__(lock_slot) p2_lock_slot=-1;
    static __typeof__(lock_identity) p2_lock_identity;
    static __typeof__(lock_who) p2_lock_who=SOM_PARTY_MEMBERS;
    static __typeof__(lock_orbiting) p2_lock_orbiting;
    static __typeof__(lock_radius) p2_lock_radius;
    static __typeof__(lock_dpad_out) p2_lock_dpad_out;
    static __typeof__(lock_map) p2_lock_map;
    static __typeof__(lock_keep_radius) p2_lock_keep_radius;
    static __typeof__(lock_orbit_sign) p2_lock_orbit_sign;
    static __typeof__(lock_last_x) p2_lock_last_x;
    static __typeof__(lock_last_y) p2_lock_last_y;
    static __typeof__(lock_stuck) p2_lock_stuck;
    static __typeof__(lock_orbit_dirs) p2_lock_orbit_dirs;
    if(reset){
        p2_lock_slot=-1;
        p2_lock_identity=0;
        p2_lock_who=SOM_PARTY_MEMBERS;
        p2_lock_orbiting=0;
        p2_lock_radius=0;
        p2_lock_dpad_out=0;
        p2_lock_map=0;
        p2_lock_keep_radius=0;
        p2_lock_orbit_sign=0;
        p2_lock_last_x=0;
        p2_lock_last_y=0;
        p2_lock_stuck=0;
        p2_lock_orbit_dirs=0;
        return;
    }
    {__typeof__(lock_slot) t=lock_slot;lock_slot=p2_lock_slot;p2_lock_slot=t;}
    {__typeof__(lock_identity) t=lock_identity;lock_identity=p2_lock_identity;p2_lock_identity=t;}
    {__typeof__(lock_who) t=lock_who;lock_who=p2_lock_who;p2_lock_who=t;}
    {__typeof__(lock_orbiting) t=lock_orbiting;lock_orbiting=p2_lock_orbiting;p2_lock_orbiting=t;}
    {__typeof__(lock_radius) t=lock_radius;lock_radius=p2_lock_radius;p2_lock_radius=t;}
    {__typeof__(lock_dpad_out) t=lock_dpad_out;lock_dpad_out=p2_lock_dpad_out;p2_lock_dpad_out=t;}
    {__typeof__(lock_map) t=lock_map;lock_map=p2_lock_map;p2_lock_map=t;}
    {__typeof__(lock_keep_radius) t=lock_keep_radius;lock_keep_radius=p2_lock_keep_radius;p2_lock_keep_radius=t;}
    {__typeof__(lock_orbit_sign) t=lock_orbit_sign;lock_orbit_sign=p2_lock_orbit_sign;p2_lock_orbit_sign=t;}
    {__typeof__(lock_last_x) t=lock_last_x;lock_last_x=p2_lock_last_x;p2_lock_last_x=t;}
    {__typeof__(lock_last_y) t=lock_last_y;lock_last_y=p2_lock_last_y;p2_lock_last_y=t;}
    {__typeof__(lock_stuck) t=lock_stuck;lock_stuck=p2_lock_stuck;p2_lock_stuck=t;}
    {__typeof__(lock_orbit_dirs) t=lock_orbit_dirs;lock_orbit_dirs=p2_lock_orbit_dirs;p2_lock_orbit_dirs=t;}
}
static void som_lock_release(const char *why){
    if(lock_slot<0)return;
    fprintf(stderr,"[som-ai] LOCK off (monster slot %d): %s\n",lock_slot+1,why);
    lock_slot=-1;
    lock_who=SOM_PARTY_MEMBERS;
    lock_orbiting=0;lock_radius=0;lock_dpad_out=0;
}
static int som_lock_acquire(uint8_t *ram,unsigned slot,unsigned who){
    if(!ram||slot>=3u||who>=SOM_PARTY_MEMBERS||(int)slot==familiar.slot)return 0;
    const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
    const uint8_t *hero=ram+0xE000u+0x200u*who;
    if(!som_bun_monster_alive(m)||!som_ally_alive(hero))return 0;
    lock_slot=(int)slot;lock_identity=m[0x180];lock_who=who;
    lock_map=som_bun_u16(ram+0xDC);lock_radius=0;
    return 1;
}
static void som_lock_toggle_for(uint8_t *ram,unsigned who){
    if(who>=SOM_PARTY_MEMBERS)return;
    if(lock_slot>=0){
        som_lock_release("let go");
        som_overlay_say(&overlay,"^Lock off",SOM_MESSAGE_FRAMES/4u);
        return;
    }
    if(!ram)return;
    const uint8_t *hero=ram+0xE000u+0x200u*who;
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    int best=-1;long best_d=0;
    for(unsigned slot=0;slot<3u;slot++){
        if((int)slot==familiar.slot)continue;
        const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
        if(!som_bun_monster_alive(m))continue;
        const long dist=labs((long)som_bun_u16(m+2)-hx)+labs((long)som_bun_u16(m+4)-hy);
        if(dist<=SOM_LOCK_RANGE&&(best<0||dist<best_d)){best=(int)slot;best_d=dist;}
    }
    if(best<0){som_overlay_say(&overlay,"^Nothing to lock on",SOM_MESSAGE_FRAMES/4u);return;}
    if(!som_lock_acquire(ram,(unsigned)best,who))return;
    som_overlay_say(&overlay,"^Locked on",SOM_MESSAGE_FRAMES/4u);
    fprintf(stderr,"[som-ai] LOCK on monster slot %d (%ld px)\n",best+1,best_d);
}
static void som_lock_toggle(uint8_t *ram){som_lock_toggle_for(ram,som_pad1_member_in(ram));}
static void som_lock_frame(uint8_t *ram){
    if(lock_slot<0||!ram)return;
    if(ring_ai_enabled){som_lock_release("the AI plays");return;}
    if(lock_who>=SOM_PARTY_MEMBERS){som_lock_release("invalid actor");return;}
    const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)lock_slot;
    if(som_bun_u16(ram+0xDC)!=lock_map){som_lock_release("map changed");return;}
    if(!som_bun_monster_alive(m)||m[0x180]!=lock_identity||lock_slot==familiar.slot){
        som_lock_release("target gone");return;
    }
    uint8_t *hero=ram+0xE000u+0x200u*lock_who;
    if(!som_ally_alive(hero)){som_lock_release("actor unavailable");return;}
    if(hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0)return;
    const int dx=(int)som_bun_u16(m+2)-(int)som_bun_u16(hero+2);
    const int dy=(int)som_bun_u16(m+4)-(int)som_bun_u16(hero+4);
    if(abs(dx)+abs(dy)>SOM_LOCK_DROP){som_lock_release("too far");return;}
    som_lock_orbit_after(ram);
    /* Not during a dodge or a pose the game is playing on its own. */
    if(evade_active||hero[0x01C]==0x40u||hero[0x01C]==0x30u)return;
    hero[0x010]=abs(dx)>=abs(dy)?(dx>=0?0x02u:0x82u):(dy>=0?0x01u:0x00u);
}
static void som_lock_frame2(uint8_t *ram){
    som_lock_swap(0);
    if(lock_slot>=0&&(pad2_actor>=SOM_PARTY_MEMBERS||lock_who!=pad2_actor))
        som_lock_release("player 2 gone");
    som_lock_frame(ram);
    som_lock_swap(0);
}
/* Who gets the automatic target: player 2's hero into her own lock. */
static int som_lock_acquire_for(uint8_t *ram,unsigned slot,unsigned who){
    if(who!=pad2_actor)return som_lock_acquire(ram,slot,who);
    som_lock_swap(0);
    const int ok=som_lock_acquire(ram,slot,who);
    som_lock_swap(0);
    return ok;
}
static int som_lock_marker(int *x,int *y){
    if(lock_slot<0||!backend_retro_get_memory_data||
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE)return 0;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    const uint8_t *o=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)lock_slot;
    const unsigned cam=(o[0x0B]&0x80u)?0xACu:0xA8u;
    *x=(int16_t)(som_bun_u16(o+2)-som_bun_u16(ram+cam));
    *y=(int16_t)(som_bun_u16(o+4)-som_bun_u16(ram+cam+2u))-o[0x45]-34;
    if(*x<0||*x>=256||*y<0||*y>=224){*x=-1;return 0;}
    return 1;
}
static void som_auto_target_frame(uint8_t *ram){
    if(!ram||ram[0xE4]!=1u||ram[0xE6]!=0x0Fu||ram[0xD0]!=0u||ram[0x7D]==1u){
        auto_target_ready=0;return;
    }
    const unsigned map=som_bun_u16(ram+0xDC);
    if(!auto_target_ready||map!=auto_target_map){
        auto_target_ready=1;auto_target_map=map;
        for(unsigned i=0;i<3u;i++){
            const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*i;
            auto_target_enemy_hp[i]=(uint16_t)som_word(m+0x182);
            auto_target_enemy_id[i]=m[0x180];auto_target_enemy_live[i]=(uint8_t)som_bun_monster_alive(m);
        }
        for(unsigned i=0;i<SOM_PARTY_MEMBERS;i++)
            auto_target_party_hp[i]=(uint16_t)som_word(ram+0xE000u+0x200u*i+0x182u);
        som_lock_frame(ram);return;
    }
    unsigned hitter=som_pad1_member_in(ram);
    if(pad2_actor<SOM_PARTY_MEMBERS&&
       (pad2_combo_actor==pad2_actor||
        (pad2_raw&((1u<<RETRO_DEVICE_ID_JOYPAD_B)|(1u<<RETRO_DEVICE_ID_JOYPAD_X)|
                   (1u<<RETRO_DEVICE_ID_JOYPAD_Y)))))hitter=pad2_actor;
    for(unsigned i=0;i<3u;i++){
        const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*i;
        const unsigned hp=som_word(m+0x182);
        const int live=som_bun_monster_alive(m)&&hp>0;
        if((familiar.slot<0||(int)i!=familiar.slot)&&live&&
           auto_target_enemy_live[i]&&m[0x180]==auto_target_enemy_id[i]&&
           hp<auto_target_enemy_hp[i])
            som_lock_acquire_for(ram,i,hitter);
        auto_target_enemy_hp[i]=(uint16_t)hp;
        auto_target_enemy_id[i]=m[0x180];auto_target_enemy_live[i]=(uint8_t)live;
    }
    for(unsigned who=0;who<SOM_PARTY_MEMBERS;who++){
        const uint8_t *hero=ram+0xE000u+0x200u*who;
        const unsigned hp=som_word(hero+0x182);
        if(som_ally_alive(hero)&&auto_target_party_hp[who]&&hp<auto_target_party_hp[who]){
            const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
            int best=-1,best_d=SOM_LOCK_RANGE+1;
            const int touching=som_bun_toucher(ram,hx,hy,SOM_BUN_HERO_TOUCH_X*2,
                                                SOM_BUN_HERO_TOUCH_Y*2,familiar.slot);
            if(touching>=0)best=touching;
            else for(unsigned i=0;i<3u;i++){
                const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*i;
                if((int)i==familiar.slot||!som_bun_monster_alive(m)||
                   (m[0x0B]&0x80u)!=(hero[0x0B]&0x80u))continue;
                const int d=abs((int)som_bun_u16(m+2)-hx)+abs((int)som_bun_u16(m+4)-hy);
                if(d<=SOM_LOCK_RANGE&&d<best_d){best=(int)i;best_d=d;}
            }
            if(best>=0)som_lock_acquire_for(ram,(unsigned)best,who);
        }
        auto_target_party_hp[who]=(uint16_t)hp;
    }
    som_lock_frame(ram);
}

/* ---- ROM-derived weapon masks (Tanto, Dai Katana, Kusarigama) ---- */
static void som_weapon_mask_load(void){
    if(weapon_mask_tried)return;
    weapon_mask_tried=1;
    som_load_pack_file(weapon_mask_file,"weapon style masks",
                       &weapon_mask_pack,&weapon_mask_pack_size);
    if(!weapon_mask_pack||weapon_mask_pack_size<10u||
       memcmp(weapon_mask_pack,"SOMWORK1",8u))return;
    weapon_mask_index=calloc(3u*72u*256u,sizeof(*weapon_mask_index));
    if(!weapon_mask_index)return;
    const uint8_t *p=weapon_mask_pack+10u,*end=weapon_mask_pack+weapon_mask_pack_size;
    const unsigned entries=weapon_mask_pack[8]|(unsigned)weapon_mask_pack[9]<<8;
    unsigned count=0;
    for(unsigned e=0;e<entries;e++){
        if(end-p<44)goto malformed;
        const uint8_t *next=p+44u;
        for(unsigned frame=0;frame<p[3];frame++){
            next=som_work_next_frame(next,end);
            if(!next)goto malformed;
        }
        if(p[0]>=0xD4u&&p[0]<=0xD6u&&p[1]>=9u&&p[1]<18u&&p[3]==1u){
            weapon_mask_index[((p[0]-0xD4u)*72u+p[1])*256u+p[2]]=
                (uint32_t)(p-weapon_mask_pack)+1u;
            count++;
        }
        p=next;
    }
    fprintf(stderr,"[som-ai] weapon styles: indexed %u masked frames\\n",count);
    return;
malformed:
    free(weapon_mask_index);weapon_mask_index=NULL;
    fprintf(stderr,"[som-ai] weapon style: malformed mask pack ignored\\n");
}
/* -1 = native, 0 = Tanto, 1 = Dai Katana, 2 = Kusarigama strike. */
static int som_weapon_mask_mode(const uint8_t *ram,unsigned who){
    if(!weapon_mask_on_checked||frame_number-weapon_mask_on_checked>=120ul){
        weapon_mask_on_checked=frame_number?frame_number:1ul;
        weapon_mask_on=access(weapon_mask_on_file,F_OK)==0;
    }
    if(weapon_mask_faulted||!weapon_mask_on)return -1;
    const uint8_t *actor=ram+0xE000u+0x200u*who;
    if(kusarigama_attack_active&&who==kusarigama_attack_who&&
       som_disc_member_class(&disciplines,who)==SOM_DISC_NINJUTSU&&
       styles.selected[SOM_DISC_NINJUTSU]==SOM_STYLE_HEAVY&&actor[0x1E4]==1u)
        return 2;
    if(who!=0u||disciplines.selected!=SOM_DISC_BUSHIDO||actor[0x1E4]!=1u)return -1;
    const unsigned style=styles.selected[SOM_DISC_BUSHIDO]%SOM_STYLES_PER_CLASS;
    return style==SOM_STYLE_FAST?0:style==SOM_STYLE_HEAVY?1:-1;
}
static int som_weapon_mask_entry(unsigned mode,unsigned row,unsigned frame,SomWorkEntry *out){
    if(!weapon_mask_index||mode>2u||row<9u||row>=18u||frame>255u)return 0;
    const uint32_t at=weapon_mask_index[(mode*72u+row)*256u+frame];
    if(!at)return 0;
    const uint8_t *p=weapon_mask_pack+at-1u;
    out->kind=p[0];out->weapon=p[1];out->facing=p[2];out->frames=p[3];
    for(unsigned i=0;i<20u;i++)out->colours[i]=(uint16_t)(p[4+2u*i]|p[5+2u*i]<<8);
    out->first=p+44u;
    return 1;
}
/* Only start the hybrid move when the opt-in ROM-derived whip art covers
 * the native sword level-2 frames. An old Bushido-only pack leaves normal
 * Kusarigama whip attacks untouched. */
static int som_kusarigama_art_ready(void){
    if(!weapon_mask_on||weapon_mask_faulted||!weapon_mask_index)return 0;
    static const unsigned frames[]={171u,172u,173u,174u,175u,176u,177u,178u,179u};
    for(unsigned row=9u;row<18u;row++)for(unsigned f=0;f<sizeof(frames)/sizeof(frames[0]);f++)
        if(weapon_mask_index[(2u*72u+row)*256u+frames[f]])return 1;
    return 0;
}
/* Hide native effects only when a matching mask exists for the current
 * sword row and animation frame. On a missing mask, restore the original
 * effect tiles byte-for-byte, rather than showing an invisible weapon. */
static void som_weapon_mask_frame(uint8_t *ram){
    if(!ram)return;
    som_weapon_mask_load();
    if(kusarigama_attack_active){
        const uint8_t *owner=ram+0xE000u+0x200u*kusarigama_attack_who;
        if(frame_number-kusarigama_attack_started>420ul||
           som_disc_member_class(&disciplines,kusarigama_attack_who)!=SOM_DISC_NINJUTSU||
           styles.selected[SOM_DISC_NINJUTSU]!=SOM_STYLE_HEAVY){
            kusarigama_attack_active=kusarigama_attack_released=kusarigama_swing_seen=0;
        }else if(kusarigama_attack_released&&owner[0x01C])kusarigama_swing_seen=1;
        else if(kusarigama_swing_seen&&!owner[0x01C])
            kusarigama_attack_active=kusarigama_attack_released=kusarigama_swing_seen=0;
    }
    const unsigned who=kusarigama_attack_active?kusarigama_attack_who:0u;
    const uint8_t *actor=ram+0xE000u+0x200u*who;
    const int field=actor[0]==1u&&ram[0xE4u]==1u&&ram[0xE6u]==0x0Fu;
    const int mode=field?som_weapon_mask_mode(ram,who):-1;
    SomWorkEntry entry;
    const int want=mode>=0&&som_weapon_mask_entry((unsigned)mode,actor[0x1E3u],
                                                  actor[0x026u]>>1,&entry);
    uint8_t *tiles=ram+SOM_WEAPON_BUF;
    int blank=1;
    for(unsigned i=0;i<sizeof(weapon_native_tiles);i++)if(tiles[i]){blank=0;break;}
    if(want){
        if(!blank){
            memcpy(weapon_native_tiles,tiles,sizeof(weapon_native_tiles));
            memset(tiles,0,sizeof(weapon_native_tiles));
        }
        weapon_tiles_hidden=1;
    }else if(weapon_tiles_hidden){
        if(blank)memcpy(tiles,weapon_native_tiles,sizeof(weapon_native_tiles));
        weapon_tiles_hidden=0;
    }
}
/* The runtime image is composited using the mask-specific alpha-aware
 * renderer, keeping original hero animation and camera coordinates. */
static void som_weapon_mask_pick(const uint8_t *ram){
    weapon_masked.active=0;
    if(!ram||!weapon_tiles_hidden)return;
    const unsigned who=kusarigama_attack_active?kusarigama_attack_who:0u;
    const uint8_t *actor=ram+0xE000u+0x200u*who;
    const int mode=som_weapon_mask_mode(ram,who);
    SomWorkEntry entry;
    if(mode<0)return;
    if(!som_weapon_mask_entry((unsigned)mode,actor[0x1E3u],
                               actor[0x026u]>>1,&entry)){
        /* The SNES animation may advance after the pre-frame availability
         * check. Fail closed for the rest of the session instead of
         * continuing to blank frames with missing replacement art. */
        weapon_mask_faulted=1;
        fprintf(stderr,"[som-ai] BUSHIDO mask frame missing: row %u frame %u, disabling masks for this session\\n",
                (unsigned)actor[0x1E3u],(unsigned)(actor[0x026u]>>1));
        return;
    }
    memset(&weapon_masked,0,sizeof(weapon_masked));
    weapon_masked.active=1;weapon_masked.slot=(uint8_t)who;weapon_masked.kind=actor[0x180u];
    weapon_masked.dir=(actor[0x028u]&0x40u)?3u:2u;
    weapon_masked.entry=entry;weapon_masked.start=frame_number;weapon_masked.length=2u;
}

/* ---- Costs (FlXtR MP/AP): each technique is paid only when it goes off. ---- */
static int som_energy_pay(unsigned e,unsigned ap,const char *what){
    const unsigned cls=som_actor_class();
    if(energy_free||cls>=SOM_DISCIPLINE_COUNT)return 1;
    if(som_energy_can(som_actor_energy(),cls,e,ap))return 1;
    char text[48];
    if(som_actor_energy()->ap<ap)snprintf(text,sizeof(text),"^No AP|%s",what);
    else snprintf(text,sizeof(text),"^Not enough %s|%s needs %u",som_energy_name(cls),what,e);
    som_overlay_say(&overlay,text,SOM_MESSAGE_FRAMES/3u);
    if(frame_number-energy_refused_at>=60ul){
        energy_refused_at=frame_number;
        fprintf(stderr,"[som-ai] ENERGY refused %s: %s %u/%u, AP %u/%u\n",what,som_energy_name(cls),
                (unsigned)som_actor_energy()->energy[cls],e,(unsigned)som_actor_energy()->ap,ap);
    }
    return 0;
}
static void som_energy_spend(unsigned e,unsigned ap){
    if(!energy_free)som_energy_take(som_actor_energy(),som_actor_class(),e,ap);
}
static int som_evade_start(uint8_t *ram){
    if(!som_energy_pay(0u,1u,"evade"))return 0;
    const int ok=som_evade_start_raw(ram);
    if(ok)som_energy_spend(0u,1u);
    return ok;
}
static int som_blink_dash(uint8_t *ram,int dx,int dy){
    if(!som_energy_pay(10u,2u,"blink dash"))return 0;
    const int ok=som_blink_dash_raw(ram,dx,dy);
    if(ok)som_energy_spend(10u,2u);
    return ok;
}
static int som_blink_start(uint8_t *ram,int tx,int ty){
    if(!som_energy_pay(10u,1u,"blink"))return 0;
    const int ok=som_blink_start_raw(ram,tx,ty);
    if(ok)som_energy_spend(10u,1u);
    return ok;
}
static int som_kawarimi_arm(uint8_t *ram){
    if(!som_energy_pay(15u,1u,"kawarimi"))return 0;
    const int ok=som_kawarimi_arm_raw(ram);
    if(ok)som_energy_spend(15u,1u);
    return ok;
}
static int som_shunshin_start(uint8_t *ram){
    if(!som_energy_pay(15u,0u,"shunshin"))return 0;
    const int ok=som_shunshin_start_raw(ram);
    if(ok)som_energy_spend(15u,0u);
    return ok;
}
static int som_issen_start(uint8_t *ram){
    if(!som_energy_pay(20u,0u,"issen"))return 0;
    const int ok=som_issen_start_raw(ram);
    if(ok)som_energy_spend(20u,0u);
    return ok;
}
static int som_hundred_start(uint8_t *ram){
    if(!som_energy_pay(20u,0u,"hundred"))return 0;
    const int ok=som_hundred_start_raw(ram);
    if(ok)som_energy_spend(20u,0u);
    return ok;
}
static int som_class_move(uint8_t *ram){
    if(!som_energy_pay(15u,0u,"class move"))return 0;
    const int ok=som_class_move_raw(ram);
    if(ok)som_energy_spend(15u,0u);
    return ok;
}
static int som_telekinesis_cast(uint8_t *ram){
    if(!som_energy_pay(10u,0u,"telekinesis"))return 0;
    const int ok=som_telekinesis_cast_raw(ram);
    if(ok)som_energy_spend(10u,0u);
    return ok;
}
static int som_class_x(uint8_t *ram){
    if(familiar.slot>=0||familiar_carry.valid||mind_slot>=0)return som_class_x_raw(ram);  /* release: free */
    if(!som_energy_pay(15u,0u,"class x"))return 0;
    const int ok=som_class_x_raw(ram);
    if(ok)som_energy_spend(15u,0u);
    return ok;
}
static int som_flykick_start(uint8_t *ram){
    if(!som_energy_pay(8u,0u,"flykick"))return 0;
    const int ok=som_flykick_start_raw(ram);
    if(ok)som_energy_spend(8u,0u);
    return ok;
}
static int som_clone_jutsu(uint8_t *ram,const char *jutsu){
    if(som_actor){          /* the clones are drawn from the boy's art: his only */
        som_overlay_say(&overlay,"^Clones|the boy's jutsu",SOM_MESSAGE_FRAMES/3u);
        return 0;
    }
    if(!som_energy_pay(15u,0u,"clone jutsu"))return 0;
    const int ok=som_clone_jutsu_raw(ram,jutsu);
    if(ok)som_energy_spend(15u,0u);
    return ok;
}
static int som_throw_start(uint8_t *ram){
    if(!som_energy_pay(8u,0u,"throw"))return 0;
    const int ok=som_throw_start_raw(ram);
    if(ok)som_energy_spend(8u,0u);
    return ok;
}
static int som_bash_start(uint8_t *ram){
    if(!som_energy_pay(5u,0u,"bash"))return 0;
    const int ok=som_bash_start_raw(ram);
    if(ok)som_energy_spend(5u,0u);
    return ok;
}
static int som_iai_start(unsigned level){
    if(!som_energy_pay(10u,0u,"iai"))return 0;
    const int ok=som_iai_start_raw(level);
    if(ok)som_energy_spend(10u,0u);
    return ok;
}
static int som_portal_place(uint8_t *ram){
    if(!som_energy_pay(20u,0u,"portal"))return 0;
    const int ok=som_portal_place_raw(ram);
    if(ok)som_energy_spend(20u,0u);
    return ok;
}
/* Follow pad 1 (Select) once the current proxy move finishes. Native
 * casts and swings already belong to their own actor objects. */
static int som_actor_busy(void){
    return evade_active||flykick.active||blink_active||throw_active||special_stage||
        hundred_until||issen_active||ukemi_draw.active||bash_stage;
}
/* Player 2's evades and charge specials use the acting hero's code: pad 2
 * takes the role while nobody is mid-move, and som_actor_update hands it
 * back to pad 1's hero when the move is over. */
static int som_pad2_borrow(unsigned who){
    if(som_actor==who)return 1;
    if(som_actor_busy()||special_suppress)return 0;
    x_tap_armed=0;x_cast_at=0;som_actor=who;
    return 1;
}
static void som_actor_update(void){
    if(!backend_retro_get_memory_data||!backend_retro_get_memory_size||
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)!=SOM_WRAM_SIZE)return;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram)return;
    const unsigned want=som_pad1_member_in(ram);
    if(want==som_actor)return;
    if(som_actor_busy())return;
    special_suppress=0;                 /* a borrowed charge's held chord */
    fprintf(stderr,"[som-ai] ACTOR %s -> %s\n",som_party_names[som_actor%SOM_PARTY_MEMBERS],
            som_party_names[want]);
    x_tap_armed=0;x_cast_at=0;
    som_actor=want;
}
