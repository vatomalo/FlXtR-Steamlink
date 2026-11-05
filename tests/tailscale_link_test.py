import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import Mock,patch

spec=importlib.util.spec_from_file_location('tailscale_link',Path(__file__).resolve().parents[1]/'tools/tailscale_link.py')
link=importlib.util.module_from_spec(spec);spec.loader.exec_module(link)

class LinkTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name);link.ROOT=self.root
        (self.root/'tailscale').mkdir();(self.root/'handoff').mkdir()
        (self.root/'tailscale/enabled').touch()
    def tearDown(self):self.temp.cleanup()
    def supervisor(self):
        service=link.Supervisor();service.daemon=Mock();service.daemon.poll.return_value=None;return service
    def test_private_forward_uses_receiver_port_and_publishes_ready_only_after_success(self):
        token='a'*32;(self.root/'handoff/url.txt').write_text('http://192.168.1.2:12345/'+token+'/')
        service=self.supervisor()
        with patch.object(link,'cli',side_effect=[(0,json.dumps(dict(BackendState='Running',TailscaleIPs=['100.70.1.2']))),(0,'')]) as cli:
            service.tick()
            self.assertEqual(cli.call_args_list[1].args,('serve','--bg','--yes','--tcp=43927','tcp://127.0.0.1:12345'))
        self.assertEqual((self.root/'handoff/remote-url.txt').read_text().strip(),'http://100.70.1.2:43927/'+token+'/')
        self.assertTrue((self.root/'tailscale/ready').exists())
        self.assertFalse((self.root/'tailscale/auth-url.txt').exists())
    def test_login_link_is_private_and_not_in_status(self):
        service=self.supervisor();url='https://login.tailscale.com/a/test-device'
        with patch.object(link,'cli',side_effect=[(1,json.dumps(dict(BackendState='NeedsLogin',AuthURL=url))),(1,json.dumps(dict(AuthURL=url)))]):service.tick()
        self.assertEqual((self.root/'tailscale/auth-url.txt').read_text().strip(),url)
        self.assertNotIn(url,(self.root/'tailscale/status.txt').read_text())
        self.assertFalse((self.root/'tailscale/ready').exists())
        self.assertEqual((self.root/'tailscale/auth-url.txt').stat().st_mode&0o777,0o600)
    def test_failed_forward_never_advertises_remote_access(self):
        (self.root/'handoff/url.txt').write_text('http://192.168.1.2:12345/'+'a'*32+'/')
        service=self.supervisor()
        with patch.object(link,'cli',side_effect=[(0,json.dumps(dict(BackendState='Running',TailscaleIPs=['100.70.1.2']))),(1,'')]):
            with self.assertRaises(ValueError):service.tick()
        self.assertFalse((self.root/'tailscale/ready').exists())
    def test_pending_login_link_is_kept_without_restarting_login(self):
        service=self.supervisor();url='https://login.tailscale.com/a/pending-device'
        with patch.object(link,'cli',return_value=(1,json.dumps(dict(BackendState='NeedsLogin',AuthURL=url)))) as cli:
            service.tick()
            cli.assert_called_once_with('status','--json')
        self.assertEqual((self.root/'tailscale/auth-url.txt').read_text().strip(),url)
    def test_disconnect_retains_identity_but_closes_remote_receiver(self):
        (self.root/'tailscale/tailscaled.state').write_text('private-identity')
        (self.root/'tailscale/ready').touch();(self.root/'handoff/remote-url.txt').write_text('private-link')
        service=self.supervisor()
        with patch.object(link,'cli',return_value=(0,'')) as cli:service.disconnect();cli.assert_called_once_with('down')
        self.assertFalse((self.root/'tailscale/ready').exists());self.assertFalse((self.root/'tailscale/enabled').exists())
        self.assertFalse((self.root/'handoff/remote-url.txt').exists());self.assertEqual((self.root/'tailscale/tailscaled.state').read_text(),'private-identity')
    def test_auth_host_and_tailnet_address_validation(self):
        self.assertFalse(link.auth_url('https://login.tailscale.com.evil.example/a/key'))
        self.assertFalse(link.auth_url('http://login.tailscale.com/a/key'))
        self.assertFalse(link.auth_url('https://evil@login.tailscale.com/a/key'))
        self.assertTrue(link.tail_ip('100.64.0.1'));self.assertFalse(link.tail_ip('100.128.0.1'));self.assertFalse(link.tail_ip('8.8.8.8'))
        login='https://login.tailscale.com/a/test-device'
        self.assertEqual(link.login_from_output(json.dumps({'AuthURL':login},indent=2)),login)
        self.assertEqual(link.login_from_output('{}\n'+json.dumps({'AuthURL':login})),login)
    def test_sign_out_while_disconnected_forgets_only_private_identity(self):
        (self.root/'tailscale/tailscaled.state').write_text('private-identity')
        other=self.root/'unrelated-state';other.write_text('keep')
        link.Supervisor().disconnect(logout=True)
        self.assertFalse((self.root/'tailscale/tailscaled.state').exists())
        self.assertEqual(other.read_text(),'keep')

if __name__=='__main__':unittest.main()
