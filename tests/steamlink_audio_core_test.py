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
current_frame = 0
nonzero_frames = set()
single = ctypes.CFUNCTYPE(None, ctypes.c_int16, ctypes.c_int16)
batch = ctypes.CFUNCTYPE(ctypes.c_size_t, ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t)

@single
def on_single(left, right):
    global single_count, single_peak
    single_count += 1
    single_peak = max(single_peak, abs(left), abs(right))
    if left or right:
        nonzero_frames.add(current_frame)

@batch
def on_batch(data, frames):
    global batch_count, batch_peak
    batch_count += frames
    peak = max(abs(data[i]) for i in range(frames * 2))
    batch_peak = max(batch_peak, peak)
    if peak:
        nonzero_frames.add(current_frame)
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

class Variable(ctypes.Structure):
    _fields_ = [('key', ctypes.c_char_p), ('value', ctypes.c_char_p)]

environment = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_uint, ctypes.c_void_p)
video = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_size_t)
input_state = ctypes.CFUNCTYPE(ctypes.c_int16, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint)
white_frames = set()

@environment
def on_environment(command, data):
    if command == 15:  # RETRO_ENVIRONMENT_GET_VARIABLE
        ctypes.cast(data, ctypes.POINTER(Variable)).contents.value = b'av-sync'
    return True

@video
def on_video(data, width, height, pitch):
    assert (width, height, pitch) == (160, 144, 320)
    if ctypes.c_uint16.from_address(data).value == 0x7fff:
        white_frames.add(current_frame)

@input_state
def on_input(port, device, index, button):
    return int(current_frame in (30, 31))

core.retro_set_environment.argtypes = [environment]
core.retro_set_video_refresh.argtypes = [video]
core.retro_set_input_state.argtypes = [input_state]
core.retro_load_game.argtypes = [ctypes.c_void_p]
core.retro_load_game.restype = ctypes.c_bool
core.retro_set_environment(on_environment)
core.retro_set_video_refresh(on_video)
core.retro_set_input_state(on_input)
core.retro_init()
assert core.retro_load_game(None)
nonzero_frames.clear()
for current_frame in range(120):
    core.retro_run()
core.retro_deinit()
assert white_frames == nonzero_frames == {0, 30, 60}
print('PASS: periodic and button-triggered flash/click pairs occur on the same core frame')
