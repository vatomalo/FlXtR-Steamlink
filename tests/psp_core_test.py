import hashlib,importlib.util,io,json,tarfile,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('psp_core',Path(__file__).resolve().parents[1]/'tools/psp_core.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
def package(name='cores/ppsspp_libretro.so',bad_arch=False,link=False):
 buffer=io.BytesIO();header=bytearray(20);header[:6]=b'\x7fELF\x01\x01';header[18]=62 if bad_arch else 40
 with tarfile.open(fileobj=buffer,mode='w:gz') as archive:
  info=tarfile.TarInfo(name)
  if link:info.type=tarfile.SYMTYPE;info.linkname='/tmp/outside'
  else:info.size=len(header)
  archive.addfile(info,None if link else io.BytesIO(header))
  info=tarfile.TarInfo('system/PPSSPP/test.txt');info.size=5;archive.addfile(info,io.BytesIO(b'asset'))
 return buffer.getvalue()
class CoreInstallTest(unittest.TestCase):
 def test_valid_and_cached(self):
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);data=package();digest=hashlib.sha256(data).hexdigest();module.install(root,data,digest)
   assert (root/'cores/ppsspp_libretro.so').is_file() and (root/'system/PPSSPP/test.txt').read_text()=='asset'
   (root/'assets').mkdir();(root/'assets/psp-core.json').write_text(json.dumps({'sha256':digest,'url':'unused'}))
   (root/'cores/ppsspp-installed.json').write_text(json.dumps({'sha256':digest,'size':20}))
   with patch.object(module.urllib.request,'urlopen',side_effect=AssertionError('Cached core downloaded again')):module.ensure(root)
 def test_bad_hash_keeps_old_core(self):
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);(root/'cores').mkdir();core=root/'cores/ppsspp_libretro.so';core.write_bytes(b'previous')
   with self.assertRaises(ValueError):module.install(root,package(),'0'*64)
   assert core.read_bytes()==b'previous'
 def test_rejects_paths_links_architecture(self):
  for data in (package('../escape'),package('/tmp/escape'),package(link=True),package(bad_arch=True)):
   with self.subTest(),tempfile.TemporaryDirectory() as directory:
    with self.assertRaises(ValueError):module.install(Path(directory),data,hashlib.sha256(data).hexdigest())
    assert not (Path(directory)/'cores/ppsspp_libretro.so').exists()
if __name__=='__main__':unittest.main()