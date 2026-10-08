/* Single hardware overlay plane: the menu owns it while visible. */
static int control_fd=-1,menu_open,menu_row,paused,requested_action;
static int menu_height=720,menu_subtitles,menu_episodes,menu_servers;
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
    SLVideo_HideOverlay(menu_overlay);
    uint32_t *pixels=NULL;int pitch=0;SLVideo_GetOverlayPixels(menu_overlay,&pixels,&pitch);
    if(!pixels||pitch<740*4)return;
    for(int y=0;y<416;y++)for(int x=0;x<740;x++)((uint32_t*)((char*)pixels+y*pitch))[x]=0xe8000000;
    char rows[11][80],heading[80];int seconds=(int)playback_position();
    snprintf(heading,sizeof(heading),"FLXTR  %02d:%02d:%02d / %d MIN",seconds/3600,seconds/60%60,seconds%60,(int)(media_duration/60));
    snprintf(rows[0],80,"%s",paused?"RESUME":"PAUSE");
    snprintf(rows[1],80,"SEEK: LEFT -10S / RIGHT +10S");
    snprintf(rows[2],80,"SUBTITLES: %s",(const char*[]){"OFF","AUTO","ENGLISH","NORWEGIAN"}[menu_subtitles]);
    snprintf(rows[3],80,"SUBTITLE SIZE: %s",subtitle_size==2?"NORMAL":"LARGE");
    snprintf(rows[4],80,"SUBTITLE DELAY: %+d SEC",subtitle_delay);
    snprintf(rows[5],80,"VIDEO SIZE: %s",view_names[viewing]);
    snprintf(rows[6],80,"QUALITY LIMIT: %dP",menu_height);
    snprintf(rows[7],80,"%s",menu_servers?"TRY NEXT SERVER":"NEXT SERVER: NOT AVAILABLE");
    snprintf(rows[8],80,"%s",menu_episodes?"PREVIOUS EPISODE":"PREVIOUS EPISODE: NOT AVAILABLE");
    snprintf(rows[9],80,"%s",menu_episodes?"NEXT EPISODE":"NEXT EPISODE: NOT AVAILABLE");
    snprintf(rows[10],80,"STOP PLAYBACK");
    menu_text(pixels,pitch,16,heading,0xff74ff84);
    for(int i=0;i<11;i++){char line[80];snprintf(line,sizeof(line),"%c %.70s",i==menu_row?'>':' ',rows[i]);menu_text(pixels,pitch,54+i*29,line,i==menu_row?0xff74ff84:0xffd4e2d6);}
    menu_text(pixels,pitch,386,"DPAD SELECT / A CHANGE / B CLOSE",0xff74ff84);
    SLVideo_SetOverlayDisplayArea(menu_overlay,0.115f,0.10f,0.77f,0.77f);SLVideo_ShowOverlay(menu_overlay);
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
static void menu_command(char c){
    if(c=='m'||(c=='b'&&menu_open)){
        menu_open=!menu_open;
        if(!menu_open){if(menu_overlay)SLVideo_HideOverlay(menu_overlay);subtitle_visible=-2;}else menu_draw();
        return;
    }
    if(c=='b'){menu_restart(7,playback_position());return;}
    if(c=='v'){viewing=(viewing+1)%VIEW_COUNT;apply_view(!menu_open);}
    if(!menu_open){if(c=='l'||c=='r')menu_restart(1,playback_position()+(c=='l'?-10:10));return;}
    if(c=='u')menu_row=(menu_row+10)%11;
    if(c=='d')menu_row=(menu_row+1)%11;
    if(c=='a'||c=='l'||c=='r'){
        int direction=c=='l'?-1:1;
        switch(menu_row){
        case 0:
            if(!paused){paused=1;pause_started=av_gettime_relative();}
            else {if(clock_start)clock_start+=av_gettime_relative()-pause_started;paused=0;}
            if(menu_audio)SDL_PauseAudioDevice(menu_audio,paused);
            break;
        case 1:menu_restart(1,playback_position()+direction*10);break;
        case 2:menu_subtitles=(menu_subtitles+direction+4)%4;menu_restart(1,playback_position());break;
        case 3:subtitle_size=subtitle_size==2?3:2;subtitle_visible=-2;break;
        case 4:subtitle_delay+=direction;if(subtitle_delay>5)subtitle_delay=5;if(subtitle_delay< -5)subtitle_delay=-5;subtitle_visible=-2;break;
        case 5:viewing=(viewing+direction+VIEW_COUNT)%VIEW_COUNT;apply_view(0);break;
        case 6:menu_restart(direction>0?2:3,playback_position());break;
        case 7:if(menu_servers)menu_restart(4,playback_position());break;
        case 8:if(menu_episodes)menu_restart(5,0);break;
        case 9:if(menu_episodes)menu_restart(6,0);break;
        case 10:menu_restart(7,playback_position());break;
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
