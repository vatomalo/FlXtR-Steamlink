import io
from pathlib import Path
import runpy
import tarfile
import tempfile
import unittest

filter_member = runpy.run_path(str(Path(__file__).resolve().parents[1]/'scripts/fetch-windows-sdk.py'))['compiler_filter']


class CompilerExtractionTest(unittest.TestCase):
    def test_internal_parent_relative_link_is_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            stream = io.BytesIO()
            with tarfile.open(fileobj=stream, mode='w') as archive:
                file = tarfile.TarInfo('compiler/generic/include/header.h')
                file.size = 6
                archive.addfile(file, io.BytesIO(b'header'))
                link = tarfile.TarInfo('compiler/x86/include')
                link.type = tarfile.SYMTYPE
                link.linkname = '../generic/include'
                archive.addfile(link)
            stream.seek(0)
            with tarfile.open(fileobj=stream) as archive:
                archive.extractall(output, filter=filter_member)
            self.assertEqual((output/'compiler/x86/include/header.h').read_bytes(), b'header')

    def test_links_outside_destination_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            for target in ('../../../outside', '/etc/passwd'):
                link = tarfile.TarInfo('compiler/x86/include')
                link.type = tarfile.SYMTYPE
                link.linkname = target
                with self.assertRaises(ValueError):
                    filter_member(link, temporary)


if __name__ == '__main__':
    unittest.main()
