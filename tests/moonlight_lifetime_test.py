"""Exercise the patched upstream SLAudio renderer with a fake hardware backend."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get("FLXTR_MOONLIGHT_SOURCE", ROOT / "build/moonlight-src"))


class AudioLifetimeTest(unittest.TestCase):
    @unittest.skipUnless((SOURCE / "app/streaming/audio/renderers/slaud.cpp").exists(),
                         "Build the pinned Moonlight source first")
    def test_probe_and_stream_share_context_until_process_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("slaud.cpp", "slaud.h", "renderer.h"):
                shutil.copy(SOURCE / "app/streaming/audio/renderers" / name, root)
            (root / "QtGlobal").write_text('#include <cstdlib>\n#define Q_UNREACHABLE() std::abort()\n')
            (root / "Limelight.h").write_text('''#pragma once
struct OPUS_MULTISTREAM_CONFIGURATION { int sampleRate,channelCount,samplesPerFrame; unsigned char mapping[8]; };
typedef OPUS_MULTISTREAM_CONFIGURATION* POPUS_MULTISTREAM_CONFIGURATION;
inline int LiGetPendingAudioDuration(){return 0;}
inline int LiGetPendingAudioFrames(){return 0;}
''')
            (root / "SDL_compat.h").write_text('''#pragma once
#include <cassert>
#include <cstring>
#define SDL_assert assert
#define SDL_LOG_CATEGORY_APPLICATION 0
enum SDL_LogPriority {SDL_LOG_PRIORITY_ERROR,SDL_LOG_PRIORITY_WARN,SDL_LOG_PRIORITY_INFO,SDL_LOG_PRIORITY_DEBUG};
inline void SDL_LogError(int,const char*,...){}
inline void SDL_LogInfo(int,const char*,...){}
inline void SDL_LogMessage(int,SDL_LogPriority,const char*,...){}
''')
            (root / "SLAudio.h").write_text('''#pragma once
struct CSLAudioContext {bool alive;};
struct CSLAudioStream {};
enum ESLAudioLog {k_ESLAudioLogError,k_ESLAudioLogWarning,k_ESLAudioLogInfo,k_ESLAudioLogDebug};
extern int creates,frees,opens,closes;
extern CSLAudioContext* callbackContext;
void SLAudio_SetLogFunction(void(*)(void*,ESLAudioLog,const char*),void*);
CSLAudioContext* SLAudio_CreateContext();
CSLAudioStream* SLAudio_CreateStream(CSLAudioContext*,int,int,int,int);
void* SLAudio_BeginFrame(CSLAudioStream*);
void SLAudio_SubmitFrame(CSLAudioStream*);
void SLAudio_FreeStream(CSLAudioStream*);
void SLAudio_FreeContext(CSLAudioContext*);
''')
            (root / "test.cpp").write_text('''#include "slaud.h"
#include <cassert>
#include <cstdlib>
int creates=0,frees=0,opens=0,closes=0;
CSLAudioContext* callbackContext=nullptr;
void SLAudio_SetLogFunction(void(*)(void*,ESLAudioLog,const char*),void*){}
CSLAudioContext* SLAudio_CreateContext(){creates++;callbackContext=new CSLAudioContext{true};return callbackContext;}
CSLAudioStream* SLAudio_CreateStream(CSLAudioContext* c,int,int,int,int){assert(c->alive);opens++;return new CSLAudioStream;}
void* SLAudio_BeginFrame(CSLAudioStream*){static short frame[960];return frame;}
void SLAudio_SubmitFrame(CSLAudioStream*){}
void SLAudio_FreeStream(CSLAudioStream* s){closes++;delete s;}
void SLAudio_FreeContext(CSLAudioContext* c){assert(c->alive);c->alive=false;frees++;}
void verifyShutdown(){assert(creates==1 && frees==1 && opens==3 && closes==3);delete callbackContext;}
int main(){
    std::atexit(verifyShutdown);
    OPUS_MULTISTREAM_CONFIGURATION cfg={48000,2,480,{0}};
    for(int phase=0;phase<3;phase++){
        {SLAudioRenderer renderer;assert(renderer.prepareForPlayback(&cfg));
         int size=1920;assert(renderer.getAudioBuffer(&size));assert(renderer.submitAudio(size));}
        // A queued device-added event must still have a live callback context
        // after the sound probe, actual stream and renderer recovery each end.
        assert(callbackContext && callbackContext->alive && creates==1 && frees==0);
    }
}
''')
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root),
                            str(root / "slaud.cpp"), str(root / "test.cpp"), "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
