/* SDL device output only. Call serially from the frontend's audio thread.
 * No emulation clock, resampler, video scheduler, or ALSA dependency. */
#ifndef FLXTR_STEAMLINK_SDL_AUDIO_H
#define FLXTR_STEAMLINK_SDL_AUDIO_H
#include <SDL.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    SDL_AudioDeviceID device;
    SDL_AudioSpec spec;
    Uint32 capacity, wait_ms;
    unsigned underruns, errors;
    int owned, paused, fed, empty;
} SteamlinkSDLAudio;
/* period=0 uses the player's proven 1024-frame request; capacity_ms is a
 * frontend latency request, not a measured end-to-end latency guarantee. */
int steamlink_sdl_audio_open(SteamlinkSDLAudio *, const char *device,
                            unsigned rate, unsigned period, unsigned capacity_ms);
size_t steamlink_sdl_audio_queued(SteamlinkSDLAudio *);
size_t steamlink_sdl_audio_available(SteamlinkSDLAudio *);
/* Byte counts, stereo S16 frames only. 0 means backpressure; -1 is fatal.
 * Blocking waits are bounded so a stalled device cannot hang emulation. */
ptrdiff_t steamlink_sdl_audio_write(SteamlinkSDLAudio *, const void *, size_t, int nonblock);
int steamlink_sdl_audio_pause(SteamlinkSDLAudio *, int pause);
void steamlink_sdl_audio_clear(SteamlinkSDLAudio *);
void steamlink_sdl_audio_close(SteamlinkSDLAudio *);
#endif
