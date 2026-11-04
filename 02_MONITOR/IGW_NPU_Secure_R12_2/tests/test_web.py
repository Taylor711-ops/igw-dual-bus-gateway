"""Real C HTTP service -> real R9 C UDP gateways -> C slave register protocol.
UNIX socket bus/MMIO surrogates; not hardware or latency benchmarks.
"""
import concurrent.futures
import http.client
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import sys
import tempfile
import struct
import shlex
import hashlib
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
import threading
import time
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'baseline_r9/tests'))
import test_can_rw as baseline

class WebTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory();cls.base=Path(cls.temp.name);cls.binary=str(cls.base/'web')
  flags=['gcc','-std=c11','-O2','-Wall','-Wextra','-Wmissing-field-initializers','-Werror','-pthread']
  if os.environ.get('IGW_WEB_SANITIZE'):flags+=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
  subprocess.run(flags+[str(ROOT/'src/igw_web.c'),'-lm','-o',cls.binary],check=True)
  subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Wmissing-field-initializers','-Werror','-pthread','-static',str(ROOT/'src/igw_web.c'),'-lm','-o',str(cls.base/'web_static')],check=True)
  for name in ('igw_can_udp.c','igw_rs485_rw.c','rw_wire.h'):
   assert (ROOT/'src'/name).read_bytes()==(ROOT/'baseline_r9/src'/name).read_bytes()
  cls.vault_binary=str(cls.base/'vault')
  subprocess.run(flags+[str(ROOT/'src/igw_vault.c'),'-ldl','-o',cls.vault_binary],check=True)
  baseline.CanRWTests.setUpClass()
 @classmethod
 def tearDownClass(cls):baseline.CanRWTests.tearDownClass();cls.temp.cleanup()
 def setUp(self):
  self.fixture=baseline.CanRWTests('test_standalone_can_counter_wrap_and_full_u16_range');self.fixture.setUp();self.fixture.launch_can();self.fixture.launch_rs()
  with socket.socket() as s:s.bind(('127.0.0.1',0));self.port=s.getsockname()[1]
  self.logfile=(self.base/f'web_{self.port}.log').open('w+')
  self.archive_client,archive_peer=socket.socketpair()
  self.proc=subprocess.Popen([self.binary,'--archive-fd',str(archive_peer.fileno()),'--bind','127.0.0.1','--peer','127.0.0.1','--port',str(self.port),'--can-port',str(self.fixture.can_port),'--rs-port',str(self.fixture.rs_port),'--udp-timeout-ms','1500'],stdout=self.logfile,stderr=subprocess.STDOUT,pass_fds=(archive_peer.fileno(),))
  archive_peer.close()
  end=time.monotonic()+5
  while time.monotonic()<end:
   try:
    code,state=self.request('GET','/api/state')
    if code==200 and state['buses']['CAN']['valid'] and state['buses']['RS485']['valid']:self.initial=state;return
   except (OSError,http.client.HTTPException):pass
   self.assertIsNone(self.proc.poll());time.sleep(.02)
  self.fail('HTTP not ready '+self.log_text())
 def log_text(self):self.logfile.flush();return Path(self.logfile.name).read_text()
 def tearDown(self):
  if self.proc.poll() is None:self.proc.terminate()
  self.assertEqual(self.proc.wait(6),0,self.log_text());self.logfile.close();self.archive_client.close();self.fixture.tearDown()
 def request(self,method,path,body=None,headers=None):
  h=dict(headers or {})
  if isinstance(body,dict):body=json.dumps(body);h.setdefault('Content-Type','application/json')
  c=http.client.HTTPConnection('127.0.0.1',self.port,timeout=4)
  try:
   c.request(method,path,body=body,headers=h);r=c.getresponse();raw=r.read();return r.status,json.loads(raw) if 'application/json' in r.getheader('Content-Type','') else raw.decode()
  finally:c.close()
 def cmd(self,bus='CAN',op='WRITE',address=16,value=1234):return {'id':secrets.token_hex(16),'bus':bus,'op':op,'address':address,'value':value}
 def submit(self,cmd):return self.request('POST','/api/job',cmd,{'X-IGW-Token':self.initial['token']})
 def wait(self,cmd):
  end=time.monotonic()+5
  while time.monotonic()<end:
   code,result=self.request('GET','/api/job/'+cmd['id']);self.assertEqual(code,200,result)
   if result['job']['status'] in ('PASS','REVIEW'):return result['job']
   time.sleep(.02)
  self.fail('job did not finish')
 def test_page_state_export_and_no_unsolicited_write(self):
  code,page=self.request('GET','/');self.assertEqual(code,200);self.assertIn('工业边缘网关',page);self.assertIn('写入并读回',page)
  self.assertEqual(self.fixture.lib.bridge_get(5),0)
  code,state=self.request('GET','/api/state');self.assertEqual(code,200);self.assertTrue(state['buses']['CAN']['valid']);self.assertEqual(state['buses']['CAN']['values'][8],0x0901)
  self.assertEqual(state['buses']['RS485']['values'][4],0x0801)
  code,evidence=self.request('GET','/api/evidence');self.assertEqual(code,200);self.assertNotIn('token',evidence);self.assertEqual(evidence['session'],state['session'])
  for _ in range(10):self.request('GET','/api/state')
  self.assertEqual(self.fixture.lib.bridge_get(5),0)
 def test_can_and_rs485_manual_write_have_independent_readback(self):
  for bus,value in [('CAN',12345),('RS485',65535)]:
   cmd=self.cmd(bus=bus,address=31,value=value);code,result=self.submit(cmd);self.assertEqual(code,202,result)
   j=self.wait(cmd);self.assertEqual((j['status'],j['execution'],j['readback']),('PASS','verified',value));self.assertEqual(j['write_count_after'],j['write_count_before']+1);self.assertGreater(len(j['trace']),4)
  self.assertEqual(self.fixture.lib.bridge_get(31),65535)
 def test_50_cross_rounds_and_evidence_raw_replies(self):
  for i in range(50):
   cmd=self.cmd(bus='BOTH',op='CROSS',address=16+i%16,value=0);self.assertEqual(self.submit(cmd)[0],202)
   j=self.wait(cmd);self.assertEqual(j['status'],'PASS',j);self.assertEqual(j['write_count_after'],2*(i+1));self.assertEqual(len(j['trace']),10)
  code,evidence=self.request('GET','/api/evidence');self.assertEqual(code,200);self.assertEqual(len(evidence['jobs']),50)
  for j in evidence['jobs']:
   self.assertEqual(j['readback'],j['rs485_written']);self.assertTrue(all(t['reply']['status']=='ok' for t in j['trace']))
  self.assertEqual(self.fixture.lib.bridge_get(5),100)
 def test_http_duplicate_id_replays_without_writing_and_conflict_refused(self):
  cmd=self.cmd();self.submit(cmd);first=self.wait(cmd);count=self.fixture.lib.bridge_get(5)
  code,r=self.submit(cmd);self.assertEqual(code,200);self.assertEqual(r['job'],first);self.assertEqual(self.fixture.lib.bridge_get(5),count)
  different=dict(cmd,value=1235);code,r=self.submit(different);self.assertEqual((code,r['error']),(409,'job_id_conflict'));self.assertEqual(self.fixture.lib.bridge_get(5),count)
 def test_unchanged_value_does_not_send_redundant_write(self):
  cmd=self.cmd(value=0);self.submit(cmd);j=self.wait(cmd)
  self.assertEqual((j['status'],j['execution'],j['write_count_after']),('PASS','unchanged',0));self.assertEqual(self.fixture.lib.bridge_get(5),0)
 def test_readonly_and_invalid_json_are_rejected_before_write(self):
  cases=[dict(self.cmd(),address=15),dict(self.cmd(),value=65536),dict(self.cmd(),address=32),dict(self.cmd(),bus='OTHER'),dict(self.cmd(),op='PUSH'),dict(self.cmd(),extra=1),dict(self.cmd(),value=True),dict(self.cmd(),address=-1)]
  for cmd in cases:self.assertEqual(self.submit(cmd)[0],400)
  for body in ['{"id":"a","id":"b"}','{}','[]','{"value":1e3}','{"value":01}','{"value":18446744073709551616}']:
   self.assertEqual(self.request('POST','/api/job',body,{'X-IGW-Token':self.initial['token'],'Content-Type':'application/json'})[0],400)
  self.assertEqual(self.fixture.lib.bridge_get(5),0)
 def test_missing_token_foreign_origin_wrong_host_no_write(self):
  cmd=self.cmd()
  self.assertEqual(self.request('POST','/api/job',cmd)[0],403)
  self.assertEqual(self.request('POST','/api/job',cmd,{'X-IGW-Token':'bad'})[0],403)
  self.assertEqual(self.request('POST','/api/job',cmd,{'X-IGW-Token':self.initial['token'],'Origin':'http://foreign.invalid'})[0],403)
  self.assertEqual(self.request('POST','/api/job',cmd,{'X-IGW-Token':self.initial['token'],'Host':'foreign.invalid'})[0],421)
  self.assertEqual(self.request('GET','/api/job/'+cmd['id'])[0],404)
  self.assertEqual(self.fixture.lib.bridge_get(5),0)
 def test_partial_http_body_and_duplicate_content_length(self):
  cmd=self.cmd(value=4321);body=json.dumps(cmd).encode();header=(f'POST /api/job HTTP/1.1\r\nHost: 127.0.0.1:{self.port}\r\nContent-Length: {len(body)}\r\nContent-Type: application/json\r\nX-IGW-Token: {self.initial["token"]}\r\n\r\n').encode()
  with socket.create_connection(('127.0.0.1',self.port),timeout=3) as s:
   s.sendall(header+body[:5]);time.sleep(.05);s.sendall(body[5:]);response=b''
   while True:
    b=s.recv(4096)
    if not b:break
    response+=b
  self.assertTrue(response.startswith(b'HTTP/1.1 202'),response);self.assertEqual(self.wait(cmd)['status'],'PASS')
  bad=header.replace(b'Content-Length:',b'Content-Length: 0\r\nContent-Length:')
  with socket.create_connection(('127.0.0.1',self.port),timeout=3) as s:
   s.sendall(bad+body);self.assertTrue(s.recv(1024).startswith(b'HTTP/1.1 400'))
 def test_multiple_status_clients_and_slow_client_do_not_block(self):
  with socket.create_connection(('127.0.0.1',self.port),timeout=3) as slow:
   slow.sendall(b'GET /api/state HTTP/1.1\r\nHost: ')
   with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    results=list(pool.map(lambda _:self.request('GET','/api/state')[0],range(12)))
   self.assertEqual(results,[200]*12)
  self.assertEqual(self.fixture.lib.bridge_get(5),0)
 def test_fake_ack_is_not_verified_write(self):
  self.fixture.mode='echo_only';cmd=self.cmd(value=1234);self.submit(cmd);j=self.wait(cmd)
  self.assertEqual(j['status'],'REVIEW');self.assertEqual(j['error'],'independent_readback_or_count');self.assertEqual(j['execution'],'unknown_or_unverified')
 def test_lost_write_reply_is_unknown_no_retry_same_http_id(self):
  self.fixture.mode='drop';cmd=self.cmd(value=1234);self.submit(cmd);j=self.wait(cmd)
  self.assertEqual(j['status'],'REVIEW');self.assertEqual(j['execution'],'unknown_or_unverified');self.assertEqual(self.fixture.lib.bridge_get(16),1234)
  code,r=self.submit(cmd);self.assertEqual(code,200);self.assertEqual(r['job'],j);self.assertEqual(self.fixture.lib.bridge_get(5),1)
  self.fixture.mode='ok';time.sleep(3)
  nextcmd=self.cmd(op='READ',value=0);self.submit(nextcmd);nextjob=self.wait(nextcmd);self.assertEqual((nextjob['status'],nextjob['readback']),('PASS',1234))
 def test_can_failure_does_not_prevent_rs485_write(self):
  self.fixture.procs[0].terminate();self.assertEqual(self.fixture.procs[0].wait(2),0)
  cmd=self.cmd(bus='RS485',value=4567);self.submit(cmd);j=self.wait(cmd);self.assertEqual((j['status'],j['readback']),('PASS',4567),j)
 def test_pending_command_returns_busy_and_read_has_no_write(self):
  # Holding CAN replies makes the first command remain queued/running.
  self.fixture.mode='drop';cmd=self.cmd(value=7);self.submit(cmd)
  other=self.cmd(bus='RS485',value=8);code,r=self.submit(other);self.assertEqual((code,r['error']),(409,'worker_busy'))
  self.wait(cmd);self.assertEqual(self.fixture.lib.bridge_get(5),1)
 def test_periodic_can_rs485_sampling_overlaps(self):
  entered=threading.Event();release=threading.Event();original=self.fixture.bridge_can
  def held(request,now=0):
   if not entered.is_set():entered.set();release.wait(1)
   return original(request,now)
  self.fixture.bridge_can=held;before=self.fixture.rs_peer.kicks
  try:
   self.assertTrue(entered.wait(4),'next CAN sampling did not start')
   deadline=time.monotonic()+.25
   while self.fixture.rs_peer.kicks==before and time.monotonic()<deadline:time.sleep(.005)
   self.assertGreater(self.fixture.rs_peer.kicks,before,'RS485 must progress while CAN sample is held')
  finally:release.set()
 def test_direct_read_and_new_clock_session(self):
  cmd=self.cmd(op='READ',address=0,value=0);self.submit(cmd);j=self.wait(cmd)
  self.assertEqual((j['status'],j['readback'],j['execution']),('PASS',0x1234,'read'));self.assertEqual(self.fixture.lib.bridge_get(5),0)
  self.assertEqual(len(self.initial['session']),32);self.assertNotEqual(self.initial['session'],self.initial['token'])

 def test_private_archive_export_has_raw_traces_and_no_control_token(self):
  cmd=self.cmd();self.submit(cmd);self.assertEqual(self.wait(cmd)['status'],'PASS')
  self.archive_client.settimeout(3);self.archive_client.sendall(b'R')
  def receive(n):
   out=b''
   while len(out)<n:
    part=self.archive_client.recv(n-len(out));self.assertTrue(part);out+=part
   return out
  n=struct.unpack('>Q',receive(8))[0];data=json.loads(receive(n));self.assertNotIn('token',data);self.assertEqual(data['session'],self.initial['session']);self.assertTrue(data['jobs'][0]['trace'])
  self.assertEqual(self.fixture.lib.bridge_get(5),1)
 def test_live_web_to_encrypted_archive_and_final_stop(self):
  self.proc.terminate();self.assertEqual(self.proc.wait(6),0)
  run=self.base/('secure-'+str(self.port));run.mkdir();script=run/'web.sh'
  args=[self.binary,'--archive-fd','3','--bind','127.0.0.1','--peer','127.0.0.1','--port',str(self.port),'--can-port',str(self.fixture.can_port),'--rs-port',str(self.fixture.rs_port),'--udp-timeout-ms','1500']
  script.write_text('#!/bin/sh\nexec '+shlex.join(args)+'\n')
  password=b'Integration-fixture-339!';rd,wr=os.pipe();os.write(wr,password+b'\n'+password+b'\n');os.close(wr)
  self.proc=subprocess.Popen([self.vault_binary,'serve','--vault',str(run/'vault'),'--run-script',str(script),'--interval','1','--pass-fd',str(rd)],pass_fds=(rd,),stdout=self.logfile,stderr=subprocess.STDOUT);os.close(rd)
  until=time.monotonic()+8
  while time.monotonic()<until:
   try:
    code,state=self.request('GET','/api/state')
    if code==200 and state['buses']['CAN']['valid']:self.initial=state;break
   except OSError:pass
   time.sleep(.05)
  else:self.fail(self.log_text())
  cmd=self.cmd(bus='BOTH',op='CROSS',address=31,value=0);self.assertEqual(self.submit(cmd)[0],202);self.assertEqual(self.wait(cmd)['status'],'PASS')
  time.sleep(1.7);self.proc.terminate();self.assertEqual(self.proc.wait(12),0,self.log_text());records=[]
  for f in (run/'vault').glob('igw-*.igwe'):
   b=f.read_bytes();key=hashlib.pbkdf2_hmac('sha256',password,b[16:32],int.from_bytes(b[12:16],'big'),32);records.append(json.loads(AESGCM(key).decrypt(b[32:44],b[64:],b[:64])))
  self.assertGreaterEqual(len(records),2);self.assertTrue(any(any(j['id']==cmd['id'] and j['status']=='PASS' and len(j['trace'])==10 for j in x['jobs']) for x in records));self.assertTrue(all('token' not in x for x in records));self.assertEqual(self.fixture.lib.bridge_get(5),2)
