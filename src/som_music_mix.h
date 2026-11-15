/* Background music mixed into the game's own audio stream.
 *
 * The Steam Link refuses a second audio output while RetroArch plays
 * (MV_PE_StreamCreate 0x80007065), so the soundtrack bridge decodes the
 * arrangement to raw stereo s16 at the core's sample rate into a named pipe
 * (`greenlink-audio --pcm`) and the proxy adds it to every audio batch the
 * core produces. This header is the pure part: a bounded frame ring and the
 * saturating mix. Pure, no I/O. */
#ifndef FLXTR_SOM_MUSIC_MIX_H
#define FLXTR_SOM_MUSIC_MIX_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SOM_MIX_RING_FRAMES 32768u      /* ~1 s at the SNES rate */
#define SOM_MIX_GAIN_DEFAULT 176u       /* music at 176/256 under the effects */

typedef struct {
    int16_t pcm[SOM_MIX_RING_FRAMES*2u];
    unsigned head;                      /* next frame to play */
    unsigned count;                     /* frames buffered */
    unsigned gain;                      /* 0..256 */
    uint8_t partial[4];                 /* bytes of a frame split across reads */
    unsigned partial_bytes;
} SomMusicRing;

static inline void som_mix_reset(SomMusicRing *r,unsigned gain){
    r->head=r->count=r->partial_bytes=0;r->gain=gain>256u?256u:gain;
}
static inline unsigned som_mix_free_bytes(const SomMusicRing *r){
    return (SOM_MIX_RING_FRAMES-r->count)*4u-r->partial_bytes;
}
/* Append raw bytes (little-endian s16 stereo); keeps a split frame. */
static inline void som_mix_push_bytes(SomMusicRing *r,const uint8_t *bytes,size_t n){
    while(n&&r->count<SOM_MIX_RING_FRAMES){
        const size_t take=4u-r->partial_bytes<n?4u-r->partial_bytes:n;
        memcpy(r->partial+r->partial_bytes,bytes,take);
        r->partial_bytes+=(unsigned)take;bytes+=take;n-=take;
        if(r->partial_bytes==4u){
            const unsigned slot=(r->head+r->count)%SOM_MIX_RING_FRAMES;
            r->pcm[2u*slot]=(int16_t)(r->partial[0]|r->partial[1]<<8);
            r->pcm[2u*slot+1u]=(int16_t)(r->partial[2]|r->partial[3]<<8);
            r->count++;r->partial_bytes=0;
        }
    }
}
static inline int16_t som_mix_clamp(int32_t v){
    return (int16_t)(v>32767?32767:v<-32768?-32768:v);
}
/* out = game + music * gain / 256 for `frames` stereo frames; missing music
 * is silence. game and out may be the same buffer. Returns frames of music
 * used. */
static inline size_t som_mix_apply(SomMusicRing *r,int16_t *out,const int16_t *game,size_t frames){
    size_t used=0;
    for(size_t i=0;i<frames;i++){
        int32_t ml=0,mr=0;
        if(r->count){
            ml=r->pcm[2u*r->head];mr=r->pcm[2u*r->head+1u];
            r->head=(r->head+1u)%SOM_MIX_RING_FRAMES;r->count--;used++;
        }
        out[2u*i]=som_mix_clamp(game[2u*i]+ml*(int32_t)r->gain/256);
        out[2u*i+1u]=som_mix_clamp(game[2u*i+1u]+mr*(int32_t)r->gain/256);
    }
    return used;
}
#endif
