"""Create an original GB fixture and exact-state reference with a 64-bit core.

Usage: python3 tests/handoff_cross_arch.py CORE_LIBRARY OUTPUT_DIRECTORY
Feed session.json to the 32-bit runner's --verify mode and compare stateHash.
"""
import ctypes as c
import hashlib
import json
from pathlib import Path
import sys

def fnv(data):
    result=2166136261
    for byte in data: result=((result^byte)*16777619)&0xffffffff
    return result

core=c.CDLL(sys.argv[1]);root=Path(sys.argv[2]).resolve();root.mkdir(parents=True,exist_ok=True)
rom=bytearray(32768);rom[0x100:0x104]=bytes.fromhex('00 c3 50 01')
rom[0x104:0x134]=bytes.fromhex('ce ed 66 66 cc 0d 00 0b 03 73 00 83 00 0c 00 0d 00 08 11 1f 88 89 00 0e dc cc 6e e6 dd dd d9 99 bb bb 67 63 6e 0e ec cc dd dc 99 9f bb b9 33 3e')
rom[0x134:0x13b]=b'FLXTRTV';rom[0x147]=0;rom[0x148]=0;rom[0x149]=0
# Original program: increment WRAM continuously. Serialized CPU/RAM state
# therefore proves exact continuation even if the screen is a solid colour.
rom[0x150:0x15b]=bytes.fromhex('f3 21 00 c0 34 18 fd 00 00 00 00')
checksum=0
for byte in rom[0x134:0x14d]:checksum=(checksum-byte-1)&255
rom[0x14d]=checksum;(root/'fixture.gb').write_bytes(rom)
options={};strings=[];video_hash=0
ENV=c.CFUNCTYPE(c.c_bool,c.c_uint,c.c_void_p)
VIDEO=c.CFUNCTYPE(None,c.c_void_p,c.c_uint,c.c_uint,c.c_size_t)
BATCH=c.CFUNCTYPE(c.c_size_t,c.c_void_p,c.c_size_t)
SAMPLE=c.CFUNCTYPE(None,c.c_int16,c.c_int16)
POLL=c.CFUNCTYPE(None)
INPUT=c.CFUNCTYPE(c.c_int16,c.c_uint,c.c_uint,c.c_uint,c.c_uint)
class Variable(c.Structure):_fields_=[('key',c.c_char_p),('value',c.c_char_p)]
@ENV
def environment(command,data):
    if command==10:c.cast(data,c.POINTER(c.c_uint))[0]=2;return True
    if command==3:c.cast(data,c.POINTER(c.c_bool))[0]=True;return True
    if command in (9,31):
        value=str(root).encode();strings.append(value);c.cast(data,c.POINTER(c.c_char_p))[0]=value;return True
    if command==16:
        values=c.cast(data,c.POINTER(Variable));index=0
        while values[index].key:
            key=values[index].key.decode();value=values[index].value.decode().split(';',1)[-1].strip().split('|')[0];options[key]=value;index+=1
        return True
    if command==15:
        variable=c.cast(data,c.POINTER(Variable))[0];value=options.get(variable.key.decode())
        if value is None:return False
        encoded=value.encode();strings.append(encoded);c.cast(data,c.POINTER(Variable))[0].value=encoded;return True
    if command==17:c.cast(data,c.POINTER(c.c_bool))[0]=False;return True
    return False
@VIDEO
def video(data,width,height,pitch):
    global video_hash
    if data:video_hash=fnv(b''.join(c.string_at(data+y*pitch,width*2) for y in range(height)))
@BATCH
def batch(data,count):return count
@SAMPLE
def sample(left,right):pass
@POLL
def poll():pass
@INPUT
def input_state(port,device,index,key):return 0
for name,callback in [('environment',environment),('video_refresh',video),('audio_sample_batch',batch),('audio_sample',sample),('input_poll',poll),('input_state',input_state)]:getattr(core,'retro_set_'+name)(callback)
core.retro_init()
class Info(c.Structure):_fields_=[('name',c.c_char_p),('version',c.c_char_p),('extensions',c.c_char_p),('fullpath',c.c_bool),('extract',c.c_bool)]
class Game(c.Structure):_fields_=[('path',c.c_char_p),('data',c.c_void_p),('size',c.c_size_t),('meta',c.c_char_p)]
info=Info();core.retro_get_system_info(c.byref(info));buffer=c.create_string_buffer(bytes(rom));game=Game(str(root/'fixture.gb').encode(),c.cast(buffer,c.c_void_p),len(rom),None)
core.retro_load_game.restype=c.c_bool;assert core.retro_load_game(c.byref(game))
for _ in range(125):core.retro_run()
core.retro_serialize_size.restype=c.c_size_t;size=core.retro_serialize_size();state=c.create_string_buffer(size);core.retro_serialize.argtypes=[c.c_void_p,c.c_size_t];core.retro_serialize.restype=c.c_bool;assert core.retro_serialize(state,size)
(root/'state.bin').write_bytes(state.raw)
metadata=dict(protocol=1,type='game',source='gambatte:77f2567d718b8973d02caa2daa8dce4953faf811',coreInfo=info.name.decode()+' '+info.version.decode(),quirks=0,options=options,rom=str(root/'fixture.gb'),state=str(root/'state.bin'),core=sys.argv[1],saveDirectory=str(root),romSha256=hashlib.sha256(rom).hexdigest(),stateSha256=hashlib.sha256(state.raw).hexdigest())
(root/'session.json').write_text(json.dumps(metadata))
for _ in range(5):core.retro_run()
assert core.retro_serialize(state,size)
reference=dict(stateHash=fnv(state.raw),frameHash=video_hash,stateBytes=size)
(root/'reference.json').write_text(json.dumps(reference));print(json.dumps(reference));core.retro_unload_game();core.retro_deinit()
