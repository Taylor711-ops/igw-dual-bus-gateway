"""Real production C HTTP/UDP loops and shared R13 slave protocol, 16 nodes.
UNIX CAN/MMIO surrogates: no FPGA, transceiver, AArch64 or real-time claims.
"""
import ctypes
import http.client
import json
import os
from pathlib import Path
import secrets
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'baseline_r9/tests'))
from test_rw import WirePeer, adu

class MultiTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();cls.base=Path(cls.tmp.name)
  flags=['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pthread']
  if os.environ.get('IGW_WEB_SANITIZE'):flags+=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
  cls.binaries={}
  for name,define in [('igw_can_udp','-DIGW_TEST_TRANSPORT'),('igw_rs485_rw','-DIGW_RS485_TEST'),('igw_web','-DIGW_R13_TEST')]:
   out=str(cls.base/name);subprocess.run(flags+[define,str(ROOT/f'src/{name}.c'),'-lm','-o',out],check=True);cls.binaries[name]=out
  # ASan-loaded shared libraries require the host interpreter to preload ASan;
  # C event loops above are instrumented. This library is checked by the
  # separate instrumented full firmware scheduler executable.
  so=str(cls.base/'nodes.so')
  subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-DIGW_NODE_COUNT=16','-I'+str(ROOT/'slave_r17/slave_src'),str(ROOT/'tests/nodes_bridge.c'),'-o',so],check=True)
  cls.lib=ctypes.CDLL(so)
  cls.lib.nodes_can.argtypes=[ctypes.c_uint,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint64]
  cls.lib.nodes_rtu.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p]
  cls.lib.nodes_get.argtypes=[ctypes.c_uint,ctypes.c_uint]
  cls.zlib=ctypes.CDLL(os.environ['CAN3_BRIDGE_PATH'])
  cls.zlib.zynq_can.argtypes=[ctypes.c_uint,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint64]
  cls.zlib.zynq_get.argtypes=[ctypes.c_uint]
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 @staticmethod
 def port(kind=socket.SOCK_DGRAM):
  with socket.socket(socket.AF_INET,kind) as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
 def setUp(self):
  self.lib.nodes_reset();self.zlib.zynq_reset();self.mutex=threading.Lock();self.stop=threading.Event();self.procs=[]
  self.mode='ok';self.drop_unit=0;self.inject_wrong=False;self.frames=[];self.rs_requests=[];self.clients=[]
  self.device,app=socket.socketpair(socket.AF_UNIX,socket.SOCK_DGRAM);self.device.settimeout(.05)
  self.can_port=self.port();self.rs_port=self.port();self.port_http=self.port(socket.SOCK_STREAM)
  self.can=self.launch('igw_can_udp',['--nodes','3','--bind','127.0.0.1','--peer','127.0.0.1','--port',str(self.can_port),'--timeout-ms','80','--test-can-fd',str(app.fileno())],app);app.close()
  def can_run():
   while not self.stop.is_set():
    try:raw=self.device.recv(64)
    except TimeoutError:continue
    except OSError:return
    ident,dlc,data=struct.unpack('=IB3x8s',raw);self.frames.append((ident,dlc,data))
    unit=ident-0x210
    if unit==self.drop_unit:continue
    out=ctypes.create_string_buffer(8)
    with self.mutex:rid=(self.zlib.zynq_can(ident,data,out,int(time.monotonic()*1e6)) if unit==3 else self.lib.nodes_can(ident,data,out,int(time.monotonic()*1e6)))
    if not rid:continue
    if self.mode=='drop_write' and data[4]==2:continue
    try:
     if self.inject_wrong:self.device.send(struct.pack('=IB3x8s',0x190+(unit%16+1),8,out.raw))
     self.device.send(struct.pack('=IB3x8s',rid,8,out.raw))
    except OSError:return
  self.thread=threading.Thread(target=can_run);self.thread.start()
  device,app=socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET)
  def rtu(req):
   self.rs_requests.append(req)
   if req[0]==self.drop_unit:return b''
   if self.mode=='rs_wrong_unit':return adu(bytes((2,3,2,0,2)))
   out=ctypes.create_string_buffer(256)
   with self.mutex:n=self.lib.nodes_rtu(req,len(req),out)
   return out.raw[:n]
  self.rs_peer=WirePeer(device,rtu)
  self.producer=None;control_args=[];cp=None
  if getattr(self,'enable_coordinated_uploads',False):
   control,cp=socket.socketpair(socket.AF_UNIX,socket.SOCK_DGRAM)
   self.accounting_state=self.base/f'accounting_{self.rs_port}.json'
   control_args=['--uplink','--test-control-fd',str(cp.fileno()),'--test-state',str(self.accounting_state)]
  self.rs=self.launch('igw_rs485_rw',['--nodes','3','--bind','127.0.0.1','--peer','127.0.0.1','--port',str(self.rs_port),'--test-fd',str(app.fileno()),'--test-lock',str(self.base/f'lock_{self.rs_port}')]+control_args,app,cp);app.close()
  if cp:
   cp.close();from accounting_fixture import UploadProducer
   self.producer=UploadProducer(control,self.rs_peer)
  for port in (self.can_port,self.rs_port):
   sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.connect(('127.0.0.1',port));sock.settimeout(.05);self.clients.append(sock)
   until=time.monotonic()+3
   while time.monotonic()<until:
    try:sock.send(b'IGW1 STATS');json.loads(sock.recv(4096));break
    except (TimeoutError,ConnectionRefusedError):time.sleep(.01)
   else:self.fail('gateway startup failed')
   sock.settimeout(.01)
   try:
    while True:sock.recv(4096)
   except TimeoutError:pass
   sock.settimeout(2)
  self.web=self.launch('igw_web',['--nodes','3','--bind','127.0.0.1','--peer','127.0.0.1','--port',str(self.port_http),'--can-port',str(self.can_port),'--rs-port',str(self.rs_port),'--udp-timeout-ms','1500'])
  until=time.monotonic()+7
  while time.monotonic()<until:
   try:
    status,self.initial=self.request('GET','/api/state')
    if status==200 and len(self.initial['devices'])==3 and all(s['valid'] for d in self.initial['devices'][:2] for s in d['buses'].values()) and self.initial['devices'][2]['buses']['CAN']['valid'] and self.initial['devices'][2]['buses']['RS485']['error']=='bus_not_supported':break
   except OSError:pass
   self.assertIsNone(self.web.poll());time.sleep(.02)
  else:self.fail('16-node sample not ready')
 def launch(self,name,args,app=None,control=None):
  p=subprocess.Popen([self.binaries[name]]+args,pass_fds=tuple(x.fileno() for x in (app,control) if x is not None),stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
  self.procs.append(p);return p
 def tearDown(self):
  for p in reversed(self.procs):
   if p.poll() is None:p.terminate()
   _,err=p.communicate(timeout=8);self.assertEqual(p.returncode,0,err.decode())
  for c in self.clients:c.close()
  if self.producer:self.producer.close()
  self.stop.set();self.thread.join(2);self.device.close();self.rs_peer.close()
  self.assertFalse(self.thread.is_alive());self.assertEqual(self.rs_peer.r[0xc],0)
 def request(self,method,path,body=None,headers=None):
  h=dict(headers or {})
  if isinstance(body,dict):body=json.dumps(body);h.setdefault('Content-Type','application/json')
  c=http.client.HTTPConnection('127.0.0.1',self.port_http,timeout=5)
  try:
   c.request(method,path,body=body,headers=h);r=c.getresponse();return r.status,json.loads(r.read())
  finally:c.close()
 def command(self,unit,op='CROSS',bus='BOTH',address=31,value=0,id=None):
  return dict(id=id or secrets.token_hex(16),unit=unit,bus=bus,op=op,address=address,value=value)
 def submit(self,q):return self.request('POST','/api/job',q,{'X-IGW-Token':self.initial['token']})
 def wait(self,q):
  until=time.monotonic()+8
  while time.monotonic()<until:
   _,r=self.request('GET','/api/job/'+q['id'])
   if r['job']['status'] in ('PASS','REVIEW'):return r['job']
   time.sleep(.01)
  self.fail('job timeout')
 def get(self,unit,addr):
  with self.mutex:return self.zlib.zynq_get(addr) if unit==3 else self.lib.nodes_get(unit,addr)
 def raw(self,bus,text):
  self.clients[bus].send(text.encode());return json.loads(self.clients[bus].recv(4096))
 def stop_web(self):self.web.terminate();self.assertEqual(self.web.wait(6),0)
 def test_can_only_third_node_healthy_and_no_rs485_poll(self):
  d=self.initial['devices'];self.assertEqual(len(d),3)
  self.assertEqual(d[2]['buses']['CAN']['values'][1],3)
  self.assertFalse(d[2]['buses']['RS485']['valid'])
  self.assertEqual(d[2]['buses']['RS485']['error'],'bus_not_supported')
  self.assertTrue(all(q[0]!=3 for q in self.rs_requests),self.rs_requests)
  write=self.command(3,op='WRITE',bus='CAN',address=16,value=4567)
  self.assertEqual(self.submit(write)[0],202)
  done=self.wait(write);self.assertEqual((done['status'],done['readback']),('PASS',4567),done)
  self.assertEqual((self.get(1,16),self.get(2,16),self.get(3,16)),(0,0,4567))
  for bus,op in [('RS485','READ'),('RS485','WRITE'),('BOTH','CROSS')]:
   q=self.command(3,op=op,bus=bus,address=16,value=7 if op=='WRITE' else 0)
   status,res=self.submit(q);self.assertEqual((status,res['error']),(400,'bus_not_supported'))
  for unit in (1,2):
   q=self.command(unit);self.assertEqual(self.submit(q)[0],202)
   self.assertEqual(self.wait(q)['status'],'PASS')
  time.sleep(3.1)
  self.assertTrue(all(q[0]!=3 for q in self.rs_requests),self.rs_requests)
  _,s=self.request('GET','/api/evidence');self.assertEqual(s['device_count'],3)
  self.assertEqual(s['devices'][2]['buses']['RS485']['error'],'bus_not_supported')
  self.assertNotIn('token',s)
  print('PASS_CAN3_HOST_AND_ZYNQ_SIMULATED_INTEGRATION: node3 CAN, no node3 RS485 polling, API gate, nodes1/2 cross')

if __name__=='__main__':unittest.main()
