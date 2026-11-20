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
#include "som_npc_work.h"
#include "som_general.h"
#include "som_raid.h"
#include "som_net_pad.h"
#include "som_songs.h"
#include "som_combo.h"
#include "som_bunshin.h"
#include "som_telekinesis.h"
#include "som_bunshin_draw.h"
#include "som_xp.h"
#include "som_progress.h"
#include "som_perks.h"
#include "som_weapon_styles.h"
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
#define SOM_KILL_CREDIT_FRAMES 120ul    /* a death this soon after our hit is our kill */
static int perk_first_hit_used;
static unsigned perk_hero_hp_prev,perk_last_hit_damage=8u;
static unsigned som_perk_bonus(unsigned slot,unsigned damage,const char *what);
static void som_perk_hit(uint8_t *ram,unsigned slot,unsigned drop,unsigned hp_after);
static void som_perks_frame(uint8_t *ram);
static uint16_t som_word(const uint8_t *p);
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
static int som_iai_start(unsigned level);
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
 * once companions have joined): hold L2 or R3 ALONE for one second and
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
/* Independent from the six Gambit priority digits, shared with the
 * external song-id -> yt-dlp playback bridge. */
#define SOM_MUSIC_GENRE_FILE "/home/apps/retroarch/cores/experimental/som_music_genre"
static const char *music_genre_file=SOM_MUSIC_GENRE_FILE;
static void som_music_genre_load(void);
static void som_music_genre_save(void);
static SomGambits gambits={0,0,{4,3,3,3,4,1},0,SOM_PAGE_ACTIONS,
    {3,2,2,2,3,3,3,3},{3,3},0,0,1};
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
    char quest[16]={0};
    in=fopen(quest_file,"r");
    if(in){if(!fgets(quest,sizeof(quest),in))quest[0]=0;fclose(in);}
    som_quest_parse(&gambits,quest[0]?quest:NULL);
    char brain[16]={0};
    in=fopen(brain_file,"r");
    if(in){if(!fgets(brain,sizeof(brain),in))brain[0]=0;fclose(in);}
    som_brain_parse(&gambits,brain[0]?brain:NULL);
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
        som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES);
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
#define SOM_EQUIP_OK 1
#define SOM_EQUIP_BUSY 0                /* hero mid-action: retry later */
#define SOM_EQUIP_REFUSED (-1)          /* will not work: use the ring */
static int som_cast_spell(uint8_t *ram,unsigned spell);
static int som_spell_affordable(const uint8_t *ram,unsigned spell);
#define SOM_SPELL_FIRE_BALL 0x0Cu
/* X casts the current spell of the chosen discipline (som_magic.h); holding
 * X steps to the next one. One cursor per discipline (+1: no discipline). */
static unsigned spell_cursor[SOM_DISCIPLINE_COUNT+1u];
static unsigned long x_press_frame;
static int x_cycled;
/* X was pressed with L2 (an L2+X move): its release is not a cast or an
 * Iaijutsu, even when L2 lets go in the same frame (swordsman cast a saber
 * after Whirlwind, 2026-11-20). */
static int x_l2_chord;
#define SOM_X_HOLD_FRAMES 30ul
/* Where X's list starts: a caster's first two spells are on B and Y. */
static unsigned som_x_spell_base(void){
    const unsigned n=som_class_spell_count(disciplines.selected);
    return disciplines.selected<SOM_DISCIPLINE_COUNT&&som_disc_caster(disciplines.selected)&&
           n>SOM_CASTER_BUTTON_SPELLS?SOM_CASTER_BUTTON_SPELLS:0u;
}
static unsigned som_current_spell(void){
    const unsigned d=disciplines.selected<SOM_DISCIPLINE_COUNT?disciplines.selected:SOM_DISCIPLINE_COUNT;
    const unsigned base=som_x_spell_base(),n=som_class_spell_count(disciplines.selected);
    return som_class_spell(disciplines.selected,
                           n>base?base+spell_cursor[d]%(n-base):spell_cursor[d]);
}
static void som_next_spell(void){
    const unsigned d=disciplines.selected<SOM_DISCIPLINE_COUNT?disciplines.selected:SOM_DISCIPLINE_COUNT;
    const unsigned n=som_class_spell_count(disciplines.selected),base=som_x_spell_base();
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
static int som_evade_start(uint8_t *ram);
/* Kage Bunshin (src/som_bunshin.h): clones the proxy keeps and draws over
 * the frame (src/som_bunshin_draw.h); touch makes them explode, the hero
 * can swap into them. */
static SomBunshin bunshin;
/* The clones out are Shunshin's: explosive, never a substitution (no swap);
 * a monster reaching the hero or the clones' time running out detonates
 * them. 0: Kage Bunshin clones (swap on A, substitution on touch). */
static int bunshin_shunshin;
static int evade_dir_x,evade_dir_y;    /* D-pad aim: clone swap or hop */
static int hop_dx,hop_dy;              /* hop dodge step per frame */
static unsigned hop_frames_left;
static void som_hop_step(void);
static void som_evade_shield(uint8_t *ram);
static SomBoom bunshin_booms[SOM_BUN_BOOMS];
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
static int som_shunshin_start(uint8_t *ram);
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
static unsigned long telekinesis_ready;
static int som_telekinesis_cast(uint8_t *ram);
static int som_class_x(uint8_t *ram);
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
static unsigned lock_map;
static void som_lock_toggle(uint8_t *ram);
static void som_lock_frame(uint8_t *ram);
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
/* Ukemi has its own forward somersault art (kinds 0x83..0x85) in the
 * same ROM-derived pack as the flying kick. The native 0x93 cartwheel is
 * kept for monks/rogues but Ninja/Samurai use collision-aware movement. */
#define SOM_UKEMI_DRAW_KIND 0x83u
static SomNpcWorkState ukemi_draw;
static int ukemi_hid;
static unsigned ukemi_map;
#define SOM_FLYKICK_LIFT 12             /* px at the top of the jump */
#define SOM_FLYKICK_KIND 0x90u
static int bash_kick,flykick_from_ai;
static SomNpcWorkState flykick;
static int flykick_hid;
static int som_flykick_start(uint8_t *ram);
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
 * tile rows (som_camera_follow). No MP: the boy often has none. */
#define SOM_BLINK_RANGE 12
#define SOM_BLINK_SPEED 4
#define SOM_BLINK_COOLDOWN 600ul
static int blink_active,blink_hid,blink_tx,blink_ty,blink_is_swap;
static unsigned long blink_ready;
static int som_blink_start(uint8_t *ram,int tx,int ty);
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
static void som_camera_follow(uint8_t *ram,int dx,int dy);
static void som_blink_frame(uint8_t *ram);
static void som_flykick_frame(uint8_t *ram);
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
static int x_weapon_swap;                 /* Weapon Flow: X attacks as B (third weapon) */
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
static unsigned som_taijutsu_peek(void){
    if(frame_number-taijutsu_last>SOM_TAIJUTSU_RESET)taijutsu_step=0;
    return taijutsu_step<SOM_TAIJUTSU_PUNCHES?0u:1u;   /* glove, glove, sword */
}
static void som_taijutsu_advance(void){
    taijutsu_step=(taijutsu_step+1u)%(SOM_TAIJUTSU_PUNCHES+1u);
    taijutsu_last=frame_number;
}
static SomStyles styles;
static int style_b_hidden,throw_active;
static void som_style_cycle(int to);
static void som_style_give(unsigned species);
static const char *const som_weapon_types[8];
static int som_throw_back_pressed(const uint8_t *ram,uint16_t pressed);
static int som_throw_start(uint8_t *ram);
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
            som_xp_give(sel==SOM_DISC_NINJUTSU&&weapon==0u?SOM_DISC_NINJUTSU:   /* Taijutsu */
                        som_xp_weapon_discipline(weapon,sel,
                            sel<SOM_DISCIPLINE_COUNT?som_disc_weapons[sel]:NULL),
                        SOM_XP_HIT,e[0x180],SOM_XPK_HIT,"weapon hit");
            som_style_give(e[0x180]);
            if(sel==SOM_DISC_NINJUTSU&&weapon==6u&&!fuma.active)   /* Fuma: boomerang hit */
                som_fuma_start(ram,i,combo_hp[i]-hp);
            som_perk_hit((uint8_t*)ram,i,combo_hp[i]-hp,hp);
            if(sel==SOM_DISC_ROGUE&&frame_number<backstab_until){          /* Backstab */
                uint8_t *m=(uint8_t*)ram+0xE600u+0x200u*i;
                const unsigned bonus=12u+2u*disciplines.level[SOM_DISC_ROGUE];
                const unsigned pending=m[0x1F1]+bonus;
                m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
                backstab_until=0;
                som_overlay_set(&overlay,"^Backstab!",SOM_MESSAGE_FRAMES/3u);
                fprintf(stderr,"[som-ai] BACKSTAB: +%u on monster %u\n",bonus,i);
            }
            if(sel==SOM_DISC_MARTIAL&&combo.hits&&combo.hits%3u==0u){   /* Rising Dragon */
                uint8_t *m=(uint8_t*)ram+0xE600u+0x200u*i;
                const unsigned bonus=8u+disciplines.level[SOM_DISC_MARTIAL];
                const unsigned pending=m[0x1F1]+bonus;
                m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
                som_overlay_set(&overlay,"^Rising Dragon!",SOM_MESSAGE_FRAMES/3u);
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
static int som_perk(unsigned effect){return som_perk_value(effect,&disciplines,perks_enabled);}
/* Extra damage from Coup de Grace (the monster still reels from a hit) and
 * Last Resort (hero below a quarter of max HP), as a share of `damage`. */
static unsigned som_perk_bonus(unsigned slot,unsigned damage,const char *what){
    if(slot>2u||!damage||!backend_retro_get_memory_data)return 0;
    const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram)return 0;
    int pct=0;
    const char *names="";
    if(monster_hit_frame[slot]&&frame_number-monster_hit_frame[slot]<=SOM_REEL_FRAMES){
        const int coup=som_perk(SOM_EFFECT_REELING_BONUS);
        if(coup){pct+=coup;names="Coup de Grace";}
    }
    const unsigned hp=som_word(ram+0xE182),max=som_word(ram+0xE184);
    if(max&&hp*4u<max){
        const int last=som_perk(SOM_EFFECT_LOW_HP_BONUS);
        if(last){pct+=last;names=*names?"Coup de Grace + Last Resort":"Last Resort";}
    }
    const unsigned bonus=damage*(unsigned)pct/100u;
    if(bonus)fprintf(stderr,"[som-ai] PERK %s: +%u on the %s (monster %u)\n",names,bonus,what,slot);
    return bonus;
}
/* After a hero hit lowered monster `slot` by `drop` to `hp_after`. */
static void som_perk_hit(uint8_t *ram,unsigned slot,unsigned drop,unsigned hp_after){
    if(!ram||slot>2u)return;
    if(drop)perk_last_hit_damage=drop;
    uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
    if(hp_after){
        const unsigned bonus=som_perk_bonus(slot,drop,"hit");
        if(bonus){
            const unsigned pending=m[0x1F1]+bonus;
            m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
        }
    }
    monster_hit_frame[slot]=frame_number;
}
/* The guard is up: the AI's block action, or physical R2. */
static int som_guarding(void){
    return (armed&&active_action==SOM_ACT_BLOCK)||
           (!ring_ai_enabled&&(last_physical_buttons&(1u<<RETRO_DEVICE_ID_JOYPAD_R2)));
}
/* After each frame: Uncanny Dodge (the first hit of a fight is cut) and
 * Tiger Drop (a hit landing on the guard is countered). */
static void som_perks_frame(uint8_t *ram){
    if(!ram||ram[0xE000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0){
        perk_hero_hp_prev=0;return;
    }
    uint8_t *hero=ram+0xE000u;
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    const unsigned hp=som_word(hero+0x182),max=som_word(hero+0x184);
    if(som_bun_toucher(ram,hx,hy,SOM_FIGHT_NEAR_PX,SOM_FIGHT_NEAR_PX,familiar.slot)>=0)perk_quiet=0;
    else if(perk_quiet<SOM_FIGHT_QUIET_FRAMES)perk_quiet++;
    if(perk_quiet>=SOM_FIGHT_QUIET_FRAMES)perk_first_hit_used=0;   /* a new fight */
    const int hurt=perk_hero_hp_prev&&hp&&hp<perk_hero_hp_prev&&!evade_active;
    unsigned now_hp=hp;
    if(hurt&&!perk_first_hit_used){
        perk_first_hit_used=1;
        const int pct=som_perk(SOM_EFFECT_FIRST_HIT_CUT);
        const unsigned back=(perk_hero_hp_prev-hp)*(unsigned)pct/100u;
        if(back){
            now_hp=hp+back>max?max:hp+back;
            hero[0x182]=(uint8_t)now_hp;hero[0x183]=(uint8_t)(now_hp>>8);
            fprintf(stderr,"[som-ai] PERK Uncanny Dodge: first hit of the fight %u -> %u\n",
                    perk_hero_hp_prev-hp,perk_hero_hp_prev-hp-back);
        }
    }
    const int samurai=disciplines.selected==SOM_DISC_BUSHIDO;
    const int monk=disciplines.selected==SOM_DISC_MARTIAL;
    if((samurai||monk)&&som_guarding()&&frame_number>=(samurai?iai_ready:palm_ready)&&
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
            if(hurt){hero[0x182]=(uint8_t)perk_hero_hp_prev;hero[0x183]=(uint8_t)(perk_hero_hp_prev>>8);
                     now_hp=perk_hero_hp_prev;}
            monster_hit_frame[foe]=frame_number;
            if(samurai)iai_ready=frame_number+SOM_IAI_COOLDOWN_FRAMES;
            else palm_ready=frame_number+SOM_PALM_COOLDOWN;
            som_bun_boom(bunshin_booms,(int)som_bun_u16(m+2),(int)som_bun_u16(m+4),1,frame_number);
            som_overlay_set(&overlay,samurai?"^Iai counter!":"^Counter palm!",SOM_MESSAGE_FRAMES/3u);
            fprintf(stderr,"[som-ai] %s: parried, hit monster %d for %u\n",
                    samurai?"IAI COUNTER":"COUNTER PALM",foe,damage);
        }
    }
    const int counter=som_perk(SOM_EFFECT_GUARD_COUNTER);
    if(counter&&som_guarding()&&frame_number>=tiger_ready&&
       (hurt||hero[0x059]||hero[0x1F1]||hero[0x1F2])){
        const int foe=som_bun_toucher(ram,hx,hy,SOM_BUN_HERO_TOUCH_X*2,SOM_BUN_HERO_TOUCH_Y*2,
                                      familiar.slot);
        if(foe>=0){
            uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)foe;
            unsigned damage=perk_last_hit_damage*(2u+2u*(unsigned)counter)/2u;
            damage+=som_perk_bonus((unsigned)foe,damage,"Tiger Drop");
            const unsigned pending=m[0x1F1]+damage;
            m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
            monster_hit_frame[foe]=frame_number;
            tiger_ready=frame_number+SOM_TIGER_COOLDOWN;
            som_overlay_set(&overlay,"^Tiger Drop!",SOM_MESSAGE_FRAMES/3u);
            fprintf(stderr,"[som-ai] PERK Tiger Drop: guarded hit countered for %u on monster %d\n",
                    damage,foe);
        }
    }
    /* Battle Rhythm: a monster we hit (sword, boomerang, Fuma clone, Tiger
     * Drop) that dies within SOM_KILL_CREDIT_FRAMES heals the hero. */
    for(unsigned slot=0;slot<3u;slot++){
        const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
        const int live=som_bun_monster_alive(m)&&som_word(m+0x182)>0;
        if(perk_monster_live[slot]&&!live&&(int)slot!=familiar.slot&&monster_hit_frame[slot]&&
           frame_number-monster_hit_frame[slot]<=SOM_KILL_CREDIT_FRAMES){
            const unsigned heal=max*(unsigned)som_perk(SOM_EFFECT_KILL_HEAL)/100u;
            if(heal){
                const unsigned pending=som_word(hero+0x1F3)+heal;
                hero[0x1F3]=(uint8_t)pending;hero[0x1F4]=(uint8_t)(pending>>8);
                fprintf(stderr,"[som-ai] PERK Battle Rhythm: kill heals %u HP\n",heal);
            }
            if(disciplines.selected==SOM_DISC_BUSHIDO&&frame_number<iai_ready){
                iai_ready=frame_number;                    /* Zanshin */
                som_overlay_set(&overlay,"^Zanshin|Iaijutsu ready",SOM_MESSAGE_FRAMES/3u);
                fprintf(stderr,"[som-ai] ZANSHIN: kill readies Iaijutsu\n");
            }
            monster_hit_frame[slot]=0;
        }
        if(perk_monster_live[slot]&&!live&&(int)slot!=familiar.slot)last_kill_frame=frame_number;
        perk_monster_live[slot]=(uint8_t)live;
    }
    perk_hero_hp_prev=now_hp;
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
        som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] NETPAD player 2 joined from %s\n",inet_ntoa(a));
    }else if(net_pad_was_active&&!active){
        som_overlay_set(&overlay,"Player 2 left",SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] NETPAD player 2 left\n");
    }
    net_pad_was_active=active;
}
/* Field-only input translation; never affect original menus or AI pad.
 * Native R becomes physical R2 guard, L1 is exclusive lock-on, R1 cycles
 * styles, L3+Y/X opens the native ring, L3+B cycles the old style shortcut. */
