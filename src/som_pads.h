#ifndef SOM_PADS_H
#define SOM_PADS_H
/* PS4 pads read straight from the kernel (Steam Link, 2026-11-25).
 * RetroArch's slots move when a pad drops or the Steam Controller receiver
 * adds phantom joysticks; the kernel gives every pad a permanent id (its
 * Bluetooth address). The first pad to press a button is player 1, the next
 * player 2. A pad that drops keeps its seat and gets it back; another pad
 * may take the seat of a pad that is gone by pressing a button. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#define SOM_PADS_MAX 4u
#define SOM_PADS_SCAN_FRAMES 60ul
#define SOM_PADS_QUIT_FRAMES 45u
typedef struct {int fd;unsigned node;char id[48];uint16_t keys,hat,trigger;} SomPad;
typedef struct {
    SomPad pad[SOM_PADS_MAX];
    char seat[2][48];
    unsigned quit_hold;int quit_done,seen;
    unsigned long next_scan;
} SomPads;

static inline void som_pads_init(SomPads *p){
    memset(p,0,sizeof(*p));
    for(unsigned i=0;i<SOM_PADS_MAX;i++)p->pad[i].fd=-1;
}
/* libretro ids: B0 Y1 SELECT2 START3 UP4 DOWN5 LEFT6 RIGHT7 A8 X9 L10 R11 L2 12 R2 13 L3 14 R3 15 */
static inline int som_pads_key(unsigned code){
    static const int8_t map[12]={1,0,8,9,10,11,-1,-1,2,3,14,15};
    return code>=0x130u&&code<0x13Cu?map[code-0x130u]:-1;
}
static inline void som_pads_event(SomPad *pad,unsigned type,unsigned code,int value){
    if(type==EV_KEY){
        const int id=som_pads_key(code);
        if(id>=0){
            if(value)pad->keys|=(uint16_t)(1u<<id);else pad->keys&=(uint16_t)~(1u<<id);
        }
    }else if(type==EV_ABS){
        if(code==ABS_HAT0X)
            pad->hat=(uint16_t)((pad->hat&~0xC0u)|(value<0?0x40u:value>0?0x80u:0u));
        else if(code==ABS_HAT0Y)
            pad->hat=(uint16_t)((pad->hat&~0x30u)|(value<0?0x10u:value>0?0x20u:0u));
        else if(code==ABS_RX)
            pad->trigger=(uint16_t)((pad->trigger&~0x1000u)|(value>127?0x1000u:0u));
        else if(code==ABS_RY)
            pad->trigger=(uint16_t)((pad->trigger&~0x2000u)|(value>127?0x2000u:0u));
    }
}
static inline uint16_t som_pads_raw(const SomPad *pad){
    return (uint16_t)(pad->keys|pad->hat|pad->trigger);
}
static inline int som_pads_present(const SomPads *p,const char *id){
    for(unsigned i=0;i<SOM_PADS_MAX;i++)
        if(p->pad[i].fd>=0&&!strcmp(p->pad[i].id,id))return 1;
    return 0;
}
/* The seats after this frame's buttons; 1 when the quit chord is complete. */
static inline int som_pads_seats(SomPads *p){
    const uint16_t chord=(1u<<2)|(1u<<3);
    int quitting=0;
    for(unsigned i=0;i<SOM_PADS_MAX;i++){
        const SomPad *pad=&p->pad[i];
        if(pad->fd<0)continue;
        const uint16_t b=som_pads_raw(pad);
        if((b&chord)==chord)quitting=1;
        if(!b||!strcmp(pad->id,p->seat[0])||!strcmp(pad->id,p->seat[1]))continue;
        for(unsigned s=0;s<2u;s++)
            if(!p->seat[s][0]||!som_pads_present(p,p->seat[s])){
                snprintf(p->seat[s],sizeof(p->seat[s]),"%s",pad->id);
                fprintf(stderr,"[som-ai] PADS %04u is player %u\n",pad->node,s+1u);
                break;
            }
    }
    p->quit_hold=quitting?p->quit_hold+1u:0u;
    if(p->quit_hold>=SOM_PADS_QUIT_FRAMES&&!p->quit_done){p->quit_done=1;return 1;}
    return 0;
}
static inline uint16_t som_pads_buttons(const SomPads *p,unsigned seat){
    if(seat>1u||!p->seat[seat][0])return 0;
    for(unsigned i=0;i<SOM_PADS_MAX;i++)
        if(p->pad[i].fd>=0&&!strcmp(p->pad[i].id,p->seat[seat])){
            uint16_t b=som_pads_raw(&p->pad[i]);
            if((b&0xCu)==0xCu)b&=(uint16_t)~0xCu;      /* the quit chord is not for the game */
            return b;
        }
    return 0;
}
static inline void som_pads_close(SomPad *pad){
    if(pad->fd>=0)close(pad->fd);
    memset(pad,0,sizeof(*pad));pad->fd=-1;
}
/* The whole state, after opening and after dropped events. */
static inline void som_pads_sync(SomPad *pad){
    unsigned long keys[(KEY_MAX+8u*sizeof(long))/(8u*sizeof(long))];
    memset(keys,0,sizeof(keys));
    pad->keys=pad->hat=pad->trigger=0;
    if(ioctl(pad->fd,EVIOCGKEY(sizeof(keys)),keys)>=0)
        for(unsigned c=0x130u;c<0x13Cu;c++)
            if(keys[c/(8u*sizeof(long))]>>(c%(8u*sizeof(long)))&1ul)
                som_pads_event(pad,EV_KEY,c,1);
    static const unsigned axes[]={ABS_HAT0X,ABS_HAT0Y,ABS_RX,ABS_RY};
    for(unsigned i=0;i<4u;i++){
        struct input_absinfo a;
        if(ioctl(pad->fd,EVIOCGABS(axes[i]),&a)>=0)som_pads_event(pad,EV_ABS,axes[i],a.value);
    }
}
static inline void som_pads_scan(SomPads *p){
    DIR *d=opendir("/dev/input");
    if(!d)return;
    struct dirent *e;
    while((e=readdir(d))){
        unsigned node;
        if(sscanf(e->d_name,"event%u",&node)!=1||node>999u)continue;
        int open_slot=-1,known=0;
        for(unsigned i=0;i<SOM_PADS_MAX;i++){
            if(p->pad[i].fd<0){if(open_slot<0)open_slot=(int)i;}
            else if(p->pad[i].node==node)known=1;
        }
        if(known||open_slot<0)continue;
        char path[64];snprintf(path,sizeof(path),"/dev/input/event%u",node);
        const int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        if(fd<0)continue;
        struct input_id id;
        if(ioctl(fd,EVIOCGID,&id)<0||id.vendor!=0x054C||
           (id.product!=0x05C4&&id.product!=0x09CC&&id.product!=0x0BA0)){close(fd);continue;}
        SomPad *pad=&p->pad[open_slot];
        memset(pad,0,sizeof(*pad));pad->fd=fd;pad->node=node;
        if(ioctl(fd,EVIOCGUNIQ(sizeof(pad->id)-1u),pad->id)<0||!pad->id[0])
            snprintf(pad->id,sizeof(pad->id),"event%u",node);
        pad->id[sizeof(pad->id)-1u]=0;
        for(unsigned i=0;i<SOM_PADS_MAX;i++)          /* back before its old node failed */
            if((int)i!=open_slot&&p->pad[i].fd>=0&&!strcmp(p->pad[i].id,pad->id))
                som_pads_close(&p->pad[i]);
        som_pads_sync(pad);
        p->seen=1;
        fprintf(stderr,"[som-ai] PADS %04u connected (%04x)\n",node,(unsigned)id.product);
    }
    closedir(d);
}
/* Once per frame: 1 = a player asked to quit. */
static inline int som_pads_poll(SomPads *p,unsigned long frame){
    if(frame>=p->next_scan){p->next_scan=frame+SOM_PADS_SCAN_FRAMES;som_pads_scan(p);}
    for(unsigned i=0;i<SOM_PADS_MAX;i++){
        SomPad *pad=&p->pad[i];
        if(pad->fd<0)continue;
        struct input_event ev[32];
        for(unsigned turn=0;turn<16u;turn++){
            const ssize_t n=read(pad->fd,ev,sizeof(ev));
            if(n<=0){
                if(n==0||(errno!=EAGAIN&&errno!=EINTR)){
                    fprintf(stderr,"[som-ai] PADS %04u disconnected\n",pad->node);
                    som_pads_close(pad);
                }
                break;
            }
            for(size_t k=0;k<(size_t)n/sizeof(ev[0]);k++){
                if(ev[k].type==EV_SYN&&ev[k].code==SYN_DROPPED)som_pads_sync(pad);
                else som_pads_event(pad,ev[k].type,ev[k].code,ev[k].value);
            }
            if((size_t)n<sizeof(ev))break;
        }
    }
    return som_pads_seats(p);
}
#endif
