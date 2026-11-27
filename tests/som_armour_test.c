/* som_armour.h on a synthetic hero (no ROM art): a 16x32 figure drawn with
 * the boy's measured index roles (hair 2-4, skin/shirt 8-9, stripes 10, arms
 * 5-7, trousers 11-12, boots 13), encoded as 4bpp tiles in VRAM and two 16x16
 * OAM pieces, as the game uploads them. */
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "../src/som_armour.h"

static uint8_t vram[0x10000],oam[0x220];
/* rows of the figure, 16 wide; '.' = 0 */
static const char *const boy[32]={
    "....33333333....","...3443434443...","..344444444443..","..343434343433..",
    "..334444444333..","..322222222223..","..32222222222...","...8888888888...",
    "...8988889888...","...8888888888...","...8898888988...","....88888888....",
    "....88822888....","....88888888....","..5a98888889a5..",".5679898988976..",
    ".5678a8a8a8a76..",".56798989898765.",".d679a8a8a8a76d.","..d.98988989.d..",
    "....aaaaaaaa....","....bbbbbbbb....","....bccbbccb....","....bbbbbbbb....",
    "....bbb..bbb....","....bcb..bcb....","....bbb..bbb....","....bbb..bbb....",
    "....aaa..aaa....","....ddd..ddd....","...dddd..dddd...","................"};
