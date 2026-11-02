#!/usr/bin/env python3
"""FlXtR approved-game Sunshine sync (Windows Python 3, standard library)."""
import argparse, base64, getpass, json, ssl, subprocess, urllib.request
from pathlib import Path

def request(base, username, password, method="GET", data=None, insecure=False):
    if not base.startswith("https://"):
        raise ValueError("Sunshine host must start with https://")
    auth = base64.b64encode((username+":"+password).encode()).decode()
    headers = {"Authorization": "Basic "+auth, "Accept":"application/json"}
    body = json.dumps(data).encode() if data is not None else None
    if body is not None: headers["Content-Type"]="application/json"
    ctx = ssl._create_unverified_context() if insecure else ssl.create_default_context()
    req = urllib.request.Request(base.rstrip("/")+"/api/apps", data=body, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=12, context=ctx) as response:
        return json.loads(response.read())

def app_list(response):
    apps = response if isinstance(response,list) else response.get("apps")
    if not isinstance(apps,list): raise ValueError("Unexpected Sunshine /api/apps response")
    return apps

def synchronize(catalog,host,username,password,insecure=False,dry_run=False):
    apps = app_list(request(host,username,password,insecure=insecure))
    statuses=[]
    for game in catalog:
        if not game.get("approved"): continue
        exe=Path(game["exe"])
        if not exe.is_file() or exe.suffix.lower()!=".exe":
            statuses.append((game.get("name"),"missing executable")); continue
        name=game["name"].strip()
        if not name: raise ValueError("Approved entry has empty name")
        cmd=subprocess.list2cmdline([str(exe)])
        existing=next((a for a in apps if a.get("name","").casefold()==name.casefold()),None)
        if existing:
            statuses.append((name,"already registered" if existing.get("cmd","").strip()==cmd else "NAME CONFLICT; preserved existing entry"))
            continue
        if dry_run: statuses.append((name,"would register")); continue
        request(host,username,password,"POST",{"name":name,"cmd":cmd,"index":-1,"elevated":False,"auto-detach":False,"wait-all":True},insecure)
        apps=app_list(request(host,username,password,insecure=insecure))
        ok=any(a.get("name","").casefold()==name.casefold() and a.get("cmd","").strip()==cmd for a in apps)
        statuses.append((name,"verified" if ok else "NOT VERIFIED"))
    return statuses

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--catalog",default="gamez-approved.json")
    p.add_argument("--host",required=True,help="Actual Sunshine HTTPS host:port")
    p.add_argument("--user",required=True)
    p.add_argument("--insecure-local",action="store_true",help="Accept local self-signed certificate")
    p.add_argument("--dry-run",action="store_true")
    a=p.parse_args()
    pw=getpass.getpass("Sunshine password: ")
    for name,status in synchronize(json.loads(Path(a.catalog).read_text(encoding="utf-8")),a.host,a.user,pw,a.insecure_local,a.dry_run):
        print(f"{name}: {status}")

if __name__=="__main__": main()
