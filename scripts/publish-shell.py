#!/usr/bin/env python3
"""Publish an immutable shell-only release. Token comes from GH_TOKEN or stdin."""
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
    sys.exit('Refusing to publish a dirty checkout; commit and rebuild first.')
version = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
binary = pathlib.Path('build/greenlink-arm').read_bytes()
manifest = pathlib.Path('dist/shell-manifest.txt').read_text()
expected = f'version={version}\nsha256={hashlib.sha256(binary).hexdigest()}\n'
if manifest != expected:
    sys.exit('Build does not match the checkout or binary. Rebuild first.')
token = os.environ.get('GH_TOKEN') or sys.stdin.readline().strip()
if not token:
    sys.exit('Publishing requires GH_TOKEN or a token on stdin (never saved).')
api = 'https://api.github.com/repos/vatomalo/FlXtR-Steamlink'
def request(url, data, content_type='application/json', method='POST'):
    req = urllib.request.Request(url, data=data, method=method, headers={
        'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json',
        'Content-Type': content_type, 'User-Agent': 'FlXtR-build'})
    with urllib.request.urlopen(req, timeout=60) as response:
        return json.load(response)

# Draft first: clients cannot see the release until all assets have uploaded.
release = request(api + '/releases', json.dumps({
    'tag_name': 'shell-' + version, 'target_commitish': version,
    'name': 'Steam Link shell ' + version[:8], 'draft': True,
    'body': 'Native ARM shell update. Adds scheduled TV mode with seven editable Oslo-time genre blocks, episode completion history and automatic source selection. Requires the matching catalog helper from the local package. Includes Internet Archive video browsing. '
            'Requires the existing FlXtR installation; player, resolver and private catalogs stay installed. '
            'The launcher checks this release over HTTPS and verifies SHA-256 before replacement.'
}).encode())
upload = release['upload_url'].split('{')[0]
for name, data, mime in [('greenlink', binary, 'application/octet-stream'),
                         ('shell-manifest.txt', manifest.encode(), 'text/plain')]:
    request(upload + '?name=' + name, data, mime)
published = request(api + '/releases/' + str(release['id']),
                    b'{"draft": false, "make_latest": "true"}', method='PATCH')
print(published['html_url'])
