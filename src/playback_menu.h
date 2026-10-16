/* Single hardware overlay plane: the menu owns it while visible. */
static int control_fd=-1,menu_open,menu_row,menu_info,paused,requested_action;
static int menu_height=720,menu_subtitles,menu_episodes,menu_servers;
static int seek_scrubbing,seek_was_paused;
static double seek_target;
static double playback_start,media_duration;
static SDL_AudioDeviceID menu_audio;
static CSLVideoOverlay *menu_overlay;
static int64_t pause_started;
static double playback_position(void){
    if(origin==AV_NOPTS_VALUE||!clock_start)return playback_start;
    double p=(origin+(paused?pause_started:av_gettime_relative())-clock_start)/(double)AV_TIME_BASE;
    return p<0?0:p;
}
static void menu_text(uint32_t *pixels,int pitch,int y,const char *label,uint32_t color){
    for(int n=0;label[n]&&n<58;n++){
        size_t i;for(i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++)if(glyphs[i].c==label[n])break;
        if(i==sizeof(glyphs)/sizeof(glyphs[0]))continue;
        for(int yy=0;yy<7;yy++)for(int x=0;x<5;x++)if(glyphs[i].row[yy]&(16>>x))
            for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++)
                ((uint32_t*)((char*)pixels+(y+yy*2+dy)*pitch))[16+n*12+x*2+dx]=color;
    }
}
static void menu_draw(void){
    if(!view_context)return;
    if(!menu_overlay)menu_overlay=SLVideo_CreateOverlay(view_context,740,416);
    if(!menu_overlay)return;
    if(view_overlay)SLVideo_HideOverlay(view_overlay);
    if(subtitle_overlay)SLVideo_HideOverlay(subtitle_overlay);
    overlay_until=0;subtitle_visible=-2;
    /* Keep the overlay visible while repainting to avoid a blank-frame flash. */
    uint32_t *pixels=NULL;int pitch=0;SLVideo_GetOverlayPixels(menu_overlay,&pixels,&pitch);
    if(!pixels||pitch<740*4)return;
    for(int y=0;y<416;y++)for(int x=0;x<740;x++)((uint32_t*)((char*)pixels+y*pitch))[x]=seek_scrubbing?0x38000000:0xe8000000;
    if(menu_info){
        const char *title=getenv("FLXTR_VIDEO_TITLE"),*synopsis=getenv("FLXTR_VIDEO_SYNOPSIS");
        menu_text(pixels,pitch,22,"NOW PLAYING / VIDEO INFO",0xff74ff84);
        if(title&&*title)menu_text(pixels,pitch,68,title,0xffd4e2d6);
        else menu_text(pixels,pitch,68,"UNKNOWN TITLE",0xffd4e2d6);
        menu_text(pixels,pitch,117,"SYNOPSIS",0xff74ff84);
        if(!synopsis||!*synopsis)menu_text(pixels,pitch,157,"DESCRIPTION NOT CACHED",0xffd4e2d6);
        else{
            int row=0;
            const char *p=synopsis;
            while(*p&&row<8){
                char line[52];int n=0;
                while(p[n]&&p[n]!='\\n'&&n<49)n++;
                if(n==49&&p[n]&&p[n]!=' '){int cut=n;while(cut>0&&p[cut]!=' ')cut--;if(cut>0)n=cut;}
                memcpy(line,p,(size_t)n);line[n]=0;
                menu_text(pixels,pitch,157+row*26,line,0xffd4e2d6);
                p+=n;while(*p==' '||*p=='\\n')p++;
                row++;
            }
        }
        menu_text(pixels,pitch,383,"B / START RETURN TO PLAYBACK MENU",0xff74ff84);
        SLVideo_SetOverlayDisplayArea(menu_overlay,0,0,1,1);SLVideo_ShowOverlay(menu_overlay);
        return;
    }
    char rows[15][80],heading[80];int seconds=(int)(seek_scrubbing?seek_target:playback_position());
    snprintf(heading,sizeof(heading),"FLXTR  %02d:%02d:%02d / %d MIN",seconds/3600,seconds/60%60,seconds%60,(int)(media_duration/60));
    snprintf(rows[0],80,"%s",paused?"RESUME":"PAUSE");
    snprintf(rows[1],80,"%s",seek_scrubbing?"SEEK PREVIEW: LEFT/RIGHT / A PLAY / B CANCEL":"SEEK: LEFT/RIGHT PREVIEW / A PLAY");
    snprintf(rows[2],80,"SUBTITLES: %s",(const char*[]){"OFF","AUTO","ENGLISH","NORWEGIAN","SPANISH"}[menu_subtitles]);
    snprintf(rows[3],80,"SUBTITLE SIZE: %s",subtitle_size==2?"NORMAL":"LARGE");
    snprintf(rows[4],80,"SUBTITLE DELAY: %+d SEC",subtitle_delay);
    snprintf(rows[5],80,"VIDEO SIZE: %s",view_names[viewing]);
    snprintf(rows[6],80,"QUALITY LIMIT: %dP",menu_height);
    snprintf(rows[7],80,"%s",menu_servers?"TRY NEXT SERVER":"NEXT SERVER: NOT AVAILABLE");
    snprintf(rows[8],80,"%s",menu_episodes?"PREVIOUS EPISODE":"PREVIOUS EPISODE: NOT AVAILABLE");
    snprintf(rows[9],80,"%s",menu_episodes?"NEXT EPISODE":"NEXT EPISODE: NOT AVAILABLE");
    snprintf(rows[10],80,"%s",menu_episodes?"[EPISODES] SEASONS / EPISODES":"[EPISODES] NOT AVAILABLE");
    snprintf(rows[11],80,"VIDEO INFO / SYNOPSIS");
    snprintf(rows[12],80,"SAVE BOOKMARK");
    snprintf(rows[13],80,"FREEZE TO BACKGROUND");
    snprintf(rows[14],80,"STOP PLAYBACK");
    menu_text(pixels,pitch,16,heading,0xff74ff84);
    for(int i=0;i<15;i++){char line[80];snprintf(line,sizeof(line),"%c %.70s",i==menu_row?'>':' ',rows[i]);menu_text(pixels,pitch,49+i*19,line,i==menu_row?0xff74ff84:0xffd4e2d6);}
    /* Native overlay progress timeline. Clamp to the known duration;
     * unknown-length live streams show elapsed time without a false percentage. */
    double position=seek_scrubbing?seek_target:playback_position();
    if(position<0)position=0;
    double fraction=media_duration>0?position/media_duration:0;
    if(fraction<0)fraction=0;
    if(fraction>1)fraction=1;
    const int bar_left=18,bar_top=359,bar_width=704,bar_height=9;
    int filled=(int)(fraction*bar_width);
    for(int y=bar_top;y<bar_top+bar_height;y++){
        uint32_t *line=(uint32_t*)((char*)pixels+y*pitch);
        for(int x=bar_left;x<bar_left+bar_width;x++)
            line[x]=x<bar_left+filled?0xff74ff84:0xff394d42;
    }
    char timeline[80];
    int elapsed=(int)position;
    if(media_duration>0){
        int length=(int)media_duration;
        snprintf(timeline,sizeof(timeline),"%02d:%02d:%02d / %02d:%02d:%02d",
            elapsed/3600,elapsed/60%60,elapsed%60,
            length/3600,length/60%60,length%60);
    }else snprintf(timeline,sizeof(timeline),"%02d:%02d:%02d / LIVE OR UNKNOWN",
        elapsed/3600,elapsed/60%60,elapsed%60);
    menu_text(pixels,pitch,371,timeline,0xffd4e2d6);
    menu_text(pixels,pitch,386,"DPAD / A CHANGE / X NEXT EP / B CLOSE",0xff74ff84);
    SLVideo_SetOverlayDisplayArea(menu_overlay,0.0f,0.0f,1.0f,1.0f);SLVideo_ShowOverlay(menu_overlay);
}
static void menu_restart(int action,double position){
    if(position<0)position=0;
    if(media_duration>1&&position>=media_duration)position=media_duration-1;
    FILE *f=fopen("playback-request.next","w");
    if(!f){show_view("CONTROL REQUEST FAILED");return;}
    fprintf(f,"%d %.3f %d %d %d %d\n",action,position,viewing,menu_subtitles,subtitle_size,subtitle_delay);
    if(fclose(f)||rename("playback-request.next","playback-request")){show_view("CONTROL REQUEST FAILED");return;}
    requested_action=40;stopped=1;
}
/* Per-title timestamp, kept separate from playback cache and ROM saves. */
static int bookmark_save(double position){
    const char *key=getenv("FLXTR_BOOKMARK_KEY");
    if(!key||strlen(key)!=8||strspn(key,"0123456789abcdef")!=8||
       !(position>=0&&position<=86400))return 0;
    mkdir("bookmarks",0700);
    char path[64],temp[72];
    snprintf(path,sizeof(path),"bookmarks/%s.txt",key);
    snprintf(temp,sizeof(temp),"%s.next",path);
    FILE *f=fopen(temp,"w");if(!f)return 0;
    fprintf(f,"%.3f\n",position);
    if(fclose(f)||rename(temp,path)){unlink(temp);return 0;}
    show_view("BOOKMARK SAVED");
    return 1;
}
static void seek_cancel(void){
    if(!seek_scrubbing)return;
    seek_scrubbing=0;
    if(!seek_was_paused){
        if(clock_start)clock_start+=av_gettime_relative()-pause_started;
        paused=0;
        if(menu_audio)SDL_PauseAudioDevice(menu_audio,0);
    }
}
static void menu_command(char c){
    if(menu_info){if(c=='b'||c=='m'||c=='a'){menu_info=0;menu_draw();}return;}
    if(c=='n'){if(menu_episodes)menu_restart(6,0);return;}
    if(c=='P'){
        if(!paused){paused=1;pause_started=av_gettime_relative();}
        else{if(clock_start)clock_start+=av_gettime_relative()-pause_started;paused=0;}
        if(menu_audio)SDL_PauseAudioDevice(menu_audio,paused);
        return;
    }
    if(c=='F'||c=='R'){
        double target=playback_position()+(c=='F'?10:-10);
        if(target<0)target=0;
        if(media_duration>1&&target>=media_duration)target=media_duration-1;
        menu_restart(1,target);return;
    }
    if(c=='S'){menu_restart(7,playback_position());return;}
    if(seek_scrubbing&&(c=='b'||c=='m')){seek_cancel();menu_draw();return;}
    if(c=='m'||(c=='b'&&menu_open)){
        menu_open=!menu_open;
        if(!menu_open){if(menu_overlay)SLVideo_HideOverlay(menu_overlay);subtitle_visible=-2;}else menu_draw();
        return;
    }
    if(c=='b'){menu_restart(7,playback_position());return;}
    if(c=='v'){viewing=(viewing+1)%VIEW_COUNT;apply_view(!menu_open);}
    if(c=='l'||c=='r'){
        /* Left/right always invokes the same persistent seek preview.
         * A confirms the target; B cancels without restarting playback.
         * Other menu options remain changeable with A. */
        menu_open=1;
        menu_row=1;
    }
    if(!menu_open)return;
    if(c=='u'&&!seek_scrubbing)menu_row=(menu_row+14)%15;
    if(c=='d'&&!seek_scrubbing)menu_row=(menu_row+1)%15;
    if(c=='a'||c=='l'||c=='r'){
        int direction=c=='l'?-1:1;
        switch(menu_row){
        case 0:
            if(!paused){paused=1;pause_started=av_gettime_relative();}
            else {if(clock_start)clock_start+=av_gettime_relative()-pause_started;paused=0;}
            if(menu_audio)SDL_PauseAudioDevice(menu_audio,paused);
            break;
        case 1:
            if(!seek_scrubbing){
                seek_target=playback_position();
                seek_was_paused=paused;
                seek_scrubbing=1;
                if(!paused){
                    paused=1;pause_started=av_gettime_relative();
                    if(menu_audio)SDL_PauseAudioDevice(menu_audio,1);
                }
            }
            if(c=='a')menu_restart(1,seek_target);
            else{
                seek_target+=direction*10;
                if(seek_target<0)seek_target=0;
                if(media_duration>1&&seek_target>=media_duration)seek_target=media_duration-1;
            }
            break;
        case 2:menu_subtitles=(menu_subtitles+direction+5)%5;menu_restart(1,playback_position());break;
        case 3:subtitle_size=subtitle_size==2?3:2;subtitle_visible=-2;break;
        case 4:subtitle_delay+=direction;if(subtitle_delay>5)subtitle_delay=5;if(subtitle_delay< -5)subtitle_delay=-5;subtitle_visible=-2;break;
        case 5:viewing=(viewing+direction+VIEW_COUNT)%VIEW_COUNT;apply_view(0);break;
        case 6:menu_restart(direction>0?2:3,playback_position());break;
        case 7:if(menu_servers)menu_restart(4,playback_position());break;
        case 8:if(menu_episodes)menu_restart(5,0);break;
        case 9:if(menu_episodes)menu_restart(6,0);break;
        case 10:if(menu_episodes)menu_restart(8,playback_position());break;
        case 11:menu_info=1;break;
        case 12:bookmark_save(playback_position());break;
        case 13:if(bookmark_save(playback_position()))menu_restart(9,playback_position());break;
        case 14:menu_restart(7,playback_position());break;
        }
    }
    if(menu_open&&!stopped)menu_draw();
}
static void menu_poll(void){
    do {
        char commands[32];ssize_t n;
        while(control_fd>=0&&(n=read(control_fd,commands,sizeof(commands)))>0)
            for(ssize_t i=0;i<n&&!stopped;i++)menu_command(commands[i]);
        if(paused&&!stopped)SDL_Delay(20);
    }while(paused&&!stopped);
}
