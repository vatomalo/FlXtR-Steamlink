#!/usr/bin/env python3
"""Find an approved Android SoM bridge through the local Tailscale node."""
import concurrent.futures
import ipaddress
import json
import os
import re
import shutil
import subprocess
import urllib.parse
import urllib.request

TAILNET = ipaddress.ip_network("100.64.0.0/10")
TOKEN = re.compile(r"/[0-9a-f]{32}/som$")
DIRECT_OPEN = urllib.request.build_opener(urllib.request.ProxyHandler({})).open


def peer_hosts(status):
    peers = status.get("Peer", {}) if isinstance(status, dict) else {}
    peers = list(peers.values()) if isinstance(peers, dict) else peers
    if not isinstance(peers, (list, tuple)):
        return []
    found = []
    for peer in peers:
        if not isinstance(peer, dict) or peer.get("Online") is False:
            continue
        for value in peer.get("TailscaleIPs", []):
            try:
                address = ipaddress.ip_address(value)
            except ValueError:
                continue
            if address.version == 4 and address in TAILNET:
                found.append(str(address))
                break
    return sorted(set(found))[:128]

def endpoint_from_reply(host, value):
    if not isinstance(value, dict) or value.get("app") != "FlXtR" or value.get("protocol") != 1 or value.get("role") != "som-ai":
        return None
    endpoint = value.get("endpoint")
    if not isinstance(endpoint, str):
        return None
    parsed = urllib.parse.urlparse(endpoint)
    if (parsed.scheme != "http" or parsed.hostname != host or parsed.port != 43927 or
            parsed.username or parsed.password or parsed.query or parsed.fragment or
            not TOKEN.fullmatch(parsed.path)):
        return None
    return endpoint


def probe(host, opener=DIRECT_OPEN):
    request = urllib.request.Request(f"http://{host}:43927/.well-known/flxtr/som", method="GET")
    try:
        with opener(request, timeout=2.0) as response:
            raw = response.read(4097)
        if len(raw) > 4096:
            return None
        return endpoint_from_reply(host, json.loads(raw))
    except (OSError, ValueError, TypeError, json.JSONDecodeError):
        return None


def tailscale_status():
    command = os.environ.get("TAILSCALE_CLI") or shutil.which("tailscale")
    if not command:
        return {}
    try:
        result = subprocess.run([command, "status", "--json"], capture_output=True,
                                check=True, text=True, timeout=8)
        return json.loads(result.stdout)
    except (OSError, ValueError, subprocess.SubprocessError):
        return {}


def discover(status=None, opener=DIRECT_OPEN):
    hosts = peer_hosts(tailscale_status() if status is None else status)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for endpoint in pool.map(lambda host: probe(host, opener), hosts):
            if endpoint:
                return endpoint
    return None


def main():
    endpoint = discover()
    if endpoint:
        print(endpoint)
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
