"""Decode HTTP audio with the packaged libmpv worker, including EOF/error status."""
import argparse
from functools import partial
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
import subprocess
import tempfile
import threading
import wave
parser=argparse.ArgumentParser()
parser.add_argument('binary', type=Path)
args=parser.parse_args()
binary=args.binary.resolve()
class Handler(SimpleHTTPRequestHandler):
    def log_message(self,*args):pass
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory)
    with wave.open(str(root/'tone.wav'),'wb') as audio:
        audio.setparams((1,2,8000,0,'NONE','not compressed'))
        audio.writeframes(b'\0\0'*8000)
    server=ThreadingHTTPServer(('127.0.0.1',0),partial(Handler,directory=directory))
    threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        for path, expected in [('tone.wav',0),('missing.wav',1)]:
            stream=root/'stream.txt'
            stream.write_text('http://127.0.0.1:'+str(server.server_port)+'/'+path+'\nMozilla/5.0\n')
            result=subprocess.run([str(binary),str(stream),'--headless'],capture_output=True,timeout=15)
            assert result.returncode==expected,(result.returncode,result.stderr)
    finally:
        server.shutdown();server.server_close()
print('PASS: packaged HTTP audio decodes, EOF succeeds, unavailable media fails')
