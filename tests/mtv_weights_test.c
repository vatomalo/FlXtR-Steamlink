#define _POSIX_C_SOURCE 200809L
/* Run: cc -std=c11 -Wall -Wextra -Werror -Isrc tests/mtv_weights_test.c -o /tmp/mtv-weights-test && /tmp/mtv-weights-test */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "mtv_preferences.h"
#include "mtv_weights.h"
static void weights_file(int first,int first_weight,int second,int second_weight){
    FILE *f=fopen("mtv-weights.tsv","w");assert(f);
    for(int i=1;i<MTV_GENRES;i++)
        fprintf(f,"%d %d\n",i,i==first?first_weight:i==second?second_weight:0);
    assert(!fclose(f));
}
int main(void){
    char tmp[]="/tmp/flxtr-mtv-XXXXXX";
    assert(mkdtemp(tmp));assert(chdir(tmp)==0);
    int weights[MTV_GENRES];mtv_weights_load(weights);
    assert(mtv_genre==0&&weights[1]>=0);
    weights_file(3,100,0,0);
    mtv_weights_load(weights);
    for(unsigned i=0;i<1000;i++)assert(mtv_weighted_genre(i)==3);
    weights_file(1,1,2,3);
    assert(mtv_weighted_genre(0)==1);
    assert(mtv_weighted_genre(1)==2);
    assert(mtv_weighted_genre(2)==2);
    assert(mtv_weighted_genre(3)==2);
    weights_file(11,70,12,30);
    for(unsigned i=0;i<100;i++)
        assert(mtv_weighted_genre(i)==(i<70?11:12));
    for(int genre=13;genre<MTV_GENRES;genre++){
        weights_file(genre,100,0,0);
        for(unsigned i=0;i<100;i++)assert(mtv_weighted_genre(i)==genre);
    }
    remove("mtv-weights.tsv");assert(chdir("/")==0);assert(rmdir(tmp)==0);
    puts("mtv_weights_test: PASS");
    return 0;
}
