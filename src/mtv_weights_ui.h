#ifndef FLXTR_MTV_WEIGHTS_UI_H
#define FLXTR_MTV_WEIGHTS_UI_H
static int mtv_weights_on,mtv_weights_row=1,mtv_weights_first=1;
static int mtv_edit_weights[MTV_GENRES];
static char mtv_weights_message[100];
static void mtv_weights_open(void){
    mtv_weights_load(mtv_edit_weights);mtv_weights_on=1;
    mtv_weights_row=mtv_weights_first=1;mtv_weights_message[0]=0;
    settings_on=1;settings_tab=SETTINGS_MUSIC;settings_row=3;
}
static void mtv_fusion_step(int step){
    static const int choices[]={0,1,2,3,4,5,6,7,8,9,10,13,14,15,MTV_FUSION_CUSTOM};
    int count=(int)(sizeof(choices)/sizeof(choices[0])),index=0;
    for(int i=0;i<count;i++)if(choices[i]==mtv_fusion_genre)index=i;
    mtv_fusion_genre=choices[(index+step+count)%count];
    tv_cancel_prepared();
}
static void mtv_weights_action(SDL_Keycode key){
    if(key==SDLK_ESCAPE||key==SDLK_BACKSPACE){
        if(mtv_weights_save(mtv_edit_weights)){snprintf(mtv_weights_message,sizeof(mtv_weights_message),"SAVE FAILED / CHECK FREE SPACE AND PERMISSIONS");return;}
        mtv_weights_on=0;tv_cancel_prepared();snprintf(status,sizeof(status),"MTV GENRE WEIGHTS SAVED");return;
    }
    if(key==SDLK_UP&&mtv_weights_row>1)mtv_weights_row--;
    if(key==SDLK_DOWN&&mtv_weights_row<MTV_GENRES-1)mtv_weights_row++;
    int change=key==SDLK_LEFT?-1:key==SDLK_RIGHT?1:key==SDLK_v?-10:key==SDLK_m?10:0;
    if(change){
        int value=mtv_edit_weights[mtv_weights_row]+change;
        mtv_edit_weights[mtv_weights_row]=value<0?0:value>100?100:value;
    }
    if(key==SDLK_RETURN)mtv_edit_weights[mtv_weights_row]=mtv_edit_weights[mtv_weights_row]?0:1;
    if(key==SDLK_F2)for(int i=1;i<MTV_GENRES;i++)mtv_edit_weights[i]=1;
    if(mtv_weights_row<mtv_weights_first)mtv_weights_first=mtv_weights_row;
    if(mtv_weights_row>=mtv_weights_first+9)mtv_weights_first=mtv_weights_row-8;
}
static void mtv_weights_draw(void){
    SDL_Color green={116,255,132,255},white={212,226,214,255},dim={150,174,155,255};
    SDL_SetRenderDrawColor(renderer,8,12,15,255);SDL_RenderClear(renderer);
    text(55,30,"MTV GENRE WEIGHTS",3,green,50);
    char mode[120];snprintf(mode,sizeof(mode),"MTV: %.35s / FUSION: %.35s",mtv_genre_names[mtv_genre],mtv_fusion_name(mtv_fusion_genre));
    text(55,85,mode,1,dim,110);
    text(55,110,"USE CUSTOM / MIX OR FUSION WEIGHTED MIX TO APPLY WEIGHTS. ZERO MUTES.",1,dim,115);
    unsigned total=0;for(int i=1;i<MTV_GENRES;i++)total+=(unsigned)mtv_edit_weights[i];
    text(630,148,"WEIGHT / PER 100",1,dim,30);
    for(int row=0;row<9&&mtv_weights_first+row<MTV_GENRES;row++){
        int genre=mtv_weights_first+row,y=178+row*28;char value[40];
        SDL_Color color=genre==mtv_weights_row?green:mtv_edit_weights[genre]?white:dim;
        if(genre==mtv_weights_row)text(35,y,">",2,green,2);
        text(55,y,mtv_genre_names[genre],2,color,35);
        snprintf(value,sizeof(value),"%3d / %3u",mtv_edit_weights[genre],total?100u*(unsigned)mtv_edit_weights[genre]/total:0);
        text(630,y,value,2,color,25);
    }
    text(55,450,"UP/DOWN GENRE / LEFT/RIGHT +/-1 / LB/RB +/-10 / A MUTE",1,green,110);
    text(55,477,"X RESET TO EQUAL / B SAVE AND BACK",1,green,100);
    if(mtv_weights_message[0])text(55,510,mtv_weights_message,1,white,100);
    else text(55,510,total?"SCROLL DOWN FOR VGA, KARAOKE, ENKA, CITY POP AND LATIN":"ALL ZERO: CUSTOM / MIX USES THE DEFAULT SEARCH",1,dim,110);
}
#endif