static unsigned hexv(char c){return c=='.'?0u:c<='9'?(unsigned)(c-'0'):(unsigned)(c-'a'+10);}
static void put_tile(unsigned tile,const char *const *rows,unsigned ox,unsigned oy){
    const unsigned word=(tile&0x100u?0x7000u:0x6000u)+(tile&0xFFu)*16u;
    uint8_t *t=vram+((word*2u)&0xFFFFu);
    memset(t,0,32);
    for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
        const unsigned c=hexv(rows[oy+y][ox+x]),bit=7u-x;
        t[2u*y]|=(uint8_t)((c&1u)<<bit);t[2u*y+1u]|=(uint8_t)(((c>>1)&1u)<<bit);
        t[16u+2u*y]|=(uint8_t)(((c>>2)&1u)<<bit);t[16u+2u*y+1u]|=(uint8_t)(((c>>3)&1u)<<bit);
    }
}
/* a 16x16 piece at screen (x,y) from figure rows oy..oy+15, base tile t */
static void put_piece(unsigned slot,unsigned tile,int x,int y,unsigned oy,unsigned who,int hflip){
    put_tile(tile,boy,0,oy);put_tile(tile+1u,boy,8,oy);put_tile(tile+16u,boy,0,oy+8);put_tile(tile+17u,boy,8,oy+8);
    uint8_t *e=oam+4u*slot;
    e[0]=(uint8_t)x;e[1]=(uint8_t)y;e[2]=(uint8_t)tile;
    e[3]=(uint8_t)(((tile>>8)&1u)|((who+1u)<<1)|0x20u|(hflip?0x40u:0u));
    oam[512u+slot/4u]&=(uint8_t)~(3u<<(2u*(slot%4u)));
}
int main(void){
    memset(oam,0,sizeof(oam));
    for(unsigned i=0;i<128;i++)oam[4u*i+1u]=0xF0u;          /* all off screen */
    put_piece(8,0x1C0,100,80,0,0,0);put_piece(9,0x1C2,100,96,16,0,0);
    uint8_t img[SOM_ARMOUR_BOX][SOM_ARMOUR_BOX],mask[SOM_ARMOUR_BOX][SOM_ARMOUR_BOX];
    int x0,y0,from;
    assert(som_armour_compose(oam,vram,0,img,&x0,&y0)&&x0==100&&y0==80);
    for(int y=0;y<32;y++)for(int x=0;x<16;x++)assert(img[y][x]==hexv(boy[y][x]));
    const unsigned n=som_armour_torso_mask(img,0,mask,&from);
    /* the neck is below the last hair row (12, the mouth uses 2) and the
     * 14-row head floor: row 14; eight rows of torso */
    assert(from==14&&n>0);
    for(int y=0;y<32;y++)for(int x=0;x<16;x++){
        const unsigned c=img[y][x];
        const int want=y>=14&&y<22&&(c==8u||c==9u||c==10u);
        assert(mask[y][x]==want);                  /* never face, arms, hands, legs */
    }
    /* mirrored piece: same mask, mirrored */
    put_piece(8,0x1C0,100,80,0,0,1);put_piece(9,0x1C2,100,96,16,0,1);
    assert(som_armour_compose(oam,vram,0,img,&x0,&y0));
    for(int y=0;y<32;y++)for(int x=0;x<16;x++)assert(img[y][x]==hexv(boy[y][15-x]));
    /* another hero's palette is not the boy */
    assert(som_armour_compose(oam,vram,1,img,&x0,&y0)==0);
    /* the sprite's body tiles (0x1E0) count; HUD portraits (low on screen) do not */
    memset(oam,0,sizeof(oam));for(unsigned i=0;i<128;i++)oam[4u*i+1u]=0xF0u;
    put_piece(3,0x1E0,60,70,0,2,0);put_piece(4,0x1E2,60,86,16,2,0);
    put_piece(5,0x1E8,90,199,0,2,0);                         /* portrait */
    assert(som_armour_compose(oam,vram,2,img,&x0,&y0)&&x0==60&&y0==70);
    /* sprite rule: first tunic (10-12) row 9+ rows down */
    {int a,z;assert(som_armour_torso_rows(img,2,&a,&z)&&a==14&&z==22);}
    /* nothing to compose: no torso */
    memset(oam,0,sizeof(oam));for(unsigned i=0;i<128;i++)oam[4u*i+1u]=0xF0u;
    assert(!som_armour_compose(oam,vram,0,img,&x0,&y0));
    /* colours: lacing every third row, shades by index rank */
    const SomOutfit *o=&som_outfits[SOM_OUTFIT_SAMURAI];
    assert(som_armour_colour(o,SOM_SLOT_TORSO,0,8,2)==o->layer[0].lacing);
    assert(som_armour_colour(o,SOM_SLOT_TORSO,0,8,0)==o->layer[0].rgb[0]);
    assert(som_armour_colour(o,SOM_SLOT_TORSO,0,10,1)==o->layer[0].rgb[2]);
    /* Both class outfits use the same pose mask, but have distinct art and
     * palettes. Every head view must keep the fixed 20-pixel canvas. */
    assert(som_class_look(8u).torso==SOM_OUTFIT_WEAPONMASTER);
    assert(som_class_look(8u).head==SOM_HEAD_WEAPONMASTER);
    assert(som_class_look(10u).torso==SOM_OUTFIT_SAMURAI);
    assert(som_outfits[SOM_OUTFIT_WEAPONMASTER].layer[0].rgb[0][0]!=o->layer[0].rgb[0][0]);
    for(unsigned head=0;head<SOM_HEADS;head++)for(unsigned view=0;view<3u;view++)
        for(unsigned y=0;y<SOM_HEAD_H;y++)assert(strlen(som_head_art[head][view][y])==SOM_HEAD_W);
    /* cost of compose + mask for one hero */
    put_piece(8,0x1C0,100,80,0,0,0);put_piece(9,0x1C2,100,96,16,0,0);
    {clock_t c0=clock();unsigned total=0;
     for(unsigned i=0;i<20000;i++){som_armour_compose(oam,vram,0,img,&x0,&y0);total+=som_armour_torso_mask(img,0,mask,&from);}
     printf("armour: %.2f us per hero per frame (x86), %u\n",(double)(clock()-c0)*1e6/CLOCKS_PER_SEC/20000.0,total/20000u);}
    {   /* the Monk's head art picked from the drawn sprite (real boy frames, 2026-11-27) */
static const char *front[]={
        ".....11111......",
        "...1113343111...",
        "..134432233241..",
        ".13422233123221.",
        ".12212344313131.",
        "123222212412131.",
        "142233321211221.",
        "13144433211111..",
        "1214322118189A1.",
        ".1232218A98A891.",
        "..1321198A98891.",
        "..1211111111891.",
        "...16F1661F1891.",
        "...16F1771F1891.",
        "...116777761891.",
        "..198A6776A111..",
        "..198AAAAA8911..",
        ".1619888889A661.",
        ".17AA99999A1671.",
        ".162BAAAAAB16761",
        "1ED1BCCCCBA126E1",
        "1ED1BBCBBBA12DD1",
        "1731899988816671",
        "17319AABBA916761",
        ".1189AABCA98111.",
        "..19AB1BCCA991..",
        "...1CB1BCCCAA1..",
        "...1CB1BBCCB1...",
        "...1AA1ABBBB1...",
        "..1DEDD1DAA11...",
        "..111111DDEED1..",
        ".......1111111..",
        NULL};
static const char *side[]={
        ".....11111.....",
        "..11122332111..",
        ".1222333223321.",
        "123314313444321",
        "124224133211231",
        "123123132134421",
        ".12112121334441",
        ".12111111112341",
        ".1A88A8A8911231",
        "..19189A1A81231",
        "..11189AF16121.",
        "..13189AF1761..",
        "..12189A67771..",
        "...1189A6671...",
        "...118911111...",
        "..189118AA891..",
        ".1BA899888891..",
        ".156AA899991...",
        ".176ABAAAAAB1..",
        "16755BBBCCCA1..",
        "16761AABBCBAE1.",
        "16651888998A71.",
        "1EDD589BBAA171.",
        "1D77589BAAB11..",
        "1677589CABC1...",
        "155589CCABC1...",
        ".1889BCBABB1...",
        ".1ABBBBA1B1....",
        "..1ABBA1DAA1...",
        ".1DD111DDDDE1..",
        ".1DDDED111111..",
        ".111111........",
        NULL};
static const char *back[]={
        "......11111.....",
        "...1.1234421....",
        "..12112223431...",
        ".1322323322231..",
        ".13242344322221.",
        ".14232322113421.",
        ".13121213342241.",
        ".12312121234231.",
        "..112121212321..",
        ".18A8198A8A21...",
        ".19A98A98A9A91..",
        ".191A988A8A891..",
        ".191411111A891..",
        ".1813214131891..",
        ".181121312A891..",
        "..1AA12121AAA1..",
        "..198AAAA588A1..",
        "..1988888889A61.",
        ".16A9888899A671.",
        ".171A9999AA1661.",
        "1661ABCBBAA1571.",
        "1DD588999881DDD1",
        "1671888888815DE1",
        ".161999999916671",
        "..11AAAAAAAA5661",
        "...1BBABBBBB111.",
        "...1CB1BCCCB1...",
        "...1BB1BCCCB1...",
        "...1111ABCBA1...",
        "..1EDD1AAAA1....",
        "..11111DEDDD1...",
        "......1111111...",
        NULL};
        const char **pics[3]={front,side,back};
        for(int i=0;i<3;i++){
            static uint8_t im[SOM_ARMOUR_BOX][SOM_ARMOUR_BOX];
            memset(im,0,sizeof(im));
            for(int y=0;pics[i][y];y++)for(int x=0;pics[i][y][x];x++){
                const char ch=pics[i][y][x];
                im[y+2][x+4]=(uint8_t)(ch=='.'?0:ch<='9'?ch-'0':ch-'A'+10);
            }
            SomMonkHead h;
            assert(som_monk_head(im,0,&h));
            if(i==0){assert(h.art==som_monk_front&&h.y==2+12-9);}
            if(i==1){assert(h.art==som_monk_side&&!h.mirror&&h.y==2+10-9);}
            if(i==2){assert(h.art==som_monk_back&&h.y==2+9-9);}
            for(int v=0;v<h.h;v++)assert((int)strlen(h.art[v])==h.w);
        }
    }
    puts("som_armour_test: ok");
    return 0;
}
