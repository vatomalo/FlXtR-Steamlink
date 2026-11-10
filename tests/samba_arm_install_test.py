"""Run with the bundled ARM Python under QEMU; use signature-checked cached APKs."""
from pathlib import Path
import hashlib
import importlib.util
import shutil
import tempfile
root=Path("/work")
spec=importlib.util.spec_from_file_location("samba_install",root/"tools/samba_install.py")
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix="FlXtR ARM installer ") as directory:
    base=Path(directory)
    shutil.copyfile(root/"packaging/samba-packages.json",base/"samba-packages.json")
    shutil.copyfile(root/"dist/steamlink/apps/greenlink/samba-paths.so",base/"samba-paths.so")
    def fetch(package, output, cert):
        source=root/"build/samba-apks"/package["url"].rsplit("/",1)[1]
        assert source.stat().st_size==package["size"]
        assert hashlib.sha256(source.read_bytes()).hexdigest()==package["sha256"]
        shutil.copyfile(source,output)
    module.install(base,fetch)
    module.validate(base/"samba-runtime")
    assert (base/"samba-runtime/PACKAGE-SOURCES.json").exists()
    assert not list(base.glob(".samba-download-*"))
    module.install(base,fetch)
print("PASS: bundled ARM Python installs all 53 pinned APKs, validates runtime, cleans staging and handles repeat install")
