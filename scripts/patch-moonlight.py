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
    if after in text:
        return
    if text.count(before) != 1:
        raise SystemExit("Moonlight SDL audio source changed; review the patch")
    path.write_text(text.replace(before, after))


if __name__ == "__main__":
    patch(sys.argv[1])
