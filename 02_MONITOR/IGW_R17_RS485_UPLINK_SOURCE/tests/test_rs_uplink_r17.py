"""Actual R17 C RS owner + Web; UNIX CAN/MMIO surrogates, no timing/hardware claim."""
import http.client,json,os,socket,struct,subprocess,tempfile,threading,time,unittest,hashlib,shlex
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def crc(p):
 c=65535
 for x in p:
  c^=x
  for _ in range(8):c=(c>>1)^(0xa001 if c&1 else 0)
 return c
def adu(p):return p+crc(p).to_bytes(2,'little')
class Mmio:
 def __init__(self,s):
  self.sock=s;self.lock=threading.Lock();self.r={0:0x32574749,4:0x20000,8:31,0x18:50000000,0x20:5208,0x24:1,0x28:85932,0x2c:200508,0x1c:64};self.kicks=0;self.frame=None;self.at=0;self.quit=False;self.delay=.003;self.thread=threading.Thread(target=self.run);self.thread.start()
 def inject(self,p):
  with self.lock:self.frame=p
 def queue(self,p):
  self.r[0x40]=len(p)
  for i in range(0,len(p),4):self.r[0x100+i]=int.from_bytes(p[i:i+4].ljust(4,b'\0'),'little')
  self.r[0x50]=self.r.get(0x50,0)+1;self.r[0x1c]|=1
 def run(self):
  try:
   while True:
    raw=self.sock.recv(12)
    if not raw:return
    op,off,v=struct.unpack('=III',raw)
    with self.lock:
     if self.at and time.monotonic()>=self.at:
      self.at=0;self.r[0x60]=self.r.get(0x60,0)+1;self.r[0x1c]=64
      req=b''.join(self.r.get(0x200+i,0).to_bytes(4,'little') for i in (0,4))
      self.queue(adu(bytes((req[0],3,2,0x12,0x34))))
     if self.frame is not None and not(self.r[0x1c]&1):self.queue(self.frame);self.frame=None
     if op:
      if off==0x38:self.r[0x1c]&=~1
      else:self.r[off]=v
      if off==0x34:self.kicks+=1;self.r[0x1c]=2;self.at=time.monotonic()+self.delay
     answer=self.r.get(off,0)
    self.sock.send(struct.pack('=I',answer))
  except OSError:pass
 def close(self):self.thread.join(3);self.sock.close();assert not self.thread.is_alive()
class RsUplinkTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();cls.root=Path(cls.tmp.name);cls.binary=cls.root/'rs';cls.web=cls.root/'web';cls.path=cls.root/'rs.json'
  flags=['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
  subprocess.run(flags+['-DIGW_RS485_TEST',str(ROOT/'src/igw_rs485_rw.c'),'-o',str(cls.binary)],check=True)
  subprocess.run(flags+['-pthread','-DIGW_RS_UPLINK_PATH='+json.dumps(str(cls.path)),str(ROOT/'src/igw_web.c'),'-lm','-o',str(cls.web)],check=True)
  cls.vault=cls.root/'vault'
  subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(ROOT/'src/igw_vault.c'),'-ldl','-o',str(cls.vault)],check=True)
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def setUp(self):
  self.path.unlink(missing_ok=True);self.can,cp=socket.socketpair(socket.AF_UNIX,socket.SOCK_DGRAM);self.can.settimeout(2);m,mp=socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET);self.mm=Mmio(m);self.log=tempfile.TemporaryFile(mode='w+');self.wp=None
  with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as p:p.bind(('127.0.0.1',0));self.port=p.getsockname()[1]
  self.lock=self.root/f'lock{self.port}';self.lease=None
  self.proc=subprocess.Popen([str(self.binary),'--uplink','--nodes','16','--bind','127.0.0.1','--peer','127.0.0.1','--port',str(self.port),'--test-fd',str(mp.fileno()),'--test-lock',str(self.lock),'--test-control-fd',str(cp.fileno()),'--test-state',str(self.path)],pass_fds=(mp.fileno(),cp.fileno()),stdout=self.log,stderr=subprocess.STDOUT,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));mp.close();cp.close()
  self.wait(lambda:self.path.exists());self.udp=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);self.udp.connect(('127.0.0.1',self.port));self.udp.settimeout(2)
 def tearDown(self):
  if self.wp:self.wp.terminate();self.assertEqual(self.wp.wait(4),0)
  if self.lease:
   self.done(*self.lease);self.lease=None
  self.proc.terminate();code=self.proc.wait(5);self.log.seek(0);self.assertEqual(code,0,self.log.read());self.can.close();self.udp.close();self.mm.close();self.log.close();self.assertFalse(self.lock.exists());self.assertEqual(self.mm.r.get(0xc,0),0)
 def wait(self,fn):
  end=time.monotonic()+3
  while time.monotonic()<end:
   if fn():return
   self.assertIsNone(self.proc.poll());time.sleep(.005)
  self.fail('wait timed out')
 def snapshot(self):return json.loads(self.path.read_text())
 def send(self,cid,p,dlc=8):self.can.send(struct.pack('=IB3x8s',cid,dlc,p))
 def recv(self,wanted=None):
  end=time.monotonic()+2
  while time.monotonic()<end:
   cid,dlc,p=struct.unpack('=IB3x8s',self.can.recv(16))
   if wanted is None or cid==wanted:return cid,p
  self.fail('control message not found')
 def request(self,unit=1,epoch=123,seq=1):
  self.send(0x400+unit,struct.pack('<IHH',epoch,seq,0x1752));cid,p=self.recv(0x440+unit);self.assertEqual(cid,0x440+unit);nonce,received_epoch=struct.unpack('<II',p);self.assertEqual(received_epoch,epoch);self.lease=(unit,nonce,seq);return nonce
 def data(self,unit,nonce,epoch,seq,value):return adu(bytes((unit,0x41))+struct.pack('<IIH',nonce,epoch,seq)+value.to_bytes(2,'big'))
 def done(self,unit,nonce,seq):
  self.send(0x4c0+unit,struct.pack('<IHH',nonce,seq,0x1744));_,p=self.recv(0x500+unit);self.assertEqual(struct.unpack('<IHH',p),(nonce,seq,0x1745));self.lease=None
 def upload(self,unit,epoch,seq,value):
  nonce=self.request(unit,epoch,seq);self.mm.inject(self.data(unit,nonce,epoch,seq,value));_,a=self.recv(0x480+unit);self.assertEqual(a,struct.pack('<IH',nonce,seq)+value.to_bytes(2,'big'));self.assertEqual(self.snapshot()['devices'][unit-1]['value'],value);self.done(unit,nonce,seq);return nonce
 def test_no_polling_required_16_nodes_and_commit_before_ack(self):
  for unit in range(1,17):self.upload(unit,123+unit,1,unit*123)
  s=self.snapshot();self.assertEqual(sum(n['accepted'] for n in s['devices']),16);self.assertEqual(self.mm.kicks,0);self.assertEqual(self.path.stat().st_mode&0o777,0o600)
 def test_lost_data_ack_retries_do_not_repeat_host_write(self):
  nonce=self.upload(1,123,1,456)
  self.send(0x401,struct.pack('<IHH',123,1,0x1752));_,a=self.recv(0x481);self.assertEqual(a,struct.pack('<IH',nonce,1)+b'\x01\xc8')
  s=self.snapshot()['devices'][0];self.assertEqual((s['accepted'],s['duplicates']),(1,1))
 def test_wrong_nonce_crc_unit_do_not_commit(self):
  nonce=self.request()
  for p in [self.data(1,nonce+1,123,1,999),self.data(2,nonce,123,1,999),self.data(1,nonce,123,1,999)[:-1]+b'\x01']:
   self.mm.inject(p);time.sleep(.02)
  self.mm.inject(self.data(1,nonce,123,1,456));self.recv(0x481);self.assertEqual(self.snapshot()['devices'][0]['accepted'],1);self.done(1,nonce,1)
 def test_late_grant_timeout_freezes_polling_until_done(self):
  nonce=self.request();self.udp.send(b'IGW2 RS485 READ 42 1 0 1');time.sleep(.24)
  self.assertEqual(self.mm.kicks,0);self.recv(0x541)
  self.mm.inject(self.data(1,nonce,123,1,456));time.sleep(.02);self.assertEqual(self.mm.kicks,0) # expired RS data cannot commit
  self.assertEqual(self.snapshot()['devices'][0]['accepted'],0)
  self.done(1,nonce,1);r=json.loads(self.udp.recv(4096));self.assertEqual(r['status'],'ok');self.assertEqual(self.mm.kicks,1)
 def test_done_arrives_before_pl_crc_frame_publication(self):
  nonce=self.request();self.send(0x4c1,struct.pack('<IHH',nonce,1,0x1744));time.sleep(.004);self.mm.inject(self.data(1,nonce,123,1,987));self.recv(0x481);self.recv(0x501);self.lease=None;self.assertEqual(self.snapshot()['devices'][0]['value'],987)
 def test_stale_epoch_data_and_device_restart(self):
  old=self.upload(1,100,1,111);new=self.request(1,200,1);self.mm.inject(self.data(1,old,100,1,333));time.sleep(.01);self.mm.inject(self.data(1,new,200,1,222));self.recv(0x481);self.done(1,new,1);s=self.snapshot()['devices'][0];self.assertEqual((s['value'],s['accepted']),(222,2))
 def test_ordinary_read_finishes_before_grant_and_resumes_after_upload(self):
  self.mm.delay=.07;self.udp.send(b'IGW2 RS485 READ 700 1 0 1');self.wait(lambda:self.mm.kicks==1)
  self.send(0x401,struct.pack('<IHH',123,1,0x1752));self.can.settimeout(.025)
  with self.assertRaises(TimeoutError):self.can.recv(16)
  self.can.settimeout(2);self.assertEqual(json.loads(self.udp.recv(4096))['status'],'ok')
  _,p=self.recv(0x441);nonce,epoch=struct.unpack('<II',p);self.assertEqual(epoch,123);seq=1;self.lease=(1,nonce,seq)
  self.mm.inject(self.data(1,nonce,123,seq,789));self.recv(0x481);self.done(1,nonce,seq)
  self.udp.send(b'IGW2 RS485 READ 701 2 0 1');r=json.loads(self.udp.recv(4096));self.assertEqual((r['status'],r['unit']),('ok',2));self.assertEqual(self.mm.kicks,2)
 def test_operator_stop_completes_cancel_quiescence_handshake(self):
  nonce=self.request();self.proc.terminate();self.recv(0x541);self.done(1,nonce,1);self.assertEqual(self.proc.wait(3),0)
 def test_web_evidence_contains_rs_payload_committed_without_web(self):
  self.upload(1,123,1,1357);self.upload(2,124,1,2468)
  with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
  self.wp=subprocess.Popen([str(self.web),'--bind','127.0.0.1','--peer','127.0.0.1','--port',str(port),'--nodes','2'],stdout=self.log,stderr=subprocess.STDOUT,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1'))
  def get():
   c=http.client.HTTPConnection('127.0.0.1',port,timeout=2)
   try:c.request('GET','/api/evidence');r=c.getresponse();self.assertEqual(r.status,200);return json.loads(r.read())
   finally:c.close()
  evidence=None
  for _ in range(100):
   try:evidence=get();break
   except OSError:time.sleep(.02)
  self.assertIsNotNone(evidence);self.assertEqual(evidence['software_revision'],'R17_RS485_UPLINK');self.assertTrue(evidence['rs485_uplink']['available']);self.assertEqual([n['value'] for n in evidence['rs485_uplink']['snapshot']['devices'][:2]],[1357,2468])
 def test_real_web_to_encrypted_archive_contains_rs_upload(self):
  self.upload(1,123,1,9876)
  with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
  run=self.root/f'secure{port}';run.mkdir();script=run/'web.sh'
  script.write_text('#!/bin/sh\nexec '+shlex.join([str(self.web),'--archive-fd','3','--bind','127.0.0.1','--peer','127.0.0.1','--port',str(port),'--nodes','2'])+'\n')
  password=b'R17-only-test-password-391!';fd,wr=os.pipe();os.write(wr,password+b'\n'+password+b'\n');os.close(wr)
  self.wp=subprocess.Popen([str(self.vault),'serve','--vault',str(run/'vault'),'--run-script',str(script),'--interval','1','--pass-fd',str(fd)],pass_fds=(fd,),stdout=self.log,stderr=subprocess.STDOUT,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1'));os.close(fd)
  self.wait(lambda:any((run/'vault').glob('igw-*.igwe')));time.sleep(.3);self.wp.terminate();self.assertEqual(self.wp.wait(12),0)
  records=[]
  for archive in (run/'vault').glob('igw-*.igwe'):
   b=archive.read_bytes();key=hashlib.pbkdf2_hmac('sha256',password,b[16:32],int.from_bytes(b[12:16],'big'),32);records.append(json.loads(AESGCM(key).decrypt(b[32:44],b[64:],b[:64])))
  self.assertTrue(records);self.assertTrue(all('token' not in x for x in records))
  self.assertTrue(any(x['rs485_uplink']['snapshot']['devices'][0]['value']==9876 and x['rs485_uplink']['snapshot']['devices'][0]['accepted']==1 for x in records))
if __name__=='__main__':unittest.main(verbosity=2)
