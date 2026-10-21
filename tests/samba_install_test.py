"""Installer integrity, archive confinement and transactional failure tests."""
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch
spec=importlib.util.spec_from_file_location("samba_install",Path(__file__).resolve().parents[1]/"tools/samba_install.py")
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

def archive(path, name="usr/lib/test", link=None):
    with tarfile.open(path,"w:gz") as t:
        i=tarfile.TarInfo(name)
        if link is not None:i.type=tarfile.SYMTYPE;i.linkname=link;t.addfile(i)
        else:i.size=4;t.addfile(i,io.BytesIO(b"data"))

class InstallerTests(unittest.TestCase):
    def test_path_and_symlink_confinement(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)/"root";root.mkdir()
            for name,link in (("usr/../../escape",None),("/usr/lib/escape",None),("usr/lib/escape","../../../escape")):
                p=Path(d)/"package";archive(p,name,link)
                with self.assertRaises(ValueError):m.unpack(p,root,0)
            (root/"usr").mkdir(exist_ok=True);(root/"usr/lib").mkdir(exist_ok=True)
            (root/"usr/lib/link").symlink_to(Path(d))
            p=Path(d)/"package";archive(p,"usr/lib/link/out")
            with self.assertRaises(ValueError):m.unpack(p,root,0)
            self.assertFalse((Path(d)/"out").exists())

    def test_limit(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)/"root";root.mkdir();p=Path(d)/"package";archive(p)
            with self.assertRaises(ValueError):m.unpack(p,root,m.MAX_UNPACKED-2)

    def test_download_hash_size_https(self):
        data=b"fixture"
        package=dict(url=m.HOST+"fixture.apk",size=len(data),sha256=hashlib.sha256(data).hexdigest())
        with tempfile.TemporaryDirectory() as d:
            for content,ok in ((data,True),(b"corrupt",False),(data+b"x",False)):
                target=Path(d)/("file"+str(len(content))+str(ok))
                with patch.object(m.urllib.request,"build_opener") as opener:
                    opener.return_value.open.return_value=io.BytesIO(content)
                    if ok:m.download(package,target,Path(d)/"cert")
                    else:
                        with self.assertRaises(ValueError):m.download(package,target,Path(d)/"cert")
            with self.assertRaises(ValueError):m.download(dict(package,url="http://unsafe"),Path(d)/"no",Path(d)/"cert")
            with self.assertRaises(ValueError):m.HTTPSRedirect().redirect_request(None,None,302,"",{},"http://unsafe")

    def test_failure_is_atomic_and_lock_released(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);manifest=dict(version="test",packages=[dict(size=4,url=m.HOST+"test.apk",sha256="0"*64)])
            (root/"samba-packages.json").write_text(json.dumps(manifest))
            def fail(*args):raise ValueError("network failure")
            for _ in range(2):
                with self.assertRaises(ValueError):m.install(root,fail)
                self.assertFalse((root/"samba-runtime").exists())
                self.assertFalse(list(root.glob(".samba-download-*")))

    def test_concatenated_apk(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/"package";control=io.BytesIO();data=io.BytesIO()
            for stream,name in ((control,".PKGINFO"),(data,"usr/lib/file")):
                with tarfile.open(fileobj=stream,mode="w") as t:
                    i=tarfile.TarInfo(name);i.size=4;t.addfile(i,io.BytesIO(b"data"))
            p.write_bytes(gzip.compress(control.getvalue())+gzip.compress(data.getvalue()))
            root=Path(d)/"root";root.mkdir();m.unpack(p,root,0)
            self.assertEqual((root/"usr/lib/file").read_bytes(),b"data")
            self.assertFalse((root/".PKGINFO").exists())

if __name__=="__main__":unittest.main()
