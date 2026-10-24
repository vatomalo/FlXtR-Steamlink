/* Run: cc -std=c11 -Wall -Wextra -I src tests/tv_network_test.c -o /tmp/tv-network-test && /tmp/tv-network-test */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tv_network.h"
int main(void){
    TvNetworkDay *d=calloc(1,sizeof(*d)),*readback=calloc(1,sizeof(*readback));
    assert(d&&readback);
    d->channel=15;d->day_start=1792886400LL;d->day_end=d->day_start+3600;d->count=2;
    d->slots[0]=(TvNetworkAiring){d->day_start,1800,1,101,1,4,"First"};
    d->slots[1]=(TvNetworkAiring){d->day_start+1800,1800,1,101,1,5,"Second"};
    assert(tv_network_valid(d));
    int offset=-1;
    assert(tv_network_at(d,d->day_start,&offset)==0&&offset==0);
    assert(tv_network_at(d,d->day_start+1799,&offset)==0&&offset==1799);
    assert(tv_network_at(d,d->day_start+1800,&offset)==1&&offset==0);
    assert(tv_network_at(d,d->day_start+3599,&offset)==1&&offset==1799);
    assert(tv_network_at(d,d->day_start+3600,&offset)==-1);
    char path[256];assert(tv_network_path(path,sizeof(path),"/tmp",15,"20261025"));
    assert(strstr(path,"tv-channel-16-20261025.tsv"));
    assert(tv_network_save(d,path));
    assert(tv_network_load(readback,path));
    assert(readback->channel==15&&readback->count==2);
    assert(tv_network_at(readback,d->day_start+2500,&offset)==1&&offset==700);
    /* A gap is a damaged guide, not a reason to reset to episode one. */
    readback->slots[1].start++;
    assert(!tv_network_valid(readback));
    remove(path);free(d);free(readback);
    puts("tv_network_test: PASS");
    return 0;
}
