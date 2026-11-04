"""Real C uplink daemon + real C HTTP/evidence; UNIX CAN surrogate only."""
import ctypes
import http.client
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]
class UplinkTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory();cls.root=Path(cls.temp.name)
  cls.snapshot=cls.root/'uplink.json';cls.daemon=cls.root/'uplink';cls.web=cls.root/'web'
  flags=['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
  subprocess.run(flags+['-DIGW_UPLINK_HOST_TEST',str(ROOT/'src/igw_can_uplink.c'),'-o',str(cls.daemon)],check=True)
  subprocess.run(flags+['-pthread','-DIGW_UPLINK_PATH='+json.dumps(str(cls.snapshot)),str(ROOT/'src/igw_web.c'),'-lm','-o',str(cls.web)],check=True)
 @classmethod
 def tearDownClass(cls):cls.temp.cleanup()
 def setUp(self):
  self.snapshot.unlink(missing_ok=True);self.sock,peer=socket.socketpair(socket.AF_UNIX,socket.SOCK_DGRAM);self.sock.settimeout(3)
  self.log=tempfile.TemporaryFile(mode='w+')
  env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
  self.p=subprocess.Popen([str(self.daemon),'--test-fd',str(peer.fileno()),'--state-file',str(self.snapshot),'--nodes','16'],pass_fds=(peer.fileno(),),stdout=self.log,stderr=subprocess.STDOUT,env=env);peer.close();self.webproc=None
  self.wait_snapshot(lambda s:s['running'])
 def tearDown(self):
  for p in [self.webproc,self.p]:
   if p:
    if p.poll() is None:p.terminate()
    code=p.wait(6);self.log.seek(0);self.assertEqual(code,0,self.log.read())
  self.sock.close();self.log.close()
 def wait_snapshot(self,predicate):
  end=time.monotonic()+3
  while time.monotonic()<end:
   try:
    s=json.loads(self.snapshot.read_text())
    if predicate(s):return s
   except (OSError,json.JSONDecodeError):pass
   self.assertIsNone(self.p.poll());time.sleep(.01)
  self.fail('snapshot predicate not reached')
 def send(self,canid,payload,dlc=8):self.sock.send(struct.pack('=IB3x8s',canid,dlc,payload))
 def recv(self):
  canid,dlc,payload=struct.unpack('=IB3x8s',self.sock.recv(16));self.assertEqual(dlc,8);return canid,payload
 def hello(self,unit,cookie=4):
  self.send(0x300+unit,struct.pack('<II',cookie,0x31554749));cid,p=self.recv();self.assertEqual(cid,0x340+unit);c,epoch=struct.unpack('<II',p);self.assertEqual(c,cookie);self.assertNotEqual(epoch,0);return epoch
 def upload(self,unit,epoch,seq,value):
  data=struct.pack('<IH',epoch,seq)+value.to_bytes(2,'big');self.send(0x380+unit,data);cid,p=self.recv();self.assertEqual((cid,p),(0x3c0+unit,data));return data
 def test_zynq_node3_can_uplink_real_gateway_protocol(self):
  lib=ctypes.CDLL(os.environ['CAN3_BRIDGE_PATH'])
  lib.zynq_can.argtypes=[ctypes.c_uint,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint64]
  lib.zynq_tick.argtypes=[ctypes.c_uint64,ctypes.c_void_p];lib.zynq_reset()
  payload=ctypes.create_string_buffer(8);answer=ctypes.create_string_buffer(8)
  self.assertEqual(lib.zynq_tick(0,payload),0,'must wait for a master request')
  read=bytes((1,0,0,0,1,0,0,0));self.assertEqual(lib.zynq_can(0x213,read,answer,0),0x193)
  self.assertEqual(lib.zynq_tick(0,payload),0x303);self.send(0x303,payload.raw)
  cid,data=self.recv();self.assertEqual(cid,0x343);self.assertEqual(lib.zynq_can(cid,data,answer,1000),0)
  self.assertEqual(lib.zynq_tick(1,payload),0x383);self.send(0x383,payload.raw)
  cid,data=self.recv();self.assertEqual(cid,0x3c3);lib.zynq_can(cid,data,answer,2000)
  state=self.wait_snapshot(lambda s:s['devices'][2]['accepted']==1)
  self.assertEqual(state['devices'][2]['value'],3000)
  for seq,addr,value in [(2,30,54321),(3,28,0xa526)]:
   req=struct.pack('<I',seq)+bytes((2,addr,value>>8,value&255))
   self.assertEqual(lib.zynq_can(0x213,req,answer,10000),0x193)
  self.assertEqual(lib.zynq_tick(1002,payload),0x383);self.send(0x383,payload.raw)
  cid,data=self.recv();self.assertEqual(cid,0x3c3);lib.zynq_can(cid,data,answer,1003000)
  state=self.wait_snapshot(lambda s:s['devices'][2]['accepted']==2)
  self.assertEqual(state['devices'][2]['value'],54321)
  print('PASS_CAN3_UPLINK_REAL_HOST: handshake, accepted counter, fixed 54321')

if __name__=='__main__':unittest.main(verbosity=2)
