"""Package rejection and payload isolation tests; no network or device access."""
import hashlib
import importlib.util
import io
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("deploy", Path(__file__).resolve().parents[1] / "scripts/deploy-steamlink.py")
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)


class DeploymentTest(unittest.TestCase):
    def package(self, root, *, dirty=b"0\n", corrupt=False, extra=None, duplicate=False):
        elf = b"\x7fELF\x01" + bytes(13) + b"\x28\x00" + bytes(44)
        files = {name: elf for name in deploy.BINARIES}
        files["build-version.txt"] = b"a" * 40 + b"\n"
        files["build-source-dirty.txt"] = dirty
        files["build-sha256.txt"] = "".join(f"{hashlib.sha256(elf).hexdigest()}  {name}\n" for name in deploy.BINARIES).encode()
        files["settings.cfg"] = b"must not be installed\n"
        if corrupt:
            files["greenlink"] += b"corrupt"
        path = root / "package.tgz"
        with tarfile.open(path, "w:gz") as archive:
            for name, data in files.items():
                info = tarfile.TarInfo("greenlink/" + name)
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
            if extra:
                archive.addfile(tarfile.TarInfo(extra))
            if duplicate:
                archive.addfile(tarfile.TarInfo("greenlink/greenlink"))
        return path

    def test_verified_payload_excludes_private_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "payload.tgz"
            self.assertEqual(deploy.verified_payload(self.package(root), output), "a" * 40)
            with tarfile.open(output) as archive:
                self.assertEqual(set(archive.getnames()), set(deploy.BINARIES) | {"build-version.txt", "build-sha256.txt"})

    def test_invalid_packages_rejected(self):
        for options in ({"dirty": b"1\n"}, {"corrupt": True}, {"extra": "greenlink/../../settings.cfg"}, {"extra": "other/greenlink"}, {"duplicate": True}):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                with self.assertRaises(ValueError):
                    deploy.verified_payload(self.package(root, **options), root / "payload.tgz")

    def test_remote_shell_syntax(self):
        script = deploy.REMOTE.replace("TOKEN", "a" * 32).replace("VERSION", "b" * 40).replace("REQUIRED_KB", "12345")
        subprocess.run(["sh", "-n"], input=script, text=True, check=True)


if __name__ == "__main__":
    unittest.main()
