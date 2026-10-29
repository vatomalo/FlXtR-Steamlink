import importlib.util
import tempfile
import unittest
from pathlib import Path

module_path=Path(__file__).resolve().parents[1]/"tools"/"phone_local_catalog.py"
spec=importlib.util.spec_from_file_location("phone_local_catalog",module_path)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
update=module.update

class PhoneLocalTest(unittest.TestCase):
    def test_indexes_videos_and_preserves_catalog(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); (root/"phone-files").mkdir()
            (root/"catalog.tsv").write_text("DEMO\tLOCAL\t\thttps://example.com/v\n")
            (root/"phone-files"/"My_Movie.mov").touch()
            (root/"phone-files"/"unsafe.exe").touch()
            n,changed=update(root)
            self.assertEqual((n,changed),(1,True))
            result=(root/"catalog.tsv").read_text()
            self.assertIn("DEMO\tLOCAL",result)
            self.assertIn("My Movie\tPHONE UPLOAD / LOCAL\t\tphone-files/My_Movie.mov\toffline",result)
            self.assertNotIn("unsafe.exe",result)
            self.assertEqual(update(root),(1,False))

    def test_rejects_symlinks(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); (root/"phone-files").mkdir()
            (root/"phone-files"/"real.mp4").touch()
            (root/"phone-files"/"alias.mp4").symlink_to("real.mp4")
            count,_=update(root)
            self.assertEqual(count,1)

if __name__=="__main__": unittest.main()
