/* Runtime renderer included by the proxy after its WRAM and ROM helpers. */
#ifndef FLXTR_SOM_WEAPON_RENDERER_H
#define FLXTR_SOM_WEAPON_RENDERER_H
/* ---- Bushido per-frame blade masks (the native Katana is unchanged) ---- */
static void som_weapon_mask_load(void){
    if(weapon_mask_tried)return;
    weapon_mask_tried=1;
    som_load_pack_file(weapon_mask_file,"Bushido weapon masks",
                       &weapon_mask_pack,&weapon_mask_pack_size);
    if(!weapon_mask_pack||weapon_mask_pack_size<10u||
       memcmp(weapon_mask_pack,"SOMWORK1",8u))return;
    weapon_mask_index=calloc(5u*9u*256u,sizeof(*weapon_mask_index));
    if(!weapon_mask_index)return;
    const uint8_t *p=weapon_mask_pack+10u,*end=weapon_mask_pack+weapon_mask_pack_size;
    const unsigned entries=weapon_mask_pack[8]|(unsigned)weapon_mask_pack[9]<<8;
    unsigned count=0;
    for(unsigned e=0;e<entries;e++){
        if(end-p<44)goto malformed;
        if(p[0]<0xD4u||p[0]>0xD8u||p[1]<9u||p[1]>=18u||p[3]!=1u)goto malformed;
        const uint8_t *frame=p+44u;
        const uint8_t *next=som_work_next_frame(frame,end);
        if(!next||!frame[0]||!frame[3]||!frame[4])goto malformed;
        const unsigned index=((p[0]-0xD4u)*9u+p[1]-9u)*256u+p[2];
        if(weapon_mask_index[index])goto malformed;
        for(unsigned i=0;i<20u;i++)if(p[5u+2u*i]&0x80u)goto malformed;
        int visible=0;
        for(const uint8_t *pixel=frame+5u;pixel<next;pixel++){
            if(*pixel>10u)goto malformed;
            if(*pixel)visible=1;
        }
        if(!visible)goto malformed;
        weapon_mask_index[index]=(uint32_t)(p-weapon_mask_pack)+1u;
        count++;
        p=next;
    }
    if(p!=end)goto malformed;
    fprintf(stderr,"[som-ai] Bushido: indexed %u masked weapon frames\n",count);
    return;
malformed:
    free(weapon_mask_index);weapon_mask_index=NULL;
    fprintf(stderr,"[som-ai] Bushido: malformed mask pack ignored\n");
}
/* Blade-only profiles apply to selected sword styles. Other weapon
 * families retain their native art, including the Ninja's flail. */
static int som_weapon_mask_mode(const uint8_t *ram,unsigned who){
    if(!weapon_mask_on_checked||frame_number-weapon_mask_on_checked>=120ul){
        weapon_mask_on_checked=frame_number?frame_number:1ul;
        weapon_mask_on=access(weapon_mask_on_file,F_OK)==0;
    }
    const unsigned cls=som_disc_member_class(&disciplines,who);
    const uint8_t *actor=ram+0xE000u+0x200u*who;
    if(weapon_mask_faulted||!weapon_mask_on||cls>=SOM_DISCIPLINE_COUNT||actor[0x1E4]!=1u)
        return -1;
    if(kusarigama_attack_active&&who==kusarigama_attack_who&&cls==SOM_DISC_NINJUTSU&&
       styles.selected[cls]==SOM_STYLE_HEAVY)return 2;
    if(who)return -1;
    const unsigned style=styles.selected[cls]%SOM_STYLES_PER_CLASS;
    if(som_weapon_styles[som_style_index(cls,style)].weapon!=1u)return -1;
    switch(cls){
    case SOM_DISC_BUSHIDO:
        return style==SOM_STYLE_FAST?0:style==SOM_STYLE_HEAVY?1:-1;
    case SOM_DISC_SWORD:
    case SOM_DISC_NINJUTSU:
    case SOM_DISC_FLOW:
    case SOM_DISC_ROGUE:
        return style==SOM_STYLE_FAST?3:style==SOM_STYLE_HEAVY?1:-1;
    default:
        return -1;
    }
}
static int som_weapon_mask_entry(unsigned mode,unsigned row,unsigned frame,SomWorkEntry *out){
    if(!weapon_mask_index||mode>4u||row<9u||row>=18u||frame>255u)return 0;
    const uint32_t at=weapon_mask_index[(mode*9u+row-9u)*256u+frame];
    if(!at)return 0;
    const uint8_t *p=weapon_mask_pack+at-1u;
    out->kind=p[0];out->weapon=p[1];out->facing=p[2];out->frames=p[3];
    for(unsigned i=0;i<20u;i++)out->colours[i]=(uint16_t)(p[4+2u*i]|p[5+2u*i]<<8);
    out->first=p+44u;
    return 1;
}
static int som_kusarigama_art_ready(void){
    if(!weapon_mask_on||weapon_mask_faulted||!weapon_mask_index)return 0;
    SomWorkEntry entry;
    for(unsigned row=9u;row<18u;row++)
        for(unsigned frame=171u;frame<=179u;frame++)
            if(som_weapon_mask_entry(2u,row,frame,&entry))return 1;
    return 0;
}
/* Keep the native buffer intact outside the single backend frame and never
 * replace tiles that the backend refreshed after our pre-frame suppression. */
