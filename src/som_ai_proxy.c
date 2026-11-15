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
#include "som_gambit.h"
#include "som_disciplines.h"
#include "som_magic.h"
#include "som_familiar.h"
#include "som_npc_walk.h"
#include "som_net_pad.h"
#include "som_songs.h"
#include "som_combo.h"
#include "som_bunshin.h"
#include "som_bunshin_draw.h"
#include "som_xp.h"
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
static SomFamiliar familiar={-1,-1,0,0};
static uint16_t combo_hp[3];
static uint8_t combo_ids[3],combo_live[3];
static int combo_input_held;
static unsigned long combo_hold_start;
static int combo_hold_tried;
/* R2+B: native level-1 charged primary attack.
 * R2+Y: native level-2 charged secondary attack.
 * These are existing weapon animations, not new ROM routines. */
static unsigned special_stage,special_weapon;
static unsigned long special_started;
static uint16_t special_chord;
static int special_suppress;
#define SOM_SPECIAL_TIMEOUT 360ul
static int som_special_start(unsigned which,unsigned stage);
static void som_special_tick(void);
static void som_combo_clear(void){som_combo_reset(&combo);}
/* which: 0 = B (primary weapon), 1 = Y (secondary). */
static void som_combo_swing(unsigned which){
    som_combo_on_swing(&combo,which,frame_number);last_attack_frame=frame_number;
}

static const char *trigger_file;
static const char *action_file="/tmp/flxtr-som-ai-action";
static const char *wram_file="/tmp/flxtr-som-wram.bin";
static const char *wram_temp_file="/tmp/flxtr-som-wram.bin.tmp";
/* The bridge's per-decision ranges (som_qwen_bridge.py HOT_RANGES_KB, KiB
 * start/count), written ready-made while the AI plays so the Steam Link
 * serves them with one gzip instead of four dd processes. */
static const char *hot_file="/tmp/flxtr-som-hot.bin";
static const char *hot_temp_file="/tmp/flxtr-som-hot.bin.tmp";
static const unsigned som_hot_ranges[4][2]={{0,8},{39,2},{50,10},{96,4}};
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
 * once companions have joined): hold R2 or R3 ALONE for one second and
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
/* Independent from the six Gambit priority digits, shared with the
 * external song-id -> yt-dlp playback bridge. */
#define SOM_MUSIC_GENRE_FILE "/home/apps/retroarch/cores/experimental/som_music_genre"
static const char *music_genre_file=SOM_MUSIC_GENRE_FILE;
static void som_music_genre_load(void);
static void som_music_genre_save(void);
static SomGambits gambits={0,0,{4,3,3,3,4,1},0,SOM_PAGE_ACTIONS,
    {3,2,2,2,3,3,3,3},0};
static SomDisciplines disciplines;
/* The hero's sprite palette (its +0x8B attribute: palette 1 = 7E:DF20 for
 * the boy, live 2026-11-14) follows the chosen discipline's colour ramp.
 * The original colours are kept and re-read whenever the game rewrites
 * the palette (map change, events), so the tint never stacks. */
#define SOM_HERO_CLOTHES 0x7F00u     /* palette colours 8-14 */
static uint8_t hero_pal_orig[32],hero_pal_written[32];
static int hero_pal_have;
static unsigned hero_pal_addr;
/* The chosen discipline, levels and experience survive game restarts. */
#define SOM_DISC_FILE "/home/apps/retroarch/cores/experimental/som_ai_disciplines"
#define SOM_DISCIPLINES_V1 9u          /* disciplines before rogue/samurai/archer */
static const char *disc_file=SOM_DISC_FILE;
static void som_disc_save(void){
    char temp[256];snprintf(temp,sizeof(temp),"%s.tmp",disc_file);
    FILE *out=fopen(temp,"w");
    if(!out)return;
    int ok=fprintf(out,"v1 %u",(unsigned)disciplines.selected)>0;
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT&&ok;i++)
        ok=fprintf(out," %u %lu",(unsigned)disciplines.level[i],
                   (unsigned long)disciplines.experience[i])>0;
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
    fclose(in);
    if(!ok){fprintf(stderr,"[som-ai] DISCIPLINES file unreadable; starting fresh\n");return;}
    loaded.selected=(uint8_t)selected;
    disciplines=loaded;
}
static void som_hero_palette_frame(uint8_t *ram){
    if(!ram||ram[0xE000]!=1)return;
    const unsigned addr=0xDE00u+(128u+((ram[0xE08B]>>1)&7u)*16u)*2u;
    uint8_t *current=ram+addr;
    const unsigned profile=som_disc_visual_profile(&disciplines);
    if(hero_pal_have&&addr!=hero_pal_addr)hero_pal_have=0;
    if(profile==SOM_PAL_ORIGINAL){
        if(hero_pal_have&&!memcmp(current,hero_pal_written,32))memcpy(current,hero_pal_orig,32);
        hero_pal_have=0;
        /* No discipline: still no block/dash glow. The shadow keeps the
         * plain colours (the glow only touches the shown buffer). */
        if(ram[0xE4]==1&&ram[0xE6]==0x0F)
            memcpy(ram+0x0700u+((ram[0xE08B]>>1)&7u)*32u,current,32);
        return;
    }
    if(!hero_pal_have||memcmp(current,hero_pal_written,32)){
        memcpy(hero_pal_orig,current,32);       /* the game wrote fresh colours */
        hero_pal_have=1;hero_pal_addr=addr;
    }
    uint8_t tinted[32];
    memcpy(tinted,hero_pal_orig,32);
    som_pal_apply(tinted,profile);
    /* Clothes only: outline, hair and skin (1-7) and white (15) keep their
     * colours. Judged from the boy's live palette, not from sprite data. */
    uint8_t dressed[32];
    memcpy(dressed,hero_pal_orig,32);
    for(unsigned i=0;i<16u;i++)
        if(SOM_HERO_CLOTHES&(1u<<i)){dressed[2u*i]=tinted[2u*i];dressed[2u*i+1u]=tinted[2u*i+1u];}
    memcpy(current,dressed,32);
    /* The game works on (and sends to the screen) a second buffer,
     * 7E:0700 + palette*32 (live trace 2026-11-14: hero palette 1 at 7E:0720
     * equals 7E:DF20, and only it changes during dash/block flashes).
     * In field play it is held at the tint every frame: the dash/block glow
     * is the attack-palette routine $C1:CA04 (cidwel/som-decomp
     * actor-attack-palette.md), which reloads the 15 ROM colours (the
     * "old palette" coming back) and then flashes them. retro_run ends at
     * vblank, so this write is what the NMI sends to CGRAM: no glow, no
     * relapse. Outside field play (events, fades) only plain or tinted
     * colours are replaced, as before. */
    uint8_t *working=ram+0x0700u+((ram[0xE08B]>>1)&7u)*32u;
    const int field_play=ram[0xE4]==1&&ram[0xE6]==0x0F;
    if(field_play||!memcmp(working,hero_pal_orig,32)||!memcmp(working,hero_pal_written,32))
        memcpy(working,dressed,32);
    memcpy(hero_pal_written,dressed,32);
}
static int som_menu_disciplines;
static int som_manual_disciplines;
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
/* Teaching: while the marker exists, physical presses are appended to a log
 * as the action names the bridge can replay. Nothing is injected. */
static const char *record_marker="/tmp/flxtr-som-ai-record";
static const char *record_file="/tmp/flxtr-som-ai-recorded";
static int recording;
static uint8_t *overlay_frame;
static size_t overlay_frame_size;
static void load_backend(void); /* RetroArch may query core info before init. */
static void som_auto_state_path_set(const char *rom);

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
FORWARD_VOID(retro_set_audio_sample_batch,(retro_audio_sample_batch_t cb),(cb))
FORWARD_VOID(retro_set_controller_port_device,(unsigned port,unsigned device),(port,device))
FORWARD_VOID(retro_reset,(void),())
FORWARD(size_t,retro_serialize_size,(void),())
FORWARD(bool,retro_serialize,(void *data,size_t size),(data,size))
FORWARD(bool,retro_unserialize,(const void *data,size_t size),(data,size))
FORWARD_VOID(retro_cheat_reset,(void),())
FORWARD_VOID(retro_cheat_set,(unsigned index,bool enabled,const char *code),(index,enabled,code))
FORWARD(bool,retro_load_game_special,(unsigned type,const struct retro_game_info *info,size_t count),(type,info,count))
FORWARD(unsigned,retro_get_region,(void),())
FORWARD(void *,retro_get_memory_data,(unsigned id),(id))
FORWARD(size_t,retro_get_memory_size,(unsigned id),(id))
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
    som_overlay_set(&overlay,gambits.infinite_mp?"^Infinite MP ON|test only":
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
    {4,6},   /* Magic: whip, boomerang */
    {3,4},   /* Divine Arts: spear, whip */
    {4,7},   /* Dark Arts: whip, javelin */
    {6,4},   /* Summoning: boomerang, whip */
    {4,0},   /* Support & Dance: whip, glove */
    {1,3},   /* Weapon Flow: sword, spear */
    {1,5},   /* Roguecraft: sword, bow */
    {1,5},   /* Bushido: katana (sword), yumi (bow) */
    {5,7},   /* Archery: bow, javelin */
};
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
    if(!amount||!som_disc_award(&disciplines,discipline,amount,
                                som_xp_event(frame_number,kind),1))return;
    xp_dirty=1;
    if(disciplines.level[discipline]!=before){
        char text[64];
        snprintf(text,sizeof(text),"^%s|level %u",som_discipline_names[discipline],
                 (unsigned)disciplines.level[discipline]);
        som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES);
        som_disc_save();xp_dirty=0;xp_saved_frame=frame_number;
    }
    fprintf(stderr,"[som-ai] XP %s +%u (%s) -> Lv%u %lu/%u\n",som_discipline_names[discipline],
            amount,why,(unsigned)disciplines.level[discipline],
            (unsigned long)disciplines.experience[discipline],
            som_disc_next_xp(disciplines.level[discipline]));
}
static void som_xp_flush(int now){
    if(xp_dirty&&(now||frame_number-xp_saved_frame>=3600ul)){
        som_disc_save();xp_dirty=0;xp_saved_frame=frame_number;
    }
}
/* A spell cast at a monster waits for that monster's HP to drop. */
static int xp_spell_slot=-1;
static uint16_t xp_spell_hp;
static uint8_t xp_spell_species;
static unsigned long xp_spell_frame;
static int som_equip_weapon(uint8_t *ram,unsigned type);
static int som_equip_weapon_direct(uint8_t *ram,unsigned type);
#define SOM_EQUIP_OK 1
#define SOM_EQUIP_BUSY 0                /* hero mid-action: retry later */
#define SOM_EQUIP_REFUSED (-1)          /* will not work: use the ring */
static int som_cast_spell(uint8_t *ram,unsigned spell);
#define SOM_SPELL_FIRE_BALL 0x0Cu
/* X casts the current spell of the chosen discipline (som_magic.h); holding
 * X steps to the next one. One cursor per discipline (+1: no discipline). */
