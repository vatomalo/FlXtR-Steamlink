/* Lightweight procedural atmosphere, rendered at the 960x540 logical resolution.
 * No textures, allocations or frame-to-frame state. Animation derives from ticks. */
#ifndef FLXTR_AMBIENT_EFFECTS_H
#define FLXTR_AMBIENT_EFFECTS_H
enum { AMBIENT_OFF, AMBIENT_MATRIX, AMBIENT_RAIN, AMBIENT_SNOW, AMBIENT_SAKURA, AMBIENT_COUNT };
static const char *const ambient_names[AMBIENT_COUNT]={"OFF","MATRIX CODE","RAIN","SNOW","SAKURA PETALS"};
static int ambient_effect,ambient_density=1,ambient_speed=1;
static uint32_t ambient_hash(uint32_t x){
    x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;
}
static int ambient_animated(void){return ambient_effect!=AMBIENT_OFF;}
static void ambient_draw(SDL_Renderer *r,Uint32 ticks){
    if(!ambient_animated())return;
    const int density[]={1,2,3},speed[]={1,2,3};
    const int amount=density[ambient_density],rate=speed[ambient_speed];
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    if(ambient_effect==AMBIENT_MATRIX){
        /* Fixed lanes and trailing glyphs. The head of each column moves independently. */
        for(int lane=0;lane<30*amount/2;lane++){
            uint32_t seed=ambient_hash((uint32_t)lane+0x314159u);
            int x=(lane*37+(int)(seed%19u))%W;
            int step=13,travel=(int)((ticks/(uint32_t)(85/rate+1)+seed%720u)%(uint32_t)(H/step+12));
            for(int tail=0;tail<7;tail++){
                int y=(travel-tail)*step;
                if(y<0||y>=H)continue;
                uint32_t code=ambient_hash(seed+(uint32_t)(travel-tail)*17u);
                SDL_Color ink=tail==0?(SDL_Color){165,255,190,190}:(SDL_Color){17,185,65,(Uint8)(105-tail*13)};
                SDL_SetRenderDrawColor(r,ink.r,ink.g,ink.b,ink.a);
                /* 3x5 procedural digital glyphs keep this renderer font-independent. */
                for(int py=0;py<5;py++)for(int px=0;px<3;px++){
                    if((ambient_hash(code+(uint32_t)py*11u)>>(px+3))&1u)
                        SDL_RenderDrawPoint(r,x+px*2,y+py*2);
                }
            }
        }
    }else{
        int count=ambient_effect==AMBIENT_RAIN?65*amount:ambient_effect==AMBIENT_SNOW?30*amount:23*amount;
        for(int i=0;i<count;i++){
            uint32_t seed=ambient_hash((uint32_t)i*733u+0x9e3779b9u);
            int x0=(int)(seed%(uint32_t)W);
            int phase=(int)((seed>>12)%((uint32_t)H+90u));
            int fall=ambient_effect==AMBIENT_RAIN?3:ambient_effect==AMBIENT_SNOW?1:2;
            int y=(phase+(int)((ticks/(uint32_t)(38/rate+1))*(uint32_t)fall)% (H+90))%(H+90)-45;
            int drift=(int)((ticks/(uint32_t)(150/rate+1)+(seed>>20))%41u)-20;
            int x=(x0+drift+(ambient_effect==AMBIENT_RAIN?y/7:0)+W)%W;
            if(y<0||y>=H)continue;
            if(ambient_effect==AMBIENT_RAIN){
                SDL_SetRenderDrawColor(r,130,170,215,(Uint8)(75+(seed%70u)));
                SDL_RenderDrawLine(r,x,y,x-3,y+9+(int)(seed%10u));
            }else if(ambient_effect==AMBIENT_SNOW){
                int size=1+(int)(seed%3u);
                SDL_SetRenderDrawColor(r,235,245,255,(Uint8)(120+(seed%100u)));
                SDL_Rect flake={x,y,size,size};SDL_RenderFillRect(r,&flake);
                if(size==3){SDL_RenderDrawPoint(r,x-1,y+1);SDL_RenderDrawPoint(r,x+3,y+1);}
            }else{
                /* Two small angled pink blades form a rotating, windblown petal. */
                SDL_SetRenderDrawColor(r,255,(Uint8)(145+seed%60u),200,(Uint8)(145+seed%85u));
                int tilt=(int)((ticks/240u+(seed>>16))%7u)-3;
                SDL_RenderDrawLine(r,x-3,y-1,x+2+tilt,y+2);
                SDL_RenderDrawLine(r,x-2,y-2,x+3+tilt,y+1);
                SDL_RenderDrawLine(r,x-1,y-3,x+2+tilt,y+3);
            }
        }
    }
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
}
#endif