static int som_weapon_mask_restore(uint8_t *ram){
    if(!ram||!weapon_tiles_hidden)return 0;
    uint8_t *tiles=ram+SOM_WEAPON_BUF+0x1000u*weapon_tiles_who;
    weapon_tiles_hidden=0;
    for(unsigned i=0;i<sizeof(weapon_native_tiles);i++)if(tiles[i])return 0;
    memcpy(tiles,weapon_native_tiles,sizeof(weapon_native_tiles));
    return 1;
}
/* Hide native effects only when a matching mask exists for the current
 * sword row and animation frame. On a missing mask, restore the original
 * effect tiles byte-for-byte, rather than showing an invisible weapon. */
static void som_weapon_mask_frame(uint8_t *ram){
    if(!ram)return;
    som_weapon_mask_load();
    if(kusarigama_attack_active){
        const uint8_t *owner=ram+0xE000u+0x200u*kusarigama_attack_who;
        if(frame_number-kusarigama_attack_started>420ul||
           som_disc_member_class(&disciplines,kusarigama_attack_who)!=SOM_DISC_NINJUTSU||
           styles.selected[SOM_DISC_NINJUTSU]!=SOM_STYLE_HEAVY){
            kusarigama_attack_active=kusarigama_attack_released=kusarigama_swing_seen=0;
        }else if(kusarigama_attack_released&&owner[0x01C])kusarigama_swing_seen=1;
        else if(kusarigama_swing_seen&&!owner[0x01C])
            kusarigama_attack_active=kusarigama_attack_released=kusarigama_swing_seen=0;
    }
    const unsigned who=kusarigama_attack_active?kusarigama_attack_who:0u;
    const uint8_t *actor=ram+0xE000u+0x200u*who;
    const int field=actor[0]==1u&&ram[0xE4u]==1u&&ram[0xE6u]==0x0Fu;
    const int mode=field?som_weapon_mask_mode(ram,who):-1;
    SomWorkEntry entry;
    const int want=mode>=0&&som_weapon_mask_entry((unsigned)mode,actor[0x1E3u],
                                                  actor[0x026u]>>1,&entry);
    if(!want){
        som_weapon_mask_restore(ram);
        return;
    }
    uint8_t *tiles=ram+SOM_WEAPON_BUF+0x1000u*who;
    for(unsigned i=0;i<sizeof(weapon_native_tiles);i++){
        if(!tiles[i])continue;
        memcpy(weapon_native_tiles,tiles,sizeof(weapon_native_tiles));
        memset(tiles,0,sizeof(weapon_native_tiles));
        weapon_tiles_who=who;
        weapon_tiles_hidden=1;
        break;
    }
}
/* The runtime image is composited using the mask-specific alpha-aware
 * renderer, keeping original hero animation and camera coordinates. */
static void som_weapon_mask_pick(uint8_t *ram){
    weapon_masked.active=0;
    if(!ram||!weapon_tiles_hidden)return;
    const unsigned who=weapon_tiles_who;
    const uint8_t *actor=ram+0xE000u+0x200u*who;
    const int mode=som_weapon_mask_mode(ram,who);
    const unsigned row=actor[0x1E3u], frame=actor[0x026u]>>1;
    /* A backend-side equip/refresh may have restored the native weapon
     * within this frame. Do not composite a duplicate over that art. */
    const uint8_t *tiles=ram+SOM_WEAPON_BUF+0x1000u*who;
    for(unsigned i=0;i<sizeof(weapon_native_tiles);i++){
        if(!tiles[i])continue;
        weapon_tiles_hidden=0;
        return;
    }
    SomWorkEntry entry;
    if(mode<0||!som_weapon_mask_entry((unsigned)mode,row,frame,&entry)){
        /* The core has already drawn this frame. Restore its native WRAM
         * immediately, then composite the exact pose's native pixels if the
         * pack includes them. Old packs still fail closed. */
        if(!som_weapon_mask_restore(ram)||!som_weapon_mask_entry(4u,row,frame,&entry)){
            weapon_mask_faulted=1;
            fprintf(stderr,"[som-ai] Bushido mask coverage miss at row %u frame %u; switching to native art\n",
                    row,frame);
            return;
        }
    }
    memset(&weapon_masked,0,sizeof(weapon_masked));
    weapon_masked.active=1;weapon_masked.slot=(uint8_t)who;weapon_masked.kind=actor[0x180u];
    weapon_masked.dir=(actor[0x028u]&0x40u)?3u:2u;
    weapon_masked.entry=entry;weapon_masked.start=frame_number;weapon_masked.length=2u;
}

#endif
