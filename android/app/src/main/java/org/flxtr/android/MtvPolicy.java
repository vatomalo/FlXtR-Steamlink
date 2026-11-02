package org.flxtr.android;
import java.util.*;
final class MtvPolicy {
    static final String[] NAMES={"Custom / mix","Pop","Rock","Hip hop","Electronic","Metal","Jazz","Country","R&B / soul","K-pop","Anime music","VGA + genre","Karaoke + genre","Enka","City pop","Latin"};
    static final String[] QUERIES={"official music videos 80s 90s pop rock","official pop music videos","official rock music videos","official hip hop music videos","official electronic dance music videos","official metal music videos","official jazz music videos","official country music videos","official R&B soul music videos","official k-pop music videos","official anime music videos","video game anime soundtrack remix","karaoke instrumental backing track","official enka music videos","official city pop music videos","official latin salsa bachata reggaeton music videos"};
    static int pick(int[] weights,int seed){long sum=0;for(int i=1;i<Math.min(weights.length,16);i++)sum+=Math.max(0,Math.min(100,weights[i]));if(sum==0)return 0;long value=Integer.toUnsignedLong(seed)%sum;for(int i=1;i<Math.min(weights.length,16);i++){value-=Math.max(0,Math.min(100,weights[i]));if(value<0)return i;}return 0;}
    static String query(int genre,int fusion,int[] weights,int seed,String custom){
        if(genre==0&&!custom.trim().isEmpty())return custom.trim();if(genre==0)genre=pick(weights,seed);if(genre<0||genre>=NAMES.length)genre=0;
        if(genre==11||genre==12){int style=fusion>0&&fusion<NAMES.length&&fusion!=11&&fusion!=12?fusion:pick(weights,seed^0x12345678);String suffix=style>0&&style!=11&&style!=12?NAMES[style]:"pop rock";return genre==11?"video game anime soundtrack "+suffix+" remix":suffix+" karaoke instrumental backing track";}
        return genre==0&&!custom.trim().isEmpty()?custom:QUERIES[genre];
    }
}
