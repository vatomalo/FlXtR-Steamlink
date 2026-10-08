/* Desktop backend. libmpv owns video/audio; the shell retains controller input. */
#define _POSIX_C_SOURCE 200809L
#include <mpv/client.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef FLXTR_VERSION
#define FLXTR_VERSION "development"
#endif
static mpv_handle *mpv;
static volatile sig_atomic_t stop;
static int fd=-1,menu,row,view,subs,scale=2,delay,height=720,episodes,servers,result=1;
static double start;
static const char *languages[]={"off","auto","eng","nor"};
static void signal_stop(int sig){(void)sig;stop=1;}
static void command(const char *a,const char *b,const char *c){const char *v[]={a,b,c,NULL};mpv_command(mpv,v);}
static void property(const char *name,const char *value){mpv_set_property_string(mpv,name,value);}
static double position(void){double p=start;mpv_get_property(mpv,"time-pos",MPV_FORMAT_DOUBLE,&p);return p;}
static void request(int action,double p){
    if(p<0)p=0;
    FILE *f=fopen("playback-request.next","w");if(!f)return;
    fprintf(f,"%d %.3f %d %d %d %d\n",action,p,view,subs,scale,delay);
    if(fclose(f)||rename("playback-request.next","playback-request"))return;
    result=40;stop=1;
}
static void sizing(void){
    property("keepaspect",view==1?"no":"yes");
    property("video-unscaled",view==2?"yes":"no");
}
static void draw(void){
    char lines[11][96],text[1500];int paused=0;mpv_get_property(mpv,"pause",MPV_FORMAT_FLAG,&paused);
    snprintf(lines[0],96,"%s",paused?"Resume":"Pause");
    snprintf(lines[1],96,"Seek: left -10s / right +10s");
    snprintf(lines[2],96,"Subtitles: %s",languages[subs]);
    snprintf(lines[3],96,"Subtitle size: %d",scale);
    snprintf(lines[4],96,"Subtitle delay: %+d sec",delay);
    snprintf(lines[5],96,"Video size: %s",(const char*[]){"Fit","Stretch","1:1"}[view]);
    snprintf(lines[6],96,"Quality limit: %dp",height);
    snprintf(lines[7],96,"Next server%s",servers?"":" (unavailable)");
    snprintf(lines[8],96,"Previous episode%s",episodes?"":" (unavailable)");
    snprintf(lines[9],96,"Next episode%s",episodes?"":" (unavailable)");
    snprintf(lines[10],96,"Stop playback");
    size_t n=(size_t)snprintf(text,sizeof(text),"FlXtR  %.0f sec\n\n",position());
    for(int i=0;i<11;i++)n+=(size_t)snprintf(text+n,sizeof(text)-n,"%c %s\n",row==i?'>':' ',lines[i]);
    command("show-text",text,"60000");
}
static void key(char c){
    if(c=='m'||(c=='b'&&menu)){menu=!menu;if(menu)draw();else command("show-text","","0");return;}
    if(c=='b'){request(7,position());return;}
    if(c=='v'){view=(view+1)%3;sizing();}
    if(!menu){if(c=='l'||c=='r')request(1,position()+(c=='l'?-10:10));return;}
    if(c=='u')row=(row+10)%11;
    if(c=='d')row=(row+1)%11;
    if(c=='a'||c=='l'||c=='r'){
        int d=c=='l'?-1:1;char value[32];
        switch(row){
        case 0:command("cycle","pause",NULL);break;
        case 1:request(1,position()+d*10);break;
        case 2:subs=(subs+d+4)%4;request(1,position());break;
        case 3:scale=scale==2?3:2;property("sub-scale",scale==2?"1":"1.5");break;
        case 4:delay+=d;if(delay>5)delay=5;if(delay< -5)delay=-5;snprintf(value,sizeof(value),"%d",delay);property("sub-delay",value);break;
        case 5:view=(view+d+3)%3;sizing();break;
        case 6:request(d>0?2:3,position());break;
        case 7:if(servers)request(4,position());break;
        case 8:if(episodes)request(5,0);break;
        case 9:if(episodes)request(6,0);break;
        case 10:request(7,position());break;
        }
    }
    if(menu&&!stop)draw();
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--version")){puts(FLXTR_VERSION);return 0;}
    if(argc<2)return 2;
    int buffer=15,mb=128,headless=0;double duration=0;
    for(int i=2;i<argc;i++){
        const char *a=argv[i];
        if(!strcmp(a,"--episodes")){episodes=1;continue;}
        if(!strcmp(a,"--servers")){servers=1;continue;}
        if(!strcmp(a,"--headless")){headless=1;continue;}
        if(i+1==argc)return 2;
        const char *v=argv[++i];
        if(!strcmp(a,"--height"))height=atoi(v);
        else if(!strcmp(a,"--start"))start=strtod(v,NULL);
        else if(!strcmp(a,"--duration"))duration=strtod(v,NULL);
        else if(!strcmp(a,"--control-fd"))fd=atoi(v);
        else if(!strcmp(a,"--buffer-seconds"))buffer=atoi(v);
        else if(!strcmp(a,"--buffer-mb"))mb=atoi(v);
        else if(!strcmp(a,"--subtitle-size"))scale=atoi(v);
        else if(!strcmp(a,"--subtitle-delay"))delay=atoi(v);
        else if(!strcmp(a,"--view")){if(!strcmp(v,"stretch"))view=1;else if(!strcmp(v,"pixel"))view=2;}
        else if(!strcmp(a,"--subtitles")){for(int n=0;n<4;n++)if(!strcmp(v,languages[n]))subs=n;}
        else return 2;
    }
    if((height!=480&&height!=720&&height!=1080)||mb<16||mb>256||buffer<1||buffer>30||start<0||start>86400)return 2;
    signal(SIGTERM,signal_stop);signal(SIGINT,signal_stop);
    mpv=mpv_create();if(!mpv)return 1;
    mpv_set_option_string(mpv,"config","no");mpv_set_option_string(mpv,"terminal","yes");
    mpv_set_option_string(mpv,"term-status-msg","");
    mpv_set_option_string(mpv,"ytdl","no");mpv_set_option_string(mpv,"input-default-bindings","yes");
    mpv_set_option_string(mpv,"input-vo-keyboard","yes");mpv_set_option_string(mpv,"osc","no");
    mpv_set_option_string(mpv,"scripts","./linux-controls.lua");
    mpv_set_option_string(mpv,"force-window","yes");mpv_set_option_string(mpv,"hwdec","auto-safe");
    mpv_set_option_string(mpv,"tls-verify","yes");mpv_set_option_string(mpv,"cache","yes");
    /* mpv's disk cache is append-only, unlike Steam Link's bounded ring file. */
    mpv_set_option_string(mpv,"cache-on-disk","no");
    char value[160];snprintf(value,sizeof(value),"%d",buffer);mpv_set_option_string(mpv,"cache-secs",value);
    mpv_set_option_string(mpv,"cache-pause-initial","yes");mpv_set_option_string(mpv,"cache-pause-wait",value);
    snprintf(value,sizeof(value),"%d",mb*1024*1024);mpv_set_option_string(mpv,"demuxer-max-bytes",value);
    mpv_set_option_string(mpv,"demuxer-max-back-bytes","0");
    snprintf(value,sizeof(value),"%.3f",start);mpv_set_option_string(mpv,"start",value);
    snprintf(value,sizeof(value),"%d",delay);mpv_set_option_string(mpv,"sub-delay",value);
    mpv_set_option_string(mpv,"sub-scale",scale==3?"1.5":"1");
    if(!subs)mpv_set_option_string(mpv,"sid","no");else if(subs>1)mpv_set_option_string(mpv,"slang",languages[subs]);
    /* Match the source variant to the same height limit as the Steam Link. */
    mpv_set_option_string(mpv,"hls-bitrate",height<=480?"1000000":height<=720?"3000000":"6000000");
    if(getenv("FLXTR_MEDIA_PROVIDER")&&!strcmp(getenv("FLXTR_MEDIA_PROVIDER"),"megaplay")){
        mpv_set_option_string(mpv,"referrer","https://megaplay.buzz/");
        mpv_set_option_string(mpv,"user-agent","FlXtR-Steamlink/0.2");
        mpv_set_option_string(mpv,"demuxer-lavf-o","allowed_extensions=ALL,extension_picky=0");
    }
    if(headless){mpv_set_option_string(mpv,"vo","null");mpv_set_option_string(mpv,"ao","null");}
    if(duration>0){snprintf(value,sizeof(value),"%.3f",duration);mpv_set_option_string(mpv,"length",value);}
    if(mpv_initialize(mpv)<0){mpv_terminate_destroy(mpv);return 1;}
    sizing();if(fd>=0)fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);
    command("loadfile",argv[1],NULL);
    while(!stop){
        char keys[32];ssize_t n;while(fd>=0&&(n=read(fd,keys,sizeof(keys)))>0)for(ssize_t i=0;i<n;i++)key(keys[i]);
        if(stop)break;
        mpv_event *e=mpv_wait_event(mpv,0.02);
        if(e->event_id==MPV_EVENT_CLIENT_MESSAGE){mpv_event_client_message *m=e->data;if(m->num_args==2&&!strcmp(m->args[0],"flxtr"))key(m->args[1][0]);}
        if(e->event_id==MPV_EVENT_END_FILE){
            mpv_event_end_file *end=e->data;
            if(end->reason==MPV_END_FILE_REASON_EOF)result=0;
            else if(end->reason!=MPV_END_FILE_REASON_ERROR)request(7,position());
            break;
        }
        if(e->event_id==MPV_EVENT_SHUTDOWN){request(7,position());break;}
    }
    if(stop&&result==1)result=0;
    mpv_terminate_destroy(mpv);return result;
}
