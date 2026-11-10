import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('usb_setup',ROOT/'tools/usb_setup.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class UsbSafety(unittest.TestCase):
    def setUp(self):
        self.root=Path(tempfile.mkdtemp(prefix='flxtr-usb-test-'))
        self.app=self.root/'greenlink';self.app.mkdir()
        self.disk=dict(device='/dev/block/sdb',sysfs='/sys/devices/usb1/port2/block/sdb',dev='8:16',size=32*m.GIB,
                       sector=512,model='Identical model',serial='SERIAL-TWO',port='/sys/devices/usb1/port2',
                       generation='7',vendor='0781',product='5591',head_hash=hashlib.sha256(bytes(65536)).hexdigest(),token='token-two',blocked=False,
                       partitions=[dict(device='/dev/block/sdb1',dev='8:17',type='vfat',uuid='ABCD-1234',size=24*m.GIB,mounts=[]),
                                   dict(device='/dev/block/sdb2',dev='8:18',type='ext3',uuid='11111111-2222-3333-4444-555555555555',size=8*m.GIB,mounts=[])])
    def plan(self,action='restore'):
        with patch.object(m,'inventory',return_value=[self.disk]), patch.object(m.os,'access',return_value=True),patch.object(m,'source_bytes',return_value=1):
            return m.create_plan(self.disk['device'],action,self.app)
    def test_layout_and_partition_table(self):
        shape=m.layout(32*m.GIB);self.assertEqual(shape['apps_sectors']*512,8*m.GIB)
        image=m.mbr(shape);self.assertEqual(image[-2:],b'\x55\xaa')
        entries=[struct.unpack('<B3sB3sII',image[o:o+16]) for o in (446,462)]
        self.assertEqual([x[2] for x in entries],[0x0c,0x83]);self.assertEqual(entries[0][4]+entries[0][5],entries[1][4])
        for start in [entries[0][4],entries[1][4]]:self.assertEqual(start%2048,0)
        self.assertLess(entries[1][4]+entries[1][5],32*m.GIB//512)
    def test_too_small_and_too_large_rejected(self):
        for size in (2*m.GIB,2*1024*m.GIB):
            with self.assertRaises(ValueError):m.layout(size)
        with self.assertRaises(ValueError):m.layout(8*m.GIB,8)
    def test_running_drive_cannot_be_erased(self):
        self.disk['blocked']=True
        with self.assertRaises(ValueError):self.plan('prepare')
        self.assertEqual(self.plan()['action'],'restore')
    def test_multiple_drives_select_exact_one(self):
        other=copy.deepcopy(self.disk);other.update(device='/dev/block/sda',serial='SERIAL-ONE',token='one',blocked=True)
        with patch.object(m,'inventory',return_value=[other,self.disk]),patch.object(m.os,'access',return_value=True),patch.object(m,'source_bytes',return_value=1):
            value=m.create_plan('/dev/block/sdb','prepare',self.app)
        self.assertEqual(value['disk']['serial'],'SERIAL-TWO');self.assertFalse(value['activate'])
    def test_expired_and_consumed_confirmation(self):
        for field,value in [('created_monotonic',time.monotonic()-301),('state','executing'),('boot_id','other-boot')]:
            plan=self.plan();plan[field]=value
            with patch.object(m,'inventory',return_value=[self.disk]),self.assertRaises(ValueError):m.revalidate(plan)
    def test_replug_or_edited_identity_rejected(self):
        plan=self.plan();changed=copy.deepcopy(self.disk);changed['generation']='8'
        with patch.object(m,'inventory',return_value=[changed]),self.assertRaises(ValueError):m.revalidate(plan)
        plan=self.plan();plan['disk']=copy.deepcopy(plan['disk']);plan['disk']['size']=64*m.GIB
        with patch.object(m,'inventory',return_value=[self.disk]),self.assertRaises(ValueError):m.revalidate(plan)
    def test_wrong_partition_identity_rejected(self):
        plan=self.plan();plan['data']=dict(plan['data'],uuid='FFFF-0000')
        with patch.object(m,'inventory',return_value=[self.disk]),self.assertRaises(ValueError):m.revalidate(plan)
    def test_layout_tampering_rejected(self):
        plan=self.plan('prepare');plan['layout']['data_start']=0
        with patch.object(m,'inventory',return_value=[self.disk]),self.assertRaises(ValueError):m.revalidate(plan)
    def test_non_steamlink_execution_is_blocked(self):
        if not Path('/dev/block/mtdblock9').exists():
            with self.assertRaises(ValueError):m.platform_guard()
    def test_wrong_confirmation_never_formats(self):
        plan=self.plan('prepare');path=self.root/'plan.json';path.write_text(json.dumps(plan))
        with patch.object(m,'platform_guard'),patch.object(m,'format_drive') as formatter,self.assertRaises(ValueError):m.execute(path,'ERASE-SOMETHING-ELSE')
        formatter.assert_not_called()
    def test_raw_superblock_detection(self):
        file=self.root/'image';raw=bytearray(4096);raw[82:90]=b'FAT32   ';struct.pack_into('<I',raw,67,0xACDDE787);file.write_bytes(raw)
        self.assertEqual(m.filesystem(file),{'type':'vfat','uuid':'ACDD-E787'})
        raw=bytearray(4096);raw[1080:1082]=b'\x53\xef';struct.pack_into('<I',raw,1116,4);raw[1128:1144]=bytes.fromhex('11111111222233334444555555555555');file.write_bytes(raw)
        self.assertEqual(m.filesystem(file)['type'],'ext3')
    def test_seed_preserves_symlinks_and_originals(self):
        source=self.root/'source';source.mkdir();(source/'save.srm').write_bytes(b'keep-save');(source/'linked.srm').symlink_to('save.srm')
        target=self.root/'copy';m.seed_tree(source,target)
        self.assertEqual((source/'save.srm').read_bytes(),b'keep-save');self.assertTrue((target/'linked.srm').is_symlink())
        self.assertEqual(os.readlink(target/'linked.srm'),'save.srm')
    def test_existing_hook_not_overwritten(self):
        hook=self.root/'hook';hook.write_text('user startup script')
        with patch.object(m,'HOOK',hook),patch.object(m,'STATE',self.root/'state'),self.assertRaises(ValueError):
            m.configure(self.disk['partitions'][0],self.disk['partitions'][1],'greenlink')
        self.assertEqual(hook.read_text(),'user startup script');self.assertFalse((self.root/'state').exists())
    def test_symbolic_link_conflicts_are_not_overwritten(self):
        template=m.TEMPLATE.read_text();start=template.index('safe_link()');end=template.index('mkdir -p /mnt/flxtr-usb/by-uuid',start)
        function=template[start:end]
        original=self.root/'valuable';original.write_text('keep')
        script='fail() { exit 17; };\n'+function+'\nsafe_link target '+str(original)+'\n'
        result=subprocess.run(['/bin/sh','-c',script]);self.assertEqual(result.returncode,17);self.assertEqual(original.read_text(),'keep')
        link=self.root/'link';script='fail() { exit 17; };\n'+function+'\nsafe_link target '+str(link)+'\nsafe_link target '+str(link)+'\n'
        self.assertEqual(subprocess.run(['/bin/sh','-c',script]).returncode,0);self.assertEqual(os.readlink(link),'target')
    def test_hidden_mounts_are_not_bound(self):
        fake=[dict(source='/dev/block/sdb1',root='/',path=str(self.root),dev='8:17')]
        with patch.object(m,'mount_records',return_value=fake):self.assertIsNone(m.mounted_root('/dev/block/sdb1'))

    def test_critical_mount_never_opens_disk(self):
        plan=self.plan('prepare')
        with patch.object(m,'mount_records',return_value=[dict(dev='8:17',path='/home/apps')]),patch.object(m.os,'open') as opened,self.assertRaises(ValueError):
            m.format_drive(plan)
        opened.assert_not_called()
    def test_busy_unmount_never_opens_disk(self):
        plan=self.plan('prepare')
        with patch.object(m,'mount_records',return_value=[dict(dev='8:17',path='/mnt/spare')]),patch.object(m,'run',side_effect=subprocess.CalledProcessError(1,'umount')),patch.object(m.os,'open') as opened,self.assertRaises(subprocess.CalledProcessError):
            m.format_drive(plan)
        opened.assert_not_called()
    def test_short_write_stops_before_formatting(self):
        from types import SimpleNamespace
        plan=self.plan('prepare')
        with patch.object(m,'mount_records',return_value=[]),patch.object(m,'revalidate'),patch.object(m.os,'open',return_value=91) as opened,patch.object(m.os,'fstat',return_value=SimpleNamespace(st_mode=0o060000,st_rdev=os.makedev(8,16))),patch.object(m.os,'pread',return_value=bytes(65536)),patch.object(m.os,'pwrite',return_value=0),patch.object(m.os,'close') as closed,patch.object(m,'run') as commands,self.assertRaises(OSError):
            m.format_drive(plan)
        self.assertEqual(opened.call_args.args[0],'/dev/block/sdb')
        self.assertTrue(opened.call_args.args[1]&os.O_EXCL)
        commands.assert_not_called();closed.assert_called_once_with(91)
    def test_failed_apply_never_commits_startup_hook(self):
        hook=self.root/'hook';state=self.root/'state';state.mkdir()
        old=state/'mounts.sh';old.write_text('old working config')
        def command(args):
            if args[-1]=='--apply':raise subprocess.CalledProcessError(1,args)
        with patch.object(m,'HOOK',hook),patch.object(m,'STATE',state),patch.object(m,'run',side_effect=command),self.assertRaises(subprocess.CalledProcessError):
            m.configure(self.disk['partitions'][0],self.disk['partitions'][1],'greenlink')
        self.assertEqual(old.read_text(),'old working config');self.assertFalse(hook.exists())

    def test_formatter_flow_uses_only_selected_disk_image(self):
        # All block access is redirected into a fresh sparse regular-file image.
        # No physical device or real mkfs/umount/ioctl command is used.
        import builtins
        from types import SimpleNamespace
        plan=self.plan('prepare');image=self.root/'disk-image'
        with image.open('wb') as stream:stream.truncate(self.disk['size'])
        fd=os.open(image,os.O_RDWR);real_open=builtins.open;real_read=Path.read_text
        def reader(path,*args,**kwargs):
            return real_open(image if str(path)==self.disk['device'] else path,*args,**kwargs)
        def read_text(path,*args,**kwargs):
            name=str(path)
            for number,prefix in ((1,'data'),(2,'apps')):
                for suffix,key in (('start','_start'),('size','_sectors')):
                    if name=='/sys/class/block/sdb%d/%s'%(number,suffix):return str(plan['layout'][prefix+key])
            return real_read(path,*args,**kwargs)
        def fs(path):return self.disk['partitions'][0 if str(path).endswith('1') else 1]
        with patch.object(m,'mount_records',return_value=[]),patch.object(m,'revalidate'),patch.object(m.os,'open',return_value=fd) as opened,patch.object(m.os,'fstat',return_value=SimpleNamespace(st_mode=0o060000,st_rdev=os.makedev(8,16))),patch.object(m.fcntl,'ioctl') as ioctl,patch.object(m,'inventory',return_value=[self.disk]),patch.object(Path,'exists',return_value=True),patch.object(Path,'read_text',read_text),patch.object(builtins,'open',reader),patch.object(m,'filesystem',side_effect=fs),patch.object(m,'run') as commands:
            data,system=m.format_drive(plan)
        opened.assert_called_once_with('/dev/block/sdb',os.O_RDWR|os.O_EXCL)
        ioctl.assert_called_once_with(fd,0x125f)
        self.assertEqual([c.args[0][-1] for c in commands.call_args_list],['/dev/block/sdb1','/dev/block/sdb2'])
        with image.open('rb') as stream:
            head=stream.read(512);self.assertEqual(head[510:512],b'\x55\xaa')
            self.assertEqual(struct.unpack_from('<I',head,454)[0],2048)
            stream.seek(self.disk['size']-1024);self.assertEqual(stream.read(),bytes(1024))

if __name__=='__main__':unittest.main(verbosity=2)