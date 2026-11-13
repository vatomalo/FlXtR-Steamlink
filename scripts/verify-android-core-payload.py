#!/usr/bin/env python3
"""Verify each requested APK ABI contains the SoM proxy and its backend."""
import sys
import zipfile

apk=sys.argv[1]
abis=[item for item in sys.argv[2].split(',') if item]
required=("libflxtr_som_ai.so","libflxtr_snes9x.so")
with zipfile.ZipFile(apk) as archive:
    names=set(archive.namelist())
missing=[f"lib/{abi}/{library}" for abi in abis for library in required if f"lib/{abi}/{library}" not in names]
if missing:
    raise SystemExit("Android AI core payload incomplete: "+", ".join(missing))
print("Verified Android SoM AI proxy and Snes9x backend for "+", ".join(abis))
