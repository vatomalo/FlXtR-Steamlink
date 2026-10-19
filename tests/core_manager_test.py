"""Core installation boundaries: archives, architecture, publication and redirects."""
from pathlib import Path
import importlib.util
import io
import json
import os
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import warnings
spec=importlib.util.spec_from_file_location('core_manager',Path(__file__).resolve().parents[1]/'tools/core_manager.py')
manager=importlib.util.module_from_spec(spec);spec.loader.exec_module(manager)
class CoreTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='core test ø ')
        self.previous=Path.cwd();os.chdir(self.temp.name)
    def tearDown(self):
        os.chdir(self.previous);self.temp.cleanup()
    def archive(self,entries):
        result=io.BytesIO()
        with warnings.catch_warnings(),zipfile.ZipFile(result,'w') as bundle:
            warnings.simplefilter('ignore',UserWarning)
            for name,value in entries:bundle.writestr(name,value)
        result.seek(0);return result
    def elf(self,machine=62):
        data=bytearray(64);data[:6]=b'\x7fELF\x02\x01';struct.pack_into('<H',data,18,machine);return data
    def test_architecture(self):
        path=Path('binary');path.write_bytes(self.elf())
        self.assertTrue(manager.compatible_header(path,'linux'))
        path.write_bytes(self.elf(40));self.assertFalse(manager.compatible_header(path,'linux'))
        self.assertFalse(manager.compatible_header(path,'windows'))
        data=bytearray(256);data[:2]=b'MZ';struct.pack_into('<I',data,60,128);data[128:132]=b'PE\0\0'
        struct.pack_into('<H',data,132,0x8664);struct.pack_into('<H',data,152,0x20b);path.write_bytes(data)
        self.assertTrue(manager.compatible_header(path,'windows'))
    def test_archive_rejection(self):
        for entries in [[('../fceumm_libretro.so',b'bad')],[('/fceumm_libretro.so',b'bad')],
                        [('fceumm_libretro.so',b'ok'),('other',b'bad')],
                        [('fceumm_libretro.so',b'a'),('fceumm_libretro.so',b'b')]]:
            with self.subTest(entries=entries),tempfile.TemporaryFile('w+b') as output:
                with self.assertRaises(ValueError):manager.unpack(self.archive(entries),'fceumm_libretro.so',output)
        symlink=zipfile.ZipInfo('fceumm_libretro.so');symlink.external_attr=0o120777<<16
        with tempfile.TemporaryFile('w+b') as output:
            with self.assertRaises(ValueError):manager.unpack(self.archive([(symlink,b'/tmp/evil')]),symlink.filename,output)
    def test_install_and_existing_preserved(self):
        payload=self.archive([('fceumm_libretro.so',self.elf())]).getvalue()
        def download(url,stream):
            self.assertEqual(url,'https://buildbot.libretro.com/nightly/linux/x86_64/latest/fceumm_libretro.so.zip')
            stream.write(payload);stream.seek(0)
        Path('games.json').write_text('[]')
        with patch.object(manager,'target',return_value=('linux','.so')),patch.object(manager,'download',side_effect=download),patch.object(manager.subprocess,'run') as run:
            run.return_value.returncode=0
            self.assertEqual(manager.install('fceumm'),'Core installed')
            self.assertFalse(Path('games.json').exists())
            self.assertEqual(run.call_args.kwargs['creationflags'],getattr(manager.subprocess,'CREATE_NO_WINDOW',0))
            info=json.loads(Path('cores/fceumm_libretro.so.json').read_text())
            self.assertEqual(info['platform'],'linux-x86_64');self.assertEqual(len(info['sha256']),64)
            Path('cores/fceumm_libretro.so').write_bytes(b'custom core')
            self.assertEqual(manager.install('fceumm'),'Existing core kept')
            self.assertEqual(Path('cores/fceumm_libretro.so').read_bytes(),b'custom core')
    def test_failed_download_cleans_staging(self):
        def download(url,stream):raise OSError('network down')
        with patch.object(manager,'target',return_value=('linux','.so')),patch.object(manager,'download',side_effect=download):
            with self.assertRaises(OSError):manager.install('fceumm')
        self.assertEqual([p.name for p in Path('cores').iterdir()],['.install.lock'])
    def test_failed_probe_never_publishes(self):
        payload=self.archive([('fceumm_libretro.so',self.elf())]).getvalue()
        def download(url,stream):stream.write(payload);stream.seek(0)
        with patch.object(manager,'target',return_value=('linux','.so')),patch.object(manager,'download',side_effect=download),patch.object(manager.subprocess,'run') as run:
            run.return_value.returncode=1;run.return_value.stderr=b'missing library'
            with self.assertRaises(ValueError):manager.install('fceumm')
        self.assertFalse(Path('cores/fceumm_libretro.so').exists())
        self.assertEqual([p.name for p in Path('cores').iterdir()],['.install.lock'])
    def test_official_https_redirect_only(self):
        for url in ['http://buildbot.libretro.com/x','https://evil.example/x','https://buildbot.libretro.com@evil.example/x']:
            with self.assertRaises(ValueError):manager.OfficialRedirects().redirect_request(None,None,302,'',{},url)
    def test_unsupported_architecture(self):
        with patch.object(manager.platform,'machine',return_value='armv7l'):
            with self.assertRaisesRegex(ValueError,'Steam Link'):manager.target()
if __name__=='__main__':unittest.main()
