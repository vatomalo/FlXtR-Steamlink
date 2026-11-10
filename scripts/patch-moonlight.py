#!/usr/bin/env python3
"""Small, repeatable patch against the pinned upstream Moonlight tree."""
from pathlib import Path
import sys


def patch(tree):
    path = Path(tree) / "app/streaming/audio/renderers/sdlaud.cpp"
    text = path.read_text()
    before = "    m_AudioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);"
    after = '''    // FlXtR: a named output must never silently become the default output.
    const char* output = SDL_getenv("FLXTR_AUDIO_DEVICE");
    if (output && !*output) output = nullptr;
    if (output) {
        bool found = false;
        for (int i = 0; i < SDL_GetNumAudioDevices(0); i++) {
            const char* name = SDL_GetAudioDeviceName(i, 0);
            if (name && SDL_strcmp(name, output) == 0) found = true;
        }
        if (!found) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Selected audio output unavailable: %s", output);
            return false;
        }
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "FlXtR audio output: %s", output ? output : "system default");
    m_AudioDevice = SDL_OpenAudioDevice(output, 0, &want, &have, 0);'''
    if after not in text:
        if text.count(before) != 1:
            raise SystemExit("Moonlight SDL audio source changed; review the patch")
        path.write_text(text.replace(before, after))

    path = Path(tree) / "app/streaming/audio/renderers/slaud.cpp"
    text = path.read_text()
    if "flxtr-shared-slaudio-context-v2" in text:
        return
    includes = '#include "SDL_compat.h"'
    lifetime = '''#include "SDL_compat.h"
#include <cstdlib>

namespace {
// flxtr-shared-slaudio-context-v2
// Firmware keeps audio-device event callbacks associated with this context.
// Keep it alive between testAudio(), streaming, and renderer recovery. Closing
// the short-lived test context can leave queued callbacks with stale state.
CSLAudioContext* flxtrAudioContext = nullptr;
void freeFlxtrAudioContext()
{
    if (flxtrAudioContext) {
        SLAudio_FreeContext(flxtrAudioContext);
        flxtrAudioContext = nullptr;
    }
}
}
'''
    create = '    m_AudioContext = SLAudio_CreateContext();'
    shared = '''    if (!flxtrAudioContext) {
        flxtrAudioContext = SLAudio_CreateContext();
        if (flxtrAudioContext) std::atexit(freeFlxtrAudioContext);
    }
    m_AudioContext = flxtrAudioContext;'''
    destroy = '''    if (m_AudioContext != nullptr) {
        SLAudio_FreeContext(m_AudioContext);
    }'''
    for old, new in ((includes, lifetime), (create, shared),
                     (destroy, '    m_AudioContext = nullptr; // Borrowed process-lifetime context.')):
        if text.count(old) != 1:
            raise SystemExit("Moonlight SLAudio source changed; review the lifetime patch")
        text = text.replace(old, new)
    path.write_text(text)


if __name__ == "__main__":
    patch(sys.argv[1])
