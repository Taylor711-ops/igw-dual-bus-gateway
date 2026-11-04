from pathlib import Path
import ctypes
import importlib.util
import json
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('r8_pc', ROOT/'pc/rs485_rw_test.py')
pc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pc)


def adu(prefix):
    return prefix+pc.crc16(prefix).to_bytes(2,'little')


class WirePeer:
    """Register transport mock, invoking the actual new C slave protocol."""
    def __init__(self, sock, bridge, mode='ok'):
        self.sock, self.bridge, self.mode = sock, bridge, mode
        self.r = {0:0x32574749,4:0x20000,8:0x1f,0x18:50000000,
                  0x20:5208,0x24:1,0x28:85932,0x2c:200508,0x1c:64}
        self.kicks = 0
        self.requests = []
        self.sent = None
        self.completed = self.replied = False
        self.worker = threading.Thread(target=self.run)
        self.worker.start()

    def run(self):
        try:
            while True:
                raw = self.sock.recv(12)
                if not raw:
                    return
                op, offset, value = struct.unpack('=III',raw)
                now = time.monotonic()
                if self.sent is not None:
                    if now-self.sent > .001 and not self.completed and self.mode != 'tx_stall':
                        self.completed = True
                        self.r[0x60] = (self.r.get(0x60,0)+1) & 0xffffffff
                        self.r[0x1c] = 64
                    if now-self.sent > .003 and self.completed and not self.replied:
                        self.replied = True
                        req = self.requests[-1]
                        if self.mode == 'echo_only' and req[1] == 6:
                            data = req
                        else:
                            data = self.bridge(req)
                        if self.mode == 'write_drop' and req[1] == 6:
                            data = b''
                        if data and req[1] == 6:
                            if self.mode == 'bad_echo':
                                data = adu(data[:5]+bytes((data[5]^1,)))
                            elif self.mode == 'exception':
                                data = adu(b'\x01\x86\x02')
                            elif self.mode == 'bad_crc':
                                data = data[:-1]+bytes((data[-1]^1,))
                            elif self.mode == 'zero_exception':
                                data = adu(b'\x01\x86\x00')
                            elif self.mode == 'pl_gap':
                                self.r[0x68] = self.r.get(0x68,0)+1
                        if data:
                            self.r[0x40] = len(data)
                            padded = data+b'\0'*((-len(data))%4)
                            for i in range(0,len(padded),4):
                                self.r[0x100+i] = int.from_bytes(padded[i:i+4],'little')
                            self.r[0x50] = self.r.get(0x50,0)+1
                            self.r[0x1c] |= 1
                if op:
                    if offset == 0x38:
                        self.r[0x1c] &= ~1
                    else:
                        self.r[offset] = value
                    if offset == 0x34:
                        self.kicks += 1
                        self.requests.append(b''.join(self.r.get(0x200+i,0).to_bytes(4,'little')
                                                       for i in (0,4)))
                        self.sent = now
                        self.completed = self.replied = False
                        self.r[0x1c] = 2
                self.sock.send(struct.pack('=I',self.r.get(offset,0)))
        except OSError:
            pass

    def close(self):
        self.worker.join(2)
        self.sock.close()
        if self.worker.is_alive():
            raise AssertionError('mock thread did not stop')


class RWTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.binary = str(Path(cls.tmp.name)/'rw_test')
        flags = ['gcc','-std=c11','-O2','-Wall','-Wextra','-Wmissing-field-initializers','-Werror']
        subprocess.run(flags+['-DIGW_RS485_TEST',str(ROOT/'src/igw_rs485_rw.c'),'-o',cls.binary],check=True)
        shared = str(Path(cls.tmp.name)/'slave_protocol.so')
        subprocess.run(flags+['-shared','-fPIC','-I'+str(ROOT/'slave_src'),
                              str(ROOT/'tests/protocol_bridge.c'),'-o',shared],check=True)
        cls.lib = ctypes.CDLL(shared)
        cls.lib.bridge_reply.argtypes = [ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p]
        cls.lib.bridge_get.argtypes = [ctypes.c_uint]
        cls.lib.bridge_set.argtypes = [ctypes.c_uint,ctypes.c_uint]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.lib.bridge_reset()

    def bridge(self, request):
        output = ctypes.create_string_buffer(256)
        count = self.lib.bridge_reply(request,len(request),output)
        return output.raw[:count]

    def launch(self, mode='ok'):
        device, app = socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET)
        self.peer = WirePeer(device,self.bridge,mode)
        reserve = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        reserve.bind(('127.0.0.1',0))
        self.port = reserve.getsockname()[1]
        reserve.close()
        self.lock = str(Path(self.tmp.name)/f'lock_{self.port}')
        self.proc = subprocess.Popen([self.binary,'--bind','127.0.0.1','--peer','127.0.0.1',
                                     '--port',str(self.port),'--test-fd',str(app.fileno()),
                                     '--test-lock',self.lock],pass_fds=(app.fileno(),),
                                     stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        app.close()
        self.sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.sock.connect(('127.0.0.1',self.port))
        self.sock.settimeout(.03)
        for _ in range(60):
            try:
                self.sock.send(b'IGW1 STATS')
                self.sock.recv(4096)
                break
            except (socket.timeout,ConnectionRefusedError):
                self.assertIsNone(self.proc.poll())
                time.sleep(.005)
        else:
            self.fail('gateway did not start')
        self.sock.settimeout(1)

    def tearDown(self):
        if hasattr(self,'proc'):
            if self.proc.poll() is None:
                self.proc.terminate()
                self.assertEqual(self.proc.wait(2),0)
            self.sock.close()
            self.proc.stderr.close()
            self.peer.close()
            self.assertFalse(Path(self.lock).exists())
            del self.proc

    def raw(self, command):
        self.sock.send(command.encode())
        return json.loads(self.sock.recv(4096))

    def test_numeric_values_and_boundaries(self):
        for address in (16,31):
            for value in (0,1,127,128,255,256,0x8000,0xffff):
                request = adu(bytes((1,6))+address.to_bytes(2,'big')+value.to_bytes(2,'big'))
                self.assertEqual(self.bridge(request),request)
                read = adu(bytes((1,3))+address.to_bytes(2,'big')+b'\0\1')
                self.assertEqual(self.bridge(read),adu(b'\1\3\2'+value.to_bytes(2,'big')))
        self.assertEqual(self.lib.bridge_get(5),16)

    def test_illegal_readonly_broadcast_and_crc_have_no_mutation(self):
        for address in (0,4,5,7,15,32,65535):
            reply = self.bridge(adu(b'\1\6'+address.to_bytes(2,'big')+b'\xab\xcd'))
            self.assertEqual(reply,adu(b'\1\x86\2'))
        for unit in (0,2,247):
            self.assertEqual(self.bridge(adu(bytes((unit,6))+b'\0\x10\xab\xcd')),b'')
        bad = bytearray(adu(b'\1\6\0\x10\xab\xcd'))
        bad[-1] ^= 1
        self.assertEqual(self.bridge(bytes(bad)),b'')
        self.assertEqual(self.lib.bridge_get(16),0)
        self.assertEqual(self.lib.bridge_get(5),0)

    def test_malformed_lengths_unknown_function_and_read_boundaries(self):
        self.assertEqual(self.bridge(adu(b'\1\6')),adu(b'\1\x86\3'))
        self.assertEqual(self.bridge(adu(b'\1\x10\0\x10\0\1')),adu(b'\1\x90\1'))
        for address,count,error in [(31,2,2),(32,1,2),(0,0,3),(0,126,3)]:
            reply = self.bridge(adu(b'\1\3'+address.to_bytes(2,'big')+count.to_bytes(2,'big')))
            self.assertEqual(reply,adu(bytes((1,0x83,error))))

    def test_100_full_network_write_readbacks(self):
        self.launch()
        client = pc.Client('127.0.0.1',self.port)
        try:
            for i in range(100):
                row = pc.write_and_verify(client,16+i%16)
                self.assertEqual(row['readback'],row['written'])
        finally:
            client.close()
        self.assertEqual(self.peer.kicks,500)
        self.assertEqual(self.lib.bridge_get(5),100)
        self.assertEqual(self.peer.r[0xc],0)

    def test_replay_cache_and_command_conflicts(self):
        self.launch()
        read = 'IGW2 RS485 READ 10 1 16 1'
        write = 'IGW2 RS485 WRITE 11 1 16 43981'
        self.assertEqual(self.raw(read)['status'],'ok')
        acknowledged = self.raw(write)
        self.assertEqual(acknowledged['status'],'ok')
        self.assertEqual(self.raw(read)['registers'],[0])
        self.assertEqual(self.raw(write),acknowledged)
        self.assertEqual(self.peer.kicks,2)
        self.assertEqual(self.lib.bridge_get(5),1)
        self.assertEqual(self.raw('IGW2 RS485 WRITE 11 1 16 1')['status'],'sequence_conflict')
        self.assertEqual(self.raw('IGW2 RS485 READ 11 1 16 1')['status'],'sequence_conflict')
        self.assertEqual(self.peer.kicks,2)

    def test_pending_conflict_timeout_does_not_retry(self):
        self.launch('write_drop')
        self.sock.send(b'IGW2 RS485 WRITE 1 1 16 9')
        self.sock.send(b'IGW2 RS485 WRITE 1 1 16 10')
        self.assertEqual(json.loads(self.sock.recv(4096))['status'],'sequence_conflict')
        timed = json.loads(self.sock.recv(4096))
        self.assertEqual((timed['status'],timed['execution']),('timeout','unknown'))
        self.assertEqual(self.raw('IGW2 RS485 WRITE 1 1 16 9'),timed)
        self.assertEqual(self.peer.kicks,1)
        self.assertEqual(self.lib.bridge_get(16),9)
        self.assertEqual(self.peer.r[0xc],0)

    def test_invalid_commands_never_transmit(self):
        self.launch()
        for command in ['IGW2 RS485 WRITE 1 1 15 2','IGW2 RS485 WRITE 1 0 16 2',
                        'IGW2 RS485 WRITE 1 2 16 2','IGW2 RS485 WRITE 1 1 16 65536',
                        'IGW2 RS485 WRITE -1 1 16 2','IGW2 RS485 WRITE 4294967296 1 16 2',
                        'IGW2 RS485 READ 1 1 31 2','IGW2 RS485 READ 1 1 0 9',
                        'IGW2 RS485 READ 1 1 0 0','IGW2 RS485 WRITE 1 1 16 2 ',
                        'IGW2 RS485 WRITE 1 1 16 +2']:
            self.assertEqual(self.raw(command)['status'],'bad_request')
        self.assertEqual(self.peer.kicks,0)

    def test_fault_echo_exception_crc_and_pl_error(self):
        for mode,expected in [('bad_echo','bad_reply'),('bad_crc','bad_reply'),
                              ('zero_exception','bad_reply'),('exception','modbus_exception'),
                              ('pl_gap','pl_error')]:
            with self.subTest(mode=mode):
                self.launch(mode)
                self.assertEqual(self.raw('IGW2 RS485 WRITE 1 1 16 9')['status'],expected)
                self.assertEqual(self.peer.r[0xc],0)
                self.tearDown()

    def test_local_echo_without_slave_write_is_not_success(self):
        self.launch('echo_only')
        client = pc.Client('127.0.0.1',self.port)
        try:
            with self.assertRaisesRegex(ValueError,'readback'):
                pc.write_and_verify(client)
        finally:
            client.close()
        self.assertEqual(self.lib.bridge_get(5),0)

    def test_old_slave_firmware_is_rejected_before_write(self):
        self.lib.bridge_set(4,0)
        self.launch()
        client = pc.Client('127.0.0.1',self.port)
        try:
            with self.assertRaisesRegex(ValueError,'revision'):
                pc.write_and_verify(client)
        finally:
            client.close()
        self.assertTrue(all(req[1]==3 for req in self.peer.requests))

    def test_same_value_cannot_claim_state_change(self):
        self.launch()
        client = pc.Client('127.0.0.1',self.port)
        try:
            with self.assertRaisesRegex(ValueError,'different value'):
                pc.write_and_verify(client,desired=0)
        finally:
            client.close()
        self.assertEqual(self.lib.bridge_get(5),0)

    def test_cache_full_refuses_instead_of_forgetting_recent_writes(self):
        self.launch()
        for seq in range(512):
            self.assertEqual(self.raw(f'IGW2 RS485 READ {seq} 1 4 1')['status'],'ok')
        self.assertEqual(self.raw('IGW2 RS485 WRITE 999 1 16 8')['status'],'cache_full')
        self.assertEqual(self.peer.kicks,512)
        self.assertEqual(self.lib.bridge_get(5),0)

    def test_lost_write_ack_client_stops_without_retry(self):
        self.launch('write_drop')
        client = pc.Client('127.0.0.1',self.port)
        try:
            with self.assertRaisesRegex(RuntimeError,'no automatic retransmission'):
                pc.write_and_verify(client)
        finally:
            client.close()
        self.assertEqual(self.lib.bridge_get(5),1)
        self.assertEqual(self.peer.kicks,3)

    def test_cli_creates_evidence_for_real_udp_flow(self):
        self.launch()
        import shutil
        import sys
        folder = Path(self.tmp.name)/'cli'
        folder.mkdir(exist_ok=True)
        script = folder/'rs485_rw_test.py'
        shutil.copy2(ROOT/'pc/rs485_rw_test.py',script)
        result = subprocess.run([sys.executable,str(script),'--host','127.0.0.1',
                                 '--port',str(self.port)],capture_output=True,text=True,timeout=3)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('RESULT=PASS_RS485_WRITE_READBACK',result.stdout)
        paths = list(folder.glob('*.jsonl'))
        self.assertEqual(len(paths),1)
        rows = [json.loads(x) for x in paths[0].read_text().splitlines()]
        self.assertEqual(sum(x['event']=='request' for x in rows),5)
        self.assertEqual(rows[-1]['result'],'PASS_RS485_WRITE_READBACK')
        self.assertEqual(rows[0]['event'],'clock')
        self.assertEqual(rows[0]['clock'],'perf_counter_ns')
        self.assertTrue(rows[0]['monotonic'])
        self.assertGreater(rows[0]['resolution_seconds'],0)
        durations = [row['pc_rtt_us'] for row in rows if row['event']=='validated']
        self.assertTrue(all(duration > 0 for duration in durations))

    def test_write_metadata_wraps_and_remains_readable(self):
        self.lib.bridge_set(5,65535)
        self.launch()
        client = pc.Client('127.0.0.1',self.port)
        try:
            row = pc.write_and_verify(client,address=31,desired=65535)
            self.assertEqual(row['slave_write_count'],0)
            self.assertEqual(row['readback'],65535)
        finally:
            client.close()


if __name__ == '__main__':
    unittest.main(verbosity=2)
