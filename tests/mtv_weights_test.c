#define _POSIX_C_SOURCE 200809L
/* Run: cc -std=c11 -Wall -Wextra -Werror -Isrc tests/mtv_weights_test.c -o /tmp/mtv-weights-test && /tmp/mtv-weights-test */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "mtv_preferences.h"
#include "mtv_weights.h"
int main(void){
    char tmp[]="/tmp/flxtr-mtv-XXXXXX";
    assert(mkdtemp(tmp));assert(chdir(tmp)==0);
    int weights[MTV_GENRES];mtv_weights_load(weights);
    assert(weights[1]>=0);
    FILE *f=fopen("mtv-weights.tsv","w");assert(f);
    fprintf(f,"1 0\n2 0\n3 100\n4 0\n5 0\n6 0\n7 0\n8 0\n9 0\n10 0\n11 0\n12 0\n");
    assert(!fclose(f));
    mtv_weights_load(weights);
    for(unsigned i=0;i<1000;i++)assert(mtv_weighted_genre(i)==3);
    f=fopen("mtv-weights.tsv","w");assert(f);
    fprintf(f,"1 1\n2 3\n3 0\n4 0\n5 0\n6 0\n7 0\n8 0\n9 0\n10 0\n11 0\n12 0\n");assert(!fclose(f));
    assert(mtv_weighted_genre(0)==1);
    assert(mtv_weighted_genre(1)==2);
    assert(mtv_weighted_genre(2)==2);
    assert(mtv_weighted_genre(3)==2);
    f=fopen("mtv-weights.tsv","w");assert(f);
    fprintf(f,"1 0\\n2 0\\n3 0\\n4 0\\n5 0\\n6 0\\n7 0\\n8 0\\n9 0\\n10 0\\n11 70\\n12 30\\n");
    assert(!fclose(f));
    for(unsigned i=0;i<100;i++)
        assert(mtv_weighted_genre(i)==(i<70?11:12));
    remove("mtv-weights.tsv");assert(chdir("/")==0);assert(rmdir(tmp)==0);
    puts("mtv_weights_test: PASS");
    return 0;
}
