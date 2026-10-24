#ifndef FLXTR_MTV_WEIGHTS_H
#define FLXTR_MTV_WEIGHTS_H
/* Indexes 1..MTV_GENRES-1 match mtv_genre_queries; index 0 is CUSTOM/MIX.
 * Zero disables a genre. A single positive weight locks selection to it.
 * File is intentionally tiny and may be edited from SSH or desktop.
 */
static void mtv_weights_load(int weights[MTV_GENRES]){
    for(int i=0;i<MTV_GENRES;i++)weights[i]=i?1:0;
    FILE *f=fopen("mtv-weights.tsv","r");
    if(!f)return;
    char line[128];int seen[MTV_GENRES]={0};
    while(fgets(line,sizeof(line),f)){
        int id,weight;char extra;
        if(sscanf(line," %d %d %c",&id,&weight,&extra)!=2||
           id<=0||id>=MTV_GENRES||weight<0||weight>100||seen[id])continue;
        weights[id]=weight;seen[id]=1;
    }
    fclose(f);
}
static int mtv_weighted_genre(unsigned int selection){
    int weights[MTV_GENRES];mtv_weights_load(weights);
    unsigned int sum=0;
    for(int i=1;i<MTV_GENRES;i++)sum+=(unsigned int)weights[i];
    if(!sum)return 0;
    unsigned int chosen=selection%sum;
    for(int i=1;i<MTV_GENRES;i++){
        if(chosen<(unsigned int)weights[i])return i;
        chosen-=(unsigned int)weights[i];
    }
    return 0;
}
#endif
