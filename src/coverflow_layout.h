#ifndef FLXTR_COVERFLOW_LAYOUT_H
#define FLXTR_COVERFLOW_LAYOUT_H
typedef struct {int x,y,w,h,skew,direction;} FlowCard;
static FlowCard flow_card(float distance){
    float a=distance<0?-distance:distance,scale=1.0f/(1.0f+a*0.24f);
    int height=(int)(300*scale),width=(int)(200*scale/(1.0f+a*0.18f));
    int center=554+(int)(distance*148),bottom=400-(int)(a*9);
    FlowCard card={center-width/2,bottom-height,width,height,(int)(height*(a<1?a:1)*0.14f),distance<0?-1:1};
    return card;
}
#endif
