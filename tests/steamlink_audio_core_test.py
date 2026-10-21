"""Load the ROM-free fixture through the public Libretro ABI, including symbols."""
import ctypes
import pathlib
import re

core = ctypes.CDLL(str(pathlib.Path('build/steamlink-audio-test-core-host.so').resolve()))
header = pathlib.Path('src/vendor/libretro.h').read_text()
symbols = re.findall(r'RETRO_API\s+[^;()]+?\b(retro_\w+)\s*\(', header)
assert len(symbols) == 25
for name in symbols:
    getattr(core, name)
single_count = batch_count = single_peak = batch_peak = 0
single = ctypes.CFUNCTYPE(None, ctypes.c_int16, ctypes.c_int16)
batch = ctypes.CFUNCTYPE(ctypes.c_size_t, ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t)

@single
def on_single(left, right):
    global single_count, single_peak
    single_count += 1
    single_peak = max(single_peak, abs(left), abs(right))

@batch
def on_batch(data, frames):
    global batch_count, batch_peak
    batch_count += frames
    batch_peak = max(batch_peak, max(abs(data[i]) for i in range(frames * 2)))
    return frames

core.retro_set_audio_sample.argtypes = [single]
core.retro_set_audio_sample_batch.argtypes = [batch]
core.retro_set_audio_sample(on_single)
core.retro_set_audio_sample_batch(on_batch)
core.retro_init()
for _ in range(120):
    core.retro_run()
core.retro_deinit()
assert single_count == batch_count == 48000
assert single_peak > 0 and batch_peak > 0
print('PASS: all 25 Libretro exports and nonzero stereo PCM through both audio callbacks')
