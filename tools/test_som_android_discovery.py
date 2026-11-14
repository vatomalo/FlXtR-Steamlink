"""Android SoM bridge discovery tests; no Tailscale node required."""
import json
import unittest

from som_android_discovery import discover, endpoint_from_reply, peer_hosts


class Reply:
    def __init__(self, data):
        self.data = data
    def __enter__(self):
        return self
    def __exit__(self, *unused):
        pass
    def read(self, limit=-1):
        return self.data[:limit] if limit >= 0 else self.data


class TestAndroidDiscovery(unittest.TestCase):
    endpoint = "http://100.70.1.2:43927/" + "a" * 32 + "/som"

    def test_filters_online_tailnet_ipv4_peers(self):
        status = {"Peer": {"a": {"Online": True, "TailscaleIPs": ["100.70.1.2"]},
                           "b": {"Online": False, "TailscaleIPs": ["100.70.1.3"]},
                           "c": {"Online": True, "TailscaleIPs": ["192.168.1.2"]}}}
        self.assertEqual(peer_hosts(status), ["100.70.1.2"])

    def test_requires_matching_private_endpoint(self):
        good = {"app": "FlXtR", "protocol": 1, "role": "som-ai", "endpoint": self.endpoint}
        self.assertEqual(endpoint_from_reply("100.70.1.2", good), self.endpoint)
        for endpoint in (self.endpoint.replace("100.70.1.2", "100.70.1.3"),
                         self.endpoint.replace("http://", "https://"),
                         "http://100.70.1.2:43927/bad/som"):
            self.assertIsNone(endpoint_from_reply("100.70.1.2", {**good, "endpoint": endpoint}))

    def test_discovers_approved_android_peer(self):
        status = {"Peer": {"android": {"Online": True, "TailscaleIPs": ["100.70.1.2"]}}}
        payload = json.dumps({"app": "FlXtR", "protocol": 1, "role": "som-ai",
                              "endpoint": self.endpoint}).encode()
        self.assertEqual(discover(status, lambda request, timeout: Reply(payload)), self.endpoint)


if __name__ == "__main__":
    unittest.main()