static unsigned spell_cursor[SOM_DISCIPLINE_COUNT+1u];
static unsigned long x_press_frame;
static int x_cycled;
#define SOM_X_HOLD_FRAMES 30ul
static unsigned som_current_spell(void){
    const unsigned d=disciplines.selected<SOM_DISCIPLINE_COUNT?disciplines.selected:SOM_DISCIPLINE_COUNT;
    return som_class_spell(disciplines.selected,spell_cursor[d]);
}
static void som_next_spell(void){
    const unsigned d=disciplines.selected<SOM_DISCIPLINE_COUNT?disciplines.selected:SOM_DISCIPLINE_COUNT;
    spell_cursor[d]=(spell_cursor[d]+1u)%som_class_spell_count(disciplines.selected);
}
/* Seals: D-pad presses counted while R2 is held (som_magic.h). */
static unsigned seal_count;
static int seal_mode;
static int special_from_ai;           /* special_<n> command while the AI plays */
/* R1+A evade: the hero plays the game's own back-roll pose 0x8D (rolls
 * 55 px away from the facing, 65 frames; som-decomp hero-poses.md 1-3:
 * pose = +0x1C 0x40, +0x11 code, +0x42 busy, as ACTOR_ANIM does) and is
 * untouchable meanwhile: status 0x0400 (+0x191 bit 2), "no hit is
 * resolved on the actor" ($C0:4F7B, rom-combat.md 12). Only the
 * disciplines with a dodge (som_disc_has_dodge). */
static int evade_active,evade_a_hidden,evade_xp_given;
static unsigned evade_hp;
static unsigned evade_frames;
static unsigned long evade_started;
#define SOM_TRAIL_LENGTH 16u
static int trail_x[SOM_TRAIL_LENGTH],trail_y[SOM_TRAIL_LENGTH];
static unsigned long trail_frame[SOM_TRAIL_LENGTH],trail_until;
static int som_evade_start(uint8_t *ram);
/* Kage Bunshin (src/som_bunshin.h): clones the proxy keeps and draws over
 * the frame (src/som_bunshin_draw.h); touch makes them explode, the hero
 * can swap into them. */
static SomBunshin bunshin;
static int evade_dir_x,evade_dir_y;    /* D-pad aim: clone swap or hop */
static int hop_dx,hop_dy;              /* hop dodge step per frame */
static unsigned hop_frames_left;
static void som_hop_step(void);
static void som_evade_shield(uint8_t *ram);
static SomBoom bunshin_booms[SOM_BUN_BOOMS];
static void som_bunshin_frame(uint8_t *ram);
static int som_bunshin_swap(uint8_t *ram,int flee,int fx,int fy,int dir_x,int dir_y);
/* R1+A bash (swordsman): the game's own run for up to SOM_BASH_LUNGE_FRAMES
 * (A held: 3 px per frame, stopped by walls, hero-movement.md 4.1), then one
 * frame with nothing pressed (A released), then the game's own sword swing
 * (B) at a full gauge. Only the pad the game sees is changed; R1 (block),
 * the D-pad, Y and X are hidden while it plays. */
#define SOM_BASH_LUNGE_FRAMES 14ul      /* ~42 px */
#define SOM_BASH_STRIKE_FRAMES 3ul
#define SOM_BASH_REACH 22               /* px: stop the lunge when this close */
enum {SOM_BASH_NONE,SOM_BASH_LUNGE,SOM_BASH_RELEASE,SOM_BASH_STRIKE};
static int bash_stage;
static unsigned long bash_started;
static int som_bash_start(uint8_t *ram);
static void som_bash_tick(void);
static void som_class_supply_frame(void);
static int class_supply_pending, class_equip_pending;
static unsigned long class_last_equip_attempt;
static void som_class_selected(void){
    class_supply_pending=disciplines.selected<SOM_DISCIPLINE_COUNT;
    class_equip_pending=class_supply_pending;
    class_last_equip_attempt=0;
}
/* Set each poll: the player is in field play with a discipline and Y (not
 * L2+Y) held. */
