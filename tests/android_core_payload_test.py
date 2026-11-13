import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SCRIPT=ROOT/"scripts/verify-android-core-payload.py"

class AndroidCorePayloadTest(unittest.TestCase):
    def make_apk(self,path,libraries):
        with zipfile.ZipFile(path,"w") as archive:
            for name in libraries:archive.writestr(name,b"\x7fELF")
    def test_requires_proxy_and_backend_for_every_abi(self):
        with tempfile.TemporaryDirectory() as folder:
            apk=Path(folder)/"app.apk"
            names=[f"lib/{abi}/{library}" for abi in ("arm64-v8a","armeabi-v7a") for library in ("libflxtr_som_ai.so","libflxtr_snes9x.so")]
            self.make_apk(apk,names)
            self.assertEqual(0,subprocess.run([sys.executable,SCRIPT,apk,"arm64-v8a,armeabi-v7a"]).returncode)
            self.make_apk(apk,names[:-1])
            self.assertNotEqual(0,subprocess.run([sys.executable,SCRIPT,apk,"arm64-v8a,armeabi-v7a"],capture_output=True).returncode)

if __name__=="__main__":unittest.main()
