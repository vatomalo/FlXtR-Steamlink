"""Join Secret of Mana as player 2 from any device on the Tailscale network.

Open http://<this PC's Tailscale address>:43932/ on a phone, tablet or PC:
touch buttons, or a Bluetooth/USB controller through the browser's Gamepad
API. Presses are relayed as 8-byte UDP packets to the Steam Link core
(src/som_net_pad.h), which feeds them to SNES pad 2.

One player at a time: the first browser owns pad 2 until it has been quiet
for OWNER_S; the others see "pad 2 taken". Pages send their state every
50 ms, so a closed tab releases every button within half a second (the
core does that on its own too). Select is never sent (RetroArch hotkey).
Native clients may send the UDP packets straight to the Steam Link.
"""
import argparse
import http.server
import json
import socket
import struct
import threading
import time

OWNER_S = 3.0
SELECT = 1 << 2
PAGE = r"""<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Mana Player 2</title><style>
:root{--bg:#10131a;--fg:#e8ecf4;--key:#2a3142;--on:#5b7cfa;--dim:#8a93a8}
body{margin:0;background:var(--bg);color:var(--fg);font:16px system-ui;touch-action:none;
user-select:none;-webkit-user-select:none;height:100vh;display:flex;flex-direction:column}
#s{padding:10px 16px;color:var(--dim)}#pad{flex:1;display:flex;justify-content:space-between;
align-items:center;padding:0 16px 24px}.g{display:grid;gap:10px}
#d{grid-template-columns:repeat(3,64px);grid-template-rows:repeat(3,64px)}
#f{grid-template-columns:repeat(3,64px);grid-template-rows:repeat(3,64px)}
.k{background:var(--key);border-radius:14px;display:flex;align-items:center;justify-content:center;
font-weight:600}.k.on{background:var(--on)}.r{border-radius:50%}
#top{display:flex;justify-content:space-between;padding:0 16px}#top .k{width:96px;height:40px}
</style></head><body><div id="s">connecting…</div>
<div id="top"><div class="k" data-b="10">L</div><div class="k" data-b="3">Start</div>
<div class="k" data-b="11">R</div></div><div id="pad">
<div class="g" id="d"><i></i><div class="k" data-b="4">▲</div><i></i>
<div class="k" data-b="6">◀</div><i></i><div class="k" data-b="7">▶</div>
<i></i><div class="k" data-b="5">▼</div><i></i></div>
<div class="g" id="f"><i></i><div class="k r" data-b="9">X</div><i></i>
<div class="k r" data-b="1">Y</div><i></i><div class="k r" data-b="8">A</div>
<i></i><div class="k r" data-b="0">B</div><i></i></div></div><script>
const id=Math.random().toString(36).slice(2);let touch=0;const held=new Map();
function paint(){document.querySelectorAll('.k').forEach(k=>{const b=1<<k.dataset.b;
k.classList.toggle('on',!!(touch&b))})}
function keys(e){touch=0;for(const t of e.touches){const el=document.elementFromPoint(t.clientX,t.clientY);
const k=el&&el.closest('.k');if(k)touch|=1<<k.dataset.b}paint();e.preventDefault()}
['touchstart','touchmove','touchend','touchcancel'].forEach(n=>document.addEventListener(n,keys,{passive:false}));
document.querySelectorAll('.k').forEach(k=>{k.onmousedown=()=>{touch|=1<<k.dataset.b;paint()};
k.onmouseup=k.onmouseleave=()=>{touch&=~(1<<k.dataset.b);paint()}});
// Standard Gamepad mapping -> SNES (Libretro ids): south=B, east=A, west=Y, north=X.
const map={0:0,1:8,2:1,3:9,4:10,5:11,9:3,12:4,13:5,14:6,15:7};
function pads(){let m=0;for(const g of navigator.getGamepads?navigator.getGamepads():[]){if(!g)continue;
for(const[i,b]of Object.entries(map))if(g.buttons[i]&&g.buttons[i].pressed)m|=1<<b;
if(g.axes.length>=2){if(g.axes[0]<-.5)m|=1<<6;if(g.axes[0]>.5)m|=1<<7;if(g.axes[1]<-.5)m|=1<<4;
if(g.axes[1]>.5)m|=1<<5}}return m}
async function tick(){try{const r=await fetch('/pad',{method:'POST',headers:{'content-type':'application/json'},
body:JSON.stringify({id,buttons:touch|pads()})});const j=await r.json();
document.getElementById('s').textContent=j.player2?'You are player 2':'Pad 2 taken: waiting'}
catch(e){document.getElementById('s').textContent='not connected'}setTimeout(tick,50)}
tick();
</script></body></html>"""


class Relay:
    def __init__(self, target, clock=time.monotonic):
        self.target = target
        self.clock = clock
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.lock = threading.Lock()
        self.owner = None
        self.seen = 0.0
        self.seq = 0

    def press(self, client, buttons):
        """Forward one state from client; True if it owns pad 2."""
        now = self.clock()
        with self.lock:
            if self.owner is not None and self.owner != client and now - self.seen < OWNER_S:
                return False
            if self.owner != client:
                print(f"[pad2] player 2 is browser {client}", flush=True)
            self.owner, self.seen = client, now
            self.seq = (self.seq + 1) & 0xFFFF
            packet = b"S2\x01\x00" + struct.pack("<HH", int(buttons) & 0x0FFF & ~SELECT, self.seq)
        self.sock.sendto(packet, self.target)
        return True


def serve(listen, relay):
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path not in ("/", "/index.html"):
                self.send_error(404); return
            body = PAGE.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers(); self.wfile.write(body)

        def do_POST(self):
            if self.path != "/pad":
                self.send_error(404); return
            try:
                length = min(int(self.headers.get("Content-Length", "0")), 256)
                data = json.loads(self.rfile.read(length) or b"{}")
                client, buttons = str(data["id"])[:32], int(data["buttons"])
            except (ValueError, KeyError, TypeError, json.JSONDecodeError):
                self.send_error(400); return
            body = json.dumps({"player2": relay.press(client, buttons)}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers(); self.wfile.write(body)

        def log_message(self, *_):
            pass

    host, _, port = listen.rpartition(":")
    server = http.server.ThreadingHTTPServer((host, int(port)), Handler)
    print(f"[pad2] open http://{listen}/ on the Tailscale network to join", flush=True)
    server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--listen", default="100.69.7.81:43932",
                        help="this PC's Tailscale address:port (only the tailnet can reach it)")
    parser.add_argument("--steamlink", default="100.124.74.13:43931")
    args = parser.parse_args()
    host, _, port = args.steamlink.rpartition(":")
    serve(args.listen, Relay((host, int(port))))


if __name__ == "__main__":
    main()
