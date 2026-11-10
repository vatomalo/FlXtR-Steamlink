#ifndef FLXTR_PHONE_FEATURES_H
#define FLXTR_PHONE_FEATURES_H
#include <dirent.h>
#include "platform_paths.h"

static char phone_pending[24],phone_value[2048],phone_subtitle[256];
static int phone_upload=-1;
static size_t phone_upload_left;
static char phone_temp[256],phone_target[256];
static uint32_t phone_now;
static int phone_rom_uploaded;
#include "party_questions.h"
static struct {int phase,round,count,spicy,deck[PHONE_ROUNDS],votes[4],winners;uint32_t deadline;
    struct {char name[20],token[33];int score,answer,gain,streak;} players[8];
} phone_party;
static const PhoneQuestion *phone_question(void){return &phone_questions[phone_party.deck[phone_party.round<PHONE_ROUNDS?phone_party.round:PHONE_ROUNDS-1]];}
static void phone_deck(uint32_t seed){
    int previous[PHONE_ROUNDS];memcpy(previous,phone_party.deck,sizeof(previous));
    for(int r=0;r<PHONE_ROUNDS;r++){
        int candidates[PHONE_QUESTION_COUNT],n=0;
        for(int i=0;i<PHONE_QUESTION_COUNT;i++){
            const PhoneQuestion *q=&phone_questions[i];
            if(q->kind!=r%2||(q->kind&&q->spicy!=phone_party.spicy))continue;
            int used=0;for(int k=0;k<r;k++)if(phone_party.deck[k]==i)used=1;
            for(int k=0;k<PHONE_ROUNDS;k++)if(previous[k]==i)used=1;
            if(!used)candidates[n++]=i;
        }
        /* Each deck has at least twelve prompts, enough to avoid the last game. */
        seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
        phone_party.deck[r]=candidates[seed%(unsigned)n];
    }
}
static int phone_random(char token[33]){
    unsigned char bytes[16];if(flxtr_random_bytes(bytes,sizeof(bytes)))return -1;
    for(int i=0;i<16;i++)snprintf(token+i*2,3,"%02X",bytes[i]);
    return 0;
}
static void phone_party_tick(uint32_t now){
    phone_now=now;
    if((phone_party.phase!=1&&phone_party.phase!=2)||(int32_t)(now-phone_party.deadline)<0)return;
    if(phone_party.phase==1){
        const PhoneQuestion *q=phone_question();memset(phone_party.votes,0,sizeof(phone_party.votes));
        for(int i=0;i<phone_party.count;i++){int a=phone_party.players[i].answer;if(a>=0&&a<4)phone_party.votes[a]++;}
        phone_party.winners=q->kind?0:1<<q->correct;
        int top=0;for(int i=0;i<4;i++)if(phone_party.votes[i]>top)top=phone_party.votes[i];
        if(q->kind&&top)for(int i=0;i<4;i++)if(phone_party.votes[i]==top)phone_party.winners|=1<<i;
        for(int i=0;i<phone_party.count;i++){
            int a=phone_party.players[i].answer,win=a>=0&&a<4&&(phone_party.winners&(1<<a));
            if(!q->kind)phone_party.players[i].streak=win?phone_party.players[i].streak+1:0;
            int bonus=!q->kind&&phone_party.players[i].streak>1?50:0;
            int gain=win?((q->kind?100:200)+bonus)*(phone_party.round>=6?2:1):0;
            phone_party.players[i].gain=gain;phone_party.players[i].score+=gain;
        }
        phone_party.phase=2;phone_party.deadline=now+8000;
    }else{
        if(++phone_party.round==PHONE_ROUNDS){phone_party.phase=3;return;}
        for(int i=0;i<phone_party.count;i++){phone_party.players[i].answer=-1;phone_party.players[i].gain=0;}
        phone_party.phase=1;phone_party.deadline=now+25000;
    }
}
static int phone_decode(const char *encoded,char *out,size_t limit){
    size_t n=0;
    for(size_t i=0;encoded[i];i++){
        unsigned char c=(unsigned char)encoded[i];
        if(c=='%'){
            if(!encoded[i+1]||!encoded[i+2]||!isxdigit((unsigned char)encoded[i+1])||!isxdigit((unsigned char)encoded[i+2]))return -1;
            char hex[3]={encoded[i+1],encoded[i+2],0};c=(unsigned char)strtoul(hex,NULL,16);i+=2;
        }else if(c=='+')c=' ';
        if(c<32||c>126||n+1>=limit)return -1;
        out[n++]=(char)c;
    }out[n]=0;return 0;
}
/* Upload names are a restricted basename, never a path or executable. */
static int phone_filename(const char *name){
    size_t n=strlen(name);if(!n||n>100||flxtr_reserved_basename(name)||name[0]=='.'||strstr(name,".."))return 0;
    for(size_t i=0;i<n;i++)if(!isalnum((unsigned char)name[i])&&name[i]!='.'&&name[i]!='-'&&name[i]!='_')return 0;
    const char *ext=strrchr(name,'.');if(!ext)return 0;
    const char *allowed[]={".mp4",".mkv",".webm",".avi",".mov",".srt",".vtt",".nes",".sfc",".smc",".gb",".gbc",".gba",".zip",".md",".gen",".smd",".sms",".gg",".cue",".pbp",".chd",".m3u",".a26",".bin",".iso"};
    for(size_t i=0;i<sizeof(allowed)/sizeof(allowed[0]);i++)if(!strcasecmp(ext,allowed[i]))return 1;
    return 0;
}
static int phone_is_sub(const char *name){const char *e=strrchr(name,'.');return e&&(!strcasecmp(e,".srt")||!strcasecmp(e,".vtt"));}
static int phone_is_rom(const char *name){
    const char *e=strrchr(name,'.');if(!e)return 0;
    const char *extensions[]={".nes",".sfc",".smc",".gb",".gbc",".gba",".zip",".md",".gen",".smd",".sms",".gg",".cue",".pbp",".chd",".m3u",".a26",".bin",".iso"};
    for(size_t i=0;i<sizeof(extensions)/sizeof(extensions[0]);i++)if(!strcasecmp(e,extensions[i]))return 1;
    return 0;
}
static void phone_upload_abort(void){
    if(phone_upload>=0)flxtr_fd_close(phone_upload);
    phone_upload=-1;
    if(phone_temp[0])flxtr_path_unlink(phone_temp);
    phone_temp[0]=0;phone_upload_left=0;
}
static int phone_upload_begin(const char *name,size_t length){
    if(!phone_filename(name)||!length||length>512u*1024u*1024u)return -1;
    flxtr_path_mkdir("phone-files");flxtr_path_mkdir("roms");flxtr_path_mkdir("roms/phone");
    const char *dir=phone_is_rom(name)?"roms/phone":"phone-files";
    uint64_t available;if(flxtr_disk_free(dir,&available)||available<length+16u*1024u*1024u)return -2;
    snprintf(phone_target,sizeof(phone_target),"%s/%s",dir,name);
    if(!flxtr_path_access(phone_target,F_OK))return -3;
    snprintf(phone_temp,sizeof(phone_temp),"%s/.upload-XXXXXX",dir);
    phone_upload=flxtr_temp_file(phone_temp,sizeof(phone_temp));if(phone_upload<0){phone_temp[0]=0;return -2;}
    phone_upload_left=length;return 0;
}
/* Returns 1 when installed, 0 while streaming, -1 on failure. */
static int phone_upload_write(const char *bytes,size_t n){
    if(n>phone_upload_left)return -1;
    size_t done=0;while(done<n){ptrdiff_t w=flxtr_fd_write(phone_upload,bytes+done,n-done);if(w<0&&errno==EINTR)continue;if(w<=0)return -1;done+=(size_t)w;}
    phone_upload_left-=n;if(phone_upload_left)return 0;
    int ok=flxtr_fd_sync(phone_upload)==0;if(flxtr_fd_close(phone_upload))ok=0;phone_upload=-1;
    /* Publish atomically without replacing an existing name. */
    if(!ok||flxtr_install_new(phone_temp,phone_target))return -1;
    flxtr_path_unlink(phone_temp);phone_temp[0]=0;
    if(!strncmp(phone_target,"roms/",5)){flxtr_path_unlink("games.json");phone_rom_uploaded=1;}
    return 1;
}
static int phone_command(const char *field,const char *encoded,char *reply,size_t size){
    char value[2048];if(phone_decode(encoded,value,sizeof(value)))return 400;
    if(!strcmp(field,"join")){
        if(phone_party.phase==1||phone_party.phase==2||phone_party.count==8)return 409;
        size_t n=strlen(value);if(n<1||n>18)return 400;
        for(size_t i=0;i<n;i++)if(!isalnum((unsigned char)value[i])&&value[i]!=' '&&value[i]!='_')return 400;
        for(int i=0;i<phone_party.count;i++)if(!strcasecmp(value,phone_party.players[i].name))return 409;
        int p=phone_party.count;if(phone_random(phone_party.players[p].token))return 500;
        snprintf(phone_party.players[p].name,20,"%.18s",value);phone_party.players[p].answer=-1;
        phone_party.count++;snprintf(reply,size,"%s",phone_party.players[p].token);return 200;
    }
    if(!strcmp(field,"answer")){
        char token[33],extra;int round,choice;
        if(sscanf(value,"%32[^:]:%d:%d%c",token,&round,&choice,&extra)!=3||choice<0||choice>4)return 400;
        if(phone_party.phase!=1||round!=phone_party.round)return 409;
        for(int i=0;i<phone_party.count;i++)if(!strcmp(token,phone_party.players[i].token)){
            if(phone_party.players[i].answer!=-1)return 409;
            phone_party.players[i].answer=choice;
            int all=1;for(int p=0;p<phone_party.count;p++)if(phone_party.players[p].answer<0)all=0;
            if(all&&(int32_t)(phone_party.deadline-phone_now)>1500)phone_party.deadline=phone_now+1500;
            snprintf(reply,size,"Answer locked in");return 200;
        }return 403;
    }
    if(!strcmp(field,"party")){
        if((strcmp(value,"start")&&strcmp(value,"start-clean")&&strcmp(value,"start-spicy"))||!phone_party.count)return 400;
        if(phone_party.phase==1||phone_party.phase==2)return 409;
        phone_party.spicy=!strcmp(value,"start-spicy");
        char random[33];if(phone_random(random))return 500;
        uint32_t seed=(uint32_t)strtoul(random+24,NULL,16);phone_deck(seed?seed:1);
        phone_party.round=0;phone_party.phase=1;phone_party.deadline=phone_now+25000;
        for(int i=0;i<phone_party.count;i++){phone_party.players[i].answer=-1;phone_party.players[i].score=phone_party.players[i].gain=phone_party.players[i].streak=0;}
        snprintf(reply,size,"Quiz started");return 200;
    }
    if(!strcmp(field,"subtitle")){
        if(value[0]&&(!phone_filename(value)||!phone_is_sub(value)))return 400;
        if(value[0]){snprintf(phone_subtitle,sizeof(phone_subtitle),"phone-files/%.100s",value);if(flxtr_path_access(phone_subtitle,R_OK)){phone_subtitle[0]=0;return 404;}}
        else phone_subtitle[0]=0;
        snprintf(reply,size,"Subtitle selected for next playback");return 200;
    }
    if(strcmp(field,"play")&&strcmp(field,"url")&&strcmp(field,"control")&&strcmp(field,"music"))return 400;
    if(phone_pending[0])return 409;
    if(!strcmp(field,"play")){
        if(!phone_filename(value)||phone_is_rom(value)||phone_is_sub(value))return 400;
        char path[256];snprintf(path,sizeof(path),"phone-files/%.100s",value);if(flxtr_path_access(path,R_OK))return 404;
    }else if(!strcmp(field,"music")){
        /* Public YouTube URLs, searches and Spotify playlist links.
         * No shell interpretation: the worker receives one literal argument. */
        size_t len=strlen(value);
        if(!len||len>220)return 400;
        if(strstr(value,"://")){
            if(strncmp(value,"https://",8))return 400;
            if(strncmp(value,"https://open.spotify.com/playlist/",34)&&
               strncmp(value,"https://music.youtube.com/",26)&&
               strncmp(value,"https://www.youtube.com/",24)&&
               strncmp(value,"https://youtube.com/",20)&&
               strncmp(value,"https://youtu.be/",17))return 400;
        }
    }else if(!strcmp(field,"url")){
        if((strncmp(value,"https://",8)&&strncmp(value,"http://",7))||strpbrk(value," \"<>\\")||strlen(value)>1800)return 400;
    }else if(strlen(value)!=1||!strchr("pfrsmudalbv",value[0]))return 400;
    snprintf(phone_pending,sizeof(phone_pending),"%s",field);snprintf(phone_value,sizeof(phone_value),"%s",value);
    #ifdef FLXTR_PHONE_DISPATCH
    return FLXTR_PHONE_DISPATCH(reply,size);
#else
    snprintf(reply,size,"Request queued on TV");return 200;
#endif
}
static void phone_state(char *out,size_t size){
    const PhoneQuestion *q=phone_question();
    int left=(int32_t)(phone_party.deadline-phone_now)>0?(int)(phone_party.deadline-phone_now)/1000:0;
    size_t n=(size_t)snprintf(out,size,"{\"phase\":%d,\"round\":%d,\"rounds\":%d,\"kind\":%d,\"spicy\":%d,\"seconds\":%d,\"question\":\"%s\",\"choices\":[\"%s\",\"%s\",\"%s\",\"%s\"],\"correct\":%d,\"winners\":%d,\"explanation\":\"%s\",\"votes\":[%d,%d,%d,%d],\"players\":[",phone_party.phase,phone_party.round,PHONE_ROUNDS,q->kind,phone_party.spicy,left,q->question,q->answers[0],q->answers[1],q->answers[2],q->answers[3],phone_party.phase==2?q->correct:-1,phone_party.phase==2?phone_party.winners:0,phone_party.phase==2?q->explanation:"",phone_party.phase==2?phone_party.votes[0]:0,phone_party.phase==2?phone_party.votes[1]:0,phone_party.phase==2?phone_party.votes[2]:0,phone_party.phase==2?phone_party.votes[3]:0);
    for(int i=0;i<phone_party.count&&n<size;i++)n+=(size_t)snprintf(out+n,size-n,"%s{\"name\":\"%s\",\"score\":%d,\"gain\":%d,\"answered\":%d}",i?",":"",phone_party.players[i].name,phone_party.players[i].score,phone_party.phase==2?phone_party.players[i].gain:0,phone_party.players[i].answer>=0);
    if(n<size)n+=(size_t)snprintf(out+n,size-n,"],\"files\":[");
    DIR *d=opendir("phone-files");int count=0;
    if(d){struct dirent *e;while((e=readdir(d))&&count<32&&n+180<size){if(!phone_filename(e->d_name))continue;n+=(size_t)snprintf(out+n,size-n,"%s\"%s\"",count++?",":"",e->d_name);}closedir(d);}
    if(n<size)snprintf(out+n,size-n,"]}");
}
#endif
