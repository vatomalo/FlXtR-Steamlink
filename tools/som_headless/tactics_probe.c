/* Native monster probe for the enemy tactics layer (real ROM, bare SNES core).
 *   tactics_probe CORE ROM CORE_STATE MODE FRAMES SLOT [DX DY]
 * MODE: base | zerovel | revvel | flipdir | push{l,r,u,d} | gauge0 | gaugehold | tac:<level>
 * Env: GOD keeps the monster alive and holds the companions away, HEROHP refills
 * the hero, SWING=N presses B every N frames, QUIET drops the per-frame log.
 * Per-frame log: frame x y vx vy state anim cmd0..3 gauge target status height
 * facing | hero x y state hp. Used to measure the levers documented in
 * src/som_enemy_tactics.h (2026-11-26). */
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vendor/libretro.h"
#include "som_enemy_tactics.h"
static unsigned long fno;static unsigned bmask_every=0;
static unsigned pf;
static bool env(unsigned cmd,void *data){
    if(cmd==RETRO_ENVIRONMENT_SET_PIXEL_FORMAT){pf=*(const unsigned*)data;return true;}
    (void)data;  /* no system or save directory: the core writes nothing */
    return false;
}
static void video(const void*d,unsigned w,unsigned h,size_t p){(void)d;(void)w;(void)h;(void)p;}
static void audio(int16_t l,int16_t r){(void)l;(void)r;}
static size_t batch(const int16_t*d,size_t n){(void)d;return n;}
static void poll(void){}
static int16_t input(unsigned a,unsigned b,unsigned c,unsigned d){(void)b;(void)c;if(a)return 0;if(bmask_every&&fno%bmask_every<2u&&d==RETRO_DEVICE_ID_JOYPAD_B)return 1;if(bmask_every&&d==256)return fno%bmask_every<2u?1:0;return 0;}
static void *slurp(const char *p,size_t *n){
    FILE *f=fopen(p,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);*n=(size_t)ftell(f);rewind(f);
    void *b=malloc(*n);if(fread(b,1,*n,f)!=*n){fclose(f);return NULL;}fclose(f);return b;
}
static unsigned w16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
int main(int argc,char **argv){
    if(argc<7)return 2;
    void *h=dlopen(argv[1],RTLD_NOW);if(!h){fprintf(stderr,"%s\n",dlerror());return 2;}
    ((void(*)(retro_environment_t))dlsym(h,"retro_set_environment"))(env);
    ((void(*)(retro_video_refresh_t))dlsym(h,"retro_set_video_refresh"))(video);
    ((void(*)(retro_audio_sample_t))dlsym(h,"retro_set_audio_sample"))(audio);
    ((void(*)(retro_audio_sample_batch_t))dlsym(h,"retro_set_audio_sample_batch"))(batch);
    ((void(*)(retro_input_poll_t))dlsym(h,"retro_set_input_poll"))(poll);
    ((void(*)(retro_input_state_t))dlsym(h,"retro_set_input_state"))(input);
    ((void(*)(void))dlsym(h,"retro_init"))();
    size_t rn,sn;void *rom=slurp(argv[2],&rn),*st=slurp(argv[3],&sn);
    struct retro_game_info gi={argv[2],rom,rn,NULL};
    if(!((bool(*)(const struct retro_game_info*))dlsym(h,"retro_load_game"))(&gi))return 2;
    size_t want=((size_t(*)(void))dlsym(h,"retro_serialize_size"))();
    if(!((bool(*)(const void*,size_t))dlsym(h,"retro_unserialize"))(st,sn)){
        fprintf(stderr,"state %zu refused (want %zu)\n",sn,want);return 2;}
    uint8_t *ram=((void*(*)(unsigned))dlsym(h,"retro_get_memory_data"))(RETRO_MEMORY_SYSTEM_RAM);
    void(*run)(void)=(void(*)(void))dlsym(h,"retro_run");
    const char *mode=argv[4];unsigned frames=(unsigned)atoi(argv[5]),slot=(unsigned)atoi(argv[6]);
    uint8_t *m=ram+0xE600+0x200*slot,*hero=ram+0xE000;
    if(argc>=9){ /* place the monster relative to the hero */
        unsigned x=w16(hero+2)+(unsigned)atoi(argv[7]),y=w16(hero+4)+(unsigned)atoi(argv[8]);
        m[2]=(uint8_t)x;m[3]=(uint8_t)(x>>8);m[4]=(uint8_t)y;m[5]=(uint8_t)(y>>8);
    }
    const int god=getenv("GOD")!=NULL;
    static SomTac tac;int tac_on=!strncmp(mode,"tac:",4);
    if(tac_on){tac.arena_archetype=-1;som_tac_reset(&tac,som_fight_parse(mode+4));}
    if(getenv("SWING"))bmask_every=(unsigned)atoi(getenv("SWING"));
    unsigned taken=0,attacks=0,lost=0,inband=0,prevhp=w16(hero+0x182),prev80=0;long dsum=0;
    SomTacHeroExtra ex[3];memset(ex,0,sizeof(ex));
    for(unsigned f=0;f<frames;f++){
        if(god){unsigned mh=w16(m+0x182);if(mh<999u)taken+=999u-mh;m[0x182]=0xE7;m[0x183]=0x03;for(unsigned k=1;k<3;k++){uint8_t*a=ram+0xE000+0x200*k;a[0x1ED]=200;a[0x1C]=a[0x1C]==0x20||a[0x1C]==0x80||a[0x1C]==0x40?0:a[0x1C];
            unsigned ax=w16(hero+2)+(k==1?-150:-170),ay=w16(hero+4)-120;a[2]=(uint8_t)ax;a[3]=(uint8_t)(ax>>8);a[4]=(uint8_t)ay;a[5]=(uint8_t)(ay>>8);a[0x60]=0;}}
        if(!strcmp(mode,"zerovel")){m[6]=m[7]=0;}
        if(!strcmp(mode,"revvel")){ /* reverse the native velocity's sign bits */
            if(m[6])m[6]^=0x80;
            if(m[7])m[7]^=0x80;
        }
        if(!strncmp(mode,"push",4)&&mode[4]&&mode[4]!='x'&&(m[6]||m[7])){
            char c=mode[4];m[6]=c=='r'?0x02:c=='l'?0x82:0;m[7]=c=='d'?0x02:c=='u'?0x82:0;
        }
        if(!strcmp(mode,"pushx")&&(m[6]||m[7])){m[6]=0x02;m[7]=0;} /* native move: go right 2 */
        if(!strcmp(mode,"gauge0"))m[0x1ED]=0;
        if(!strcmp(mode,"gaugehold"))m[0x1ED]=60;
        if(!strcmp(mode,"flipdir")&&m[0x140]==0xC1){
            uint8_t d=m[0x141],lo=d&15;unsigned nd=((lo&3)?(lo^3):0)|((lo&12)?(lo^12)&12:0);
            m[0x141]=(uint8_t)((d&0xF0)|nd);
        }
        if(getenv("HEROHP")&&w16(hero+0x182)<120){hero[0x182]=200;hero[0x183]=0;prevhp=200;}
        if(tac_on)som_tac_frame(&tac,ram,ex,-1,1000+f);
        fno=f;run();
        if(m[0x1C]==0x80&&!prev80)attacks++;
        prev80=m[0x1C]==0x80;
        {unsigned hp=w16(hero+0x182);if(hp<prevhp)lost+=prevhp-hp;prevhp=hp;}
        {int d=som_tac_dist(w16(m+2),w16(m+4),w16(hero+2),w16(hero+4));dsum+=d;}
        if(getenv("QUIET"))continue;
        printf("%4u %4u %4u %4d %4d %02x %02x %02x %02x %02x %02x g%3u t%02x s%04x h%3u e%02x | %4u %4u %02x hp%u\n",f,w16(m+2),w16(m+4),
               (int8_t)m[6],(int8_t)m[7],m[0x1C],m[0x11],m[0x140],m[0x141],m[0x142],m[0x143],
               m[0x1ED],m[0x1AC],w16(m+0x190),m[0x45],m[0x10],w16(hero+2),w16(hero+4),hero[0x1C],w16(hero+0x182));
    }
    fprintf(stderr,"%-11s attacks %2u, dealt %4u, taken %4u, mean dist %ld",mode,attacks,lost,taken,dsum/(long)frames);
    if(tac_on){fprintf(stderr,", actions:");
        for(unsigned a=0;a<SOM_TA_COUNT;a++)if(tac.foe[slot].stats_actions[a])fprintf(stderr," %s %u",som_tac_action_names[a],tac.foe[slot].stats_actions[a]);
        fprintf(stderr," | habits P1:");for(unsigned k=0;k<SOM_HABITS;k++)fprintf(stderr," %u",tac.foe[slot].habit[0][k]);}
    fputc('\n',stderr);(void)inband;
    return 0;
}
