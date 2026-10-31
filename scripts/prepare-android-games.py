#!/usr/bin/env python3
"""Fetch pinned upstream sources; generate the embedded library manifest."""
import subprocess, re, shutil, io, tarfile, xml.etree.ElementTree as ET
from pathlib import Path
root=Path(__file__).resolve().parents[1]
deps=root/'android/build/game-sources';deps.mkdir(parents=True,exist_ok=True)
def checkout(name,url,revision):
    path=deps/name
    if not (path/'.git').exists(): subprocess.run(['git','clone','--no-checkout',url,str(path)],check=True)
    subprocess.run(['git','-C',str(path),'checkout','--detach',revision],check=True)
    subprocess.run(['git','-C',str(path),'submodule','update','--init','--recursive'],check=True)
    return path
moon=checkout('moonlight','https://github.com/moonlight-stream/moonlight-android.git','1b1b100e63e8ffcf1496e324ff7b57ee9f2db2a9')
# Keep the Android 8-compatible UI/JNI interface and refresh native crypto/audio
# from the current upstream release without requiring its Android 37 UI APIs.
crypto_revision='b48494cb96bff23d8886c4775cc4f39a1075495d'
subprocess.run(['git','-C',str(moon),'fetch','origin',crypto_revision],check=True)
native=deps/'moonlight-native'
if native.exists():shutil.rmtree(native)
shutil.copytree(moon/'app/src/main/jni',native,ignore=shutil.ignore_patterns('.git'))
prefix='app/src/main/jni/'
archive=subprocess.check_output(['git','-C',str(moon),'archive',crypto_revision,prefix+'moonlight-core/openssl',prefix+'moonlight-core/opus'])
with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
    for item in tar:
        if not item.isfile():continue
        relative=Path(item.name).relative_to(prefix)
        if len(relative.parts)>1 and relative.parts[1]=='opus':relative=Path(relative.parts[0],'libopus',*relative.parts[2:])
        target=(native/relative).resolve()
        if not target.is_relative_to(native.resolve()):raise RuntimeError('Invalid source path')
        target.parent.mkdir(parents=True,exist_ok=True)
        with tar.extractfile(item) as source,target.open('wb') as output:shutil.copyfileobj(source,output)
mk=native/'moonlight-core/Android.mk'
mk.write_text(mk.read_text().replace('libopus libssl libcrypto','libopus libcrypto'))
gb=checkout('gambatte','https://github.com/libretro/gambatte-libretro.git','77f2567d718b8973d02caa2daa8dce4953faf811')
android='{http://schemas.android.com/apk/res/android}'
ET.register_namespace('android',android[1:-1])
tree=ET.parse(moon/'app/src/main/AndroidManifest.xml');manifest=tree.getroot();app=manifest.find('application')
for feature in list(manifest.findall('uses-feature')):
    if feature.get(android+'name')=='android.software.leanback':manifest.remove(feature)
# FlXtR owns the application/launcher; retain native Moonlight activities/services.
app.attrib.clear();app.set(android+'networkSecurityConfig','@xml/network_security_config')
for node in list(app):
    if node.tag in ('activity','service','provider'):
        name=node.get(android+'name','')
        if name.startswith('.'): node.set(android+'name','com.limelight'+name)
        node.set(android+'exported','false')
        if node.tag=='activity':
            node.set(android+'theme','@style/AppTheme');node.set(android+'exported','false')
            for intent in list(node.findall('intent-filter')):node.remove(intent)
    elif node.tag not in ('uses-library','meta-data'):app.remove(node)
tree.write(root/'android/moonlight/AndroidManifest.xml',encoding='utf-8',xml_declaration=True)
# Translate upstream's authoritative portable source list into CMake.
text=(gb/'Makefile.common').read_text();sources=[]
for line in text.splitlines():
    value=line.strip().rstrip('\\').strip()
    if not value.startswith('$(') or not value.endswith(('.c','.cpp')) or 'net_serial' in value:continue
    value=value.replace('$(CORE_DIR)','libgambatte/src').replace('$(LIBRETRO_COMM_DIR)','libgambatte/libretro-common')
    sources.append('"${GAMBATTE}/'+value+'"')
(deps/'gambatte-sources.cmake').write_text('set(GAMBATTE_SOURCES\n'+'\n'.join(sources)+'\n)\n')
licenses=root/'android/build/media-runtime/runtime/licenses';licenses.mkdir(parents=True,exist_ok=True)
shutil.copyfile(moon/'LICENSE.txt',licenses/'Moonlight.txt')
shutil.copyfile(gb/'COPYING',licenses/'Gambatte.txt')
shutil.copyfile(moon/'app/src/main/jni/moonlight-core/moonlight-common-c/LICENSE.txt',licenses/'Moonlight-common.txt')
shutil.copyfile(moon/'app/src/main/jni/moonlight-core/moonlight-common-c/enet/LICENSE',licenses/'ENet.txt')
opus=(native/'moonlight-core/libopus/include/opus.h').read_text()
(licenses/'Opus.txt').write_text(opus.split('/**')[0])
shutil.copyfile(root/'android/app/src/main/assets/licenses/Media3-Apache-2.0.txt',licenses/'OpenSSL-Apache-2.0.txt')
print('Pinned Moonlight and Gambatte sources ready')
