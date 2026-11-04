"""Real UDP/C event loops + actual slave C protocol, UNIX bus transports.
Host simulation only: no physical CAN controller, AXI or AArch64 execution.
"""
import ctypes
import importlib.util
import json
from pathlib import Path
import select
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from test_rw import WirePeer

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'pc'))
import can_rw_test as pc


def payload(seq,op,addr,value=0):
    return struct.pack('<I',seq)+bytes((op,addr))+value.to_bytes(2,'big')


class CanRWTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory()
        cls.base=Path(cls.tmp.name)
        flags=['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror']
        cls.binary=str(cls.base/'can')
        cls.rs_binary=str(cls.base/'rs')
        subprocess.run(flags+['-DIGW_TEST_TRANSPORT',str(ROOT/'src/igw_can_udp.c'),'-o',cls.binary],check=True)
        subprocess.run(flags+['-static',str(ROOT/'src/igw_can_udp.c'),'-o',str(cls.base/'can_prod')],check=True)
        subprocess.run(flags+['-DIGW_RS485_TEST',str(ROOT/'src/igw_rs485_rw.c'),'-o',cls.rs_binary],check=True)
        so=str(cls.base/'bridge.so')
        subprocess.run(flags+['-shared','-fPIC','-I'+str(ROOT/'slave_src'),str(ROOT/'tests/protocol_bridge.c'),'-o',so],check=True)
        cls.lib=ctypes.CDLL(so)
        cls.lib.bridge_can.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint64]
        cls.lib.bridge_reply.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p]
        cls.lib.bridge_set.argtypes=[ctypes.c_uint,ctypes.c_uint]

    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()

    def setUp(self):
        self.lib.bridge_reset();self.mutex=threading.Lock();self.procs=[];self.clients=[]
        self.stop=threading.Event();self.worker=None;self.rs_peer=None;self.requests=[];self.mode='ok'

    def bridge_can(self,req,now=0):
        out=ctypes.create_string_buffer(8)
        with self.mutex:self.lib.bridge_can(req,out,now)
        return out.raw

    def bridge_rs(self,req):
        out=ctypes.create_string_buffer(256)
        with self.mutex:n=self.lib.bridge_reply(req,len(req),out)
        return out.raw[:n]

    @staticmethod
    def port():
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
            s.bind(('127.0.0.1',0));return s.getsockname()[1]

    def ready(self,port):
        s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.connect(('127.0.0.1',port));s.settimeout(.03)
        self.clients.append(s)
        for _ in range(60):
            try:s.send(b'IGW1 STATS');json.loads(s.recv(4096));break
            except (TimeoutError,ConnectionRefusedError):time.sleep(.005)
        else:self.fail('not ready')
        # Startup probes may have multiple queued replies under host scheduler load.
        s.settimeout(.01)
        try:
            while True:s.recv(4096)
        except TimeoutError:pass
        s.settimeout(1);return s

    def launch_can(self,worker=True,timeout_ms=500):
        self.device,app=socket.socketpair(socket.AF_UNIX,socket.SOCK_DGRAM)
        self.device.settimeout(.05)
        self.can_port=self.port()
        p=subprocess.Popen([self.binary,'--bind','127.0.0.1','--peer','127.0.0.1','--port',str(self.can_port),
             '--timeout-ms',str(timeout_ms),'--test-can-fd',str(app.fileno())],pass_fds=(app.fileno(),),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        app.close();self.procs.append(p);self.sock=self.ready(self.can_port)
        if worker:
            def run():
                while not self.stop.is_set():
                    try:raw=self.device.recv(64)
                    except socket.timeout:continue
                    except OSError:return
                    ident,dlc,data=struct.unpack('=IB3x8s',raw)
                    self.requests.append((ident,dlc,data))
                    if ident==0x201:
                        seq=int.from_bytes(data[:4],'little');response=struct.pack('<II',seq,0x52535000^seq);rid=0x181
                    else:
                        response=self.bridge_can(data,int(time.monotonic()*1e6));rid=0x191
                        if self.mode=='echo_only' and data[4]==2:
                            # Undo execution, so the acknowledgement alone cannot pass readback.
                            with self.mutex:self.lib.bridge_set(data[5],0)
                        if self.mode=='drop' and data[4]==2:continue
                        if self.mode=='bad_echo' and data[4]==2:response=response[:-1]+bytes((response[-1]^1,))
                    try:self.device.send(struct.pack('=IB3x8s',rid,8,response))
                    except OSError:return
            self.worker=threading.Thread(target=run);self.worker.start()

    def launch_rs(self):
        device,app=socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET)
        self.rs_peer=WirePeer(device,self.bridge_rs)
        self.rs_port=self.port()
        lock=str(self.base/f'rs_lock_{self.rs_port}')
        p=subprocess.Popen([self.rs_binary,'--bind','127.0.0.1','--peer','127.0.0.1','--port',str(self.rs_port),
             '--test-fd',str(app.fileno()),'--test-lock',lock],pass_fds=(app.fileno(),),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        app.close();self.procs.append(p);self.ready(self.rs_port)

    def client_can(self):
        c=pc.CanClient('127.0.0.1',self.can_port);self.clients.append(c);return c

    def raw(self,s):
        self.sock.send(s.encode());return json.loads(self.sock.recv(4096))

    def tearDown(self):
        for c in self.clients:c.close()
        for p in self.procs:
            if p.poll() is None:p.terminate()
            self.assertEqual(p.wait(3),0)
        self.stop.set()
        if self.worker:self.worker.join(1);self.assertFalse(self.worker.is_alive())
        if hasattr(self,'device'):self.device.close()
        if self.rs_peer:self.rs_peer.close()

    def test_80_cross_rounds_actual_c_gateways_and_slave(self):
        self.launch_can();self.launch_rs()
        c=self.client_can();rs=pc.RSClient('127.0.0.1',self.rs_port);self.clients.append(rs)
        for i in range(80):
            row=pc.cross_round(c,rs,16+i%16)
            self.assertEqual(row['write_count_after'],2*(i+1))
        self.assertEqual(self.lib.bridge_get(5),160)
        self.assertEqual(len(self.requests),320)
        self.assertEqual(self.rs_peer.kicks,480)
        self.assertEqual(self.rs_peer.r[0xc],0)

    def test_standalone_can_counter_wrap_and_full_u16_range(self):
        self.launch_can();c=self.client_can()
        self.lib.bridge_set(5,65535)
        row=pc.can_round(c,31);self.assertEqual(row['slave_write_count'],0)
        for a in (16,31):
            for v in (0,1,127,128,255,256,32768,65535):c.write(a,v);self.assertEqual(c.read(a),v)

    def test_slave_cache_duplicate_conflict_expiry_and_readonly(self):
        q=payload(0,2,31,65535)
        first=self.bridge_can(q,1);self.assertEqual(first[4],0x82)
        self.assertEqual(self.bridge_can(q,2),first);self.assertEqual(self.lib.bridge_get(5),1)
        self.assertEqual(self.bridge_can(payload(0,2,31,1),3)[4],0xe4)
        self.assertEqual(self.bridge_can(payload(0,1,31),4)[4],0xe4)
        self.assertEqual(self.bridge_can(q,60000001)[4],0x82);self.assertEqual(self.lib.bridge_get(5),2)
        for a in (0,4,5,8,15,32,255):self.assertEqual(self.bridge_can(payload(900+a,2,a,1),1)[4],0xe3)
        self.assertEqual(self.bridge_can(payload(999,1,32),1)[4],0xe2)
        self.assertEqual(self.bridge_can(payload(1000,1,16,1),1)[4],0xe1)
        self.assertEqual(self.bridge_can(payload(1001,3,16),1)[4],0xe1)
        self.assertEqual(self.lib.bridge_get(5),2)

    def test_slave_cache_full_refuses_new_write_without_eviction(self):
        for i in range(128):self.assertEqual(self.bridge_can(payload(i,2,16,i),1)[4],0x82)
        self.assertEqual(self.bridge_can(payload(200,2,16,999),2)[4],0xe5)
        self.assertEqual(self.lib.bridge_get(16),127);self.assertEqual(self.lib.bridge_get(5),128)
        self.assertEqual(self.bridge_can(payload(0,2,16,0),2)[4],0x82)
        self.assertEqual(self.lib.bridge_get(16),127)

    def test_gateway_cache_full_replay_and_sequence_conflict(self):
        self.launch_can()
        for i in range(1024):self.assertEqual(self.raw(f'IGW2 CAN READ {i} 1 16')['status'],'ok')
        self.assertEqual(self.raw('IGW2 CAN READ 1024 1 16')['status'],'cache_full')
        self.assertEqual(self.raw('IGW2 CAN WRITE 0 1 16 7')['status'],'sequence_conflict')
        self.assertEqual(self.raw('IGW2 CAN READ 0 1 16')['status'],'ok')
        self.assertEqual(len(self.requests),1024)

    def test_completed_write_replay_across_other_commands(self):
        self.launch_can()
        first=self.raw('IGW2 CAN WRITE 1 1 16 99')
        self.assertEqual(first['status'],'ok')
        self.raw('IGW2 CAN READ 2 1 16')
        self.assertEqual(self.raw('IGW2 CAN WRITE 1 1 16 99'),first)
        self.assertEqual(self.lib.bridge_get(5),1);self.assertEqual(len(self.requests),2)
        self.assertEqual(self.raw('IGW2 CAN WRITE 1 1 16 100')['status'],'sequence_conflict')

    def test_lost_write_reply_stays_unknown_and_is_not_reexecuted(self):
        self.mode='drop';self.launch_can(timeout_ms=80)
        first=self.raw('IGW2 CAN WRITE 1 1 16 99')
        self.assertEqual((first['status'],first['execution']),('timeout','unknown'))
        self.assertEqual(self.lib.bridge_get(16),99)
        self.assertEqual(self.raw('IGW2 CAN WRITE 1 1 16 99'),first)
        self.assertEqual(len(self.requests),1);self.assertEqual(self.lib.bridge_get(5),1)

    def test_acknowledgement_without_matching_readback_is_rejected(self):
        self.mode='echo_only';self.launch_can();c=self.client_can()
        with self.assertRaisesRegex(ValueError,'readback'):pc.can_round(c,16)

    def test_wrong_echo_wrong_id_wrong_sequence_no_false_success(self):
        self.launch_can(worker=False,timeout_ms=80)
        self.sock.send(b'IGW2 CAN WRITE 4294967295 1 31 43981')
        raw=self.device.recv(64);self.assertEqual(struct.unpack('=IB3x8s',raw),(0x211,8,payload(4294967295,2,31,43981)))
        for ident,seq,addr,v in [(0x181,4294967295,31,43981),(0x191,1,31,43981),(0x191,4294967295,30,43981),(0x191,4294967295,31,1)]:
            self.device.send(struct.pack('=IB3x8s',ident,8,payload(seq,0x82,addr,v)))
        r=json.loads(self.sock.recv(4096));self.assertEqual(r['status'],'bad_reply',r);self.assertEqual(r['stats']['ok'],0)

    def test_readonly_unsupported_units_and_malformed_udp_never_transmit(self):
        self.launch_can()
        for s in ['IGW2 CAN WRITE 1 1 15 1','IGW2 CAN WRITE 1 0 16 1','IGW2 CAN WRITE 1 2 16 1',
                  'IGW2 CAN WRITE 4294967296 1 16 1','IGW2 CAN WRITE +1 1 16 1','IGW2 CAN WRITE 1 1 16 65536',
                  'IGW2 CAN READ 1 1 32','IGW2 CAN READ 1 1 16 1','IGW2 CAN READ 1 1 16 ',
                  'IGW2 CAN WRITE 1 1 16 -1','IGW2 CAN WRITE 1 1 16 1\n','IGW2 CAN READ 1 1 16\0junk']:
            self.assertEqual(self.raw(s)['status'],'bad_request',s)
        self.assertEqual(len(self.requests),0);self.assertEqual(self.lib.bridge_get(5),0)

    def test_pending_duplicate_conflict_busy_do_not_change_active_write(self):
        self.launch_can(worker=False)
        self.sock.send(b'IGW2 CAN WRITE 1 1 16 4660')
        first=self.device.recv(64)
        self.sock.send(b'IGW2 CAN WRITE 1 1 16 4660')
        self.assertEqual(self.raw('IGW2 CAN WRITE 1 1 16 4661')['status'],'sequence_conflict')
        self.assertEqual(self.raw('IGW2 CAN READ 2 1 16')['status'],'busy')
        self.assertFalse(select.select([self.device],[],[],.01)[0])
        _,_,data=struct.unpack('=IB3x8s',first)
        self.device.send(struct.pack('=IB3x8s',0x191,8,self.bridge_can(data,1)))
        result=json.loads(self.sock.recv(4096))
        self.assertEqual((result['status'],result['value']),('ok',4660))
        self.assertEqual(self.lib.bridge_get(5),1)
        self.assertEqual(self.raw('IGW2 CAN WRITE 1 1 16 4660'),result)

    def test_pending_business_shutdown_exits_without_retry(self):
        self.launch_can(worker=False)
        self.sock.send(b'IGW2 CAN WRITE 7 1 16 8')
        self.device.recv(64)
        self.procs[0].terminate();self.assertEqual(self.procs[0].wait(2),0)
        self.assertFalse(select.select([self.device],[],[],.02)[0])

    def test_real_pc_cli_cross_jsonl(self):
        self.launch_can();self.launch_rs()
        # Run a temporary copy so evidence stays inside the test directory.
        import shutil
        folder=self.base/f'pc_{self.can_port}';folder.mkdir()
        for name in ('can_rw_test.py','rs485_rw_test.py'):shutil.copy(ROOT/'pc'/name,folder/name)
        result=subprocess.run([sys.executable,str(folder/'can_rw_test.py'),'--mode','cross','--host','127.0.0.1',
             '--can-port',str(self.can_port),'--rs-port',str(self.rs_port),'--count','2','--address','31'],capture_output=True,text=True,timeout=8)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        rows=[json.loads(x) for x in next(folder.glob('*.jsonl')).read_text().splitlines()]
        self.assertEqual(rows[0]['tool_revision'],'RW_R9_PC1')
        self.assertEqual(rows[-1],{'event':'summary','result':'PASS_CROSS_PROTOCOL_READBACK','verified':2})
        self.assertEqual(sum(r['event']=='cross_verified' for r in rows),2)


if __name__=='__main__':unittest.main()
