"""Real UDP/event-loop tests with a compile-time UNIX datagram CAN surrogate.
No physical CAN, ARM64 execution, or kernel ioctl recovery is simulated here.
"""
import csv
import importlib.util
import json
import os
from pathlib import Path
import select
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests/legacy_pc"))
from igw_client import Client


def frame(seq, word=None, ident=0x181, dlc=8):
    return struct.pack("=IB3x8s", ident, dlc,
                       struct.pack("<II", seq, (0x52535000 ^ seq) if word is None else word))


class GatewayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="igw_udp_tests_")
        cls.binary = str(Path(cls.tmp.name) / "gateway_test")
        subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-DIGW_TEST_TRANSPORT", str(ROOT / "src/igw_can_udp.c"),
                        "-o", cls.binary], check=True)
        subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "src/igw_can_udp.c"), "-o",
                        str(Path(cls.tmp.name) / "gateway_production")], check=True)
        # Model the actual SDK omission while compiling the production CAN path.
        headers = Path(cls.tmp.name) / "old_headers"
        (headers / "net").mkdir(parents=True)
        (headers / "net/if_arp.h").write_text(
            "#include_next <net/if_arp.h>\n#undef ARPHRD_CAN\n")
        subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-Wmissing-field-initializers", "-I", str(headers),
                        "-static", str(ROOT / "src/igw_can_udp.c"), "-o",
                        str(Path(cls.tmp.name) / "gateway_legacy_header")], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.device, app = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.device.settimeout(1)
        reserve = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        reserve.bind(("127.0.0.1", 0))
        self.port = reserve.getsockname()[1]
        reserve.close()
        self.proc = subprocess.Popen([self.binary, "--bind", "127.0.0.1",
            "--peer", "127.0.0.1", "--port", str(self.port), "--timeout-ms", "80",
            "--test-can-fd", str(app.fileno())], pass_fds=(app.fileno(),),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        app.close()
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(0.04)
        for _ in range(30):
            self.sock.sendto(b"IGW1 STATS", ("127.0.0.1", self.port))
            try:
                self.sock.recv(4096)
                break
            except socket.timeout:
                self.assertIsNone(self.proc.poll())
        else:
            self.fail("gateway did not become ready")
        self.sock.connect(("127.0.0.1", self.port))
        # Discard only startup STATS replies before the test command stream.
        self.sock.settimeout(.01)
        try:
            while True:self.sock.recv(4096)
        except socket.timeout:pass
        self.sock.settimeout(1)

    def tearDown(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
                self.fail("gateway failed to stop")
        self.sock.close()
        self.device.close()

    def request(self, seq):
        self.sock.send(f"IGW1 READ {seq}".encode())
        raw = self.device.recv(64)
        ident, dlc, data = struct.unpack("=IB3x8s", raw)
        self.assertEqual((ident, dlc), (0x201, 8))
        self.assertEqual(struct.unpack("<II", data), (seq, 0x43414e00 ^ seq))

    def receive(self):
        return json.loads(self.sock.recv(4096))

    def stats(self):
        self.sock.send(b"IGW1 STATS")
        return self.receive()["stats"]

    def test_uint32_payload_and_success(self):
        for seq in (0, 42, 256, 0x89ABCDEF, 0xFFFFFFFF):
            self.request(seq)
            self.device.send(frame(seq))
            result = self.receive()
            self.assertEqual(result["status"], "ok")
            self.assertEqual(result["seq"], seq)
            self.assertEqual(result["data"], frame(seq)[8:].hex().upper())
        self.assertEqual(self.stats()["ok"], 5)

    def test_busy_duplicates_and_cached_result(self):
        self.request(1234)
        self.sock.send(b"IGW1 READ 1234")
        self.sock.send(b"IGW1 READ 5678")
        self.assertEqual(self.receive()["status"], "busy")
        self.device.send(frame(1234))
        self.assertEqual(self.receive()["status"], "ok")
        self.sock.send(b"IGW1 READ 1234")
        self.assertEqual(self.receive()["status"], "ok")
        stats = self.stats()
        self.assertEqual((stats["attempts"], stats["busy"], stats["duplicate"]), (1, 1, 2))
        self.assertFalse(select.select([self.device], [], [], 0.02)[0])

    def test_invalid_requests_do_not_transmit(self):
        for raw in (b"IGW1 READ -1", b"IGW1 READ +1", b"IGW1 READ 4294967296",
                    b"IGW1 READ 1x", b"IGW1 READ 1\x00junk", b"X"*200):
            self.sock.send(raw)
            self.assertEqual(self.receive()["status"], "bad_request")
        self.assertEqual(self.stats()["attempts"], 0)
        self.assertFalse(select.select([self.device], [], [], 0.02)[0])

    def test_bad_frame_and_late_sequence_not_success(self):
        self.request(90)
        self.device.send(frame(89))
        self.device.send(frame(90, ident=0x201))
        self.device.send(frame(90, dlc=7))
        self.device.send(frame(90, word=0))
        self.device.send(frame(90))
        result = self.receive()
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["stats"]["mismatch"], 1)
        self.assertEqual(result["stats"]["ignored"], 3)

    def test_timeout_cooldown_and_delayed_reply(self):
        start = time.monotonic()
        self.request(99)
        result = self.receive()
        self.assertEqual(result["status"], "timeout")
        self.assertLess(time.monotonic()-start, 0.6)
        self.assertEqual(result["stats"]["resets"], 1)
        self.sock.send(b"IGW1 READ 100")
        self.assertEqual(self.receive()["status"], "cooldown")
        time.sleep(2.03)
        self.request(101)
        self.device.send(frame(99))
        self.device.send(frame(101))
        result = self.receive()
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["stats"]["ok"], 1)
        self.assertEqual(result["stats"]["ignored"], 1)

    def test_wrong_payload_times_out_as_bad_reply(self):
        self.request(15)
        self.device.send(frame(15, word=123))
        result = self.receive()
        self.assertEqual(result["status"], "bad_reply")
        self.assertEqual(result["stats"]["ok"], 0)
        self.assertEqual(result["stats"]["timeout"], 1)

    def test_unauthorized_source_ignored(self):
        other = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            other.bind(("127.0.0.2", 0))
            other.settimeout(0.05)
            other.sendto(b"IGW1 READ 42", ("127.0.0.1", self.port))
            with self.assertRaises(socket.timeout):
                other.recv(4096)
            self.assertEqual(self.stats()["attempts"], 0)
        finally:
            other.close()

    def test_cli_real_udp_csv_and_uint32_wrap(self):
        errors = []
        def device_worker():
            try:
                for seq in (0xFFFFFFFE, 0xFFFFFFFF, 0, 1):
                    raw = self.device.recv(64)
                    self.assertEqual(struct.unpack("<I", raw[8:12])[0], seq)
                    self.device.send(frame(seq))
            except BaseException as exc:
                errors.append(exc)
        worker = threading.Thread(target=device_worker)
        worker.start()
        csv_path = Path(self.tmp.name) / "cli.csv"
        result = subprocess.run([sys.executable, str(ROOT / "tests/legacy_pc/igw_can_test.py"),
            "--host", "127.0.0.1", "--port", str(self.port), "--count", "4",
            "--interval", "0", "--start-seq", "0xfffffffe", "--csv", str(csv_path)],
            capture_output=True, text=True, timeout=4)
        worker.join(timeout=2)
        self.assertFalse(worker.is_alive())
        self.assertFalse(errors, errors)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        with csv_path.open(encoding="utf-8-sig", newline="") as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual([r["status"] for r in rows], ["ok"]*4)

    def test_shutdown_with_pending_request(self):
        self.request(27)
        self.proc.send_signal(signal.SIGTERM)
        self.assertEqual(self.proc.wait(timeout=2), 0)

    def test_100_request_business_chain(self):
        errors = []
        start_seq = 0x80ABC000
        def device_worker():
            try:
                for seq in range(start_seq, start_seq+100):
                    raw = self.device.recv(64)
                    self.assertEqual(struct.unpack("<II", raw[8:]), (seq, 0x43414e00 ^ seq))
                    self.device.send(frame(seq))
            except BaseException as exc:
                errors.append(exc)
        worker = threading.Thread(target=device_worker)
        worker.start()
        client = Client("127.0.0.1", self.port)
        try:
            for seq in range(start_seq, start_seq+100):
                self.assertEqual(client.read(seq)["status"], "ok")
        finally:
            client.close()
            worker.join(timeout=2)
        self.assertFalse(worker.is_alive())
        self.assertFalse(errors, errors)
        stats = self.stats()
        self.assertEqual((stats["attempts"],stats["ok"],stats["timeout"],stats["mismatch"]), (100,100,0,0))


if __name__ == "__main__":
    unittest.main(verbosity=2)
