#ifndef FLXTR_STREAMING_SETTINGS_H
#define FLXTR_STREAMING_SETTINGS_H
/* Audio preferences are names, not enumeration indices: hotplug cannot redirect
 * a saved headset preference to a different output. */
static char stream_audio[192]="auto",stream_devices[12][184];
static int stream_device_count,stream_resolution=720,stream_fps=60,stream_bitrate=8000;
static int stream_audio_test;
static const int stream_bitrates[]={5000,8000,12000,20000,30000,40000};
static void streaming_save(void){
    FILE *f=fopen("streaming-audio.txt.next","w");
    if(f){fprintf(f,"%s\n",stream_audio);if(!fclose(f))rename("streaming-audio.txt.next","streaming-audio.txt");}
    f=fopen("streaming.cfg.next","w");
    if(f){fprintf(f,"resolution=%d\nfps=%d\nbitrate=%d\n",stream_resolution,stream_fps,stream_bitrate);
        if(!fclose(f))rename("streaming.cfg.next","streaming.cfg");}
}
static void streaming_load(void){
    FILE *f=fopen("streaming-audio.txt","r");
    if(f){if(fgets(stream_audio,sizeof(stream_audio),f))stream_audio[strcspn(stream_audio,"\r\n")]=0;fclose(f);}
    if(strcmp(stream_audio,"auto")&&strcmp(stream_audio,"hdmi")&&strcmp(stream_audio,"hdmi-sdl")&&strcmp(stream_audio,"sdl")&&strncmp(stream_audio,"device:",7))strcpy(stream_audio,"auto");
    f=fopen("moonlight/audio-devices.txt","r");stream_device_count=0;
    if(f){char line[192];while(stream_device_count<12&&fgets(line,sizeof(line),f)){
        line[strcspn(line,"\r\n")]=0;
        if(*line&&strlen(line)<sizeof(stream_devices[0]))strcpy(stream_devices[stream_device_count++],line);
    }fclose(f);}
    f=fopen("streaming.cfg","r");
    if(f){char key[32],line[80];int value;while(fgets(line,sizeof(line),f))if(sscanf(line,"%31[^=]=%d",key,&value)==2){
        if(!strcmp(key,"resolution")&&(value==720||value==1080))stream_resolution=value;
        if(!strcmp(key,"fps")&&(value==30||value==60))stream_fps=value;
        if(!strcmp(key,"bitrate"))for(int i=0;i<6;i++)if(value==stream_bitrates[i])stream_bitrate=value;
    }fclose(f);}
}
static const char *streaming_audio_label(void){
    if(!strcmp(stream_audio,"auto"))return "AUTOMATIC";
    if(!strcmp(stream_audio,"hdmi"))return "TV / HDMI";
    if(!strcmp(stream_audio,"hdmi-sdl"))return "TV / HDMI COMPATIBILITY";
    if(!strcmp(stream_audio,"sdl"))return "SYSTEM OUTPUT (SDL)";
    return stream_audio+7;
}
static void streaming_audio_step(int step){
    int index=0;
    if(!strcmp(stream_audio,"hdmi"))index=1;
    else if(!strcmp(stream_audio,"hdmi-sdl"))index=2;
    else if(!strcmp(stream_audio,"sdl"))index=3;
    else if(!strncmp(stream_audio,"device:",7))for(int i=0;i<stream_device_count;i++)if(!strcmp(stream_audio+7,stream_devices[i]))index=i+4;
    index=(index+step+4+stream_device_count)%(4+stream_device_count);
    if(index==0)strcpy(stream_audio,"auto");
    else if(index==1)strcpy(stream_audio,"hdmi");
    else if(index==2)strcpy(stream_audio,"hdmi-sdl");
    else if(index==3)strcpy(stream_audio,"sdl");
    else snprintf(stream_audio,sizeof(stream_audio),"device:%s",stream_devices[index-4]);
}
#endif
