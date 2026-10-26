#ifndef FLXTR_USB_SETUP_UI_H
#define FLXTR_USB_SETUP_UI_H
#if (!defined(_WIN32) && !defined(FLXTR_DESKTOP)) || defined(FLXTR_USB_SETUP_TEST)
typedef struct {char device[64],model[96],size[32],serial[160],identity[65],port[160];int locked;} UsbSetupDisk;
static UsbSetupDisk usb_disks[16];
static int usb_count,usb_selected,usb_operation,usb_job_kind,usb_continue,usb_code,usb_activate,usb_success;
static SDL_Keycode usb_directions[4];
static char usb_message[192],usb_plan[256],usb_output[256],usb_log[256],usb_confirmation[64],usb_layout[192],usb_direction_text[64];
static pid_t usb_pid;
static Uint32 usb_started;
static void usb_read_message(void){
    FILE *file=fopen(usb_log,"r");char line[256];if(!file)return;
    while(fgets(line,sizeof(line),file)){line[strcspn(line,"\r\n")]=0;if(line[0])snprintf(usb_message,sizeof(usb_message),"%.190s",line);}
    fclose(file);
}
static void usb_job(int kind){
    if(usb_pid)return;
    usb_job_kind=kind;usb_started=SDL_GetTicks();usb_stage=kind==3?5:1;
    snprintf(usb_message,sizeof(usb_message),"%s",kind==3?"Working on the selected USB. Do not remove it.":"Reading USB identity and layout...");
    usb_pid=shell_fork();
    if(!usb_pid){
        setpgid(0,0);
        int out=open(usb_output,O_WRONLY|O_CREAT|O_TRUNC,0600),log=open(usb_log,O_WRONLY|O_CREAT|O_TRUNC,0600);
        if(out<0||log<0)_exit(126);
        dup2(out,1);dup2(log,2);close(out);close(log);
        if(kind==1)execl("/bin/sh","sh","scripts/music-runtime.sh","tools/usb_setup.py","scan","--tsv",(char*)NULL);
        else if(kind==2)execl("/bin/sh","sh","scripts/music-runtime.sh","tools/usb_setup.py","plan","--device",usb_disks[usb_selected].device,
                             "--action",usb_operation?"prepare":"restore","--out",usb_plan,(char*)NULL);
        else execl("/bin/sh","sh","scripts/music-runtime.sh","tools/usb_setup.py","execute","--plan",usb_plan,"--confirm",usb_confirmation,(char*)NULL);
        _exit(127);
    }
    if(usb_pid>0)setpgid(usb_pid,usb_pid);
    else{usb_pid=0;usb_stage=6;snprintf(usb_message,sizeof(usb_message),"Could not start USB setup.");}
}
static void usb_open(void){
    if(usb_pid){snprintf(status,sizeof(status),"A USB task is still running.");return;}
    if(access("tools/usb_setup.py",R_OK)){snprintf(status,sizeof(status),"USB SETUP TOOL IS NOT INSTALLED");return;}
    preview_stop();youtube_stop();tv_active=0;tv_cancel_prepared();music_stop();
    char root[160];snprintf(root,sizeof(root),"/tmp/flxtr-usb-ui-%lu",(unsigned long)getuid());
    mkdir(root,0700);chmod(root,0700);
    snprintf(usb_plan,sizeof(usb_plan),"%s/plan.json",root);snprintf(usb_output,sizeof(usb_output),"%s/output.tsv",root);snprintf(usb_log,sizeof(usb_log),"%s/task.log",root);
    usb_on=1;usb_count=usb_selected=usb_operation=usb_code=usb_success=0;usb_job(1);
}
static int usb_poll(void){
    if(!usb_pid)return 0;
    int code;pid_t done=waitpid(usb_pid,&code,WNOHANG);if(done!=usb_pid)return 0;
    usb_pid=0;
    if(!WIFEXITED(code)||WEXITSTATUS(code)){
        usb_read_message();usb_stage=usb_job_kind==2?2:6;usb_success=0;return 1;
    }
    FILE *file=fopen(usb_output,"r");char line[1024];
    if(usb_job_kind==1){
        usb_count=0;
        if(file)while(usb_count<16&&fgets(line,sizeof(line),file)){
            char *fields[8],*part=line;int n=0;
            line[strcspn(line,"\r\n")]=0;
            while(n<8){fields[n++]=part;char *tab=strchr(part,'\t');if(!tab)break;*tab=0;part=tab+1;}
            if(n<6)continue;
            UsbSetupDisk *disk=&usb_disks[usb_count++];
            snprintf(disk->device,sizeof(disk->device),"%s",fields[0]);snprintf(disk->model,sizeof(disk->model),"%s",fields[1]);
            snprintf(disk->size,sizeof(disk->size),"%s",fields[2]);snprintf(disk->serial,sizeof(disk->serial),"%s",fields[3]);
            snprintf(disk->identity,sizeof(disk->identity),"%s",fields[4]);disk->locked=!strcmp(fields[5],"LOCKED");
            snprintf(disk->port,sizeof(disk->port),"%s",n>=8?fields[7]:"");
        }
        usb_stage=usb_count?2:6;snprintf(usb_message,sizeof(usb_message),"%s",usb_count?"Choose the USB by model, size and serial.":"No physical USB storage drive was detected.");
    }else if(usb_job_kind==2){
        int identity_ok=0,steps=0;usb_confirmation[0]=usb_layout[0]=usb_direction_text[0]=0;
        if(file)while(fgets(line,sizeof(line),file)){
            char *value=strchr(line,'\t');if(!value)continue;*value++=0;value[strcspn(value,"\r\n")]=0;
            if(!strcmp(line,"identity"))identity_ok=!strcmp(value,usb_disks[usb_selected].identity);
            else if(!strcmp(line,"confirmation"))snprintf(usb_confirmation,sizeof(usb_confirmation),"%.63s",value);
            else if(!strcmp(line,"layout"))snprintf(usb_layout,sizeof(usb_layout),"%.191s",value);
            else if(!strcmp(line,"activate"))usb_activate=atoi(value);
            else if(!strcmp(line,"directions"))snprintf(usb_direction_text,sizeof(usb_direction_text),"%.63s",value);
        }
        char directions[64];snprintf(directions,sizeof(directions),"%s",usb_direction_text);char *save=NULL;
        for(char *word=strtok_r(directions," ",&save);word&&steps<4;word=strtok_r(NULL," ",&save)){
            SDL_Keycode key=!strcmp(word,"UP")?SDLK_UP:!strcmp(word,"DOWN")?SDLK_DOWN:!strcmp(word,"LEFT")?SDLK_LEFT:!strcmp(word,"RIGHT")?SDLK_RIGHT:0;
            if(!key)break;
            usb_directions[steps++]=key;
        }
        if(!identity_ok||!usb_confirmation[0]||steps!=4){usb_stage=6;usb_success=0;snprintf(usb_message,sizeof(usb_message),"Drive changed or review was invalid. Scan again.");}
        else{usb_stage=3;usb_continue=usb_code=0;}
    }else{
        usb_stage=6;usb_success=1;
        snprintf(usb_message,sizeof(usb_message),"%s",usb_activate?"Mounts and links configured. Reopen FlXtR from the Steam menu.":"Spare USB prepared. Your current USB mappings were kept.");
    }
    if(file)fclose(file);
    return 1;
}
static void usb_action(SDL_Keycode key){
    if(usb_stage==5)return; /* No mid-format cancellation or accidental navigation. */
    if(usb_stage==6&&usb_success&&usb_activate){if(key==SDLK_RETURN||key==SDLK_ESCAPE||key==SDLK_BACKSPACE){running=0;restart_shell=0;}return;}
    if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE){
        if(usb_pid){kill(-usb_pid,SIGKILL);kill(usb_pid,SIGKILL);}
        usb_on=0;return;
    }
    if(usb_stage==1)return;
    if(usb_stage==2){
        if(key==SDLK_UP&&usb_selected>0)usb_selected--;
        if(key==SDLK_DOWN&&usb_selected+1<usb_count)usb_selected++;
        if(key==SDLK_LEFT||key==SDLK_RIGHT)usb_operation=!usb_operation;
        if(key==SDLK_RETURN){
            if(usb_operation&&usb_disks[usb_selected].locked)snprintf(usb_message,sizeof(usb_message),"This USB contains active apps/libraries and cannot be erased.");
            else usb_job(2);
        }
    }else if(usb_stage==3){
        if(key==SDLK_UP||key==SDLK_DOWN||key==SDLK_LEFT||key==SDLK_RIGHT)usb_continue=!usb_continue;
        if(key==SDLK_RETURN){if(!usb_continue)usb_stage=2;else if(usb_operation){usb_stage=4;usb_code=0;}else usb_job(3);}
    }else if(usb_stage==4){
        if(usb_code<4&&(key==SDLK_UP||key==SDLK_DOWN||key==SDLK_LEFT||key==SDLK_RIGHT)){
            usb_code=key==usb_directions[usb_code]?usb_code+1:0;
        }else if(usb_code==4&&key==SDLK_RETURN)usb_job(3);
    }else if(usb_stage==6&&key==SDLK_RETURN){usb_selected=0;usb_job(1);}
}
static void usb_draw(void){
    SDL_Color warning={255,110,85,255};
    SDL_Color green={116,255,132,255},white={212,226,214,255},dim={150,174,155,255};
    SDL_SetRenderDrawColor(renderer,8,12,15,255);SDL_RenderClear(renderer);
    text(55,35,"USB STORAGE SETUP",3,green,45);
    if(usb_stage==1||usb_stage==5){
        text(55,120,usb_stage==5?"WORKING ON SELECTED USB":"READING USB DRIVES",2,white,60);
        char elapsed[96];snprintf(elapsed,sizeof(elapsed),"ELAPSED %u SECONDS",(unsigned)((SDL_GetTicks()-usb_started)/1000));text(55,175,elapsed,1,dim,70);
        wrap(55,235,usb_message,72,3,white);
        if(usb_stage==5)text(55,425,"DO NOT REMOVE THE USB OR CUT POWER",2,warning,65);
        else text(55,425,"B CANCEL",1,dim,70);
    }else if(usb_stage==6){
        wrap(55,135,usb_message,70,5,usb_success?green:warning);
        text(55,425,usb_success&&usb_activate?"A RETURN TO STEAM MENU":"A RESCAN / B CLOSE",2,white,70);
    }else{
        UsbSetupDisk *disk=&usb_disks[usb_selected];char label[180];
        snprintf(label,sizeof(label),"USB %d/%d  %.38s  %.20s",usb_selected+1,usb_count,disk->model,disk->size);text(55,92,label,2,white,70);
        size_t serial_len=strlen(disk->serial),port_len=strlen(disk->port);
        snprintf(label,sizeof(label),"%.40s  SERIAL: %.24s%s%.24s",disk->device,disk->serial,serial_len>48?"...":"",serial_len>48?disk->serial+serial_len-24:disk->serial+((serial_len>24)?24:serial_len));text(55,124,label,1,dim,130);
        snprintf(label,sizeof(label),"USB PORT: %.100s",disk->port+(port_len>100?port_len-100:0));text(55,146,label,1,dim,130);
        if(usb_stage==2){
            text(55,185,usb_operation?"ERASE AND PREPARE USB":"USE / REPAIR EXISTING USB",2,usb_operation?warning:green,65);
            wrap(55,240,usb_operation?"ERASES ALL PARTITIONS, GAMES, SAVES AND FILES ON THIS USB. Other USBs are not selected.":"Restore the filesystem mounts, stable links and boot hook. Existing filesystems and files are kept.",72,4,white);
            if(disk->locked)text(55,355,"ACTIVE APPS/LIBRARIES: ERASING IS BLOCKED",1,warning,80);
            text(55,405,"UP/DOWN USB / LEFT/RIGHT ACTION / A REVIEW / B CLOSE",1,green,95);
            text(55,465,usb_message,1,dim,110);
        }else{
            text(55,170,usb_operation?"DANGER: ALL DATA ON THIS USB WILL BE LOST":"RESTORE MOUNTS AND SYMBOLIC LINKS",2,usb_operation?warning:green,74);
            text(55,215,usb_layout,1,white,100);
            wrap(55,250,usb_operation?"Back up this drive first. This removes its existing filesystems. Formatting is not a repair and recovery is not guaranteed. FAT32 files are limited to 4 GB.":"Uses the selected volume UUIDs. Adds a startup hook and ROM-directory links. Does not format or replace your games, saves or existing settings.",74,4,white);
            if(usb_stage==3){
                text(55,365,usb_continue?"  CANCEL     > CONTINUE":"> CANCEL       CONTINUE",2,green,70);
                text(55,425,"LEFT/RIGHT CHOOSE / A SELECT / B CANCEL",1,dim,85);
            }else{
                snprintf(label,sizeof(label),"D-PAD CODE: %s",usb_direction_text);text(55,365,label,2,white,80);
                snprintf(label,sizeof(label),"%d/4 COMPLETE",usb_code);text(55,405,label,1,green,70);
                text(55,445,usb_code==4?"A ERASE THIS USB NOW / B CANCEL":"ENTER CODE IN ORDER / B CANCEL",2,warning,76);
            }
        }
    }
}
#else
static void usb_open(void){snprintf(status,sizeof(status),"USB SETUP IS FOR STEAM LINK HARDWARE");}
static int usb_poll(void){return 0;}
static void usb_action(SDL_Keycode key){(void)key;usb_on=0;}
static void usb_draw(void){}
#endif
#endif
