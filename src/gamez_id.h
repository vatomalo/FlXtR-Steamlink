#ifndef FLXTR_GAMEZ_ID_H
#define FLXTR_GAMEZ_ID_H
static int gamez_valid_id(const char *id){
    size_t n=0;
    for(;id[n];n++)if(n>=32||!((id[n]>='0'&&id[n]<='9')||(id[n]>='A'&&id[n]<='Z')||(id[n]>='a'&&id[n]<='z')))return 0;
    return n>0;
}
#endif
