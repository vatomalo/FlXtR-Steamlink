#!/usr/bin/env python3
"""Reject a stale/older build script that ignored the requested APK target."""
import sys
import zipfile

def verify(apk, requested):
    with zipfile.ZipFile(apk) as archive:
        actual = {name.split('/')[1] for name in archive.namelist() if name.startswith('lib/') and name.endswith('.so')}
    expected = set(requested.split(','))
    if actual != expected:
        raise ValueError(f'APK architectures {sorted(actual)} do not match requested {sorted(expected)}. '
                         'Use Universal or update this branch’s Android build script.')
    return actual

if __name__ == '__main__':
    try:
        print('Verified APK architectures: ' + ', '.join(sorted(verify(sys.argv[1], sys.argv[2]))))
    except ValueError as error:
        raise SystemExit(str(error))