static int y_weapon_swap, y_weapon_blocked, b_weapon_blocked, x_spell_blocked;
static int som_cycle_weapon(uint8_t *ram);
static int som_y_field_play(void){
    if(!backend_retro_get_memory_data||ring_ai_enabled||som_manual_disciplines||
       disciplines.selected>=SOM_DISCIPLINE_COUNT)return 0;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    return ram&&ram[0xE4]==1&&ram[0xE6]==0x0F&&ram[0xD0]==0&&ram[0x7D]!=1;
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
            som_xp_give(som_xp_weapon_discipline(weapon,sel,
                            sel<SOM_DISCIPLINE_COUNT?som_disc_weapons[sel]:NULL),
                        SOM_XP_HIT,e[0x180],SOM_XPK_HIT,"weapon hit");
            if(combo.mixes!=mixes_before)
                som_xp_give(SOM_DISC_FLOW,SOM_XP_FLOW,e[0x180],SOM_XPK_FLOW,"weapon switch hit");
            if(som_combo_level(&combo))
                fprintf(stderr,"[som-ai] COMBO %u hits, %u mixes: level %u ready\n",
                        combo.hits,combo.mixes,som_combo_level(&combo));
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
        som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] NETPAD player 2 joined from %s\n",inet_ntoa(a));
    }else if(net_pad_was_active&&!active){
        som_overlay_set(&overlay,"Player 2 left",SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] NETPAD player 2 left\n");
    }
    net_pad_was_active=active;
}
static int16_t som_input_state(unsigned port,unsigned device,unsigned index,unsigned id){
    int16_t actual=physical_state?physical_state(port,device,index,id):0;
    int bitmask=id==RETRO_DEVICE_ID_JOYPAD_MASK;
    if(port==1&&index==0&&(device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&
       net_pad.fd>=0&&som_net_pad_active(&net_pad,som_now_ms())){
        const uint16_t remote=som_net_pad_buttons(&net_pad,som_now_ms());
        if(bitmask)return (int16_t)remote;
        return id<16u&&(remote&(1u<<id))?1:0;
    }
    if((ring_ai_enabled||som_manual_disciplines)&&port==0&&
       (device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0)
        actual=0;                    /* the player's buttons drive the gambit menu */
    /* Bash: the game sees only A (the run), then nothing, then B (swing). */
    if(bash_stage&&port==0&&(device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0){
        const uint16_t want=(uint16_t)(bash_stage==SOM_BASH_LUNGE?1u<<RETRO_DEVICE_ID_JOYPAD_A:
                                       bash_stage==SOM_BASH_STRIKE?1u<<RETRO_DEVICE_ID_JOYPAD_B:0u);
        if(bitmask)return (int16_t)want;
        return id<16u&&(want&(1u<<id))?1:0;
    }
    /* With a discipline, Y attacks with its second weapon: the game sees B
     * (so holding Y charges) and never Y; L2+Y still opens the ring. */
    if((special_stage||special_suppress)&&port==0&&
       (device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0){
        if(special_stage){
            if(bitmask)return (int16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);
            return id==RETRO_DEVICE_ID_JOYPAD_B?1:0;
        }
        return 0; /* consume held combo after automatic release */
    }
    if((evade_a_hidden||evade_active)&&port==0&&
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
    if(seal_mode&&port==0&&(device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0){
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
    if(y_weapon_swap&&port==0&&(device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0){
        if(bitmask)actual=(int16_t)((actual&~(1<<RETRO_DEVICE_ID_JOYPAD_Y))|
                                     (1<<RETRO_DEVICE_ID_JOYPAD_B));
        else if(id==RETRO_DEVICE_ID_JOYPAD_Y)actual=0;
        else if(id==RETRO_DEVICE_ID_JOYPAD_B)actual=1;
    }
    if(port==0 && (device & RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD &&
       index==0 && (id<16||bitmask) && armed && !cancelled && !completed && on_turbo){
        if(actual){cancelled=1;armed=0;autonomous_mode=0;fprintf(stderr,"[som-ai] cancelled: physical input at frame %lu\n",frame_number);return actual;}
        uint16_t wanted=som_action_mask(active_action);
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
    som_net_pad_frame();
    /* Observe physical attack edges without mutating the game's inputs. */
    if(physical_state && on_turbo){
        uint16_t current=0;
        for(unsigned key=0;key<16;key++)
            if(physical_state(0,RETRO_DEVICE_JOYPAD,0,key))current|=(uint16_t)(1u<<key);
        const uint16_t attack=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);
        const uint16_t secondary=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);
        const uint16_t l2=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L2);
        const uint16_t r2=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_R2);
        if(special_stage&&current&&(current&~special_chord)){
            fprintf(stderr,"[som-ai] SPECIAL cancelled by manual input\\n");
            special_stage=0;special_suppress=0;
        }
        if(special_suppress&&!(current&special_chord))special_suppress=0;
        combo_input_held=(current&(attack|secondary))!=0;
        if(((current&attack)&&!(last_physical_buttons&attack))||
           ((current&secondary)&&!(last_physical_buttons&secondary)&&!(current&l2))){
            /* Modifier specials have their own charge, not basic hits. */
            if(!(current&r2)&&!som_manual_disciplines&&!ring_ai_enabled&&!bash_stage)
                som_combo_swing((current&secondary)&&!(last_physical_buttons&secondary)?1u:0u);
            combo_hold_start=frame_number;combo_hold_tried=0;
        }
        const uint16_t pressed=(uint16_t)(current&~last_physical_buttons);
        const uint16_t y_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_Y);
        const uint16_t l2_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L2);
        const uint16_t b_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_B);
        const int field=som_y_field_play();
        /* Seals: with R2 held, each D-pad press is one seal (hidden from
         * the game); B or Y then fires charge level = seals. */
        const uint16_t dpad=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_UP)|
            (1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)|(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT)|
            (1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));
        if(field&&(current&r2)&&!(current&l2_bit)&&!ring_ai_enabled&&
           !som_manual_disciplines&&!special_stage&&
           disciplines.selected<SOM_DISCIPLINE_COUNT){
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
                som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES);
            }
        }else{seal_mode=0;seal_count=0;}
        /* R2+attack is handled before ordinary B/Y weapon switching. */
        if(field&&!special_stage&&!special_suppress&&!ring_ai_enabled&&
           (current&r2)&&(pressed&(y_bit|b_bit))&&!(current&l2_bit)){
            unsigned which=(pressed&y_bit)?1u:0u;
            unsigned stage=som_seal_level(seal_count,which);
            seal_count=0;
            if(som_special_start(which,stage)){
                special_chord=(uint16_t)(r2|(which?y_bit:b_bit));
                special_suppress=1;
            }
        }
        y_weapon_swap=0;y_weapon_blocked=0;b_weapon_blocked=0;x_spell_blocked=0;
        uint8_t *wram=field?(uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL;
        if(wram&&(current&(y_bit|b_bit))&&!(current&(l2_bit|r2))&&!special_suppress){
            const unsigned which=(current&y_bit)?1u:0u; /* Y: secondary, B: primary */
            const unsigned want=som_disc_weapons[disciplines.selected][which];
            /* Switch on the press, or as soon as the hero is free while the
             * button stays held; the game then sees a fresh press. */
            int result=wram[0xE1E4]==want?SOM_EQUIP_OK:
                (pressed&(y_bit|b_bit))?som_equip_weapon(wram,want):
                som_equip_weapon_direct(wram,want);
            if(result==SOM_EQUIP_OK)y_weapon_swap=which;
            else if(which)y_weapon_blocked=1;  /* never the wrong weapon on Y */
            else if(result==SOM_EQUIP_BUSY)b_weapon_blocked=1;
            /* B with a weapon that cannot be switched directly keeps the
             * held weapon rather than leaving the hero unable to attack. */
        }
        /* X/Triangle casts the selected spell; L2+X keeps the game's X. */
        const uint16_t x_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);
        /* A: evade / jutsu for the disciplines with a dodge; the game's A
         * dash is gone for them until dash is re-added. Other classes keep
         * the native A. Field play only (menus keep A as cancel). */
        const uint16_t a_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_A);
        evade_a_hidden=wram&&!ring_ai_enabled&&!som_manual_disciplines&&
            disciplines.selected<SOM_DISCIPLINE_COUNT&&som_disc_has_dodge(disciplines.selected);
        if(evade_a_hidden&&(pressed&a_bit)){
            const uint16_t r1_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_R);
            if(disciplines.selected==0u&&(current&r1_bit))som_bash_start(wram);
            else{
                evade_dir_x=(current&(1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT))?1:
                            (current&(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT))?-1:0;
                evade_dir_y=(current&(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN))?1:
                            (current&(1u<<RETRO_DEVICE_ID_JOYPAD_UP))?-1:0;
                som_evade_start(wram);
                evade_dir_x=evade_dir_y=0;
            }
        }
        /* Combo finisher: hold B or Y with a chain level ready. That button's
         * weapon charges straight to the level (capped at what it learned)
         * once the gauge is back, then fires as the game's own power attack. */
        if(wram&&combo_input_held&&!(current&(l2_bit|r2))&&!special_stage&&!special_suppress&&
           !ring_ai_enabled&&!som_manual_disciplines&&!bash_stage&&!combo_hold_tried&&
           disciplines.selected<SOM_DISCIPLINE_COUNT&&som_combo_level(&combo)&&
           frame_number-combo_hold_start>=SOM_COMBO_HOLD_FRAMES&&
           !wram[0xE1ED]&&!wram[0xE19B]&&!wram[0xE01C]){
            const unsigned which=(current&y_bit)?1u:0u;
            const unsigned level=som_combo_level(&combo),hits=combo.hits;
            combo_hold_tried=1;
            if(som_special_start(which,level)){
                special_chord=which?y_bit:b_bit;special_suppress=1;
                char text[64];
                snprintf(text,sizeof(text),"^%u-hit combo|level %u finisher",hits,level);
                som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES/2u);
                fprintf(stderr,"[som-ai] COMBO finisher: %u hits, level %u, %s\n",
                        hits,level,which?"Y":"B");
            }
        }
        /* Tap X: cast on release. Hold X: next spell every 0.5 s. */
        if(wram&&(current&x_bit)&&!(current&(l2_bit|r2))&&!special_suppress){
            x_spell_blocked=1;
            if(pressed&x_bit){x_press_frame=frame_number;x_cycled=0;}
            else if(frame_number-x_press_frame>=SOM_X_HOLD_FRAMES){
                som_next_spell();x_cycled=1;x_press_frame=frame_number;
                char text[64];
                snprintf(text,sizeof(text),"^%s|tap X: cast",som_spell_label(som_current_spell()));
                som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES);
            }
        }else if(wram&&(last_physical_buttons&x_bit)&&!(current&x_bit)&&
                 !(current&(l2_bit|r2))&&!special_suppress){
            if(!x_cycled)som_cast_spell(wram,som_current_spell());
            x_cycled=0;
        }
        /* Start opens Class Progress manually; retain L2+R2 as an alias.
         * While open, all pad input is consumed by the overlay. */
        const uint16_t class_chord=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|
                                              (1u<<RETRO_DEVICE_ID_JOYPAD_R2));
        const uint16_t class_start=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_START);
        if(!ring_ai_enabled && !som_manual_disciplines &&
           ((pressed&class_start)||
            ((current&class_chord)==class_chord&&(pressed&class_chord)))){
            som_manual_disciplines=1;
            disciplines.open=1;
            disciplines.cursor=0;
            switch_spoiled=1;
            som_overlay_set(&overlay,"DISCIPLINES|A:close B:select",SOM_MESSAGE_FRAMES);
            fprintf(stderr,"[som-ai] MANUAL DISCIPLINES opened\n");
        }else if(som_manual_disciplines && pressed){
            int result=som_disc_press(&disciplines,pressed);
            if(result==SOM_DISC_CLOSED){
                som_manual_disciplines=0;
                fprintf(stderr,"[som-ai] MANUAL DISCIPLINES closed\n");
            }else if(result==SOM_DISC_SELECTED){
                som_disc_save();
                som_class_selected();
                fprintf(stderr,"[som-ai] DISCIPLINE selected=%u (saved)\n",
                        (unsigned)disciplines.selected);
            }
        }
        if(recording)som_record_presses(pressed);
        last_physical_buttons=current;
        if(current){last_input_frame=frame_number;idle_started=0;}
        const uint16_t ai_switch=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_R2)|
                                            (1u<<RETRO_DEVICE_ID_JOYPAD_R3));
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
                    som_overlay_set(&overlay,"AI ON|any button: gambits",SOM_MESSAGE_FRAMES);
                    fprintf(stderr,"[som-ai] SWITCH-HOLD AI START signal ready at %s\n",
                        ring_ai_file);
                }else fprintf(stderr,"[som-ai] SWITCH-HOLD AI start signal failed\n");
            }
        }
        /* The bridge removes the marker when it stops (safety stop, game
         * exit): never keep swallowing the player's buttons after that. */
        if(!direct_transport&&ring_ai_enabled&&frame_number%30u==0&&access(ring_ai_file,F_OK)!=0){
            ring_ai_enabled=0;gambits.open=0;disciplines.open=0;som_menu_disciplines=0;
            som_overlay_set(&overlay,"AI stopped|you play",SOM_MESSAGE_FRAMES);
            fprintf(stderr,"[som-ai] AI marker gone: controls back to the player\n");
        }
        if(ring_ai_enabled&&pressed&&frame_number>=ring_ai_grace_until){
            /* Shoulder tabs switch the overlay page, never reach the game. */
            const uint16_t tabs=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L)|
                                           (1u<<RETRO_DEVICE_ID_JOYPAD_R));
            if(pressed&tabs){
                /* L1/R1 cycle Actions -> Combat -> Disciplines. */
                const int forward=(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_R))!=0;
                if(som_menu_disciplines){
                    som_menu_disciplines=0;
                    gambits.page=forward?SOM_PAGE_ACTIONS:SOM_PAGE_COMBAT;gambits.cursor=0;
                }else if((forward&&gambits.page==SOM_PAGE_COMBAT)||
                         (!forward&&gambits.page==SOM_PAGE_ACTIONS)){
                    som_menu_disciplines=1;
                }else som_gambit_turn_page(&gambits,forward);
                gambits.open=!som_menu_disciplines;
                disciplines.open=som_menu_disciplines;
            }else if(som_menu_disciplines){
                int result=som_disc_press(&disciplines,pressed);
                if(result==SOM_DISC_SELECTED){
                    som_disc_save();
                    som_class_selected();
                    fprintf(stderr,"[som-ai] DISCIPLINE selected=%u (saved)\n",
                        (unsigned)disciplines.selected);
                }
            }else switch(som_gambit_press(&gambits,pressed)){
            case SOM_GAMBIT_GENRE_CHANGED:
                som_music_genre_save();
                fprintf(stderr,"[som-ai] MUSIC genre %s (%u)\n",
                        som_music_genre_names[gambits.music_genre],gambits.music_genre);
                break;
            case SOM_GAMBIT_CHANGED:
                som_gambit_save();
                {char text[SOM_GAMBIT_COUNT+1],combat[SOM_COMBAT_COUNT+1];
                 som_gambit_format(&gambits,text);som_combat_format(&gambits,combat);
                 fprintf(stderr,"[som-ai] GAMBITS %s COMBAT %s\n",text,combat);}
                break;
            case SOM_GAMBIT_TEST_SWITCH:
                som_infinite_mp_changed();
                break;
            case SOM_GAMBIT_EXIT:
                ring_ai_enabled=0;unlink(ring_ai_file);
                som_overlay_set(&overlay,"You play|hold R2 or R3 1s: AI plays",
                                SOM_MESSAGE_FRAMES);
                fprintf(stderr,"[som-ai] GAMBIT EXIT: controls back to the player\n");
                break;
            default:break;
            }
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
                cancelled=1;armed=0;autonomous_mode=0;
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
    if(ok&&ring_ai_enabled){
        int hot_fd=open(hot_temp_file,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
        FILE *hot=hot_fd>=0?fdopen(hot_fd,"wb"):NULL;
        int hot_ok=hot!=NULL;
        if(!hot&&hot_fd>=0)close(hot_fd);
        for(unsigned i=0;i<4u&&hot_ok;i++)
            hot_ok=fwrite(ram+som_hot_ranges[i][0]*1024u,1,som_hot_ranges[i][1]*1024u,hot)==
                   som_hot_ranges[i][1]*1024u;
        if(hot&&fclose(hot)!=0)hot_ok=0;
        if(hot_ok)hot_ok=rename(hot_temp_file,hot_file)==0;
        if(!hot_ok)unlink(hot_temp_file);
    }else if(ok)unlink(hot_file);   /* AI off: never serve stale ranges */
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
static const uint8_t som_slot_fields[]={0x6C,0x6E,0x86,0x8A,0x92,0x96,0x9A,0x9E,0xA2,0xA6,
    0xAA,0xAE,0xB2,0xB6,0xD2,0xD6,0xDA,0xF8,0xFA};   /* 16-bit fields */
#define SOM_CLONE_WAIT_FRAMES 600ul           /* slots refill ~3 s after a kill */
static unsigned long clone_pending_until;
static int clone_no_slot,last_clone_target=-1;
/* Familiar (src/som_familiar.h): a clone bound to fight for the hero. Its
 * monster slot (1..3) is published for the SkyLoft bridge, which must not
 * attack it. */
#define SOM_FAMILIAR_FILE "/tmp/flxtr-som-familiar"
static const char *familiar_file=SOM_FAMILIAR_FILE;
static int familiar_pending;
/* Carried between maps: saved every second while it is with the hero and
 * restored into an empty slot once the new map has settled. */
static SomFamiliarCarry familiar_carry;
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
/* Turbo's masked plane converter for hero 0 (the boy). */
static int som_expand_effect_tiles(uint32_t source,unsigned tiles,uint8_t *out){
    const uint8_t *src=som_rom_at(source,(size_t)tiles*24u);
    if(!src||((source&0xFFFFu)+tiles*24u)>0x10000u)return 0;
    for(unsigned t=0;t<tiles;t++){
        const uint8_t *s=src+t*24u;
        uint8_t *o=out+t*32u;
        for(unsigned y=0;y<8u;y++){
            const uint8_t a=s[2u*y],b=s[2u*y+1u],c=s[16u+y];
            const uint8_t keep=(uint8_t)~(b&c);
            o[2u*y]=(uint8_t)(a&keep);o[2u*y+1u]=(uint8_t)(b&keep);
            o[16u+2u*y]=(uint8_t)(c&keep);o[17u+2u*y]=0;
        }
    }
    return 1;
}
typedef struct {
    const uint8_t *row;                 /* 12-byte weapon row, D0:1000+12*row */
    uint8_t type,graphics;              /* +0x068, D030 (row byte 3) */
    uint16_t f065,f06a,f07e,f02a;
    uint8_t tiles[SOM_HERO_EFFECT_BYTES];
} SomWeaponModel;
/* $C0:EA41 for the boy (actor offset 0). */
static int som_weapon_model(const uint8_t *hero,unsigned row,SomWeaponModel *m){
    if(!hero||!m||row>=72u)return 0;
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
    m->f07e=(uint16_t)(0x1040u+(g<8u?g<<8:0u));
    m->f02a=(uint16_t)(frames[0]|frames[1]<<8);
    const uint16_t source=(uint16_t)(24u*(unsigned)(table[0]|table[1]<<8));
    return som_expand_effect_tiles(0xD40000u+source,SOM_WEAPON_TILES,m->tiles);
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
        memcmp(ram+SOM_WEAPON_BUF,m.tiles,sizeof(m.tiles))==0;
}
static int som_field_idle(const uint8_t *ram){
    return ram&&ram[0xE000]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F&&ram[0xD0]==0&&
        ram[0x7D]!=1&&!(ram[0xF1]&0x80)&&ram[0xE01C]==0&&ram[0xE060]==0&&
        ram[0xE19B]==0;
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
/* A row with Str/Agi/Con/Int/Wis bonuses needs the full stat rebuild
 * ($C0:4530); refuse those rather than leave stats half-updated. */
static int som_weapon_row_plain(const uint8_t *r){return r[1]==0&&(r[2]&3u)==0;}
static int som_equip_weapon_direct(uint8_t *ram,unsigned type){
    if(!ram||type>=8u)return SOM_EQUIP_REFUSED;
    uint8_t *hero=ram+0xE000u;
    if(hero[0x1E4]==type)return SOM_EQUIP_OK;
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
    if(owned[current]!=0xFF)owned[current]&=(uint8_t)~0x40u;  /* no longer held */
    owned[type]|=0x40u;                                        /* held by the boy */
    fprintf(stderr,"[som-ai] WEAPON equipped without the ring: %s (row %u)\n",
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
        som_overlay_set(&overlay,message,SOM_MESSAGE_FRAMES);
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
static const char *som_spell_name(unsigned spell){return som_spell_label(spell);}
/* 0 Gnome .. 5 Dryad: six spells each; 6 Shade, 7 Lumina: three each. */
static unsigned som_spell_element(unsigned spell){
    return spell<0x24u?spell/6u:spell<0x27u?6u:7u;
}
/* Hostile hero spells target the monster slots 3-5 that are present, on
 * screen and alive ($D0:DA60). Take the nearest one. */
static int som_cast_target(const uint8_t *ram){
    const uint8_t *hero=ram+0xE000u;
    const int hx=(int)som_word(hero+0x20),hy=(int)som_word(hero+0x22);
    int best=-1,best_distance=0;
    for(unsigned slot=3;slot<6u;slot++){
        const uint8_t *e=ram+0xE000u+0x200u*slot;
        const int x=(int)som_word(e+0x20),y=(int)som_word(e+0x22);
        if(e[0]!=1||(e[0x191]&0x80u)||x<8||x>=0xF8||y>=0xD8)continue;
        const int distance=abs(x-hx)+abs(y-hy);
        if(best<0||distance<best_distance){best=(int)slot;best_distance=distance;}
    }
    return best;
}
static int som_cast_spell(uint8_t *ram,unsigned spell){
    if(!ram||spell>=SOM_SPELL_COUNT||!som_rom)return 0;
    if(!som_field_idle(ram))return 0;                 /* busy: no cast */
    uint8_t *hero=ram+0xE000u;
    const uint8_t *row=som_rom_at(0xD02AD0u+64u*spell,16u);
    /* 0x81 hostile: monster slots; 0x01 ally and 0x00 self: the boy
     * himself (hero slot 0, target 1, as the ring's single-ally choice). */
    if(!row||(row[8]!=0x81u&&row[8]!=0x01u&&row[8]!=0x00u)){
        fprintf(stderr,"[som-ai] CAST refused: spell %#04x has unsupported targets\n",spell);
        return 0;
    }
    const int hostile=row[8]==0x81u;
    char message[64];
    const unsigned cost=row[15];
    if(hero[0x186]<cost){
        snprintf(message,sizeof(message),"^%s: not enough MP|%u/%u",
                 som_spell_name(spell),(unsigned)hero[0x186],cost);
        som_overlay_set(&overlay,message,SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] CAST refused: MP %u < %u\n",(unsigned)hero[0x186],cost);
        return 0;
    }
    const int target=hostile?som_cast_target(ram):0;
    if(target<0){
        snprintf(message,sizeof(message),"^%s: no target",som_spell_name(spell));
        som_overlay_set(&overlay,message,SOM_MESSAGE_FRAMES);
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
    /* $C1:88C7 for the boy */
    hero[0x111]=(uint8_t)level;hero[0x108]=0;
    hero[0x04F]=0xCE;hero[0x01C]=0x40;hero[0x030]=0xFF;hero[0x011]=0x94;
    hero[0x006]=hero[0x007]=hero[0x008]=0;
    hero[0x060]=0x20;
    memcpy(ram+SOM_WEAPON_BUF+0x800u,spark,sizeof(spark));   /* $C1:8634 */
    if(hostile&&target>=0){
        const uint8_t *m=ram+0xE600u+0x200u*(unsigned)target;
        xp_spell_slot=target;xp_spell_hp=(uint16_t)(m[0x182]|m[0x183]<<8);
        xp_spell_species=m[0x180];xp_spell_frame=frame_number;
    }
    fprintf(stderr,"[som-ai] CAST %s Lv.%u at %s slot %d (MP %u, cost %u)\n",
            som_spell_name(spell),level,hostile?"monster":"own",target,
            (unsigned)hero[0x186],cost);
    return 1;
}
/* Kage Bunshin clones (src/som_bunshin.h). Explosion damage grows with
 * the Ninjutsu discipline level. */
static unsigned som_bunshin_damage(void){return 10u+disciplines.level[2]/2u;}
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
    uint8_t *hero=ram+0xE000u;
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    const int i=som_bun_pick(&bunshin,hx,hy,flee,fx,fy,dir_x,dir_y);
    if(i<0)return 0;
    const int nx=bunshin.x[i],ny=bunshin.y[i];
    hero[2]=(uint8_t)nx;hero[3]=(uint8_t)(nx>>8);
    hero[4]=(uint8_t)ny;hero[5]=(uint8_t)(ny>>8);
    hero[0x006]=hero[0x007]=0;
    bunshin.hero_x=nx;bunshin.hero_y=ny;
    bunshin.swap_ready=frame_number+SOM_BUN_SWAP_COOLDOWN;
    som_bun_boom(bunshin_booms,nx,ny,1,frame_number);
    if(flee){
        bunshin.alive[i]=0;
        som_bunshin_explode(ram,hx,hy);
        som_overlay_set(&overlay,"^Kawarimi no Jutsu!",SOM_MESSAGE_FRAMES/2u);
    }else{
        bunshin.x[i]=hx;bunshin.y[i]=hy;
        som_bun_boom(bunshin_booms,hx,hy,1,frame_number);
    }
    fprintf(stderr,"[som-ai] BUNSHIN %s: hero %d,%d -> clone %d at %d,%d\n",
            flee?"substitution":"swap",hx,hy,i,nx,ny);
    return 1;
}
static void som_bunshin_frame(uint8_t *ram){
    if(!ram||!som_bun_alive(&bunshin))return;
    uint8_t *hero=ram+0xE000u;
    const int field=hero[0]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F&&ram[0xD0]==0;
    const int same_map=som_bun_u16(ram+0xDC)==bunshin.map;
    if(!field||!same_map||frame_number>bunshin.until){
        if(same_map)
            for(unsigned i=0;i<SOM_BUN_COUNT;i++)
                if(bunshin.alive[i])som_bun_boom(bunshin_booms,bunshin.x[i],bunshin.y[i],1,frame_number);
        som_bun_clear(&bunshin);return;
    }
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    som_bun_follow(&bunshin,ram,hx,hy,som_hero_place(hero));
    for(unsigned i=0;i<SOM_BUN_COUNT;i++){
        if(!bunshin.alive[i]||som_bun_toucher(ram,bunshin.x[i],bunshin.y[i],SOM_BUN_TOUCH_X,
                                              SOM_BUN_TOUCH_Y,familiar.slot)<0)continue;
        bunshin.alive[i]=0;
        som_bunshin_explode(ram,bunshin.x[i],bunshin.y[i]);
        som_overlay_set(&overlay,"^Bunshin Daibakuha!",SOM_MESSAGE_FRAMES/2u);
    }
    if(!som_bun_alive(&bunshin)||frame_number<bunshin.swap_ready)return;
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
    const int hx=(int)som_bun_u16(ram+0xE002),hy=(int)som_bun_u16(ram+0xE004);
    const uint8_t *vram=backend_retro_get_memory_size(RETRO_MEMORY_VIDEO_RAM)>=SOM_BUN_VRAM_SIZE?
        backend_retro_get_memory_data(RETRO_MEMORY_VIDEO_RAM):NULL;
    if(vram)
        for(unsigned i=0;i<SOM_BUN_COUNT;i++)
            if(bunshin.alive[i])
                som_bun_draw_copy(pixels,width,height,pitch,pixel_format,ram,vram,
                                  bunshin.x[i]-hx,bunshin.y[i]-hy);
    for(unsigned i=0;i<SOM_BUN_BOOMS;i++){
        SomBoom *b=&bunshin_booms[i];
        if(!b->start)continue;
        const unsigned long age=frame_number-b->start;
        if(age>=SOM_BUN_BOOM_FRAMES){b->start=0;continue;}
        som_bun_draw_boom(pixels,width,height,pitch,pixel_format,
                          hero_x+(b->x-hx),hero_y+(b->y-hy)-8,(unsigned)age,b->smoke);
    }
}
/* Swordsman bash: see SOM_BASH_LUNGE_FRAMES. */
static int som_bash_start(uint8_t *ram){
    if(!ram||bash_stage||evade_active||special_stage)return 0;
    uint8_t *hero=ram+0xE000u;
    if(hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||ram[0x7D]==1)return 0;
    /* The run's own start conditions ($02:B6CD). */
    if(hero[0x060]||hero[0x01C]||hero[0x00A]||hero[0x01A]||hero[0x01B]||
       (som_word(hero+0x190)&0xC160u))return 0;
    if(hero[0x1ED]){
        som_overlay_set(&overlay,"^Bash|catch your breath",SOM_MESSAGE_FRAMES/2u);
        return 0;
    }
    if(som_equip_weapon(ram,1u)!=SOM_EQUIP_OK)return 0;
    bash_stage=SOM_BASH_LUNGE;bash_started=frame_number;
    som_overlay_set(&overlay,"^Bash!",SOM_MESSAGE_FRAMES/3u);
    fprintf(stderr,"[som-ai] BASH lunge\n");
    return 1;
}
/* Before each emulated frame: advance the bash. */
static void som_bash_tick(void){
    if(!bash_stage||!backend_retro_get_memory_data)return;
    uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram){bash_stage=SOM_BASH_NONE;return;}
    uint8_t *hero=ram+0xE000u;
    const unsigned long t=frame_number-bash_started;
    if(hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||t>60ul){
        bash_stage=SOM_BASH_NONE;return;
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
        bash_stage=SOM_BASH_STRIKE;bash_started=frame_number;
        som_combo_swing(0);
    }else if(t>=SOM_BASH_STRIKE_FRAMES){
        bash_stage=SOM_BASH_NONE;
        fprintf(stderr,"[som-ai] BASH strike\n");
    }
}
static int som_evade_start(uint8_t *ram){
    if(!ram||evade_active)return 0;
    if(disciplines.selected>=SOM_DISCIPLINE_COUNT||!som_disc_has_dodge(disciplines.selected)){
        som_overlay_set(&overlay,"^No evade|this class cannot dodge",SOM_MESSAGE_FRAMES);
        return 0;
    }
    uint8_t *hero=ram+0xE000u;
    /* Field play, no event, not casting, not in a pose or power attack,
     * not knocked out or held by a status that stops movement. */
    if(hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||
       hero[0x060]||hero[0x01C]==0x40||hero[0x01C]==0x80||
       (som_word(hero+0x190)&0xC160u))return 0;
    /* Ninjutsu jutsu by the weapon in hand: sword = Shunshin (forward
     * leap), chakram/boomerang = Kage Bunshin (two shadow clones). With
     * clones out, A swaps the ninja into one instead. */
    const int ninja=disciplines.selected==2u;
    if(ninja&&som_bun_alive(&bunshin))
        return som_bunshin_swap(ram,0,0,0,evade_dir_x,evade_dir_y);
    const int shunshin=ninja&&hero[0x1E4]==1u;
    if(ninja&&hero[0x1E4]==6u){
        const unsigned made=som_bun_spawn(&bunshin,ram,(int)som_bun_u16(hero+2),
            (int)som_bun_u16(hero+4),hero[0x010],som_hero_place(hero),frame_number);
        if(!made){
            som_overlay_set(&overlay,"^No room|for shadow clones",SOM_MESSAGE_FRAMES/2u);
            return 0;
        }
        for(unsigned i=0;i<SOM_BUN_COUNT;i++)
            if(bunshin.alive[i])som_bun_boom(bunshin_booms,bunshin.x[i],bunshin.y[i],1,frame_number);
        som_overlay_set(&overlay,"^Kage Bunshin no Jutsu",SOM_MESSAGE_FRAMES/2u);
        fprintf(stderr,"[som-ai] EVADE Kage Bunshin: %u clone(s)\n",made);
        return 1;
    }
    const SomDodge dodge=som_disc_dodge(disciplines.selected,hero[0x1E4]);
    if(!dodge.speed)return 0;
    /* The held direction, else back from (or, forward: toward) the facing. */
    int dx=evade_dir_x,dy=evade_dir_y;
    if(!dx&&!dy){
        const unsigned facing=hero[0x010];
        if(facing==0x02u)dx=1;else if(facing==0x82u)dx=-1;
        else if(facing==0x01u)dy=1;else dy=-1;
        if(!dodge.forward){dx=-dx;dy=-dy;}
    }
    hop_dx=dx*(int)dodge.speed;hop_dy=dy*(int)dodge.speed;hop_frames_left=dodge.frames;
    evade_frames=dodge.frames+SOM_DODGE_GRACE;
    hero[0x006]=hero[0x007]=0;
    evade_hp=som_word(hero+0x182);
    evade_active=1;evade_started=frame_number;evade_xp_given=0;
    som_overlay_set(&overlay,shunshin?"^Shunshin no Jutsu":"^Evade",SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] EVADE %s as %s: %d,%d px per frame for %u frames\n",
            dodge.name,som_discipline_names[disciplines.selected],hop_dx,hop_dy,dodge.frames);
    return 1;
}
/* Before each frame: carry the dodging hero one step, per axis, onto open
 * ground only (src/som_bunshin.h); a wall ends the travel, not the guard.
 * Records the after-image trail. */
static void som_hop_step(void){
    if(!evade_active||!backend_retro_get_memory_data)return;
    uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram)return;
    uint8_t *hero=ram+0xE000u;
    if(hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0){hop_frames_left=0;return;}
    som_evade_shield(ram);
    int x=(int)som_bun_u16(hero+2),y=(int)som_bun_u16(hero+4);
    const unsigned slot=(unsigned)(frame_number%SOM_TRAIL_LENGTH);
    trail_x[slot]=x;trail_y[slot]=y;trail_frame[slot]=frame_number;trail_until=frame_number+10u;
    if(!evade_xp_given&&disciplines.selected<SOM_DISCIPLINE_COUNT){
        const int near=som_bun_toucher(ram,x,y,SOM_XP_NEAR_MISS_PX,SOM_XP_NEAR_MISS_PX,familiar.slot);
        if(near>=0){
            evade_xp_given=1;
            som_xp_give(disciplines.selected,SOM_XP_DODGE,
                        ram[0xE600u+0x200u*(unsigned)near+0x180u],SOM_XPK_DODGE,"near miss");
        }
    }
    if(!hop_frames_left)return;
    const unsigned place=som_hero_place(hero);
    int moved=0;
    if(hop_dx&&som_bun_spot_open(ram,x+hop_dx,y,place)){x+=hop_dx;moved=1;}
    if(hop_dy&&som_bun_spot_open(ram,x,y+hop_dy,place)){y+=hop_dy;moved=1;}
    hero[2]=(uint8_t)x;hero[3]=(uint8_t)(x>>8);hero[4]=(uint8_t)y;hero[5]=(uint8_t)(y>>8);
    hero[0x006]=hero[0x007]=0;
    hop_frames_left=moved?hop_frames_left-1u:0u;
}
/* Untouchable without the Barrel status (0x0400 is what item 10, the
 * Barrel, toggles: it shows the hero as a barrel). Before and after every
 * emulated frame of the dodge the hit mask E059 (bit per attacking monster)
 * and the pending damage E1F1/E1F2 are cleared before the combat tick can
 * apply them (som-decomp hit-reactions.md 1-2, rom-combat.md 4.1), and any
 * HP lost anyway is put back. */
static void som_evade_shield(uint8_t *ram){
    uint8_t *hero=ram+0xE000u;
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
    if(frame_number-evade_started<evade_frames&&ram[0xE000]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F)
        return;
    evade_active=0;hop_frames_left=0;
}
/* After-images of the dodge, 4 and 8 frames behind, on even frames only
 * (a flicker reads as translucent; the boy's palette has no colour math). */
static void som_trail_draw(uint8_t *pixels,unsigned width,unsigned height,size_t pitch){
    if(frame_number>trail_until||(frame_number&1u)||!backend_retro_get_memory_data)return;
    const uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||backend_retro_get_memory_size(RETRO_MEMORY_VIDEO_RAM)<SOM_BUN_VRAM_SIZE)return;
    const uint8_t *vram=backend_retro_get_memory_data(RETRO_MEMORY_VIDEO_RAM);
    const int hx=(int)som_bun_u16(ram+0xE002),hy=(int)som_bun_u16(ram+0xE004);
    for(unsigned back=8u;back>=4u;back-=4u){
        if(frame_number<back)continue;
        const unsigned slot=(unsigned)((frame_number-back)%SOM_TRAIL_LENGTH);
        if(trail_frame[slot]!=frame_number-back)continue;
        const int dx=trail_x[slot]-hx,dy=trail_y[slot]-hy;
        if(dx||dy)som_bun_draw_copy(pixels,width,height,pitch,pixel_format,ram,vram,dx,dy);
    }
}
/* Charge the game's actual weapon stage through its normal B input. */
static int som_special_start(unsigned which,unsigned stage){
    if(which>1||stage<1||stage>SOM_SEALS_MAX||special_stage||bash_stage||
       (ring_ai_enabled&&!special_from_ai)||som_manual_disciplines||
       disciplines.selected>=SOM_DISCIPLINE_COUNT||
       !backend_retro_get_memory_data)return 0;
    uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||ram[0xE000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||
       ram[0xD0]!=0||ram[0x7D]==1||ram[0xE1ED]!=0)return 0;
    unsigned weapon=som_disc_weapons[disciplines.selected][which];
    if(som_equip_weapon(ram,weapon)!=SOM_EQUIP_OK)return 0;
    if(!ram[0xE19C]||ram[0xE19B]!=0){
        fprintf(stderr,"[som-ai] SPECIAL refused: %s needs charge level %u\n",
                som_weapon_types[weapon],stage);
        return 0;
    }
    if(ram[0xE19C]<stage){                    /* only the levels learned */
        char text[64];
        snprintf(text,sizeof(text),"^%s level %u|not learned: level %u",
                 som_weapon_types[weapon],stage,(unsigned)ram[0xE19C]);
        som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES);
        stage=ram[0xE19C];
    }
    special_stage=stage;special_weapon=weapon;special_started=frame_number;
    som_combo_clear();
    fprintf(stderr,"[som-ai] SPECIAL charging %s level %u\\n",
            som_weapon_types[weapon],stage);
    return 1;
}
static void som_special_tick(void){
    if(!special_stage||!backend_retro_get_memory_data)return;
    uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||ram[0xE000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||
       ram[0xD0]!=0||ram[0xE1E4]!=special_weapon||
       frame_number-special_started>SOM_SPECIAL_TIMEOUT){
        fprintf(stderr,"[som-ai] SPECIAL abort: state changed or charge timeout\\n");
        special_stage=0;return;
    }
    /* Skip the hold: once the swing is over and the gauge runs ($C0:B330:
     * E01C, E061, E1ED clear; E01A counting), set the gauge word E01A to
     * the finished stage (counter parked at 44, stage in E01B = E19B),
     * as 90 frames per stage of holding would (som-decomp rom-combat.md
     * 11.3, glove-attacks.md). Releasing B then starts that power attack. */
    if(ram[0xE19B]<special_stage&&(ram[0xE01A]||ram[0xE01B])&&
       !ram[0xE01C]&&!ram[0xE061]&&!ram[0xE1ED]){
        ram[0xE01A]=0x2C;ram[0xE01B]=(uint8_t)special_stage;ram[0xE19B]=(uint8_t)special_stage;
    }
    if(ram[0xE19B]>=special_stage){
        fprintf(stderr,"[som-ai] SPECIAL release %s level %u\n",
                som_weapon_types[special_weapon],special_stage);
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
    som_overlay_set(&overlay,"^A new weapon appears!",SOM_MESSAGE_FRAMES);
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
    for(unsigned i=0;i<2;i++){
        unsigned type=som_disc_weapons[discipline][i];
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
        if(n)som_overlay_set(&overlay,"^Class weapons unlocked!",SOM_MESSAGE_FRAMES);
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
static int som_use_candy(uint8_t *ram){
    if(!ram)return 0;
    if(ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||ram[0xE000]!=1){
        fprintf(stderr,"[som-ai] CANDY refused: not in field play\n");return 0;
    }
    const unsigned hp=ram[0xE182]|ram[0xE183]<<8,max_hp=ram[0xE184]|ram[0xE185]<<8;
    if(!hp||hp>max_hp){fprintf(stderr,"[som-ai] CANDY refused: hero HP %u/%u\n",hp,max_hp);return 0;}
    for(unsigned slot=0;slot<11;slot++){
        uint8_t *item=ram+0xCC48u+slot;
        if(*item==0xFF||(*item&0x0F)!=0)continue;
        const unsigned code=*item>>4;
        if(code<4u||code>14u||(code&1u)){        /* Turbo carries up to 7 */
            fprintf(stderr,"[som-ai] CANDY refused: %u left (the ring handles the last)\n",code/2u);
            return 0;
        }
        *item=(uint8_t)(*item-0x20u);
        const unsigned heal=ram[0xE1F3]+SOM_CANDY_HEAL;
        ram[0xE1F3]=(uint8_t)(heal>0xFFu?0xFFu:heal);
        fprintf(stderr,"[som-ai] CANDY used without the ring: %u left, +%u HP queued\n",
                code/2u-1u,SOM_CANDY_HEAL);
        return 1;
    }
    fprintf(stderr,"[som-ai] CANDY refused: no Candy\n");
    return 0;
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
        fprintf(stderr,"[som-ai] WEAPON TRACE timed out after %u frames\\n",
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
    if(target<0){clone_no_slot=1;return 0;}
    if(!som_take_undo())return 0;
    uint8_t *from=ram+SOM_MONSTER_BASE+(unsigned)source*SOM_OBJECT_SIZE;
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
static int som_npc_walk_command(const char *line){
    SomNpcWalk walk;
    if(!som_npc_parse_walk(line,&walk)){
        fprintf(stderr,"[som-ai] NPC refused: malformed command\n");return 0;
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
    som_npc_walk_command(line);
}
static int som_undo_spawn(void){
    if(!undo_ready||!backend_retro_unserialize){
        fprintf(stderr,"[som-ai] UNDO refused: no spawn snapshot\n");return 0;
    }
    int ok=backend_retro_unserialize(undo_state,undo_size);
    undo_ready=0;
    familiar.slot=familiar.target=-1;familiar_pending=0;   /* memory is pre-clone */
    familiar_carry.valid=0;
    unlink(familiar_file);
    fprintf(stderr,ok?"[som-ai] UNDO restored the pre-spawn snapshot\n":
                      "[som-ai] UNDO failed to restore the snapshot\n");
    return ok;
}
static void som_start_action(SomAction action){
    if(action==SOM_ACT_SNAPSHOT){som_snapshot();return;}
    if(action==SOM_ACT_ATTACK)som_combo_swing(0);
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
    if(!strcmp(command,"ai_on")){
        if(!som_write_ring_ai_marker())return 0;
        ring_ai_enabled=1;ring_ai_grace_until=frame_number+30;
        fprintf(stderr,"[som-ai] MANUAL AI START signal ready%s\n",
                direct_transport?" in process":" on disk");
        return 1;
    }
    if(!strcmp(command,"ai_off")){
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
                som_overlay_set(&overlay,"^Only summoners can summon",SOM_MESSAGE_FRAMES);
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
    /* The AI's dodge: the same evade/jutsu as the player's A. */
    if(!strcmp(command,"evade"))
        return som_evade_start(backend_retro_get_memory_data?
            (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL);
    if(!strcmp(command,"summon_boss"))return som_summon_boss();
    if(!strcmp(command,"vram_dump")){som_vram_dump();return 1;}
    if(!strcmp(command,"weapon_trace")){
        if(!som_weapon_trace_allocate())return 0;
        weapon_trace_on=1;som_weapon_trace_reset();
        som_overlay_set(&overlay,"WEAPON TRACE ON|Equip via native ring",SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] WEAPON TRACE armed; equip via native ring\\n");
        return 1;
    }
    /* The AI picks the discipline that fits (bridge som_class_director). */
    if(!strncmp(command,"class_",6)){
        char *end=NULL;
        const unsigned long n=!strcmp(command+6,"none")?SOM_DISCIPLINE_NONE:
            strtoul(command+6,&end,10);
        if(n!=SOM_DISCIPLINE_NONE&&(!end||*end||end==command+6||n>=SOM_DISCIPLINE_COUNT))return 0;
        if(disciplines.selected!=n){
            disciplines.selected=(uint8_t)n;
            som_disc_save();som_class_selected();
            if(n<SOM_DISCIPLINE_COUNT){
                char text[64];
                snprintf(text,sizeof(text),"^%s|AI chose this class",som_discipline_names[n]);
                som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES);
            }
            fprintf(stderr,"[som-ai] DISCIPLINE selected=%lu by the AI (saved)\n",n);
        }
        return 1;
    }
    /* cast_attack / cast_heal / cast_buff: a spell of the chosen class. */
    if(!strncmp(command,"cast_",5)&&som_rom&&backend_retro_get_memory_data){
        uint8_t *ram=(uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        const unsigned d=disciplines.selected;
        const int want_attack=!strcmp(command+5,"attack"),want_heal=!strcmp(command+5,"heal");
        if(!want_attack&&!want_heal&&strcmp(command+5,"buff"))return 0;
        for(unsigned i=0;i<som_class_spell_count(d);i++){
            const unsigned spell=som_class_spell(d,i);
            const uint8_t *row=som_rom_at(0xD02AD0u+64u*spell,16u);
            if(!row)continue;
            const int heal=spell==0x0Bu||spell==0x0Au;    /* Cure Water, Remedy */
            if(want_attack?row[8]==0x81u:want_heal?heal:(row[8]!=0x81u&&!heal))
                return som_cast_spell(ram,spell);
        }
        return 0;
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
    if(!strcmp(command,"next_weapon"))
        return som_cycle_weapon(backend_retro_get_memory_data?
            (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL);
    if(!strncmp(command,"give_",5)){
        for(unsigned t=0;t<8u;t++)
            if(!strcmp(command+5,som_weapon_types[t]))
                return som_give_weapon(backend_retro_get_memory_data?
                    (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL,t);
    }
    if(!strcmp(command,"use_candy"))
        return som_use_candy(backend_retro_get_memory_data?
            (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL);
    if(!strcmp(command,"pal_trace")){
        pal_trace_frames=0;pal_trace_on=1;
        som_overlay_set(&overlay,"Recording 8 s: move now!",SOM_MESSAGE_FRAMES);
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
    if(have&&som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES))
        fprintf(stderr,"[som-ai] MESSAGE %s\n",text);
}
RETRO_API bool flxtr_som_ai_command(const char *command){
    return som_handle_command(command)!=0;
}
RETRO_API bool flxtr_som_ai_message(const char *text){
    return test_enabled&&game_loaded&&on_turbo&&text&&
           som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES)!=0;
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
        if(!som_bun_boy_piece(e))continue;
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
#define SOM_COMBO_LABEL_RISE_PX 40   /* counter top, above the hero's head */
#define SOM_COMBO_LABEL_RISE_PX 40   /* counter top, above the hero's head */
static void som_video_refresh(const void *data,unsigned width,unsigned height,size_t pitch){
    if(!frontend_video)return;
    /* NULL repeats the previous (already decorated) frame; (void*)-1 is a
     * hardware-rendered frame that has no CPU pixels to draw on. */
    const int menu=som_manual_disciplines||
        (ring_ai_enabled&&(gambits.open||disciplines.open));
    /* Class title next to the HUD portrait, under the HP numbers. Around the
     * hero, field mode only (som_hero_screen): combo counter, shadow clones,
     * explosions and dodge after-images. */
    const char *class_title=menu?NULL:som_disc_title(&disciplines);
    int hud_x=-1,hud_y=-1;
    if(class_title&&!(backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE&&
       som_hud_portrait((const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM),
                        &hud_x,&hud_y)))class_title=NULL;
    char combo_text[24];som_combo_text(&combo,combo_text);
    if(menu)combo_text[0]=0;
    const int clones=!menu&&(som_bun_alive(&bunshin)||som_bunshin_booming()||
                             frame_number<=trail_until);
    int hero_x=-1,hero_y=-1;
    const int hero_drawn=(combo_text[0]||clones)&&som_hero_screen(&hero_x,&hero_y);
    if((!overlay.frames_left&&!menu&&!hero_drawn&&!class_title)||!data||data==RETRO_HW_FRAME_BUFFER_VALID||
       (pixel_format!=0&&pixel_format!=1&&pixel_format!=2)){
        frontend_video(data,width,height,pitch);
        return;
    }
    size_t size=(size_t)height*pitch;
    if(size>overlay_frame_size){
        uint8_t *grown=realloc(overlay_frame,size);
        if(!grown){frontend_video(data,width,height,pitch);return;}
        overlay_frame=grown;overlay_frame_size=size;
    }
    memcpy(overlay_frame,data,size);
    if(overlay.speech_bubble&&!hero_drawn)som_hero_screen(&hero_x,&hero_y);
    if(hero_drawn){
        if(clones){
            som_trail_draw(overlay_frame,width,height,pitch);
            som_bunshin_draw(overlay_frame,width,height,pitch,hero_x,hero_y);
        }
        if(combo_text[0])
            som_overlay_label(overlay_frame,width,height,pitch,pixel_format,combo_text,
                              hero_x,hero_y-SOM_COMBO_LABEL_RISE_PX,1,
                              som_overlay_color(pixel_format,255,208,64));
    }
    if(class_title)
        som_overlay_label(overlay_frame,width,height,pitch,pixel_format,class_title,
                          hud_x+SOM_HUD_LABEL_DX,hud_y+SOM_HUD_LABEL_DY,0,
                          som_overlay_color(pixel_format,255,255,255));
    if(overlay.frames_left){
        som_overlay_draw(overlay_frame,width,height,pitch,pixel_format,&overlay,hero_x,hero_y);
        overlay.frames_left--;
    }
    if(menu){
        if(som_manual_disciplines||som_menu_disciplines)
            som_disc_draw(overlay_frame,width,height,pitch,pixel_format,&disciplines);
        else som_gambit_draw(overlay_frame,width,height,pitch,pixel_format,&gambits);
    }
    frontend_video(overlay_frame,width,height,pitch);
}
/* Music-free mixing (our core only): Secret of Mana's music is never
 * synthesised, effects keep playing (patches/snes9x2005-flxtr-music-voices). */
static void (*backend_skip_music)(int);
#define SOM_SKIP_MUSIC_MODE 2   /* voices 0-5 off; 6-7 only after effect requests */
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
    som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES/2u);
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
    load_backend();physical_state=cb;backend_set_input_state(som_input_state);
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
RETRO_API bool retro_load_game(const struct retro_game_info *game){
    load_backend();
    frame_number=0;cancelled=0;completed=0;armed=0;on_turbo=0;som_cam_valid=0;
    active_action=SOM_ACT_NONE;action_frames=0;som_have_previous_menu=0;
    autonomous_mode=0;auto_step=0;auto_deadline=0;
    last_physical_buttons=0;last_attack_frame=0;som_combo_clear();
    special_stage=0;special_weapon=0;special_started=0;
    evade_active=evade_a_hidden=0;evade_hp=0;evade_frames=0;trail_until=0;
    memset(trail_frame,0,sizeof(trail_frame));seal_mode=0;seal_count=0;x_cycled=0;
    som_bun_clear(&bunshin);memset(bunshin_booms,0,sizeof(bunshin_booms));
    evade_dir_x=evade_dir_y=0;bash_stage=SOM_BASH_NONE;bash_started=0;
    hop_dx=hop_dy=0;hop_frames_left=0;
    special_chord=0;special_suppress=0;
    y_weapon_swap=y_weapon_blocked=b_weapon_blocked=x_spell_blocked=0;
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
    som_disc_init(&disciplines);som_menu_disciplines=0;som_manual_disciplines=0;
    som_disc_load();
    memset(&xp_decay,0,sizeof(xp_decay));xp_dirty=0;xp_saved_frame=0;xp_spell_slot=-1;
    /* A previously selected class also receives missing weapons after load. */
    som_class_selected();
    weapon_trace_on=0;som_weapon_trace_reset();
    hero_pal_have=0;
    som_gambit_load();som_music_genre_load();
    last_input_frame=frame_number;idle_started=0;
    const char *direct=getenv("FLXTR_SOM_AI_DIRECT");
    direct_transport=direct&&direct[0]=='1'&&direct[1]=='\0';
    const char *path=game?game->path:NULL;
    som_auto_state_path_set(path);
    on_turbo=path&&strstr(path,"Secret of Mana, Turbo.smc")!=NULL;
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
        if(som_apply_rom_patches(patched_rom,game->size)){
            patched_game=*game;patched_game.data=patched_rom;load_info=&patched_game;
        }
    }
    som_rom=on_turbo&&patched_rom?patched_rom:NULL;
    som_rom_size=som_rom?game->size:0;
    weapon_gfx_verified=0;weapon_gfx_last_check=0;equip_notice_frame=0;
    game_loaded=backend_load_game(load_info);
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
    const size_t size=backend_retro_serialize_size();
    void *data=size?malloc(size):NULL;
    if(!data)return 0;
    char temp[sizeof(som_auto_state_path)+16];
    snprintf(temp,sizeof(temp),"%s.flxtr-full",som_auto_state_path);
    int ok=backend_retro_serialize(data,size);
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
    som_save_auto_state();
    som_xp_flush(1);
    som_net_pad_close(&net_pad);net_pad_was_active=0;
    armed=0;autonomous_mode=0;game_loaded=0;
    ring_ai_enabled=0;ring_ai_pending=0;
    weapon_gfx_verified=0;
    unlink(wram_file);unlink(wram_temp_file);unlink(hot_file);unlink(ring_ai_file);unlink(message_file);
    unlink(familiar_file);familiar.slot=familiar.target=-1;familiar_pending=0;
    familiar_carry.valid=0;
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
        som_familiar_save(&familiar_carry,ram,familiar.slot);
        uint8_t *vram=(uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_VIDEO_RAM);
        if(vram&&backend_retro_get_memory_size(RETRO_MEMORY_VIDEO_RAM)>=0x10000){
            som_familiar_capture_vram(&familiar_carry,ram,vram,
                ram+SOM_MONSTER_BASE+(unsigned)familiar.slot*SOM_OBJECT_SIZE,familiar.map);
            fprintf(stderr,"[som-ai] FAMILIAR sprite captured: %u tiles, palette 7E%04X\n",
                    familiar_carry.tile_count,familiar_carry.palette_address);
        }
    }
    som_familiar_publish();
    som_overlay_set(&overlay,"^A familiar joins you!",SOM_MESSAGE_FRAMES);
    fprintf(stderr,"[som-ai] FAMILIAR bound to monster slot %d\n",familiar.slot+1);
}
static int som_summon_boss(void){
    if(familiar.slot>=0||familiar_carry.valid){
        fprintf(stderr,"[som-ai] SUMMON refused: a familiar is already with you\n");return 0;
    }
    const uint8_t *ram=backend_retro_get_memory_data?
        (const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL;
    if(!som_summon_allowed(ram)){
        som_overlay_set(&overlay,"^Only summoners can summon",SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] SUMMON refused: the leader is not a summoner\n");return 0;
    }
    FILE *in=fopen(summon_file,"rb");
    if(!in){fprintf(stderr,"[som-ai] SUMMON refused: no %s\n",summon_file);return 0;}
    static SomFamiliarCarry loaded;
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
    som_overlay_set(&overlay,"^A summon answers your call!",SOM_MESSAGE_FRAMES);
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
    if(state==SOM_FAMILIAR_HERE&&familiar.map!=familiar_carry.home_map)
        som_familiar_reapply(&familiar_carry,ram,som_vram());      /* keep it drawn */
    if(state==SOM_FAMILIAR_HERE){
        if(frame_number%60u==0)som_familiar_save(&familiar_carry,ram,familiar.slot);
        return;
    }
    som_familiar_publish();
    if(state==SOM_FAMILIAR_LEFT_MAP){
        familiar_arrived=0;
        fprintf(stderr,"[som-ai] FAMILIAR follows the hero to the next map\n");
    }else{
        familiar_carry.valid=0;
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
    familiar.slot=familiar.target=-1;familiar_pending=0;familiar_carry.valid=0;
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
RETRO_API void retro_run(void){
    if(!backend_run)load_backend();
    if(!direct_transport){
        som_check_trigger();
        som_check_npc_command();
        som_check_message();
    }
    som_clone_tick();
    som_familiar_frame();
    /* No automatic hero VRAM caching: old monster-style snapshots garbled art. */
    if(test_enabled&&on_turbo&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE)
        som_weapon_gfx_verify_frame((const uint8_t*)
            backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    if(test_enabled&&on_turbo)som_class_supply_frame();
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
    if(test_enabled&&on_turbo)som_bash_tick();
    if(test_enabled&&on_turbo)som_hop_step();
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
    backend_run();
    if(test_enabled&&on_turbo)som_combo_frame();
    if(test_enabled&&on_turbo)som_special_tick();
    if(test_enabled&&on_turbo)som_evade_tick();
    if(test_enabled&&on_turbo)som_weapon_trace_frame();
    if(test_enabled&&on_turbo)som_ring_ai_tick();
    som_idle_tick();
    if(!direct_transport&&test_enabled&&on_turbo&&
       frame_number%(ring_ai_enabled||armed?SOM_WRAM_EXPORT_FRAMES:SOM_WRAM_IDLE_EXPORT_FRAMES)==0u &&
       !som_export_wram()&&!wram_export_error_logged){
        wram_export_error_logged=1;
        fprintf(stderr,"[som-ai] WRAM export unavailable; live observer disabled\n");
    }
    frame_number++;
}
