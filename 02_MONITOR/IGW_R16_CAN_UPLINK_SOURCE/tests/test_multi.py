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
  subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-DIGW_NODE_COUNT=16','-I'+str(ROOT/'slave_r16/slave_src'),str(ROOT/'tests/nodes_bridge.c'),'-o',so],check=True)
  cls.lib=ctypes.CDLL(so)
  cls.lib.nodes_can.argtypes=[ctypes.c_uint,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint64]
  cls.lib.nodes_rtu.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p]
  cls.lib.nodes_get.argtypes=[ctypes.c_uint,ctypes.c_uint]
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 @staticmethod
 def port(kind=socket.SOCK_DGRAM):
  with socket.socket(socket.AF_INET,kind) as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
 def setUp(self):
  self.lib.nodes_reset();self.mutex=threading.Lock();self.stop=threading.Event();self.procs=[]
  self.mode='ok';self.drop_unit=0;self.inject_wrong=False;self.frames=[];self.clients=[]
  self.device,app=socket.socketpair(socket.AF_UNIX,socket.SOCK_DGRAM);self.device.settimeout(.05)
  self.can_port=self.port();self.rs_port=self.port();self.port_http=self.port(socket.SOCK_STREAM)
  self.can=self.launch('igw_can_udp',['--nodes','16','--bind','127.0.0.1','--peer','127.0.0.1','--port',str(self.can_port),'--timeout-ms','80','--test-can-fd',str(app.fileno())],app);app.close()
  def can_run():
   while not self.stop.is_set():
    try:raw=self.device.recv(64)
    except TimeoutError:continue
    except OSError:return
    ident,dlc,data=struct.unpack('=IB3x8s',raw);self.frames.append((ident,dlc,data))
    unit=ident-0x210
    if unit==self.drop_unit:continue
    out=ctypes.create_string_buffer(8)
    with self.mutex:rid=self.lib.nodes_can(ident,data,out,int(time.monotonic()*1e6))
    if not rid:continue
    if self.mode=='drop_write' and data[4]==2:continue
    try:
     if self.inject_wrong:self.device.send(struct.pack('=IB3x8s',0x190+(unit%16+1),8,out.raw))
     self.device.send(struct.pack('=IB3x8s',rid,8,out.raw))
    except OSError:return
  self.thread=threading.Thread(target=can_run);self.thread.start()
  device,app=socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET)
  def rtu(req):
   if req[0]==self.drop_unit:return b''
   if self.mode=='rs_wrong_unit':return adu(bytes((2,3,2,0,2)))
   out=ctypes.create_string_buffer(256)
   with self.mutex:n=self.lib.nodes_rtu(req,len(req),out)
   return out.raw[:n]
  self.rs_peer=WirePeer(device,rtu)
  self.rs=self.launch('igw_rs485_rw',['--nodes','16','--bind','127.0.0.1','--peer','127.0.0.1','--port',str(self.rs_port),'--test-fd',str(app.fileno()),'--test-lock',str(self.base/f'lock_{self.rs_port}')],app);app.close()
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
  self.web=self.launch('igw_web',['--nodes','16','--bind','127.0.0.1','--peer','127.0.0.1','--port',str(self.port_http),'--can-port',str(self.can_port),'--rs-port',str(self.rs_port),'--udp-timeout-ms','1500'])
  until=time.monotonic()+7
  while time.monotonic()<until:
   try:
    status,self.initial=self.request('GET','/api/state')
    if status==200 and all(s['valid'] for d in self.initial['devices'] for s in d['buses'].values()):break
   except OSError:pass
   self.assertIsNone(self.web.poll());time.sleep(.02)
  else:self.fail('16-node sample not ready')
 def launch(self,name,args,app=None):
  p=subprocess.Popen([self.binaries[name]]+args,pass_fds=(app.fileno(),) if app else (),stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
  self.procs.append(p);return p
 def tearDown(self):
  for p in reversed(self.procs):
   if p.poll() is None:p.terminate()
   _,err=p.communicate(timeout=8);self.assertEqual(p.returncode,0,err.decode())
  for c in self.clients:c.close()
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
  with self.mutex:return self.lib.nodes_get(unit,addr)
 def raw(self,bus,text):
  self.clients[bus].send(text.encode());return json.loads(self.clients[bus].recv(4096))
 def stop_web(self):self.web.terminate();self.assertEqual(self.web.wait(6),0)
 def test_16_nodes_sampled_and_cross_protocol_banks_are_independent(self):
  self.assertEqual(self.initial['device_count'],16)
  for d in self.initial['devices']:
   self.assertTrue(all(b['values'][1]==d['unit'] for b in d['buses'].values()))
  for unit in range(1,17):
   q=self.command(unit);self.assertEqual(self.submit(q)[0],202);j=self.wait(q)
   self.assertEqual((j['status'],j['unit'],j['write_count_before'],j['write_count_after']),('PASS',unit,0,2),j)
   self.assertEqual(len(j['trace']),10)
   for t in j['trace']:
    reply=t['reply'];self.assertEqual(reply['unit'],unit)
    if t['bus']=='CAN':self.assertEqual((reply['request_can_id'],reply['reply_can_id']),(0x210+unit,0x190+unit))
   self.assertEqual(self.get(unit,31),65535)
   if unit<16:self.assertEqual(self.get(unit+1,31),0)
  _,e=self.request('GET','/api/evidence');self.assertNotIn('token',e);self.assertEqual(e['software_revision'],'R16_CAN_UPLINK');self.assertEqual(e['npu_scope_unit'],1)
 def test_same_job_id_on_other_node_is_conflict_and_never_written(self):
  q=self.command(1,op='WRITE',bus='CAN',value=1234);self.submit(q);j=self.wait(q);self.assertEqual(j['status'],'PASS')
  self.assertEqual(self.submit(q),(200,{'job':j}))
  status,r=self.submit(dict(q,unit=2));self.assertEqual((status,r['error']),(409,'job_id_conflict'));self.assertEqual(self.get(2,5),0)
 def test_actual_pc_acceptance_cli_and_independent_evidence_audit(self):
  out=self.base/f'accept_{self.port_http}'
  result=subprocess.run([sys.executable,str(ROOT/'pc/accept_multi.py'),'--host','127.0.0.1','--port',str(self.port_http),'--nodes','2','--output',str(out)],capture_output=True,text=True,timeout=20)
  self.assertEqual(result.returncode,0,result.stdout+result.stderr)
  evidence=json.loads((out/'evidence.json').read_text());self.assertNotIn('token',evidence)
  self.assertEqual((self.get(1,16),self.get(2,16)),(1111,2222))
  sys.path.insert(0,str(ROOT/'tools'));from audit_multi import audit
  self.assertEqual(audit(evidence)['result'],'PASS_LOGICAL_MULTI_NODE_EVIDENCE')
  # The auditor must reject a forged raw reply even if the job claims PASS.
  bad=json.loads(json.dumps(evidence));bad['jobs'][0]['trace'][0]['reply']['reply_can_id']+=1
  with self.assertRaises(AssertionError):audit(bad)
 def test_npu_input_remains_node1_when_node2_is_unavailable(self):
  from test_npu import FakeNpu
  self.stop_web();fake=FakeNpu();self.drop_unit=2
  self.web=self.launch('igw_web',['--nodes','16','--npu-port',str(fake.port),'--bind','127.0.0.1','--peer','127.0.0.1','--port',str(self.port_http),'--can-port',str(self.can_port),'--rs-port',str(self.rs_port),'--udp-timeout-ms','1500'])
  try:
   until=time.monotonic()+27
   while time.monotonic()<until:
    try:
     _,s=self.request('GET','/api/evidence')
     if s['npu']['total']:break
    except OSError:pass
    time.sleep(.05)
   else:self.fail('node1 inference did not progress with node2 absent')
   self.assertEqual(s['npu_scope_unit'],1);w=s['npu']['history'][-1]
   self.assertEqual(w['reply']['status'],'ok');self.assertEqual(w['features'][1],0);self.assertEqual(w['features'][9],0)
   self.assertTrue(all(row[0]['status']=='ok' and row[1]['status']=='ok' for row in w['samples']))
   self.assertFalse(s['devices'][1]['buses']['CAN']['valid']);self.assertFalse(s['devices'][1]['buses']['RS485']['valid'])
  finally:fake.close()
 def test_node_bounds_and_broadcasts_do_not_transmit(self):
  self.stop_web();before=(len(self.frames),self.rs_peer.kicks)
  for unit in (0,17,247,4294967295):
   self.assertEqual(self.raw(0,f'IGW2 CAN WRITE 1 {unit} 16 7')['status'],'bad_request')
   self.assertEqual(self.raw(1,f'IGW2 RS485 WRITE 1 {unit} 16 7')['status'],'bad_request')
  self.assertEqual((len(self.frames),self.rs_peer.kicks),before)
 def test_same_udp_sequence_on_distinct_nodes_has_independent_cache(self):
  self.stop_web()
  for bus,name in [(0,'CAN'),(1,'RS485')]:
   first=self.raw(bus,f'IGW2 {name} WRITE 100000 1 16 1234')
   second=self.raw(bus,f'IGW2 {name} WRITE 100000 2 16 4321')
   self.assertEqual((first['status'],second['status']),('ok','ok'))
   self.assertEqual(self.raw(bus,f'IGW2 {name} WRITE 100000 1 16 1234'),first)
  self.assertEqual((self.get(1,16),self.get(2,16)),(1234,4321));self.assertEqual((self.get(1,5),self.get(2,5)),(2,2))
 def test_wrong_node_can_reply_is_ignored_even_if_sequence_matches(self):
  self.stop_web();self.inject_wrong=True
  for unit in (1,2,16):
   r=self.raw(0,f'IGW2 CAN READ {80000+unit} {unit} 1')
   self.assertEqual((r['status'],r['unit'],r['registers']),('ok',unit,[unit]));self.assertGreaterEqual(r['stats']['ignored'],1)
 def test_wrong_node_short_rs_reply_is_ignored_until_original_deadline(self):
  self.stop_web();self.mode='rs_wrong_unit'
  baseline=self.raw(1,'IGW1 STATS')['stats']['ok']
  r=self.raw(1,'IGW2 RS485 READ 89000 1 0 8')
  self.assertEqual(r['status'],'timeout');self.assertEqual(r['stats']['ok'],baseline)
  self.assertGreaterEqual(r['stats']['ignored'],1);self.assertEqual(r['registers'],[])
 def test_lost_write_ack_is_not_retried_and_other_node_still_works(self):
  self.stop_web();self.mode='drop_write'
  first=self.raw(0,'IGW2 CAN WRITE 90000 2 16 2345');self.assertEqual((first['status'],first['execution']),('timeout','unknown'))
  self.assertEqual(self.get(2,5),1);self.assertEqual(self.raw(0,'IGW2 CAN WRITE 90000 2 16 2345'),first);self.assertEqual(self.get(2,5),1)
  self.mode='ok';healthy=self.raw(0,'IGW2 CAN READ 90001 1 1');self.assertEqual((healthy['status'],healthy['registers']),('ok',[1]))
  self.assertEqual(self.raw(0,'IGW2 CAN READ 90002 2 1')['status'],'cooldown')
 def test_absent_node_does_not_replace_healthy_node_data(self):
  self.drop_unit=2;until=time.monotonic()+9
  while time.monotonic()<until:
   _,s=self.request('GET','/api/evidence');d=s['devices']
   if d[1]['buses']['CAN']['error']=='gateway_timeout' and d[1]['buses']['RS485']['error']=='gateway_timeout':break
   time.sleep(.05)
  else:self.fail('missing-node window not observed')
  self.assertTrue(d[0]['buses']['CAN']['valid']);self.assertTrue(d[0]['buses']['RS485']['valid'])
  self.assertTrue(all(v is None for v in d[1]['buses']['CAN']['values']))
  q=self.command(1,op='WRITE',bus='RS485',value=4567);self.assertEqual(self.submit(q)[0],202);self.assertEqual(self.wait(q)['status'],'PASS')

if __name__=='__main__':unittest.main()
