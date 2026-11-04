"""Exercise the real C RS485 owner with a simulated MMIO device and control socket.

This is NOT a physical bus, FPGA, UART timing, or RTL simulation test.
"""
import json
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests/host_fixture/baseline_r9/tests'))
sys.path.insert(0, str(ROOT/'tests/host_fixture/tests'))
from test_rw import WirePeer, adu

from unit3_mock import Unit3

class SlotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();cls.path=Path(cls.tmp.name)
        cls.binary=cls.path/'rs_owner'
        subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Wmissing-field-initializers','-Werror',
                        '-DIGW_RS485_TEST',str(ROOT/'tests/host_fixture/src/igw_rs485_rw.c'),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def setUp(self):
        self.slave=Unit3();dev,app=socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET)
        self.peer=WirePeer(dev,self.slave)
        self.control,cc=socket.socketpair(socket.AF_UNIX,socket.SOCK_DGRAM)
        reserve=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);reserve.bind(('127.0.0.1',0));self.port=reserve.getsockname()[1];reserve.close()
        self.lock=self.path/f'lock_{self.port}';self.state=self.path/f'state_{self.port}.json'
        self.proc=subprocess.Popen([str(self.binary),'--uplink','--rs3-uplink','--nodes','3',
          '--bind','127.0.0.1','--peer','127.0.0.1','--port',str(self.port),
          '--test-fd',str(app.fileno()),'--test-control-fd',str(cc.fileno()),
          '--test-lock',str(self.lock),'--test-state',str(self.state)],pass_fds=(app.fileno(),cc.fileno()),
          stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        app.close();cc.close();self.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.sock.connect(('127.0.0.1',self.port));self.sock.settimeout(.05);self.seq=10
        for _ in range(100):
            try:self.sock.send(b'IGW1 STATS');self.sock.recv(4096);break
            except (TimeoutError,ConnectionRefusedError):
                if self.proc.poll() is not None:self.fail(self.proc.stderr.read().decode())
                time.sleep(.005)
        else:self.fail('owner did not start')
        self.sock.settimeout(1)
    def tearDown(self):
        if self.proc.poll() is None:self.proc.terminate()
        self.proc.wait(3);self.sock.close();self.control.close();self.peer.close();self.proc.stderr.close()
    def request(self,op,address,arg):
        self.seq+=1;self.sock.send(f'IGW2 RS485 {op} {self.seq} 3 {address} {arg}'.encode())
        r=json.loads(self.sock.recv(4096));self.assertEqual(r['status'],'ok',r);return r
    def config(self):
        self.request('READ',8,2);self.request('WRITE',29,54321)
        self.request('WRITE',27,0xa526);return self.request('READ',27,1)
    def snapshot(self,pred,timeout=4):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            try:
                d=json.loads(self.state.read_text())
                if pred(d):return d
            except (FileNotFoundError,json.JSONDecodeError):pass
            time.sleep(.01)
        self.fail('snapshot did not reach expected state')
    def test_one_shot_data_and_exact_hardware_accounting(self):
        before=self.config()
        d=self.snapshot(lambda d:d['devices'][2]['accepted']>=1)
        n=d['devices'][2]
        self.assertTrue(n['valid']);self.assertEqual(n['value'],54321);self.assertGreater(n['epoch'],0)
        after=self.request('READ',16,1)
        a,b=before['pl_accounting'],after['pl_accounting']
        self.assertGreaterEqual(b['host_grant_tx']-a['host_grant_tx'],1)
        self.assertEqual(after['pl']['tx']-before['pl']['tx'],1+b['host_grant_tx']-a['host_grant_tx'])
        self.assertEqual(after['pl']['good']-before['pl']['good'],1+b['validated_uplink_good']-a['validated_uplink_good'])
        self.assertTrue(any(r[1]==0x45 for r in self.peer.requests))
    def test_no_grant_without_confirmed_capability(self):
        self.slave.reg[9]=0x0d01;self.config();time.sleep(.15)
        self.assertFalse(any(r[1]==0x45 for r in self.peer.requests))
    def test_missing_reply_keeps_slot_and_lock(self):
        self.slave.behavior='drop';self.config()
        d=self.snapshot(lambda d:d['slot_timeouts']>0,timeout=3)
        self.assertTrue(d['slot_active']);self.assertEqual(d['slot_unit'],3)
        self.sock.settimeout(.15);self.sock.send(b'IGW2 RS485 READ 100000 3 16 1')
        with self.assertRaises(TimeoutError):self.sock.recv(4096)
        self.proc.terminate();self.assertEqual(self.proc.wait(3),2)
        self.assertTrue(self.lock.exists());self.assertEqual(self.peer.r.get(0x0c),0)
    def test_mismatched_nonce_is_not_accepted_and_retains_owner(self):
        self.slave.behavior='wrong_nonce';self.config()
        d=self.snapshot(lambda d:d['bad_frames']>0 and d['slot_timeouts']>0,timeout=3)
        self.assertEqual(d['devices'][2]['accepted'],0);self.assertTrue(d['slot_active'])
        self.proc.terminate();self.assertEqual(self.proc.wait(3),2);self.assertTrue(self.lock.exists())

if __name__=='__main__':unittest.main(verbosity=2)