static inline uint16_t som_manual_pad_layout(uint16_t raw){
    const uint16_t r=1u<<RETRO_DEVICE_ID_JOYPAD_R,r2=1u<<RETRO_DEVICE_ID_JOYPAD_R2;
    const uint16_t l=1u<<RETRO_DEVICE_ID_JOYPAD_L,l2=1u<<RETRO_DEVICE_ID_JOYPAD_L2;
    const uint16_t l3=1u<<RETRO_DEVICE_ID_JOYPAD_L3;
    const uint16_t b=1u<<RETRO_DEVICE_ID_JOYPAD_B;
    const uint16_t y=1u<<RETRO_DEVICE_ID_JOYPAD_Y,x=1u<<RETRO_DEVICE_ID_JOYPAD_X;
    uint16_t native=(uint16_t)(raw&~(r|r2|l|l3));
    if(raw&r2)native|=r;
    if((raw&l3)&&(raw&(x|y)))native|=l2;
    if((raw&l2)&&(raw&(b|y)))native&=(uint16_t)~l2;
    return native;
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
    if(hundred_until&&port==0&&(device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0){
        const uint16_t want=(uint16_t)(((frame_number/4ul)&1ul)?1u<<RETRO_DEVICE_ID_JOYPAD_B:0u);
        if(bitmask)return (int16_t)want;
        return id<16u&&(want&(1u<<id))?1:0;
    }
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
    if(port==0&&(device&RETRO_DEVICE_MASK)==RETRO_DEVICE_JOYPAD&&index==0&&
       on_turbo&&!ring_ai_enabled&&!som_manual_disciplines&&som_y_field_play()){
        if(bitmask)actual=(int16_t)som_manual_pad_layout((uint16_t)actual);
        else if(id==RETRO_DEVICE_ID_JOYPAD_R||id==RETRO_DEVICE_ID_JOYPAD_R2||
                id==RETRO_DEVICE_ID_JOYPAD_L||id==RETRO_DEVICE_ID_JOYPAD_L2||
                id==RETRO_DEVICE_ID_JOYPAD_L3){
            /* Identical behavior on per-button and bitmask-based cores. */
            static const unsigned watched[]={
                RETRO_DEVICE_ID_JOYPAD_R,RETRO_DEVICE_ID_JOYPAD_R2,
                RETRO_DEVICE_ID_JOYPAD_L,RETRO_DEVICE_ID_JOYPAD_L2,
                RETRO_DEVICE_ID_JOYPAD_L3,RETRO_DEVICE_ID_JOYPAD_B,
                RETRO_DEVICE_ID_JOYPAD_Y,RETRO_DEVICE_ID_JOYPAD_X};
            uint16_t raw=0;
            for(unsigned k=0;k<sizeof(watched)/sizeof(watched[0]);k++)
                if(physical_state&&physical_state(0,RETRO_DEVICE_JOYPAD,0,watched[k]))
                    raw|=(uint16_t)(1u<<watched[k]);
            actual=(som_manual_pad_layout(raw)&(1u<<id))?1:0;
        }
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
            fprintf(stderr,"[som-ai] SPECIAL cancelled by manual input\n");
            special_stage=0;special_suppress=0;
        }
        if(special_suppress&&!(current&special_chord))special_suppress=0;
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
        const int field=som_y_field_play();
        /* Seals: with L2 held, D-pad signs become seals; B/Y charges.
         * R2 remains the independent physical guard. */
        const uint16_t dpad=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_UP)|
            (1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)|(1u<<RETRO_DEVICE_ID_JOYPAD_LEFT)|
            (1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT));
        if(field&&(current&l2_bit)&&!(current&r2)&&!ring_ai_enabled&&
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
        uint8_t *wram=field?(uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL;
        /* L1 is lock-on only. R1 tap cycles next style. L3+B retains the
         * previous L1+B modifier, while L3+Y/X accesses the native rings. */
        const uint16_t l1_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L);
        const uint16_t l3_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_L3);
        const uint16_t r1_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_R);
        const uint16_t x_ring_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);
        const int native_ring=(current&l3_bit)&&(current&(x_ring_bit|y_bit));
        style_b_hidden=field&&(current&(l3_bit|b_bit))==(l3_bit|b_bit)&&
            !ring_ai_enabled&&!som_manual_disciplines&&
            disciplines.selected<SOM_DISCIPLINE_COUNT;
        if(wram&&!ring_ai_enabled&&!som_manual_disciplines&&(pressed&l1_bit))
            som_lock_toggle(wram); /* immediate L1 tap, no B/Y dependency */
        if(wram&&!ring_ai_enabled&&!som_manual_disciplines&&
           (pressed&r1_bit)&&!(current&(l2_bit|r2)))
            som_style_cycle(-1);
        if(style_b_hidden&&(pressed&b_bit))som_style_cycle(-1);
        if(style_b_hidden)b_weapon_blocked=1;
        /* Samurai's throw: R2 (block) + D-pad away from the facing. */
        if(wram&&disciplines.selected==SOM_DISC_BUSHIDO&&!ring_ai_enabled&&
           (current&r2)&&(pressed&dpad)&&
           som_throw_back_pressed(wram,pressed))
            som_throw_start(wram);
        /* Casters: B casts the first spell of the class, Y the second; the
         * game sees neither. B with too little MP stays the weapon swing. */
        const int caster=wram&&disciplines.selected<SOM_DISCIPLINE_COUNT&&
            som_disc_caster(disciplines.selected)&&!ring_ai_enabled&&!som_manual_disciplines&&
            !native_ring;
        if(caster&&(current&(y_bit|b_bit))&&!(current&(l2_bit|r2))&&!special_suppress){
            const unsigned which=(current&y_bit)?1u:0u;
            const unsigned spell=som_class_spell(disciplines.selected,which);
            if(which||som_spell_affordable(wram,spell)){
                if(which)y_weapon_blocked=1;else b_weapon_blocked=1;
                if(pressed&(which?y_bit:b_bit)){
                    if(som_cast_spell(wram,spell))
                        fprintf(stderr,"[som-ai] CASTER %s: %s\n",which?"Y":"B",
                                som_spell_label(spell));
                }
            }
        }
        if(wram&&!caster&&!style_b_hidden&&!native_ring&&(current&(y_bit|b_bit))&&
           !(current&(l2_bit|r2))&&!special_suppress){
            const unsigned which=(current&y_bit)?1u:0u; /* Y: secondary, B: primary */
            unsigned want=som_disc_weapons[disciplines.selected][which];
            if(!which&&disciplines.selected==SOM_DISC_NINJUTSU){
                /* Taijutsu: the chain's weapon is chosen on the press and
                 * kept while B stays held (a held B may be charging). */
                if((pressed&b_bit)||taijutsu_want==SOM_NO_WEAPON){
                    taijutsu_want=som_taijutsu_peek();taijutsu_counted=0;
                }
                want=taijutsu_want;
            }
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
            /* The swing this press lets through is the chain's: next step. */
            if(!which&&disciplines.selected==SOM_DISC_NINJUTSU&&result==SOM_EQUIP_OK&&
               taijutsu_want!=SOM_NO_WEAPON&&!taijutsu_counted){
                som_taijutsu_advance();taijutsu_counted=1;
                fprintf(stderr,"[som-ai] TAIJUTSU %s\n",som_weapon_types[taijutsu_want]);
            }
        }
        if(!(current&b_bit))taijutsu_want=SOM_NO_WEAPON;
        /* X/Triangle casts the selected spell; L2+X keeps the game's X. */
        const uint16_t x_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);
        if((current&x_bit)&&(current&(l2_bit|l3_bit)))x_l2_chord=1;
        /* A: evade / jutsu for the disciplines with a dodge; the game's A
         * dash is gone for them until dash is re-added. Other classes keep
         * the native A. Field play only (menus keep A as cancel). */
        const uint16_t a_bit=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_A);
        evade_a_hidden=wram&&!ring_ai_enabled&&!som_manual_disciplines&&
            disciplines.selected<SOM_DISCIPLINE_COUNT&&som_disc_has_dodge(disciplines.selected);
        if(evade_a_hidden&&(pressed&a_bit)){
            a_press_frame=frame_number;a_hold_done=0;
            if(disciplines.selected==0u&&(current&r2)){som_bash_start(wram);a_hold_done=1;}
            /* Samurai: A with a monster in grab reach grabs and throws it
             * (no Kawarimi: a ninja skill); otherwise A is the evade. */
            else if(disciplines.selected==SOM_DISC_BUSHIDO&&
                    som_bun_toucher(wram,(int)som_bun_u16(wram+0xE002),(int)som_bun_u16(wram+0xE004),
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
                som_evade_start(wram);
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
            else if(disciplines.selected!=SOM_DISC_NINJUTSU)a_kawarimi_pending=0;
            else if(som_kawarimi_arm(wram)){
                a_kawarimi_pending=0;
                fprintf(stderr,"[som-ai] KAWARIMI from held A\n");
            }
        }
        /* Combo finisher: hold B or Y with a chain level ready. That button's
         * weapon charges straight to the level (capped at what it learned)
         * once the gauge is back, then fires as the game's own power attack. */
        if(wram&&combo_input_held&&!(current&(l2_bit|r2|l3_bit))&&
           !special_stage&&!special_suppress&&
           !ring_ai_enabled&&!som_manual_disciplines&&!bash_stage&&!combo_hold_tried&&
           disciplines.selected<SOM_DISCIPLINE_COUNT&&som_combo_effective_level()&&
           frame_number-combo_hold_start>=SOM_COMBO_HOLD_FRAMES&&
           !wram[0xE1ED]&&!wram[0xE19B]&&!wram[0xE01C]){
            const unsigned which=(current&y_bit)?1u:0u;
            const unsigned level=som_combo_effective_level(),hits=combo.hits;
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
        const unsigned x_role=disciplines.selected<SOM_DISCIPLINE_COUNT?
            som_disc_x_role(disciplines.selected):SOM_X_SPELLS;
        if(x_role==SOM_X_THIRD_WEAPON){
            /* Weapon Flow: X equips the third weapon and attacks with it
             * (the game sees B, so holding X charges it). */
            if(wram&&(current&x_bit)&&!(current&(l2_bit|r2))&&!native_ring&&!special_suppress){
                const unsigned want=som_disc_third_weapon(disciplines.selected);
                const int result=wram[0xE1E4]==want?SOM_EQUIP_OK:
                    (pressed&x_bit)?som_equip_weapon(wram,want):som_equip_weapon_direct(wram,want);
                if(result==SOM_EQUIP_OK)x_weapon_swap=1;else x_spell_blocked=1;
            }
        }else if(x_role==SOM_X_FLYING_KICK){
            /* Martial Arts: X is the flying kick (the game never sees X). */
            if(wram&&(current&x_bit)&&!(current&(l2_bit|r2))&&!native_ring&&!special_suppress){
                x_spell_blocked=1;
                if(pressed&x_bit)som_flykick_start(wram);
            }
        }else if(x_role>=SOM_X_HOLY_WATER){
            /* Hunter, Necromancer, Artificer, Mentalist: X is the class move. */
            if(wram&&(current&x_bit)&&!(current&(l2_bit|r2))&&!native_ring&&!special_suppress){
                x_spell_blocked=1;
                if(pressed&x_bit)som_class_x(wram);
            }
        }else if(x_role==SOM_X_IAI){
            /* Bushido: release X for Iaijutsu, katana charge level 3; held
             * SOM_X_HOLD_FRAMES or longer, level 4. No spells. */
            if(wram&&(current&x_bit)&&!(current&(l2_bit|r2))&&!native_ring&&!special_suppress){
                x_spell_blocked=1;
                if(pressed&x_bit){x_press_frame=frame_number;x_cycled=0;}
                else if(!x_cycled&&frame_number-x_press_frame>=SOM_X_HOLD_FRAMES){
                    x_cycled=1;
                    som_overlay_set(&overlay,"^Iaijutsu|level 4: release X",SOM_MESSAGE_FRAMES/2u);
                }
            }else if(wram&&(last_physical_buttons&x_bit)&&!(current&x_bit)&&
                     !(current&(l2_bit|r2))&&!native_ring&&!special_suppress&&!x_l2_chord){
                som_iai_start(x_cycled?SOM_IAI_HELD_LEVEL:SOM_IAI_LEVEL);
                x_cycled=0;
            }
        }else
        /* Tap X: cast on release. Hold X: next spell every 0.5 s. */
        if(wram&&(current&x_bit)&&!(current&(l2_bit|r2))&&!native_ring&&!special_suppress){
            x_spell_blocked=1;
            if(pressed&x_bit){x_press_frame=frame_number;x_cycled=0;}
            else if(frame_number-x_press_frame>=SOM_X_HOLD_FRAMES){
                som_next_spell();x_cycled=1;x_press_frame=frame_number;
                char text[64];
                snprintf(text,sizeof(text),"^%s|tap X: cast",som_spell_label(som_current_spell()));
                som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES);
            }
        }else if(wram&&(last_physical_buttons&x_bit)&&!(current&x_bit)&&
                 !(current&(l2_bit|r2))&&!native_ring&&!special_suppress&&!x_l2_chord){
            if(!x_cycled)som_cast_spell(wram,som_current_spell());
            x_cycled=0;
        }
        /* L2+X: Whirlwind, Shadowstep or Volley. */
        if(wram&&!ring_ai_enabled&&!som_manual_disciplines&&
           (disciplines.selected==SOM_DISC_SWORD||disciplines.selected==SOM_DISC_ROGUE||
            disciplines.selected==SOM_DISC_ARCHER||disciplines.selected==SOM_DISC_HUNTER||
            disciplines.selected==SOM_DISC_ARTIFICER)&&
           (current&l2_bit)&&(current&x_bit)&&!(current&r2)&&!special_suppress){
            x_spell_blocked=1;
            if(pressed&x_bit)som_class_move(wram);
        }
        /* Mentalist: L2+X pulls a native unopened chest toward the hero.
         * X without L2 remains Mind Control. */
        if(wram&&!ring_ai_enabled&&!som_manual_disciplines&&
           disciplines.selected==SOM_DISC_PSIONICS&&
           (current&l2_bit)&&(current&x_bit)&&!(current&r2)&&!special_suppress){
            x_spell_blocked=1;
            if(pressed&x_bit)som_telekinesis_cast(wram);
        }
        /* L2+X: Hundred Fists for the monk. */
        if(wram&&!ring_ai_enabled&&!som_manual_disciplines&&
           disciplines.selected==SOM_DISC_MARTIAL&&
           (current&l2_bit)&&(current&x_bit)&&!(current&r2)&&!special_suppress){
            x_spell_blocked=1;
            if(pressed&x_bit)som_hundred_start(wram);
        }
        /* L2+X: Issen for the samurai (the game never sees that X). */
        if(wram&&!ring_ai_enabled&&!som_manual_disciplines&&
           disciplines.selected==SOM_DISC_BUSHIDO&&
           (current&l2_bit)&&(current&x_bit)&&!(current&r2)&&!special_suppress){
            x_spell_blocked=1;
            if(pressed&x_bit)som_issen_start(wram);
        }
        /* L2+X: Shunshin no Jutsu (a clone on each side) for a ninja with a
         * short-range weapon in hand; the game never sees that X. Every
         * other case keeps L2+X reaching the game. */
        if(wram&&!ring_ai_enabled&&!som_manual_disciplines&&
           disciplines.selected==SOM_DISC_NINJUTSU&&som_weapon_short_range(wram[0xE1E4])&&
           (current&l2_bit)&&(current&x_bit)&&!(current&r2)&&!special_suppress){
            x_spell_blocked=1;
            if((pressed&x_bit)&&som_shunshin_start(wram))
                fprintf(stderr,"[som-ai] SHUNSHIN from L2+X\n");
        }
        if(!(current&x_bit))x_l2_chord=0;
        /* Start opens Class Progress manually; retain L2+R2 as an alias.
         * While open, all pad input is consumed by the overlay. */
        const uint16_t class_chord=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|
                                              (1u<<RETRO_DEVICE_ID_JOYPAD_R2));
        const uint16_t class_start=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_START);
        if(!ring_ai_enabled && !som_manual_disciplines && som_overlay_field() &&
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
        const uint16_t ai_switch=(uint16_t)((1u<<RETRO_DEVICE_ID_JOYPAD_L2)|
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
                /* L1/R1 cycle Actions -> Combat -> Quest -> Disciplines. */
                const int forward=(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_R))!=0;
                if(som_menu_disciplines){
                    som_menu_disciplines=0;
                    gambits.page=forward?SOM_PAGE_ACTIONS:SOM_GAMBIT_PAGES-1u;gambits.cursor=0;
                }else if((forward&&gambits.page==SOM_GAMBIT_PAGES-1u)||
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
            case SOM_GAMBIT_EXIT:
                ring_ai_enabled=0;unlink(ring_ai_file);
                som_overlay_set(&overlay,"You play|hold L2 or R3 1s: AI plays",
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
static SomFamiliarCarry clone_source_art; /* captured before mutating destination */
static int familiar_is_native_control; /* Mind Control never repaints native art */
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
    if(disciplines.selected==SOM_DISC_SUMMONING||disciplines.selected==SOM_DISC_NECRO)return 1;
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
    if(owned[current]!=0xFF)owned[current]&=(uint8_t)~0x40u;  /* no longer held */
    owned[type]|=0x40u;                                        /* held by the boy */
    fprintf(stderr,"[som-ai] WEAPON equipped without the ring: %s (row %u)\n",
            som_weapon_types[type],row);
    return SOM_EQUIP_OK;
}
static int som_equip_weapon_direct(uint8_t *ram,unsigned type){
    return som_equip_weapon_row(ram,type,0);
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
/* Enough MP for the spell (ROM row byte 15 = cost)? */
static int som_spell_affordable(const uint8_t *ram,unsigned spell){
    if(!ram||spell>=SOM_SPELL_COUNT||!som_rom)return 0;
    const uint8_t *row=som_rom_at(0xD02AD0u+64u*spell,16u);
    return row&&ram[0xE186]>=row[15];
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
        const int detonate=bunshin_shunshin&&field&&same_map;
        if(same_map)
            for(unsigned i=0;i<SOM_BUN_COUNT;i++)
                if(bunshin.alive[i]){
                    if(detonate)som_bunshin_explode(ram,bunshin.x[i],bunshin.y[i]);
                    else som_bun_boom(bunshin_booms,bunshin.x[i],bunshin.y[i],1,frame_number);
                }
        som_bun_clear(&bunshin);bunshin_shunshin=0;return;
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
    if(!som_bun_alive(&bunshin)){bunshin_shunshin=0;return;}
    if(bunshin_shunshin){
        /* Shunshin: a monster at the hero sets off every clone at once. */
        if(som_bun_toucher(ram,hx,hy,SOM_BUN_HERO_TOUCH_X,SOM_BUN_HERO_TOUCH_Y,familiar.slot)<0)
            return;
        for(unsigned i=0;i<SOM_BUN_COUNT;i++)
            if(bunshin.alive[i])som_bunshin_explode(ram,bunshin.x[i],bunshin.y[i]);
        som_bun_clear(&bunshin);bunshin_shunshin=0;
        som_overlay_set(&overlay,"^Shunshin|Daibakuha!",SOM_MESSAGE_FRAMES/2u);
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
    const int hx=(int)som_bun_u16(ram+0xE002),hy=(int)som_bun_u16(ram+0xE004);
    const uint8_t *vram=backend_retro_get_memory_size(RETRO_MEMORY_VIDEO_RAM)>=SOM_BUN_VRAM_SIZE?
        backend_retro_get_memory_data(RETRO_MEMORY_VIDEO_RAM):NULL;
    if(vram)
        for(unsigned i=0;i<SOM_BUN_COUNT;i++)
            if(bunshin.alive[i])
                som_bun_draw_copy(pixels,width,height,pitch,pixel_format,ram,vram,
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
    const unsigned weapon=bash_kick?0u:1u;           /* glove for the kick, else sword */
    if(som_equip_weapon(ram,weapon)!=SOM_EQUIP_OK)return 0;
    bash_stage=SOM_BASH_LUNGE;bash_started=frame_number;
    som_overlay_set(&overlay,bash_kick?"^Flying kick!":"^Bash!",SOM_MESSAGE_FRAMES/3u);
    fprintf(stderr,"[som-ai] %s lunge\n",bash_kick?"FLYING KICK":"BASH");
    return 1;
}
/* Martial Arts' X: see SOM_FLYKICK_LIFT. Returns 1 when it started. */
static int som_flykick_start(uint8_t *ram){
    if(!ram||disciplines.selected!=SOM_DISC_MARTIAL_ARTS||flykick.active)return 0;
    bash_kick=1;
    if(!som_bash_start(ram)){bash_kick=0;return 0;}
    if(!cartwheel_pack_tried){
        cartwheel_pack_tried=1;
        som_load_pack_file(SOM_CARTWHEEL_PACK_FILE,"Hero moves",&cartwheel_pack,&cartwheel_pack_size);
    }
    uint8_t *hero=ram+0xE000u;
    const unsigned id=hero[0x180],facing=hero[0x010];
    const unsigned pack_facing=(facing&0x7Fu)==0x02u?2u:facing==0x01u?1u:0u;
    SomWorkEntry entry;
    if(cartwheel_pack&&id>=0x80u&&id<=0x82u&&
       som_work_find(cartwheel_pack,cartwheel_pack_size,SOM_FLYKICK_KIND+id-0x80u,pack_facing,&entry)){
        memset(&flykick,0,sizeof(flykick));
        flykick.active=1;flykick.slot=0;flykick.kind=id;
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
    if(ram&&flykick_hid)ram[0xE00E]&=(uint8_t)~0x80u;
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
    const int game_over=!ram||ram[0xE000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F;
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
    som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES/2u);
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
            fuma_damage+=som_perk_bonus((unsigned)fuma_target,fuma_damage,"fuma strike");
            monster_hit_frame[fuma_target]=frame_number;
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
static int som_blink_start(uint8_t *ram,int tx,int ty){
    if(!ram||blink_active||evade_active||bash_stage||special_stage)return 0;
    if(frame_number<blink_ready){
        fprintf(stderr,"[som-ai] BLINK refused: ready in %lu frames\n",blink_ready-frame_number);
        return 0;
    }
    uint8_t *hero=ram+0xE000u;
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
        found=som_bun_spot_open(ram,x,y,som_hero_place(hero));
    }
    if(!found){
        fprintf(stderr,"[som-ai] BLINK refused: tile %d,%d is not open ground\n",tx,ty);
        return 0;
    }
    som_blink_go(ram,x,y);
    som_overlay_set(&overlay,"^Blink",SOM_MESSAGE_FRAMES/3u);
    fprintf(stderr,"[som-ai] BLINK from %d,%d to tile %d,%d (%d,%d)\n",x0,y0,tx,ty,x,y);
    return 1;
}
/* The hidden straight-line travel shared by Blink and the Fuma swap. */
static int som_blink_go(uint8_t *ram,int x,int y){
    uint8_t *hero=ram+0xE000u;
    blink_active=1;blink_tx=x;blink_ty=y;
    blink_hid=!(hero[0x0E]&0x80u);
    hero[0x0E]|=0x80u;
    som_bun_boom(bunshin_booms,(int)som_bun_u16(hero+2),(int)som_bun_u16(hero+4),1,frame_number);
    return 1;
}
static int som_fuma_swap(uint8_t *ram){
    if(!ram||!fuma.active||frame_number<fuma.start||blink_active||
       disciplines.selected!=SOM_DISC_NINJUTSU)return 0;
    uint8_t *hero=ram+0xE000u;
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
    som_overlay_set(&overlay,"^Fuma|swap",SOM_MESSAGE_FRAMES/3u);
    fprintf(stderr,"[som-ai] FUMA swap: hero takes the clone's place at %d,%d\n",x,y);
    return 1;
}
/* Each frame of a blink: SOM_BLINK_SPEED px per axis toward the landing
 * spot, camera along; smoke and the hero back on arrival. */
static void som_blink_frame(uint8_t *ram){
    if(!blink_active)return;
    uint8_t *hero=ram?ram+0xE000u:NULL;
    const int field=ram&&hero[0]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F&&ram[0xD0]==0;
    int x=field?(int)som_bun_u16(hero+2):0,y=field?(int)som_bun_u16(hero+4):0;
    if(field){
        const int dx=blink_tx-x,dy=blink_ty-y;
        const int sx=dx>SOM_BLINK_SPEED?SOM_BLINK_SPEED:dx<-SOM_BLINK_SPEED?-SOM_BLINK_SPEED:dx;
        const int sy=dy>SOM_BLINK_SPEED?SOM_BLINK_SPEED:dy<-SOM_BLINK_SPEED?-SOM_BLINK_SPEED:dy;
        x+=sx;y+=sy;
        hero[2]=(uint8_t)x;hero[3]=(uint8_t)(x>>8);hero[4]=(uint8_t)y;hero[5]=(uint8_t)(y>>8);
        hero[0x006]=hero[0x007]=0;
        som_camera_follow(ram,sx,sy);
        if(x!=blink_tx||y!=blink_ty)return;
        som_bun_boom(bunshin_booms,x,y,1,frame_number);
        fprintf(stderr,"[som-ai] BLINK arrived at %d,%d\n",x,y);
    }
    if(ram&&blink_hid)ram[0xE00E]&=(uint8_t)~0x80u;
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
    uint8_t *hero=ram+0xE000u;
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
                som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES/2u);
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
    const uint8_t *hero=ram+0xE000u;
    return !(hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||
             hero[0x060]||hero[0x01C]==0x40||hero[0x01C]==0x80||
             (som_word(hero+0x190)&0xC160u));
}
/* A shadow clone on each side of the ninja, across his facing (clone,
 * ninja, clone): Kage Bunshin (long-range weapon, on A; swap) and Shunshin
 * (short-range weapon, AI; explosive, bunshin_shunshin). */
static int som_clone_jutsu(uint8_t *ram,const char *jutsu){
    uint8_t *hero=ram+0xE000u;
    const unsigned made=som_bun_spawn(&bunshin,ram,(int)som_bun_u16(hero+2),
        (int)som_bun_u16(hero+4),hero[0x010],som_hero_place(hero),frame_number);
    if(!made){
        som_overlay_set(&overlay,"^No room|for shadow clones",SOM_MESSAGE_FRAMES/2u);
        return 0;
    }
    for(unsigned i=0;i<SOM_BUN_COUNT;i++)
        if(bunshin.alive[i])som_bun_boom(bunshin_booms,bunshin.x[i],bunshin.y[i],1,frame_number);
    bunshin_shunshin=!strcmp(jutsu,"Shunshin no Jutsu");
    char title[40];snprintf(title,sizeof(title),"^%s",jutsu);
    som_overlay_set(&overlay,title,SOM_MESSAGE_FRAMES/2u);
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
    if(ram&&ukemi_hid&&som_bun_u16(ram+0xDCu)==ukemi_map&&ram[0xE000u]==1u)
        ram[0xE00Eu]&=(uint8_t)~0x80u;
    ukemi_draw.active=0;ukemi_hid=0;
}
static int som_ukemi_begin_draw(uint8_t *ram,int dx,int dy){
    if(!cartwheel_pack_tried){
        cartwheel_pack_tried=1;
        som_load_pack_file(SOM_CARTWHEEL_PACK_FILE,"Hero moves",
                           &cartwheel_pack,&cartwheel_pack_size);
    }
    uint8_t *hero=ram+0xE000u;
    const unsigned id=hero[0x180u];
    if(!cartwheel_pack||id<0x80u||id>0x82u)return 0;
    const unsigned facing=dx?2u:(dy>0?1u:0u);
    SomWorkEntry entry;
    if(!som_work_find(cartwheel_pack,cartwheel_pack_size,
                      SOM_UKEMI_DRAW_KIND+id-0x80u,facing,&entry))return 0;
    memset(&ukemi_draw,0,sizeof(ukemi_draw));
    ukemi_draw.active=1;ukemi_draw.slot=0;ukemi_draw.kind=id;
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
    if(!ram||!evade_active||ram[0xE000u]!=1u||
       som_bun_u16(ram+0xDCu)!=ukemi_map||
       frame_number-ukemi_draw.start>=ukemi_draw.length)
        som_ukemi_end(ram);
}
static int som_cartwheel_request(uint8_t *ram,int dx,int dy,unsigned code){
    uint8_t *hero=ram+0xE000u;
    if(!(ram[0xED]&0x80u)||hero[0x1E5]!=0xFFu||hero[0x01C]==0x40u)return 0;
    if(som_word(hero+0x190)&0xC570u)return 0;
    if(dx)hero[0x010]=dx>0?0x82u:0x02u;
    else if(dy)hero[0x010]=dy>0?0x01u:0x00u;
    hero[0x1E5]=(uint8_t)code;
    return 1;
}
/* The camera follows a dodge. The game scrolls only by the hero's own
 * walking steps, so a hero carried by the proxy left it behind (offline
 * replay of a field state, 2026-11-18: 48 px hop, camera A8 unmoved). Each
 * camera word moves with its BG scroll word, A8/B0 and AA/B2 (BG1), AC/B4
 * and AE/B6 (BG2, the second camera); moving A8 alone shifted the sprites
 * but not the ground. Clamped to the map (C0/C2 minus the screen); new
 * tile rows stream in by themselves (checked at 4 px per frame). */
static void som_camera_follow(uint8_t *ram,int dx,int dy){
    const int w=(int)som_bun_u16(ram+0xC0),h=(int)som_bun_u16(ram+0xC2);
    const int cx=(int)som_bun_u16(ram+0xA8),cy=(int)som_bun_u16(ram+0xAA);
    int nx=cx+dx,ny=cy+dy;
    if(w>=256&&w<=4096){if(nx>w-256)nx=w-256;if(nx<0)nx=0;}else nx=cx;
    if(h>=224&&h<=4096){if(ny>h-224)ny=h-224;if(ny<0)ny=0;}else ny=cy;
    dx=nx-cx;dy=ny-cy;
    static const unsigned xs[4]={0xA8u,0xB0u,0xACu,0xB4u},ys[4]={0xAAu,0xB2u,0xAEu,0xB6u};
    for(unsigned k=0;k<4u;k++){
        const unsigned vx=(som_bun_u16(ram+xs[k])+(unsigned)dx)&0xFFFFu;
        const unsigned vy=(som_bun_u16(ram+ys[k])+(unsigned)dy)&0xFFFFu;
        ram[xs[k]]=(uint8_t)vx;ram[xs[k]+1u]=(uint8_t)(vx>>8);
        ram[ys[k]]=(uint8_t)vy;ram[ys[k]+1u]=(uint8_t)(vy>>8);
    }
}
/* A (and the AI's "evade"): the class's plain dodge hop. A ninja with the
 * long-range weapon (chakram/boomerang) casts Kage Bunshin instead, and with clones out A swaps
 * him into one. Holding A arms Kawarimi after the hop, and L2+X is
 * Shunshin (also AI commands "kawarimi" / "shunshin"). */
static int som_evade_start(uint8_t *ram){
    if(!ram||evade_active)return 0;
    if(disciplines.selected>=SOM_DISCIPLINE_COUNT||!som_disc_has_dodge(disciplines.selected)){
        som_overlay_set(&overlay,"^No evade|this class cannot dodge",SOM_MESSAGE_FRAMES);
        return 0;
    }
    uint8_t *hero=ram+0xE000u;
    if(!som_jutsu_free(ram))return 0;
    const int ninja=disciplines.selected==2u;
    if(ninja&&fuma.active&&frame_number>=fuma.start)return som_fuma_swap(ram);
    if(ninja&&som_bun_alive(&bunshin)&&!bunshin_shunshin)
        return som_bunshin_swap(ram,0,0,0,evade_dir_x,evade_dir_y);
    if(ninja&&som_weapon_long_range(hero[0x1E4]))return som_clone_jutsu(ram,"Kage Bunshin no Jutsu");
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
    /* Dodge Mastery lengthens the move, not the grace. */
    const unsigned move=dodge.frames+(unsigned)som_perk(SOM_EFFECT_DODGE_FRAMES);
    hop_dx=dx*(int)dodge.speed;hop_dy=dy*(int)dodge.speed;hop_frames_left=move;
    evade_frames=move+SOM_DODGE_GRACE;
    const unsigned facing_before=hero[0x010];
    cartwheel_native=dodge.cartwheel==SOM_REACT_CARTWHEEL&&
        som_cartwheel_request(ram,dx,dy,(unsigned)dodge.cartwheel);
    if(dodge.cartwheel==SOM_REACT_UKEMI&&!som_ukemi_begin_draw(ram,dx,dy))
        fprintf(stderr,"[som-ai] UKEMI pack missing: use som_cartwheel_frames.bin with 0x83 frames\\n");
    native_restore_facing=-1;
    if(cartwheel_native){
        native_code=(unsigned)dodge.cartwheel;
        cartwheel_fallback=move;hop_frames_left=0;
        evade_frames=(native_code==SOM_REACT_UKEMI?SOM_UKEMI_FRAMES:SOM_CARTWHEEL_FRAMES)+
                     (unsigned)som_perk(SOM_EFFECT_DODGE_FRAMES)+SOM_DODGE_GRACE;
        /* Steering it (forward + A: the cartwheel travels away from the
         * facing) turned him round: he faces the old way again after. */
        if(hero[0x010]!=facing_before)native_restore_facing=(int)facing_before;
    }
    hero[0x006]=hero[0x007]=0;
    evade_hp=som_word(hero+0x182);
    evade_active=1;evade_started=frame_number;evade_xp_given=0;
    evade_blink=disciplines.selected==SOM_DISC_MAGIC&&!cartwheel_native;
    if(evade_blink){
        evade_blink_hid=!(hero[0x0E]&0x80u);
        hero[0x0E]|=0x80u;
        som_bun_boom(bunshin_booms,(int)som_bun_u16(hero+2),(int)som_bun_u16(hero+4),1,frame_number);
    }
    char dodge_text[32];snprintf(dodge_text,sizeof(dodge_text),"^%s",
                                 evade_blink?"Blink step":dodge.name);
    som_overlay_set(&overlay,dodge_text,SOM_MESSAGE_FRAMES/2u);
    if(cartwheel_native)
        fprintf(stderr,"[som-ai] EVADE %s as %s: the game's own (pose %02x) toward %d,%d\n",dodge.name,
                som_discipline_names[disciplines.selected],0x80u|native_code,dx,dy);
    else fprintf(stderr,"[som-ai] EVADE %s as %s: %d,%d px per frame for %u frames\n",
                 dodge.name,som_discipline_names[disciplines.selected],hop_dx,hop_dy,move);
    return 1;
}
/* AI "kawarimi": arm the substitution for SOM_KAWARIMI_ARM_FRAMES (the
 * classes with a dodge); som_kawarimi_check fires it when a monster touches
 * or hits him before the window closes. */
static int som_kawarimi_arm(uint8_t *ram){
    if(!ram||evade_active||kawarimi_armed_until)return 0;
    /* Kawarimi is a ninja skill (player's design, 2026-11-20). */
    if(disciplines.selected!=SOM_DISC_NINJUTSU)return 0;
    if(!som_jutsu_free(ram))return 0;
    const SomDodge dodge=som_disc_dodge(disciplines.selected,ram[0xE1E4]);
    if(!dodge.speed)return 0;
    kawarimi_armed_until=frame_number+SOM_KAWARIMI_ARM_FRAMES;
    som_overlay_set(&overlay,"^" SOM_KAWARIMI_NAME "|ready",SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] KAWARIMI armed as %s for %lu frames\n",
            som_discipline_names[disciplines.selected],SOM_KAWARIMI_ARM_FRAMES);
    return 1;
}
/* AI "shunshin": ninja with a short-range weapon in hand, no clones out. */
static int som_shunshin_start(uint8_t *ram){
    if(!ram||evade_active||disciplines.selected!=2u||!som_weapon_short_range(ram[0xE1E4]))return 0;
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
    uint8_t *hero=ram+0xE000u;
    if(frame_number>kawarimi_armed_until||evade_active||disciplines.selected>=SOM_DISCIPLINE_COUNT){
        kawarimi_armed_until=0;return 0;
    }
    if(hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0)return 0;
    const int x0=(int)som_bun_u16(hero+2),y0=(int)som_bun_u16(hero+4);
    const int toucher=som_bun_toucher(ram,x0,y0,SOM_BUN_HERO_TOUCH_X,SOM_BUN_HERO_TOUCH_Y,
                                      familiar.slot);
    const int hit=hero[0x059]||hero[0x1F1]||hero[0x1F2];
    if(toucher<0&&!hit)return 0;
    const SomDodge dodge=som_disc_dodge(disciplines.selected,hero[0x1E4]);
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
    hero[0x006]=hero[0x007]=0;
    kawarimi_armed_until=0;
    hop_dx=hop_dy=0;hop_frames_left=0;
    evade_frames=move+SOM_DODGE_GRACE;
    evade_hp=som_word(hero+0x182);
    evade_active=1;evade_started=frame_number;evade_xp_given=1;
    kawarimi_log_x=x0;kawarimi_log_y=y0;kawarimi_log_start=frame_number?frame_number:1u;
    kawarimi_log_map=som_bun_u16(ram+0xDC);
    som_bun_boom(bunshin_booms,x0,y0,1,frame_number);
    if(x!=x0||y!=y0)som_bun_boom(bunshin_booms,x,y,1,frame_number);
    som_overlay_set(&overlay,"^" SOM_KAWARIMI_NAME "!",SOM_MESSAGE_FRAMES/2u);
    if(toucher>=0)
        som_xp_give(disciplines.selected,SOM_XP_DODGE,
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
    if(!evade_active)return;
    uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram)return;
    uint8_t *hero=ram+0xE000u;
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
    if(!cartwheel_native&&!evade_blink){trail_x[slot]=x;trail_y[slot]=y;trail_frame[slot]=frame_number;trail_until=frame_number+10u;}
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
    const int x0=x,y0=y;
    if(hop_dx&&som_bun_spot_open(ram,x+hop_dx,y,place)){x+=hop_dx;moved=1;}
    if(hop_dy&&som_bun_spot_open(ram,x,y+hop_dy,place)){y+=hop_dy;moved=1;}
    if(moved)som_camera_follow(ram,x-x0,y-y0);
    hero[2]=(uint8_t)x;hero[3]=(uint8_t)(x>>8);hero[4]=(uint8_t)y;hero[5]=(uint8_t)(y>>8);
    hero[0x006]=hero[0x007]=0;
    if(issen_active)som_issen_cut(ram,x,y);
    hop_frames_left=moved?hop_frames_left-1u:0u;
    if(evade_blink&&!hop_frames_left)som_evade_blink_end(ram);
}
static void som_evade_blink_end(uint8_t *ram){
    if(!evade_blink)return;
    evade_blink=0;
    if(!ram)return;
    if(evade_blink_hid)ram[0xE00E]&=(uint8_t)~0x80u;
    evade_blink_hid=0;
    som_bun_boom(bunshin_booms,(int)som_bun_u16(ram+0xE002),(int)som_bun_u16(ram+0xE004),1,frame_number);
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
        issen_hit|=1u<<slot;monster_hit_frame[slot]=frame_number;
        som_bun_boom(bunshin_booms,(int)som_bun_u16(m+2),(int)som_bun_u16(m+4),1,frame_number);
        fprintf(stderr,"[som-ai] ISSEN cut monster %u for %u\n",slot,damage);
    }
}
static void som_class_damage(uint8_t *ram,unsigned slot,unsigned damage){
    uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
    const unsigned pending=m[0x1F1]+damage;
    m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
    monster_hit_frame[slot]=frame_number;
    som_bun_boom(bunshin_booms,(int)som_bun_u16(m+2),(int)som_bun_u16(m+4),1,frame_number);
}
static int som_class_move(uint8_t *ram){
    const unsigned d=disciplines.selected;
    if(!ram||evade_active||blink_active||hundred_until)return 0;
    const char *name=d==SOM_DISC_SWORD?"Whirlwind":d==SOM_DISC_ROGUE?"Shadowstep":
                     d==SOM_DISC_HUNTER?"Grand Cross":d==SOM_DISC_ARTIFICER?"Mine":"Volley";
    if(d==SOM_DISC_ARTIFICER){                 /* a mine where he stands */
        uint8_t *hero=ram+0xE000u;
        unsigned free_slot=SOM_MINES;
        for(unsigned i=0;i<SOM_MINES;i++)if(!mine_live[i]){free_slot=i;break;}
        if(free_slot==SOM_MINES||frame_number<class_move_ready){
            som_overlay_set(&overlay,free_slot==SOM_MINES?"^Mines|all set":"^Mine|not ready",
                            SOM_MESSAGE_FRAMES/3u);
            return 0;
        }
        mine_x[free_slot]=(int)som_bun_u16(hero+2);mine_y[free_slot]=(int)som_bun_u16(hero+4);
        mine_map[free_slot]=som_bun_u16(ram+0xDC);mine_live[free_slot]=1;
        class_move_ready=frame_number+SOM_MINE_COOLDOWN;
        som_bun_boom(bunshin_booms,mine_x[free_slot],mine_y[free_slot],1,frame_number);
        som_overlay_set(&overlay,"^Mine set",SOM_MESSAGE_FRAMES/3u);
        fprintf(stderr,"[som-ai] MINE set at %d,%d\n",mine_x[free_slot],mine_y[free_slot]);
        return 1;
    }
    if(frame_number<class_move_ready){
        char text[40];snprintf(text,sizeof(text),"^%s|not ready",name);
        som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES/3u);return 0;
    }
    if(!som_jutsu_free(ram))return 0;
    uint8_t *hero=ram+0xE000u;
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    unsigned hit=0;
    if(d==SOM_DISC_SWORD){
        hit=som_bun_blast(ram,hx,hy,14u+2u*disciplines.level[SOM_DISC_SWORD],familiar.slot);
        for(unsigned slot=0;slot<3u;slot++)if(monster_hit_frame[slot]!=frame_number&&(int)slot!=familiar.slot){
            const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
            if(som_bun_monster_alive(m)&&som_bun_near((int)som_bun_u16(m+2),(int)som_bun_u16(m+4),
                                                      hx,hy,SOM_BUN_BLAST_X,SOM_BUN_BLAST_Y))
                monster_hit_frame[slot]=frame_number;
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
        if(best<0){som_overlay_set(&overlay,"^Shadowstep|no target",SOM_MESSAGE_FRAMES/3u);return 0;}
        const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)best;
        const int mx=(int)som_bun_u16(m+2),my=(int)som_bun_u16(m+4);
        int bx=mx,by=my;
        if(abs(mx-hx)>=abs(my-hy))bx+=mx>=hx?SOM_SHADOW_BEHIND:-SOM_SHADOW_BEHIND;
        else by+=my>=hy?SOM_SHADOW_BEHIND:-SOM_SHADOW_BEHIND;
        if(!som_bun_spot_open(ram,bx,by,som_hero_place(hero))){
            som_overlay_set(&overlay,"^Shadowstep|no room behind it",SOM_MESSAGE_FRAMES/3u);return 0;
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
    som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] %s: %u target(s)\n",name,hit);
    return 1;
}
/* L2+X (monk): Hundred Fists. */
static int som_hundred_start(uint8_t *ram){
    if(!ram||disciplines.selected!=SOM_DISC_MARTIAL||hundred_until||evade_active)return 0;
    if(frame_number<hundred_ready){
        som_overlay_set(&overlay,"^Hundred Fists|not ready",SOM_MESSAGE_FRAMES/3u);return 0;
    }
    if(!som_jutsu_free(ram))return 0;
    hundred_until=frame_number+SOM_HUNDRED_FRAMES;
    hundred_ready=frame_number+SOM_HUNDRED_COOLDOWN;
    som_overlay_set(&overlay,"^Hundred Fists!",SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] HUNDRED FISTS\n");
    return 1;
}
static void som_hundred_frame(uint8_t *ram){
    uint8_t *hero=ram+0xE000u;
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
        monster_hit_frame[slot]=frame_number;
    }
}
/* L2+X (samurai): Issen, the dash-cut. */
static int som_issen_start(uint8_t *ram){
    if(!ram||disciplines.selected!=SOM_DISC_BUSHIDO||evade_active)return 0;
    if(frame_number<issen_ready){
        som_overlay_set(&overlay,"^Issen|not ready",SOM_MESSAGE_FRAMES/3u);return 0;
    }
    if(!som_jutsu_free(ram))return 0;
    uint8_t *hero=ram+0xE000u;
    const unsigned facing=hero[0x010];
    int dx=0,dy=0;
    if(facing==0x02u)dx=1;else if(facing==0x82u)dx=-1;else if(facing==0x01u)dy=1;else dy=-1;
    hop_dx=dx*SOM_ISSEN_SPEED;hop_dy=dy*SOM_ISSEN_SPEED;hop_frames_left=SOM_ISSEN_FRAMES;
    evade_frames=SOM_ISSEN_FRAMES+SOM_DODGE_GRACE;cartwheel_native=0;cartwheel_fallback=0;
    hero[0x006]=hero[0x007]=0;
    evade_hp=som_word(hero+0x182);
    evade_active=1;evade_started=frame_number;evade_xp_given=0;
    issen_active=1;issen_hit=0;issen_ready=frame_number+SOM_ISSEN_COOLDOWN;
    som_overlay_set(&overlay,"^Issen!",SOM_MESSAGE_FRAMES/2u);
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
    som_evade_blink_end(ram);
    if(native_restore_facing>=0){ram[0xE010]=(uint8_t)native_restore_facing;native_restore_facing=-1;}
    som_ukemi_end(ram);
    evade_active=0;hop_frames_left=0;cartwheel_native=0;issen_active=0;
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
    fprintf(stderr,"[som-ai] SPECIAL charging %s level %u\n",
            som_weapon_types[weapon],stage);
    return 1;
}
/* Bushido's X (and the AI's iai_<3|4>): the katana's charge level
 * straight away, one per SOM_IAI_COOLDOWN_FRAMES. */
static int som_iai_start(unsigned level){
    if(disciplines.selected!=SOM_DISC_BUSHIDO)return 0;
    if(frame_number<iai_ready){
        som_overlay_set(&overlay,"^Iaijutsu|not ready",SOM_MESSAGE_FRAMES/3u);
        return 0;
    }
    if(!som_special_start(0,level))return 0;
    iai_ready=frame_number+(unsigned long)SOM_IAI_COOLDOWN_FRAMES*
              (unsigned long)(100-som_perk(SOM_EFFECT_IAI_COOLDOWN_CUT))/100ul;   /* Iai Focus */
    special_chord=(uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_X);special_suppress=1;
    char text[40];snprintf(text,sizeof(text),"^Iaijutsu|level %u",special_stage);
    som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] IAI level %u\n",special_stage);
    return 1;
}
static void som_special_tick(void){
    if(!special_stage||!backend_retro_get_memory_data)return;
    uint8_t *ram=backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if(!ram||ram[0xE000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||
       ram[0xD0]!=0||ram[0xE1E4]!=special_weapon||
       frame_number-special_started>SOM_SPECIAL_TIMEOUT){
        fprintf(stderr,"[som-ai] SPECIAL abort: state changed or charge timeout\n");
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
    for(unsigned i=0;i<3;i++){
        /* the third: Ninjutsu's glove for Taijutsu */
        unsigned type=i<2u?som_disc_weapons[discipline][i]:
            discipline==SOM_DISC_NINJUTSU?0u:SOM_NO_WEAPON;
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
static void som_style_cycle(int to){
    const unsigned cls=disciplines.selected;
    if(cls>=SOM_DISCIPLINE_COUNT)return;
    styles.selected[cls]=(uint8_t)(to>=0?(unsigned)to%SOM_STYLES_PER_CLASS:
                                   (styles.selected[cls]+1u)%SOM_STYLES_PER_CLASS);
    unsigned index;
    const SomWeaponStyle *st=som_style_of(&styles,cls,&index);
    char text[64];
    snprintf(text,sizeof(text),"^%s|Lv%u %s",st->name,(unsigned)styles.level[index],
             st->speed_pct>100u?"fast":st->reach_bonus>0||st->power_pct>100u?"heavy":"balanced");
    som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] STYLE %s: %s Lv%u (speed %u%%, power %u%%, reach +%d)\n",
            som_discipline_names[cls],st->name,(unsigned)styles.level[index],
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
        som_overlay_set(&overlay,text,SOM_MESSAGE_FRAMES);
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
                                 som_class_arms[cls][slot].power_pct};
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
        monster_hit_frame[i]=frame_number;
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
    switch(ram[0xE010]){
    case 0x00u: back=RETRO_DEVICE_ID_JOYPAD_DOWN;break;
    case 0x01u: back=RETRO_DEVICE_ID_JOYPAD_UP;break;
    case 0x02u: back=RETRO_DEVICE_ID_JOYPAD_LEFT;break;
    case 0x82u: back=RETRO_DEVICE_ID_JOYPAD_RIGHT;break;
    default: return 0;
    }
    return (pressed&(1u<<back))!=0;
}
static int som_throw_start(uint8_t *ram){
    if(!ram||throw_active||frame_number<throw_ready||disciplines.selected!=SOM_DISC_BUSHIDO||
       ram[0xE000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0)return 0;
    const uint8_t *hero=ram+0xE000u;
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
    som_overlay_set(&overlay,"^Throw!",SOM_MESSAGE_FRAMES/3u);
    fprintf(stderr,"[som-ai] THROW monster %d from %d,%d to %d,%d\n",slot,mx,my,tx,ty);
    return 1;
}
static void som_throw_frame(uint8_t *ram){
    if(!throw_active)return;
    if(!ram||throw_slot<0||ram[0xE000]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F){throw_active=0;return;}
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
        monster_hit_frame[throw_slot]=frame_number;
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
#define SOM_ITEM_HERB 1u
#define SOM_STATUS_POISON 0x2000u
static int som_item_field(const uint8_t *ram,const char *what){
    if(ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0||ram[0xE000]!=1){
        fprintf(stderr,"[som-ai] %s refused: not in field play\n",what);return 0;
    }
    return 1;
}
static int som_use_candy(uint8_t *ram){
    if(!ram||!som_item_field(ram,"CANDY"))return 0;
    const unsigned hp=ram[0xE182]|ram[0xE183]<<8,max_hp=ram[0xE184]|ram[0xE185]<<8;
    if(!hp||hp>max_hp){fprintf(stderr,"[som-ai] CANDY refused: hero HP %u/%u\n",hp,max_hp);return 0;}
    const int left=som_item_take(ram,SOM_ITEM_CANDY);
    if(left<0){fprintf(stderr,"[som-ai] CANDY refused: no Candy\n");return 0;}
    const unsigned heal=ram[0xE1F3]+SOM_CANDY_HEAL;
    ram[0xE1F3]=(uint8_t)(heal>0xFFu?0xFFu:heal);
    fprintf(stderr,"[som-ai] CANDY used without the ring: %d left, +%u HP queued\n",
            left,SOM_CANDY_HEAL);
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
    if(action==SOM_ACT_ATTACK)som_combo_swing(0);
    if(action==SOM_ACT_ATTACK&&disciplines.selected==SOM_DISC_NINJUTSU&&
       backend_retro_get_memory_data){
        uint8_t *ram=(uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
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
        som_overlay_set(&overlay,"WEAPON TRACE ON|Equip via native ring",SOM_MESSAGE_FRAMES);
        fprintf(stderr,"[som-ai] WEAPON TRACE armed; equip via native ring\n");
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
        if(!strcmp(command+6,"next")){som_style_cycle(-1);return 1;}
        if(command[6]>='0'&&command[6]<'0'+(int)SOM_STYLES_PER_CLASS&&!command[7]){
            som_style_cycle(command[6]-'0');return 1;
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
    if(!strcmp(command,"use_candy"))
        return som_use_candy(backend_retro_get_memory_data?
            (uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM):NULL);
    if(!strcmp(command,"use_herb"))
        return som_use_herb(backend_retro_get_memory_data?
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
static void som_video_refresh_inner(const void *data,unsigned width,unsigned height,size_t pitch);
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
static void som_video_refresh_inner(const void *data,unsigned width,unsigned height,size_t pitch){
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
    char combo_text[24];som_combo_text_at(&combo,som_combo_effective_level(),combo_text);
    if(menu)combo_text[0]=0;
    const int clones=!menu&&(som_bun_alive(&bunshin)||som_bunshin_booming()||
                             kawarimi_log_start||frame_number<=trail_until);
    int hero_x=-1,hero_y=-1;
    const int hero_drawn=(combo_text[0]||clones)&&som_hero_screen(&hero_x,&hero_y);
    const int worker=(npc_work.active||flykick.active||ukemi_draw.active||companion.draw.active||
                      (fuma.active&&frame_number>=fuma.start))&&!menu;
    int general_x=-1,general_y=-1;
    char general_text[40]={0};
    if(!menu&&general_count&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE&&
       som_general_label_at((const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM),
                            0,&general_x,&general_y))
        som_gen_label(&generals[0],general_text,sizeof(general_text));
    int lock_x=-1,lock_y=-1;
    if(!menu&&lock_slot>=0&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE){
        const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        const uint8_t *o=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)lock_slot;
        const unsigned cam=(o[0x0B]&0x80u)?0xACu:0xA8u;
        lock_x=(int16_t)(som_bun_u16(o+2)-som_bun_u16(ram+cam));
        lock_y=(int16_t)(som_bun_u16(o+4)-som_bun_u16(ram+cam+2u))-o[0x45]-34;
        if(lock_x<0||lock_x>=256||lock_y<0||lock_y>=224)lock_x=-1;
    }
    if((!overlay.frames_left&&!menu&&!hero_drawn&&!class_title&&!worker&&!general_text[0]&&lock_x<0)||!data||data==RETRO_HW_FRAME_BUFFER_VALID||
       (pixel_format!=0&&pixel_format!=1&&pixel_format!=2)){
        som_frontend_video(data,width,height,pitch);
        return;
    }
    /* Copy only the visible pixels into a tightly packed frame: Snes9x 2005
     * hands over a 1024-byte pitch for a 256-pixel picture, so copying
     * height*pitch moved 229 KB per frame (0.6 ms on the Steam Link) where
     * 114 KB are visible (0.16 ms). Everything after this uses the new pitch. */
    const size_t row=(size_t)width*(pixel_format==1?4u:2u);
    if(row>pitch){som_frontend_video(data,width,height,pitch);return;}
    size_t size=(size_t)height*row;
    if(size>overlay_frame_size){
        uint8_t *grown=realloc(overlay_frame,size);
        if(!grown){som_frontend_video(data,width,height,pitch);return;}
        overlay_frame=grown;overlay_frame_size=size;
    }
    if(row==pitch)memcpy(overlay_frame,data,size);
    else for(unsigned y=0;y<height;y++)
        memcpy(overlay_frame+(size_t)y*row,(const uint8_t*)data+(size_t)y*pitch,row);
    pitch=row;
    if(overlay.speech_bubble&&!hero_drawn)som_hero_screen(&hero_x,&hero_y);
    if(lock_x>=0)                                 /* the lock-on marker, bobbing */
        som_overlay_label(overlay_frame,width,height,pitch,pixel_format,"V",
                          lock_x,lock_y-(int)((frame_number/8u)&1u),1,
                          som_overlay_color(pixel_format,255,220,64));
    if(general_text[0])
        som_overlay_label(overlay_frame,width,height,pitch,pixel_format,general_text,
                          general_x,general_y,1,
                          generals[0].rank>=3u?som_overlay_color(pixel_format,255,80,64):
                                               som_overlay_color(pixel_format,255,214,64));
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
        if(combo_text[0])
            som_overlay_label(overlay_frame,width,height,pitch,pixel_format,combo_text,
                              hero_x,hero_y-SOM_COMBO_LABEL_RISE_PX,1,
                              som_overlay_color(pixel_format,255,208,64));
    }
    if(class_title){
        som_overlay_label(overlay_frame,width,height,pitch,pixel_format,class_title,
                          hud_x+SOM_HUD_LABEL_DX,hud_y+SOM_HUD_LABEL_DY,0,
                          som_overlay_color(pixel_format,255,255,255));
        /* Only FlXtR's selected style or custom class-weapon name.
         * Never display native forge-grade strings like 'Masamune Katana'. */
        const uint8_t *ram=(const uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        const unsigned type=ram?ram[0xE1E4]:8u;
        if(type<8u&&ram[0xCC54u+type]!=0xFF){
            char text[48];
            const unsigned slot=som_class_arm_slot(disciplines.selected,type);
            const char *name=som_custom_weapon_name(&styles,disciplines.selected,slot);
            if(name[0]){
                snprintf(text,sizeof(text),"%s",name);
                /* Centred under the class title. */
                const int centre=hud_x+SOM_HUD_LABEL_DX+((int)strlen(class_title)*6-1)/2;
                som_overlay_label(overlay_frame,width,height,pitch,pixel_format,text,
                                  centre,hud_y+SOM_HUD_LABEL_DY+8,1,
                                  som_overlay_color(pixel_format,255,214,96));
            }
        }
    }
    if(overlay.frames_left){
        som_overlay_draw(overlay_frame,width,height,pitch,pixel_format,&overlay,hero_x,hero_y);
        overlay.frames_left--;
    }
    if(menu){
        if(som_manual_disciplines||som_menu_disciplines)
            som_disc_draw(overlay_frame,width,height,pitch,pixel_format,&disciplines);
        else som_gambit_draw(overlay_frame,width,height,pitch,pixel_format,&gambits);
    }
    som_frontend_video(overlay_frame,width,height,pitch);
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
    cartwheel_native=0;cartwheel_fallback=0;flykick.active=0;flykick_hid=0;bash_kick=0;
    memset(&ukemi_draw,0,sizeof(ukemi_draw));ukemi_hid=0;ukemi_map=0;
    som_tk_reset(&telekinesis);telekinesis_ready=0;
    memset(trail_frame,0,sizeof(trail_frame));seal_mode=0;seal_count=0;x_cycled=0;
    som_bun_clear(&bunshin);bunshin_shunshin=0;memset(bunshin_booms,0,sizeof(bunshin_booms));kawarimi_log_start=0;kawarimi_armed_until=0;
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
    som_disc_init(&disciplines);som_menu_disciplines=0;som_manual_disciplines=0;
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
    weapon_trace_on=0;som_weapon_trace_reset();
    hero_pal_have=0;
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
        if(som_apply_rom_patches(patched_rom,game->size)){
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
    load_backend();
    const size_t core=backend_retro_serialize_size?backend_retro_serialize_size():0;
    if(core&&data&&size==core+SOM_PROG_TRAILER_SIZE){
        if(!backend_retro_unserialize(data,core))return false;
        SomProgress loaded;
        if(som_prog_trailer_read((const uint8_t*)data+core,&loaded)==SOM_PROG_OK){
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
    familiar_carry.valid=0;
    som_tk_reset(&telekinesis);telekinesis_ready=0;
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
    if(state==SOM_FAMILIAR_HERE&&!familiar_is_native_control)
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
        familiar_carry.valid=0;familiar_is_native_control=0;
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
    som_generals_frame();
    /* No automatic hero VRAM caching: old monster-style snapshots garbled art. */
    if(test_enabled&&on_turbo&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE)
        som_weapon_gfx_verify_frame((const uint8_t*)
            backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    if(test_enabled&&on_turbo)som_class_supply_frame();
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
        som_bash_tick();
        if(fuma.active)som_fuma_frame(backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
        if(blink_active)som_blink_frame(backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
        if(companion.active)som_companion_frame(backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
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
    backend_run();
    const uint64_t perf_core_us=perf_core?som_perf_now_us()-perf_core:0;
    if(test_enabled&&on_turbo)som_combo_frame();
    if(test_enabled&&on_turbo&&backend_retro_get_memory_data&&
       backend_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)==SOM_WRAM_SIZE)
        som_style_after((uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    if(style_dirty&&frame_number-style_saved_frame>=3600ul)som_styles_save();
    if(test_enabled&&on_turbo&&backend_retro_get_memory_data)
        som_perks_frame((uint8_t*)backend_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    if(test_enabled&&on_turbo&&frame_number%30u==0u)som_prog_frame();
    if(test_enabled&&on_turbo)som_special_tick();
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
static int som_telekinesis_cast(uint8_t *ram){
    if(disciplines.selected!=SOM_DISC_PSIONICS||!ram||!som_jutsu_free(ram))return 0;
    if(telekinesis.slot>=0){
        som_overlay_set(&overlay,"^Telekinesis|already pulling",SOM_MESSAGE_FRAMES/3u);
        return 0;
    }
    if(frame_number<telekinesis_ready){
        som_overlay_set(&overlay,"^Telekinesis|recharging",SOM_MESSAGE_FRAMES/3u);
        return 0;
    }
    if(!som_tk_begin(&telekinesis,ram,frame_number)){
        som_overlay_set(&overlay,"^Telekinesis|no reachable chest",SOM_MESSAGE_FRAMES/2u);
        return 0;
    }
    telekinesis_ready=frame_number+SOM_TK_COOLDOWN;
    const uint8_t *chest=ram+0xE000u+0x200u*(unsigned)telekinesis.slot;
    som_bun_boom(bunshin_booms,(int)som_bun_u16(chest+2u),
                 (int)som_bun_u16(chest+4u),1,frame_number);
    som_overlay_set(&overlay,"^Telekinesis!|pulling treasure",SOM_MESSAGE_FRAMES/2u);
    fprintf(stderr,"[som-ai] TELEKINESIS pulling chest actor %d, map %u\\n",
            telekinesis.slot,telekinesis.map);
    return 1;
}
static void som_mind_release(uint8_t *ram){
    if(mind_slot<0)return;
    if(ram&&familiar.slot==mind_slot){
        memcpy(ram+SOM_MONSTER_PALETTE+(unsigned)mind_slot*SOM_MONSTER_PALETTE_SIZE,mind_palette,
               SOM_MONSTER_PALETTE_SIZE<sizeof(mind_palette)?SOM_MONSTER_PALETTE_SIZE:sizeof(mind_palette));
        familiar.slot=familiar.target=-1;familiar_carry.valid=0;
        som_familiar_publish();
        som_overlay_set(&overlay,"^The mind is free again",SOM_MESSAGE_FRAMES/2u);
    }
    fprintf(stderr,"[som-ai] MIND CONTROL ends (monster slot %d)\n",mind_slot+1);
    mind_slot=-1;mind_until=0;familiar_is_native_control=0;
}
static int som_class_x(uint8_t *ram){
    const unsigned d=disciplines.selected;
    if(!ram||d>=SOM_DISCIPLINE_COUNT)return 0;
    if(frame_number<x_move_ready){
        som_overlay_set(&overlay,"^Not ready",SOM_MESSAGE_FRAMES/3u);return 0;
    }
    if(!som_jutsu_free(ram))return 0;
    uint8_t *hero=ram+0xE000u;
    if(d==SOM_DISC_HUNTER){
        som_ahead(hero,SOM_HOLY_AHEAD,&holy_x,&holy_y);
        holy_until=frame_number+SOM_HOLY_FRAMES;x_move_ready=frame_number+SOM_HOLY_COOLDOWN;
        som_bun_boom(bunshin_booms,holy_x,holy_y,0,frame_number);
        som_overlay_set(&overlay,"^Holy Water!",SOM_MESSAGE_FRAMES/2u);
        fprintf(stderr,"[som-ai] HOLY WATER at %d,%d\n",holy_x,holy_y);
        return 1;
    }
    if(d==SOM_DISC_ARTIFICER){
        if(bomb_at){som_overlay_set(&overlay,"^Bomb|already set",SOM_MESSAGE_FRAMES/3u);return 0;}
        som_ahead(hero,SOM_BOMB_AHEAD,&bomb_x,&bomb_y);
        bomb_at=frame_number+SOM_BOMB_FUSE;x_move_ready=frame_number+SOM_BOMB_COOLDOWN;
        som_bun_boom(bunshin_booms,bomb_x,bomb_y,1,frame_number);
        som_overlay_set(&overlay,"^Bomb!|1 s fuse",SOM_MESSAGE_FRAMES/2u);
        fprintf(stderr,"[som-ai] BOMB set at %d,%d\n",bomb_x,bomb_y);
        return 1;
    }
    if(familiar.slot>=0||familiar_carry.valid){
        som_overlay_set(&overlay,"^One ally at a time",SOM_MESSAGE_FRAMES/3u);return 0;
    }
    if(d==SOM_DISC_NECRO){
        if(!last_kill_frame||frame_number-last_kill_frame>SOM_RAISE_WINDOW){
            som_overlay_set(&overlay,"^Raise Dead|no fresh corpse",SOM_MESSAGE_FRAMES/3u);return 0;
        }
        familiar_pending=1;
        x_move_ready=frame_number+SOM_RAISE_COOLDOWN;
        som_overlay_set(&overlay,"^Rise!",SOM_MESSAGE_FRAMES/2u);
        fprintf(stderr,"[som-ai] RAISE DEAD\n");
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
            if(!som_bun_monster_alive(m)||m[0x180]>=0x57u)continue;     /* no bosses */
            const long dist=labs((long)som_bun_u16(m+2)-hx)+labs((long)som_bun_u16(m+4)-hy);
            if(dist<=SOM_MIND_RANGE&&(best<0||dist<best_d)){best=(int)slot;best_d=dist;}
        }
        if(best<0){som_overlay_set(&overlay,"^Mind Control|no mind near",SOM_MESSAGE_FRAMES/3u);return 0;}
        memcpy(mind_palette,ram+SOM_MONSTER_PALETTE+(unsigned)best*SOM_MONSTER_PALETTE_SIZE,
               SOM_MONSTER_PALETTE_SIZE<sizeof(mind_palette)?SOM_MONSTER_PALETTE_SIZE:sizeof(mind_palette));
        familiar_is_native_control=1;clone_source_art.has_vram=0;
        familiar_pending=1;last_clone_target=best;
        som_familiar_bind();
        mind_slot=best;mind_until=frame_number+SOM_MIND_FRAMES;
        x_move_ready=frame_number+SOM_MIND_COOLDOWN;
        som_overlay_set(&overlay,"^Mind Control!|it fights for you",SOM_MESSAGE_FRAMES/2u);
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
            som_overlay_set(&overlay,"^Treasure in reach",SOM_MESSAGE_FRAMES/2u);
            fprintf(stderr,"[som-ai] TELEKINESIS chest delivered to pickup range\\n");
        }else if(result<0)fprintf(stderr,"[som-ai] TELEKINESIS stopped (obstacle or event)\\n");
    }
    const unsigned map=som_bun_u16(ram+0xDC);
    const int field=ram[0xE000]==1&&ram[0xE4]==1&&ram[0xE6]==0x0F&&ram[0xD0]==0;
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
static void som_lock_release(const char *why){
    if(lock_slot<0)return;
    fprintf(stderr,"[som-ai] LOCK off (monster slot %d): %s\n",lock_slot+1,why);
    lock_slot=-1;
}
static void som_lock_toggle(uint8_t *ram){
    if(lock_slot>=0){
        som_lock_release("let go");
        som_overlay_set(&overlay,"^Lock off",SOM_MESSAGE_FRAMES/4u);
        return;
    }
    const uint8_t *hero=ram+0xE000u;
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    int best=-1;long best_d=0;
    for(unsigned slot=0;slot<3u;slot++){
        if((int)slot==familiar.slot)continue;
        const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*slot;
        if(!som_bun_monster_alive(m))continue;
        const long dist=labs((long)som_bun_u16(m+2)-hx)+labs((long)som_bun_u16(m+4)-hy);
        if(dist<=SOM_LOCK_RANGE&&(best<0||dist<best_d)){best=(int)slot;best_d=dist;}
    }
    if(best<0){som_overlay_set(&overlay,"^Nothing to lock on",SOM_MESSAGE_FRAMES/4u);return;}
    lock_slot=best;
    lock_identity=ram[SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)best+0x180u];
    lock_map=som_bun_u16(ram+0xDC);
    som_overlay_set(&overlay,"^Locked on",SOM_MESSAGE_FRAMES/4u);
    fprintf(stderr,"[som-ai] LOCK on monster slot %d (%ld px)\n",best+1,best_d);
}
static void som_lock_frame(uint8_t *ram){
    if(lock_slot<0||!ram)return;
    if(ring_ai_enabled){som_lock_release("the AI plays");return;}
    const uint8_t *m=ram+SOM_BUN_MONSTER_BASE+SOM_BUN_OBJECT*(unsigned)lock_slot;
    if(som_bun_u16(ram+0xDC)!=lock_map){som_lock_release("map changed");return;}
    if(!som_bun_monster_alive(m)||m[0x180]!=lock_identity||lock_slot==familiar.slot){
        som_lock_release("target gone");return;
    }
    uint8_t *hero=ram+0xE000u;
    if(hero[0]!=1||ram[0xE4]!=1||ram[0xE6]!=0x0F||ram[0xD0]!=0)return;
    const int dx=(int)som_bun_u16(m+2)-(int)som_bun_u16(hero+2);
    const int dy=(int)som_bun_u16(m+4)-(int)som_bun_u16(hero+4);
    if(abs(dx)+abs(dy)>SOM_LOCK_DROP){som_lock_release("too far");return;}
    /* Not during a dodge or a pose the game is playing on its own. */
    if(evade_active||hero[0x01C]==0x40u||hero[0x01C]==0x30u)return;
    hero[0x010]=abs(dx)>=abs(dy)?(dx>=0?0x02u:0x82u):(dy>=0?0x01u:0x00u);
}
