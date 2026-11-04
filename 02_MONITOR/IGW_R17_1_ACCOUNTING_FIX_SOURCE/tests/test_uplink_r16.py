"""Real C uplink daemon + real C HTTP/evidence; UNIX CAN surrogate only."""
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
 def test_real_commit_before_ack_and_dedup_16_nodes(self):
  for unit in range(1,17):
   epoch=self.hello(unit);self.upload(unit,epoch,1,unit*100)
   # ACK receipt must imply snapshot already contains the host write.
   s=json.loads(self.snapshot.read_text());self.assertEqual(s['devices'][unit-1]['accepted'],1)
   self.upload(unit,epoch,1,unit*100)
   s=json.loads(self.snapshot.read_text());self.assertEqual(s['devices'][unit-1]['accepted'],1);self.assertEqual(s['devices'][unit-1]['duplicates'],1)
  self.assertEqual(self.snapshot.stat().st_mode&0o777,0o600)
 def test_malformed_and_unconfigured_frames_do_not_write(self):
  epoch=self.hello(1)
  payload=struct.pack('<IH',epoch,1)+b'\x12\x34'
  self.send(0x381,payload,dlc=7);self.send(0x381|0x80000000,payload);self.send(0x391,payload)
  self.upload(1,epoch,1,0x1234)
  s=json.loads(self.snapshot.read_text());self.assertEqual(sum(d['accepted'] for d in s['devices']),1)
 def test_new_host_or_device_epoch_and_stale_frame(self):
  epoch=self.hello(1);old=self.upload(1,epoch,1,111)
  new_epoch=self.hello(1);self.assertNotEqual(epoch,new_epoch)
  self.send(0x381,old);cid,control=self.recv();self.assertEqual((cid,control),(0x341,b'\0'*8))
  self.upload(1,new_epoch,1,222)
  s=json.loads(self.snapshot.read_text());self.assertEqual(s['devices'][0]['value'],222);self.assertEqual(s['devices'][0]['accepted'],2)
 def test_web_and_evidence_include_autonomous_writes_without_polling_devices(self):
  self.upload(1,self.hello(1),1,1357);self.upload(2,self.hello(2),1,2468)
  # At this point no HTTP service or master polling process has been started.
  with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
  self.webproc=subprocess.Popen([str(self.web),'--bind','127.0.0.1','--peer','127.0.0.1','--port',str(port),'--nodes','2'],stdout=self.log,stderr=subprocess.STDOUT,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1'))
  def get(path):
   connection=http.client.HTTPConnection('127.0.0.1',port,timeout=3)
   try:connection.request('GET',path);response=connection.getresponse();self.assertEqual(response.status,200);return json.loads(response.read())
   finally:connection.close()
  end=time.monotonic()+3
  while True:
   try:state=get('/api/state');break
   except OSError:
    if time.monotonic()>end:raise
    time.sleep(.02)
  self.assertTrue(state['uplink']['available']);self.assertEqual(state['software_revision'],'R17.1_RS485_UPLINK')
  evidence=get('/api/evidence');self.assertNotIn('token',evidence)
  self.assertEqual([d['value'] for d in evidence['uplink']['snapshot']['devices'][:2]],[1357,2468])
  self.assertEqual(len(evidence['uplink']['snapshot']['devices']),16)
  self.p.terminate();self.assertEqual(self.p.wait(3),0)
  self.assertFalse(get('/api/state')['uplink']['available'])

if __name__=='__main__':unittest.main(verbosity=2)
