"""Real UDP/process tests with compile-time register RPC emulation.
Not an AXI/ARM/FPGA/transceiver/timing measurement.
"""
from pathlib import Path
import json
import select
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests/legacy_pc'))
from dual_client import Client, crc16


def response(prefix=b'\x01\x03\x08\x12\x34\x00\x01\x00\x01\x01\x01'):
    return prefix+crc16(prefix).to_bytes(2, 'little')


class Device:
    def __init__(self, sock, mode='ok', identity_ok=True):
        self.sock, self.mode = sock, mode
        self.r = {0: 0x32574749, 4: 0x20000, 8: 0x1f, 0x18: 50000000,
                  0x20: 5208, 0x24: 1, 0x28: 85932, 0x2c: 200508, 0x1c: 64}
        if not identity_ok:
            self.r[0x18] = 75000000
        self.writes = []
        self.kicks = 0
        self.sent = None
        self.completed = self.replied = False
        self.worker = threading.Thread(target=self.run)
        self.worker.start()

    def run(self):
        try:
            while True:
                msg = self.sock.recv(12)
                if not msg:
                    return
                op, off, val = struct.unpack('=III', msg)
                now = time.monotonic()
                if self.sent is not None:
                    if now-self.sent > .001 and not self.completed and self.mode != 'tx_stall':
                        self.completed = True
                        self.r[0x60] = (self.r.get(0x60, 0)+1) & 0xffffffff
                        self.r[0x1c] = 64
                    if now-self.sent > .004 and self.completed and not self.replied and self.mode != 'timeout':
                        self.replied = True
                        data = response()
                        if self.mode == 'bad_crc':
                            data = data[:-1]+bytes([data[-1]^1])
                        elif self.mode == 'wrong_unit':
                            data = response(b'\x02'+data[1:-2])
                        elif self.mode == 'exception':
                            data = response(b'\x01\x83\x02')
                        elif self.mode == 'bad_length':
                            data = b'\x01\x03\x00\x00'
                        self.r[0x40] = len(data)
                        padded = data+b'\x00'*((-len(data)) % 4)
                        for i in range(0, len(padded), 4):
                            self.r[0x100+i] = int.from_bytes(padded[i:i+4], 'little')
                        self.r[0x50] = self.r.get(0x50, 0)+1
                        self.r[0x1c] |= 1
                        if self.mode == 'uart_error':
                            self.r[0x58] = self.r.get(0x58, 0)+1
                if op:
                    self.writes.append((off, val))
                    if off == 0x38:
                        self.r[0x1c] &= ~1
                    else:
                        self.r[off] = val
                    if off == 0x34:
                        self.kicks += 1
                        self.sent = now
                        self.completed = self.replied = False
                        self.r[0x1c] = 2
                self.sock.send(struct.pack('=I', self.r.get(off, 0)))
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass

    def close(self):
        self.worker.join(2)
        self.sock.close()
        if self.worker.is_alive():
            raise AssertionError('MMIO emulator still running')


class GatewayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.binary = str(Path(cls.tmp.name)/'rs_test')
        for suffix, options in [('rs_test', ['-DIGW_RS485_TEST']), ('rs_production', [])]:
            subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Wmissing-field-initializers', '-Werror',
                            *options, str(ROOT/'src/igw_rs485_rw.c'), '-o',
                            str(Path(cls.tmp.name)/suffix)], check=True)
        cls.can_binary = str(Path(cls.tmp.name)/'can_test')
        subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-DIGW_TEST_TRANSPORT', str(ROOT/'src/igw_can_udp.c'),
                        '-o', cls.can_binary], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def launch(self, mode='ok', identity_ok=True):
        device_sock, app = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.device = Device(device_sock, mode, identity_ok)
        reserve = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        reserve.bind(('127.0.0.1', 0))
        self.port = reserve.getsockname()[1]
        reserve.close()
        self.lock = str(Path(self.tmp.name)/f'lock_{self.port}')
        self.proc = subprocess.Popen([self.binary, '--bind', '127.0.0.1', '--peer', '127.0.0.1',
            '--port', str(self.port), '--test-fd', str(app.fileno()), '--test-lock', self.lock],
            pass_fds=(app.fileno(),), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        app.close()
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.connect(('127.0.0.1', self.port))
        self.sock.settimeout(.03)
        if not identity_ok:
            self.assertEqual(self.proc.wait(2), 2)
            return
        for _ in range(40):
            try:
                self.sock.send(b'IGW1 STATS')
                self.sock.recv(4096)
                break
            except (socket.timeout, ConnectionRefusedError):
                self.assertIsNone(self.proc.poll())
                time.sleep(.005)
        else:
            self.fail('RS485 gateway not ready')
        self.sock.settimeout(1)

    def tearDown(self):
        if hasattr(self, 'proc'):
            if self.proc.poll() is None:
                self.proc.terminate()
                self.assertEqual(self.proc.wait(2), 0)
            self.sock.close()
            self.device.close()
            self.assertFalse(Path(self.lock).exists())

    def request(self, seq):
        self.sock.send(f'IGW1 RS485 {seq}'.encode())
        return json.loads(self.sock.recv(4096))

    def test_100_reads_and_duplicate_cache(self):
        self.launch()
        client = Client('RS485', '127.0.0.1', self.port)
        try:
            for seq in range(100):
                r = client.read(seq)
                self.assertEqual(r['status'], 'ok')
                self.assertEqual(r['registers'], [4660, 1, 1, 257])
                self.assertEqual(client.read(seq)['status'], 'ok')
            self.assertEqual(self.device.kicks, 100)
        finally:
            client.close()
        self.assertEqual(self.device.r.get(0xc), 0)
        self.assertEqual(self.device.r[0x200], 0x301)
        self.assertEqual(self.device.r[0x204], 0x09440400)

    def test_timeout_recovers_without_process_restart(self):
        self.launch('timeout')
        self.assertEqual(self.request(1)['status'], 'timeout')
        self.assertEqual(self.device.r[0xc], 0)
        self.assertEqual(self.request(2)['status'], 'cooldown')
        time.sleep(2.02)
        # A disconnected peer did not queue a reply to release on reconnection.
        # Mark that OLD simulated TX consumed before enabling replies to NEW TX.
        self.device.replied = True
        self.device.mode = 'ok'
        r = self.request(3)
        self.assertEqual(r['status'], 'ok')
        self.assertEqual((r['stats']['attempts'], r['stats']['ok'], r['stats']['timeout']), (2, 1, 1))

    def test_preflight_wrong_clock_has_no_writes(self):
        self.launch(identity_ok=False)
        self.assertEqual(self.device.writes, [])

    def test_tx_stall_shutdown_disarms(self):
        self.launch('tx_stall')
        self.assertEqual(self.request(1)['status'], 'tx_timeout')
        self.assertEqual(self.device.r[0xc], 0)

    def test_signal_during_transmission_disarms(self):
        self.launch('tx_stall')
        self.sock.send(b'IGW1 RS485 1')
        deadline = time.monotonic()+1
        while self.device.kicks == 0 and time.monotonic() < deadline:
            time.sleep(.001)
        self.assertEqual(self.device.kicks, 1)
        self.proc.terminate()
        self.assertEqual(self.proc.wait(2), 0)
        self.assertEqual(self.device.r[0xc], 0)

    def test_malformed_requests_do_not_transmit(self):
        self.launch()
        for raw in (b'IGW1 RS485 -1', b'IGW1 RS485 +1', b'IGW1 RS485 4294967296',
                    b'IGW1 RS485 1\0x', b'X'*200):
            self.sock.send(raw)
            self.assertEqual(json.loads(self.sock.recv(4096))['status'], 'bad_request')
        self.assertEqual(self.device.kicks, 0)

    def test_busy_other_request_and_pending_duplicate(self):
        self.launch('timeout')
        self.sock.send(b'IGW1 RS485 1')
        self.sock.send(b'IGW1 RS485 1')
        self.sock.send(b'IGW1 RS485 2')
        self.assertEqual(json.loads(self.sock.recv(4096))['status'], 'busy')
        self.assertEqual(json.loads(self.sock.recv(4096))['status'], 'timeout')
        self.assertEqual(self.device.kicks, 1)

    def test_fault_replies(self):
        # Each scenario has a fresh process/lock; no simulated physical errors.
        for mode, expected in [('bad_crc', 'bad_reply'), ('wrong_unit', 'bad_reply'),
                               ('exception', 'modbus_exception'), ('bad_length', 'bad_length'),
                               ('uart_error', 'pl_error')]:
            with self.subTest(mode=mode):
                self.launch(mode)
                self.assertEqual(self.request(1)['status'], expected)
                self.assertEqual(self.device.r[0xc], 0)
                self.tearDown()
                del self.proc

    def test_unauthorized_source_no_tx(self):
        self.launch()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as other:
            other.bind(('127.0.0.2', 0))
            other.settimeout(.05)
            other.sendto(b'IGW1 RS485 1', ('127.0.0.1', self.port))
            with self.assertRaises(socket.timeout):
                other.recv(4096)
        self.assertEqual(self.device.kicks, 0)

    def test_lock_rejects_second_process_without_writes(self):
        self.launch()
        result = subprocess.run([self.binary, '--test-lock', self.lock], capture_output=True, timeout=1)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(self.device.kicks, 0)
        self.assertTrue(Path(self.lock).exists())

    def test_can_success_during_rs485_timeout(self):
        self.launch('timeout')
        device, app = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        device.settimeout(1)
        reserve = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        reserve.bind(('127.0.0.1', 0))
        port = reserve.getsockname()[1]
        reserve.close()
        can = subprocess.Popen([self.can_binary, '--bind', '127.0.0.1', '--peer', '127.0.0.1',
            '--port', str(port), '--test-can-fd', str(app.fileno())], pass_fds=(app.fileno(),),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        app.close()
        time.sleep(.03)
        can_client = Client('CAN', '127.0.0.1', port)
        rs_client = Client('RS485', '127.0.0.1', self.port)
        def peer():
            raw = device.recv(64)
            seq = struct.unpack('<I', raw[8:12])[0]
            device.send(struct.pack('=IB3x8s', 0x181, 8, struct.pack('<II', seq, 0x52535000^seq)))
        worker = threading.Thread(target=peer)
        worker.start()
        try:
            with ThreadPoolExecutor(max_workers=2) as pool:
                slow = pool.submit(rs_client.read, 7)
                fast = pool.submit(can_client.read, 7)
                self.assertEqual(fast.result(timeout=.2)['status'], 'ok')
                self.assertFalse(slow.done())
                self.assertEqual(slow.result(timeout=1)['status'], 'timeout')
        finally:
            can_client.close()
            rs_client.close()
            can.terminate()
            can.wait(2)
            worker.join(2)
            device.close()


if __name__ == '__main__':
    unittest.main(verbosity=2)
