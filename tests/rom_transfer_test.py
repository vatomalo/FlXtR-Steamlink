import hashlib,importlib.util,io,json,tempfile,unittest,zipfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('rom_transfer',Path(__file__).resolve().parents[1]/'tools/rom_transfer.py')
rom=importlib.util.module_from_spec(spec);spec.loader.exec_module(rom)

class RomTransferTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name);self.base=self.root/'roms'
        (self.root/'rom-receive-root.txt').write_text(str(self.base));self.source=self.root/'source';self.source.mkdir()
    def tearDown(self):self.temp.cleanup()
    def bundle(self,name='Fixture.nes',data=b'NES\x1aoriginal test program'):
        file=self.source/name;file.write_bytes(data);bundle=self.root/'transfer.zip';rom.make_archive(file,bundle);return bundle
    def test_each_system_goes_to_its_folder_and_retry_is_idempotent(self):
        for system,extension in [('NES','.nes'),('SNES','.sfc'),('GB','.gb'),('GBC','.gbc'),('GBA','.gba')]:
            bundle=self.bundle('Fixture'+extension);receipt=rom.receive(bundle,self.root)
            self.assertEqual(Path(receipt['path']),self.base/system/('Fixture'+extension));self.assertEqual(Path(receipt['path']).read_bytes(),b'NES\x1aoriginal test program')
            self.assertTrue(rom.receive(bundle,self.root)['alreadyPresent'])
    def test_existing_system_folder_case_is_preserved(self):
        folder=self.base/'Snes';folder.mkdir(parents=True)
        self.assertEqual(Path(rom.receive(self.bundle('Fixture.smc'),self.root)['path']).parent,folder)
    def test_existing_different_rom_is_never_overwritten(self):
        rom.receive(self.bundle(),self.root)
        with self.assertRaisesRegex(ValueError,'already exists'):rom.receive(self.bundle(data=b'different'),self.root)
        self.assertEqual((self.base/'NES/Fixture.nes').read_bytes(),b'NES\x1aoriginal test program')
    def test_checksum_failure_leaves_no_rom_or_partial_file(self):
        bundle=self.bundle();original=zipfile.ZipFile(bundle);value=json.loads(original.read('rom.json'));data=original.read('rom.bin');original.close();value['sha256']='0'*64
        with zipfile.ZipFile(bundle,'w') as output:output.writestr('rom.json',json.dumps(value));output.writestr('rom.bin',data)
        with self.assertRaisesRegex(ValueError,'checksum'):rom.receive(bundle,self.root)
        self.assertEqual(list((self.base/'NES').iterdir()),[])
    def test_traversal_mismatched_system_and_invalid_metadata_are_rejected(self):
        value=dict(protocol=1,system='NES',name='Fixture.nes',size=4,sha256=hashlib.sha256(b'test').hexdigest())
        for key,bad in [('name','../outside.nes'),('name','NES/inside.nes'),('name','..\\outside.nes'),('name','.hidden.nes'),('system','SNES'),('system',[]),('size',True),('sha256','x'*64)]:
            changed=dict(value);changed[key]=bad
            with self.subTest(key=key,bad=bad),self.assertRaises(ValueError):rom.metadata(changed)
    def test_archive_does_not_extract_unexpected_members(self):
        bundle=self.bundle()
        with zipfile.ZipFile(bundle,'a') as output:output.writestr('../outside.nes',b'bad')
        with self.assertRaises(ValueError):rom.receive(bundle,self.root)
        self.assertFalse((self.root/'outside.nes').exists())
    def test_peer_cannot_redirect_transfer_to_another_host(self):
        value=dict(app='FlXtR',protocol=1,roles=['rom-receiver'],id='device',name='TV',endpoint='http://100.70.1.2:43927/'+'a'*32+'/')
        self.assertEqual(rom.discovered(value,'100.70.1.2',True)['id'],'device')
        with self.assertRaises(ValueError):rom.discovered(value,'100.70.1.3',True)
        value['roles']=['sender'];self.assertIsNone(rom.discovered(value,'100.70.1.2',True))

if __name__=='__main__':unittest.main()
