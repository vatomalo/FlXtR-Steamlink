package org.flxtr.android;
/** Controller sticks require a dead zone and bounded repeat, not one click/frame. */
final class DirectionRepeater {
    private int held;private long next;
    int update(float x,float y,long now){
        int direction=Math.max(Math.abs(x),Math.abs(y))<0.5f?0:Math.abs(x)>=Math.abs(y)?(x<0?1:2):(y<0?3:4);
        if(direction==0){held=0;return 0;}
        if(direction!=held){held=direction;next=now+320;return direction;}
        if(now<next)return 0;next=now+160;return direction;
    }
}
