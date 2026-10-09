#!/usr/bin/env python3
"""Publish an immutable complete Steam Link package with a verified manifest."""
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import urllib.request

root = pathlib.Path(__file__).resolve().parent.parent
os.chdir(root)
if subprocess.check_output(['git', 'status', '--porcelain']).strip():
    sys.exit('Dirty checkout; commit changes and rebuild first.')
version = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
build_info = pathlib.Path('dist/build-info.txt').read_text()
if f'commit={version}\n' not in build_info:
    sys.exit('Package build does not match HEAD. Rebuild first.')
package = pathlib.Path('dist/greenlink.tgz').read_bytes()
digest = hashlib.sha256(package).hexdigest()
if digest not in build_info:
    sys.exit('Package checksum differs from build-info; rebuild first.')
manifest = f'version={version}\nsha256={digest}\n'.encode()
token = os.environ.get('GH_TOKEN') or sys.stdin.readline().strip()
if not token:
    sys.exit('Publishing requires GH_TOKEN or a token on stdin (never saved).')
api = 'https://api.github.com/repos/vatomalo/FlXtR-Steamlink'
def request(url, data, content_type='application/json', method='POST'):
    req = urllib.request.Request(url, data=data, method=method, headers={
        'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json',
        'Content-Type': content_type, 'User-Agent': 'FlXtR-package-build'})
    with urllib.request.urlopen(req, timeout=90) as response:
        return json.load(response)
release = request(api + '/releases', json.dumps({
    'tag_name': 'package-' + version,
    'target_commitish': version,
    'name': 'Steam Link package ' + version[:8],
    'draft': True,
    'body': 'Complete ARM Steam Link package: menu, catalog, player, resolver, art and launch helpers. '
            'Install through SSH once to seed the new updater; subsequent full-package releases '
            'are SHA-256 checked and applied at launch. Local settings and ROMs are not in the package.'
}).encode())
upload = release['upload_url'].split('{')[0]
for name, data, mime in [('greenlink.tgz', package, 'application/gzip'),
                         ('package-manifest.txt', manifest, 'text/plain')]:
    request(upload + '?name=' + name, data, mime)
published = request(api + '/releases/' + str(release['id']),
                    b'{"draft": false, "make_latest": "true"}', method='PATCH')
print(published['html_url'])
