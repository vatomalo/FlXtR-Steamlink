import hashlib
import http.client
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
import zipfile

spec=importlib.util.spec_from_file_location('handoff',Path(__file__).resolve().parents[1]/'tools/handoff.py')
handoff=importlib.util.module_from_spec(spec);spec.loader.exec_module(handoff)

def archive(metadata,state=None,extra=None):
    output=io.BytesIO()
    with zipfile.ZipFile(output,'w') as bundle:
        bundle.writestr('session.json',json.dumps(metadata))
        if state is not None: bundle.writestr('state.bin',state)
        if extra: bundle.writestr(extra,b'bad')
    return output.getvalue()

class HandoffTest(unittest.TestCase):
    def test_nes_transfer_carries_rom_and_verifies_both_checksums(self):
        rom=b'NES\x1a'+b'owned fixture'*100;state=b'portable NES state'
        cores=self.root/'cores';cores.mkdir()
        core=cores/'fceumm_handoff_libretro.so';core.write_bytes(b'pinned core')
        (cores/'fceumm-handoff.json').write_text(json.dumps(dict(source=handoff.NES_SOURCE,sha256=handoff.digest(core))))
        metadata=dict(protocol=1,type='game',source=handoff.NES_SOURCE,quirks=0,
            romSha256=hashlib.sha256(rom).hexdigest(),stateSha256=hashlib.sha256(state).hexdigest(),coreInfo='FCEUmm pinned',options={})
        def transfer():
            output=io.BytesIO()
            with zipfile.ZipFile(output,'w') as bundle:
                bundle.writestr('session.json',json.dumps(metadata));bundle.writestr('state.bin',state);bundle.writestr('rom.bin',rom)
            return output.getvalue()
        with patch.object(handoff.subprocess,'check_output',return_value=b'{"coreInfo":"FCEUmm pinned"}'):
            code,body=self.request('handoff',transfer());self.assertEqual(code,202,body)
            session=self.root/'handoff'/json.loads(body)['id']
            self.assertEqual((session/'game.nes').read_bytes(),rom)
            self.assertEqual((session/'state.bin').read_bytes(),state)
            (self.root/'handoff/pending').unlink();metadata['romSha256']='0'*64
            self.assertEqual(self.request('handoff',transfer())[0],400)
    def test_local_discovery_identifies_flxtr_and_rejects_nonlocal_or_invalid_probes(self):
        packet=json.dumps(dict(app='FlXtR',protocol=1,op='discover',nonce='a'*32)).encode()
        reply=handoff.local_discovery_reply(packet,'192.168.10.20','192.168.10.2',12345,'/'+'b'*32+'/','c'*32,'TV')
        self.assertEqual(reply['app'],'FlXtR');self.assertEqual(reply['nonce'],'a'*32)
        self.assertEqual(reply['endpoint'],'http://192.168.10.2:12345/'+'b'*32+'/')
        self.assertIsNone(handoff.local_discovery_reply(packet,'8.8.8.8','192.168.10.2',12345,'/','c'*32,'TV'))
        self.assertIsNone(handoff.local_discovery_reply(packet,'100.70.1.2','192.168.10.2',12345,'/','c'*32,'TV'))
        self.assertIsNone(handoff.local_discovery_reply(b'{}','192.168.10.20','192.168.10.2',12345,'/','c'*32,'TV'))
    def test_discovered_lan_client_is_ready_without_manual_tv_screen(self):
        (self.root/'handoff/armed').unlink()
        request=object.__new__(handoff.Receiver);request.server=self.server
        request.client_address=('192.168.10.20',12345);self.server.lan_clients={'192.168.10.20':time.monotonic()}
        self.assertTrue(request.armed())
        self.server.lan_clients['192.168.10.20']=time.monotonic()-601
        self.assertFalse(request.armed())
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name);handoff.ROOT=self.root
        (self.root/'handoff').mkdir();(self.root/'handoff/armed').touch()
        self.server=handoff.http.server.ThreadingHTTPServer(('127.0.0.1',0),handoff.Receiver)
        self.server.prefix='/'+'a'*32+'/';self.server.address='127.0.0.1:'+str(self.server.server_port);self.server.url='http://'+self.server.address+self.server.prefix
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start()
    def tearDown(self):self.server.shutdown();self.server.server_close();self.thread.join();self.temp.cleanup()
    def request(self,route='',body=None,prefix=None,headers=None):
        connection=http.client.HTTPConnection('127.0.0.1',self.server.server_port,timeout=5)
        connection.request('POST' if body is not None else 'GET',(prefix or self.server.prefix)+route,body,headers or {})
        response=connection.getresponse();result=(response.status,response.read());connection.close();return result
    def test_video_ack_requires_receiver_frame(self):
        metadata=dict(protocol=1,type='video',url='https://video.example/a.m3u8',positionMs=123456,subtitle='tr',item=dict(kind='kiss-episode',id=42,title='Anime'))
        code,body=self.request('handoff',archive(metadata));self.assertEqual(code,202)
        identity=json.loads(body)['id'];pending=(self.root/'handoff/pending').read_text();self.assertIn('\t123456\ttr\tAnime',pending)
        self.assertEqual(json.loads(self.request('status/'+identity)[1])['status'],'waiting')
        (self.root/'handoff'/identity/'started').touch()
        self.assertEqual(json.loads(self.request('status/'+identity)[1])['status'],'started')
        self.assertNotEqual(self.request('handoff',archive(metadata))[0],202)
    def test_anime_handoff_refreshes_source_and_preserves_provider_and_variant(self):
        metadata=dict(protocol=1,type='video',url='https://cdn.example/expired.m3u8',positionMs=1000,subtitle='',variant='DUB',item=dict(kind='kiss-episode',id=42,episode=3,title='Anime'))
        fresh='https://cdn.example/fresh.m3u8'
        with patch.object(handoff.subprocess,'check_output',return_value='AUTO\t\t\t'+fresh+'\tsource\n') as resolve:
            code,body=self.request('handoff',archive(metadata))
        self.assertEqual(code,202)
        self.assertEqual(resolve.call_args.args[0],['./greenlink-catalog','kiss-quality','1','','42','0','3','DUB'])
        directory=self.root/'handoff'/json.loads(body)['id']
        saved=json.loads((directory/'session.json').read_text())
        self.assertEqual(saved['url'],fresh);self.assertEqual(saved['variant'],'DUB')
        self.assertEqual((directory/'provider').read_text(),'megaplay\n')
    def test_rejects_unrecognized_provider_profiles(self):
        metadata=dict(protocol=1,type='video',url='https://video.example/a.m3u8',positionMs=0,subtitle='',provider='arbitrary-header')
        self.assertEqual(self.request('handoff',archive(metadata))[0],400)
    def test_pairing_expiry_origin_and_invalid_archive(self):
        self.assertEqual(self.request('capabilities',prefix='/wrong/')[0],403)
        self.assertEqual(self.request('capabilities',headers={'Origin':'https://evil.example'})[0],403)
        (self.root/'handoff/armed').unlink()
        self.assertEqual(self.request('handoff',b'bad')[0],409)
        (self.root/'handoff/armed').touch();os.utime(self.root/'handoff/armed',(time.time()-601,)*2)
        self.assertEqual(self.request('handoff',b'bad')[0],409)
        (self.root/'handoff/armed').touch()
        self.assertEqual(self.request('handoff',b'bad')[0],400)
    def test_rejects_nonportable_states_and_wrong_rom(self):
        state=b'exact-state';metadata=dict(protocol=1,type='game',source='different-core',quirks=0)
        self.assertEqual(self.request('handoff',archive(metadata,state))[0],400)
        metadata.update(source=handoff.SOURCE,quirks=64)
        self.assertEqual(self.request('handoff',archive(metadata,state))[0],400)
        metadata.update(quirks=0,romSha256='0'*64);(self.root/'games.json').write_text('[]')
        self.assertEqual(self.request('handoff',archive(metadata,state))[0],400)
    def test_tailnet_pairing_keeps_remote_address_and_requires_private_forward(self):
        (self.root/'handoff/armed').unlink();(self.root/'tailscale').mkdir();(self.root/'tailscale/ready').touch()
        remote='100.70.1.2:43927';(self.root/'handoff/remote-url.txt').write_text('http://'+remote+self.server.prefix)
        code,page=self.request(headers={'Host':remote});self.assertEqual(code,200)
        self.assertIn(b'100.70.1.2%3A43927',page)
        self.assertTrue(json.loads(self.request('capabilities',headers={'Host':remote})[1])['armed'])
        self.assertEqual(self.request('capabilities',headers={'Host':'evil.example'})[0],403)
        (self.root/'handoff/busy').touch()
        code,_=self.request('handoff',archive(dict(protocol=1,type='video',url='https://video.example/a.m3u8',positionMs=0,subtitle='')))
        self.assertNotEqual(code,202)
    def test_tailnet_discovery_needs_no_pairing_token_but_rejects_lan_and_origins(self):
        remote='100.70.1.2:43927'
        self.assertEqual(self.request('.well-known/flxtr',prefix='/',headers={'Host':remote})[0],403)
        (self.root/'tailscale').mkdir();(self.root/'tailscale/ready').touch()
        endpoint='http://'+remote+self.server.prefix
        (self.root/'handoff/remote-url.txt').write_text(endpoint)
        code,body=self.request('.well-known/flxtr',prefix='/',headers={'Host':remote})
        self.assertEqual(code,200);self.assertEqual(json.loads(body)['endpoint'],endpoint)
        self.assertEqual(self.request('.well-known/flxtr',prefix='/',headers={'Host':remote,'Origin':'https://evil.example'})[0],403)
        request=object.__new__(handoff.Receiver);request.server=self.server
        request.headers={'Host':remote};request.client_address=('192.168.10.50',1234)
        self.assertIsNone(request.tailnet_discovery())
        request.client_address=('127.0.0.1',1234)
        (self.root/'handoff/remote-url.txt').write_text('http://192.168.10.2:43927'+self.server.prefix)
        self.assertIsNone(request.tailnet_discovery())
    def test_state_bytes_preserved_and_existing_saves_untouched(self):
        rom=self.root/'test.gb';rom.write_bytes(b'owned game fixture');core=self.root/'cores';core.mkdir();(core/'gambatte_handoff_libretro.so').write_bytes(b'core fixture')
        (core/'gambatte-handoff.json').write_text(json.dumps(dict(source=handoff.SOURCE,sha256=handoff.digest(core/'gambatte_handoff_libretro.so'))))
        (self.root/'games.json').write_text(json.dumps([dict(rom=str(rom))]));save=self.root/'existing.state';save.write_bytes(b'keep')
        state=b'exact mid-frame emulator snapshot';metadata=dict(protocol=1,type='game',source=handoff.SOURCE,quirks=0,romSha256=handoff.digest(rom),stateSha256=hashlib.sha256(state).hexdigest(),coreInfo='Gambatte v0.5.0',options={})
        with patch.object(handoff.subprocess,'check_output',return_value=b'{"coreInfo":"Gambatte v0.5.0"}'):
            code,body=self.request('handoff',archive(metadata,state));self.assertEqual(code,202,body)
        identity=json.loads(body)['id'];self.assertEqual((self.root/'handoff'/identity/'state.bin').read_bytes(),state);self.assertEqual(save.read_bytes(),b'keep')
        (self.root/'handoff/pending').unlink();metadata['stateSha256']='0'*64
        with patch.object(handoff.subprocess,'check_output',return_value=b'{"coreInfo":"Gambatte v0.5.0"}'):
            self.assertEqual(self.request('handoff',archive(metadata,state))[0],400)
        self.assertEqual(self.request('handoff',archive(metadata,state,'../existing.state'))[0],400);self.assertEqual(save.read_bytes(),b'keep')

if __name__=='__main__':unittest.main()
