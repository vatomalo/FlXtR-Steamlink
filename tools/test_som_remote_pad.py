"""Player 2 relay: one owner, packets in the core's format, no Select."""
import socket
import struct
import unittest

from som_remote_pad import OWNER_S, Relay


class Clock:
    now = 0.0

    def __call__(self):
        return self.now


class RemotePadTest(unittest.TestCase):
    def setUp(self):
        self.rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rx.bind(("127.0.0.1", 0))
        self.rx.settimeout(1)
        self.clock = Clock()
        self.relay = Relay(self.rx.getsockname(), self.clock)

    def tearDown(self):
        self.rx.close()
        self.relay.sock.close()

    def test_packet_and_ownership(self):
        self.assertTrue(self.relay.press("a", (1 << 0) | (1 << 2) | (1 << 9)))
        data = self.rx.recv(16)
        self.assertEqual(data[:4], b"S2\x01\x00")
        buttons, seq = struct.unpack("<HH", data[4:])
        self.assertEqual(buttons, (1 << 0) | (1 << 9))          # Select dropped
        self.assertEqual(seq, 1)
        self.assertFalse(self.relay.press("b", 1))              # taken
        self.clock.now = OWNER_S + 1
        self.assertTrue(self.relay.press("b", 1))               # owner went quiet
        self.assertEqual(struct.unpack("<HH", self.rx.recv(16)[4:])[1], 2)


if __name__ == "__main__":
    unittest.main()
