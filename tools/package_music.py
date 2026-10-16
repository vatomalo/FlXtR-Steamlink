"""Build-time subset of yt-dlp for the YouTube-only music worker."""
import compileall
from pathlib import Path
import sys
from zipfile import ZipFile

root = Path(sys.argv[2])
with ZipFile(sys.argv[1]) as archive:
    for entry in archive.infolist():
        path = Path(entry.filename)
        if path.is_absolute() or '..' in path.parts:
            raise ValueError('Unsafe zip entry')
        if entry.filename.startswith('yt_dlp/extractor/'):
            sub = entry.filename[len('yt_dlp/extractor/'):]
            if not sub.startswith('youtube/') and sub not in {
                '__init__.py', 'common.py', 'extractors.py', 'lazy_extractors.py',
                '_extractors.py', '_mistakes.py', 'generic.py', 'commonprotocols.py',
                'afreecatv.py', 'adobepass.py', 'openload.py',
            }:
                continue
        archive.extract(entry, root)
    (root / 'yt_dlp' / '__main__.py').write_bytes(archive.read('__main__.py'))
if not compileall.compile_dir(root, quiet=1):
    raise SystemExit('Could not compile music worker dependencies')
