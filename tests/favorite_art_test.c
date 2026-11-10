#define main catalog_main
#include "../src/catalog.c"
#undef main
#include <assert.h>
static int requests;
static void fixture(Entry *entry){
    requests++;
    SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,16,24,32,SDL_PIXELFORMAT_ARGB8888);assert(surface);
    SDL_FillRect(surface,NULL,!strcmp(entry->kind,"movie")?0xffff0000:0xff0000ff);
    assert(!SDL_SaveBMP(surface,"catalog-cache/fixture.bmp"));SDL_FreeSurface(surface);
    strcpy(entry->poster,"catalog-cache/fixture.bmp");
}
static void failed_fixture(Entry *entry){strcpy(entry->poster,"catalog-cache/broken.bmp");}
int main(void){
    char temp[]="/tmp/flxtr-favorite-art-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));mkdir("catalog-cache",0700);
    Entry movie={0};strcpy(movie.title,"Same title");strcpy(movie.kind,"movie");movie.id=42;
    assert(favorite_repair_entry(&movie,fixture)==1&&requests==1&&favorite_art_valid(movie.poster));
    char original[512];strcpy(original,movie.poster);
    assert(favorite_repair_entry(&movie,fixture)==0&&requests==1);
    favorite_repair_index(&movie);struct stat before,after;assert(!stat("favorites-art-index.tsv",&before));
    favorite_repair_index(&movie);assert(!stat("favorites-art-index.tsv",&after)&&before.st_size==after.st_size);
    FILE *file=fopen(original,"wb");assert(file);fputs("corrupt",file);fclose(file);
    assert(!favorite_art_valid(original));assert(favorite_repair_entry(&movie,fixture)==1&&requests==2);
    Entry series=movie;strcpy(series.kind,"tv");
    assert(favorite_repair_entry(&series,fixture)==1&&requests==3&&strcmp(movie.poster,series.poster));
    SDL_Surface *red=SDL_LoadBMP(movie.poster),*blue=SDL_LoadBMP(series.poster);assert(red&&blue);
    assert(*(Uint32*)red->pixels!=*(Uint32*)blue->pixels);SDL_FreeSurface(red);SDL_FreeSurface(blue);
    Entry missing=movie;missing.id=99;char target[96];favorite_art_path(target,sizeof(target),missing.kind,missing.title,missing.url,99,0,0);
    assert(favorite_repair_entry(&missing,failed_fixture)==-1&&access(target,F_OK));
    assert(favorite_art_valid(original));
    puts("PASS: missing/corrupt cover repair, valid cache skips fetch, same-name movie/series isolation, failed fetch never replaces a good cover");return 0;
}
