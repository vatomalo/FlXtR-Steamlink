"""Exercise verified native-game installation, offline launch and rejection."""
import hashlib, io, os, pathlib, subprocess, tarfile, tempfile
launcher=pathlib.Path('packaging/deceptive-dice.sh').resolve()
worker=pathlib.Path('build/greenlink-catalog').resolve()
with tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp)
    bins=root/'bin';bins.mkdir()
    (bins/'curl').write_text('#!/bin/sh\nwhile [ "$1" != "-o" ]; do shift; done\ncp "$FIXTURE" "$2"\n')
    (bins/'curl').chmod(0o755)
    fixture=root/'fixture.tgz'
    def archive(unsafe=False):
        with tarfile.open(fixture,'w:gz') as out:
            for name,data in [('deceptive-dice/deceptive-dice',b'#!/bin/sh\n'),('deceptive-dice/launch.sh',b'#!/bin/sh\nprintf launched >> ../launched\n')]+([('../outside',b'bad')] if unsafe else []):
                item=tarfile.TarInfo(name);item.mode=0o755;item.size=len(data);out.addfile(item,io.BytesIO(data))
    def conf(digest):
        (root/'deceptive-dice-release.conf').write_text(f'DICE_URL=https://example.invalid/game.tgz\nDICE_SHA256={digest}\n')
    def run():
        return subprocess.run(['sh',str(launcher)],cwd=root,env={**os.environ,'PATH':str(bins)+':'+os.environ['PATH'],'FIXTURE':str(fixture)},capture_output=True,text=True)
    archive();conf('0'*64)
    assert run().returncode!=0 and not (root/'native-games/deceptive-dice').exists()
    archive(True);conf(hashlib.sha256(fixture.read_bytes()).hexdigest())
    assert run().returncode!=0 and not (root/'outside').exists()
    archive();conf(hashlib.sha256(fixture.read_bytes()).hexdigest())
    assert run().returncode==0
    fixture.unlink() # Second launch must not contact the network.
    assert run().returncode==0
    assert (root/'native-games/launched').read_text()=='launchedlaunched'
    assert not list((root/'native-games').glob('.dice-*'))
    result=subprocess.run([str(worker),'games-folders','3','','0','0','0'],cwd=root,env={**os.environ,'FLXTR_NO_ART':'1'},capture_output=True,text=True,check=True)
    assert 'DECEPTIVE DICE' in result.stdout and 'deceptive-dice' in result.stdout
    (root/'deceptive-dice.sh').write_text('#!/bin/sh\nprintf yes > handoff\n')
    (root/'game-request').write_text('2049 -1 -1\n')
    subprocess.run([str(worker),'--run-game'],cwd=root,check=True)
    assert (root/'handoff').read_text()=='yes'
print('Native game install, rejection, offline launch and catalog handoff passed')
