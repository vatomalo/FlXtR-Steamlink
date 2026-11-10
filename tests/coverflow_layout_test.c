#include <assert.h>
#include <stdio.h>
#include "../src/coverflow_layout.h"
int main(void){
    FlowCard center=flow_card(0),left=flow_card(-1),right=flow_card(1),far=flow_card(3);
    assert(center.w>158&&center.h>236&&center.skew==0);
    assert(left.w==right.w&&left.h==right.h&&left.direction==-1&&right.direction==1);
    assert(left.w<center.w&&far.w<right.w&&left.skew>0&&left.h>left.skew*2);
    assert(center.y>=96&&center.y+center.h+5+35<444);
    for(int i=-24;i<=24;i++){FlowCard card=flow_card(i/4.0f);assert(card.w>0&&card.h>card.skew*2);}
    puts("PASS: larger center cover, symmetric perspective sides, bounded reflection and animated geometry");
    return 0;
}
