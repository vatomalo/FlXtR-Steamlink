#!/usr/bin/env python3
"""Package committed app sources plus the pinned native dependency inputs."""
import io,os,subprocess,zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
deps=Path(os.environ.get('FLXTR_GAME_SOURCES',root/'android/build/game-sources'))
output=root/'dist/FlXtR-Android-source.zip';output.parent.mkdir(exist_ok=True)
output.write_bytes(subprocess.check_output(['git','-C',str(root),'archive','--format=zip','HEAD']))
moon=deps/'moonlight';common=moon/'app/src/main/jni/moonlight-core/moonlight-common-c'
inputs=[('moonlight-12.1',moon,'1b1b100e63e8ffcf1496e324ff7b57ee9f2db2a9',[]),
        ('moonlight-12.2-crypto-opus',moon,'b48494cb96bff23d8886c4775cc4f39a1075495d',['app/src/main/jni/moonlight-core/openssl','app/src/main/jni/moonlight-core/opus']),
        ('moonlight-common-c',common,'8af4562af672dd6b9ed28553ead172984fd9a683',[]),
        ('enet',common/'enet','d3a323fc8b9559786ee059205db74e8027c756f7',[]),
        ('gambatte',deps/'gambatte','77f2567d718b8973d02caa2daa8dce4953faf811',[])]
with zipfile.ZipFile(output,'a') as bundle:
    for name,path,revision,paths in inputs:
        data=subprocess.check_output(['git','-C',str(path),'archive','--format=tar.gz','--prefix='+name+'/',revision,*paths])
        bundle.writestr('android-native-inputs/'+name+'-'+revision[:12]+'.tar.gz',data)
print(output)
