"""Production C NPU worker against an independent Python numeric UDP fixture.
Host software checks only. Actual strict Alnpu execution is pending target tests.
"""
import json,math,os,socket,subprocess,tempfile,threading,time,unittest
from pathlib import Path
R=Path(__file__).resolve().parents[1]
MODEL=json.loads((R/'npu/generated/model_quantized.json').read_text())
def rounded(x):return math.floor(x+.5) if x>=0 else math.ceil(x-.5)
def sat(x):return max(0,min(255,x))
def reference(features):
 raw=[rounded((f-m)/s/.0625)+128 for f,m,s in zip(features,MODEL['mean'],MODEL['std'])]
 x=list(map(sat,raw));h=[sat(rounded((bias+sum((v-128)*(w-128) for v,w in zip(x,weights)))*.015625)) for weights,bias in zip(MODEL['weight1']['bytes'],MODEL['bias1']['values'])]
 y=[sat(rounded((bias+sum(v*(w-128) for v,w in zip(h,weights)))*.0078125)+128) for weights,bias in zip(MODEL['weight2']['bytes'],MODEL['bias2']['values'])]
 return x,y,sum(v<0 or v>255 for v in raw)
class FakeNpu:
 def __init__(self,mode='ok'):
  self.mode=mode;self.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);self.sock.bind(('127.0.0.1',0));self.port=self.sock.getsockname()[1];self.sock.settimeout(.1);self.stop=False;self.count=0;self.features=[];self.failure=None
  self.thread=threading.Thread(target=self.run);self.thread.start()
 def run(self):
  while not self.stop:
   try:raw,peer=self.sock.recvfrom(2048)
   except socket.timeout:continue
   except OSError:break
   try:
    p=raw.decode().split();assert p[0]=='IGWNPU1';hello=p[1]=='HELLO'
    d=dict(v=1,type='npu_reply',session=p[2],id=0 if hello else int(p[3]),server_id='1234567890abcdef',model_id=MODEL['model_id'],backend='Alnpu')
    if hello:d.update(status='ready',self_test=True,hardware_layers=2,inference_count=self.count)
    else:
     assert p[4]=='1234567890abcdef';features=list(map(float,p[5:]));assert len(features)==16
     self.features.append(features);x,cpu,clip=reference(features);actual=cpu.copy();self.count+=1
     if self.mode=='numeric_review':actual[0]=(actual[0]+32)%256
     pred=max(range(3),key=actual.__getitem__);cp=max(range(3),key=cpu.__getitem__);diff=[abs(a-b) for a,b in zip(actual,cpu)];bad=sum(v>t for v,t in zip(diff,MODEL['logit_lsb_tolerance']))
     d.update(status='ok' if bad==0 and pred==cp else 'review',**{'class':pred},label=MODEL['labels'][pred],cpu_class=cp,logits=actual,cpu_logits=cpu,input_u8=x,clipped_dims=clip,max_diff=max(diff),outside_budget=bad,margin_lsb=sorted(actual)[-1]-sorted(actual)[-2],logit_endpoints=sum(v in (0,255) for v in actual),enqueue_us=123,inference_count=self.count)
     if self.mode=='wrong_input':d['input_u8'][0]^=1
     if self.mode=='wrong_class':d['class']=(pred+1)%3
     if self.mode=='forged_numeric':d['max_diff']=0;d['logits'][0]^=64
     if self.mode=='wrong_id':d['id']+=1
     if self.mode=='wrong_count':d['inference_count']=0
    if self.mode=='wrong_model':d['model_id']='f'*64
    if self.mode=='cpu_backend':d['backend']='CpuRef'
    if self.mode=='no_selftest':d['self_test']=False
    if self.mode=='one_layer':d['hardware_layers']=1
    self.sock.sendto(json.dumps(d,separators=(',',':')).encode(),peer)
   except Exception as e:self.failure=e;break
 def close(self):self.stop=True;self.thread.join(2);self.sock.close();assert not self.failure,self.failure
class NpuTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();cls.binary=str(Path(cls.tmp.name)/'driver')
  flags=['gcc','-std=c11','-O2','-Wall','-Wextra','-Wmissing-field-initializers','-Werror','-pthread']
  if os.environ.get('IGW_WEB_SANITIZE'):flags+=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
  subprocess.run(flags+[str(R/'tests/npu_driver.c'),'-lm','-o',cls.binary],check=True)
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def launch(self,mode='ok'):
  self.service=FakeNpu(mode);self.p=subprocess.Popen([self.binary,str(self.service.port)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 def query(self):
  self.p.stdin.write('state\n');self.p.stdin.flush();return json.loads(self.p.stdout.readline())
 def samples(self,rows=None):
  if rows is None:rows=[(0,1000+i*100,500+i*10,0,42000+i*100,41000+i*100) for i in range(5)]
  for row in rows:self.p.stdin.write('sample '+' '.join(map(str,row))+'\n');self.p.stdin.flush();self.assertEqual(self.p.stdout.readline().strip(),'sampled')
 def finished(self):
  deadline=time.monotonic()+2
  while time.monotonic()<deadline:
   s=self.query();n=s['npu']
   if n['total'] or n['rejected']:return s
   time.sleep(.02)
  self.fail('NPU window not completed')
 def test_five_round_numeric_reference_and_explicit_transfer_contract(self):
  self.launch();self.samples();s=self.finished();n=s['npu'];self.assertEqual(n['total'],1);self.assertEqual(n['classification_validation'],'PENDING_BOARD_DOMAIN_TEST');self.assertEqual(n['feature_contract'],'ps_snapshot_window_r12_transfer')
  h=n['history'][0];self.assertEqual((h['first_generation'],h['last_generation']),(1,5));self.assertEqual(h['reply']['status'],'ok');self.assertNotIn('token',s)
  expected=[1,0,0,1.2,1.38,.52,.538,0,1,0,0,42.2,42.38,41.2,41.38,0]
  for a,b in zip(h['features'],expected):self.assertAlmostEqual(a,b,places=10)
  self.assertEqual(len(h['samples']),5);self.assertEqual(h['reply']['input_u8'],reference(expected)[0])
 def test_nonoverlapping_windows(self):
  self.launch();self.samples();self.finished();self.samples();deadline=time.monotonic()+2
  while time.monotonic()<deadline:
   n=self.query()['npu']
   if n['total']==2:break
   time.sleep(.02)
  self.assertEqual(n['total'],2);self.assertEqual([(r['first_generation'],r['last_generation']) for r in n['history']],[(1,5),(6,10)])
 def test_timeout_cooldown_features_and_no_unknown_network_fault_label(self):
  self.launch();self.samples([(1,250000,0,0,42000,41000),(2,800,0,0,42000,41000),(0,1000,500,0,42000,41000),(1,250000,0,0,42000,41000),(0,1200,600,0,42000,41000)])
  n=self.finished()['npu'];self.assertEqual(n['total'],1);f=n['history'][0]['features'];self.assertEqual(f[:3],[.4,.4,.2]);self.assertAlmostEqual(f[7],500800/3000)
  self.samples([(3,700000,0,0,42000,41000)])
  n=self.query()['npu'];self.assertEqual(n['total'],1);self.assertEqual(n['status'],'unknown_sample_window_reset');self.assertEqual(n['window_filled'],0)
 def test_numeric_review_is_retained_but_not_reported_as_pass(self):
  self.launch('numeric_review');self.samples();n=self.finished()['npu'];self.assertEqual(n['total'],1);self.assertEqual(n['history'][0]['reply']['status'],'review');self.assertEqual(n['status'],'numeric_review')
 def test_reject_malformed_or_forged_inference(self):
  # Subtests use an independent process and independently computed frozen model.
  for mode in ['wrong_input','wrong_class','forged_numeric','wrong_id','wrong_count']:
   with self.subTest(mode=mode):
    self.launch(mode);self.samples();n=self.finished()['npu'];self.assertEqual(n['total'],0);self.assertGreater(n['rejected'],0)
    out,err=self.p.communicate('stop\n',timeout=4);self.assertEqual(self.p.returncode,0,err);self.service.close();del self.p
 def test_reject_wrong_model_cpu_fallback_and_unverified_startup(self):
  for mode in ['wrong_model','cpu_backend','no_selftest','one_layer']:
   with self.subTest(mode=mode):
    self.launch(mode);self.samples();n=self.finished()['npu'];self.assertEqual(n['total'],0);self.assertEqual(n['status'],'npu_hello_failed')
    out,err=self.p.communicate('stop\n',timeout=4);self.assertEqual(self.p.returncode,0,err);self.service.close();del self.p
 def test_missing_npu_never_produces_valid_result(self):
  self.launch();self.service.close();self.samples();n=self.finished()['npu'];self.assertEqual(n['total'],0);self.assertEqual(n['status'],'npu_hello_failed');del self.service
 def test_actual_rs485_transition_preserves_original_wrong_bus_prediction(self):
  observed=json.loads((R/'evidence/board_r12/rs485_final.json').read_text())
  window=next(w for w in observed['npu']['history'] if w['id']==13)
  status={'ok':0,'timeout':1,'cooldown':2}
  rows=[(status[a['status']],a['local_us'],a['bus_us'],status[b['status']],b['local_us'],b['bus_us']) for a,b in window['samples']]
  self.launch();self.samples(rows);n=self.finished()['npu'];h=n['history'][0]
  self.assertEqual(h['window_phase'],'transition')
  self.assertEqual(h['reply']['label'],'can_link_open','known raw model error must remain visible in evidence')
  for a,b in zip(h['features'],window['features']):self.assertAlmostEqual(a,b,places=8)
  self.assertEqual(n['presentation_policy'],'transition_abstention_v1');self.assertEqual(n['total'],1)
 def test_stable_unavailable_timeout_and_cooldown_is_not_transition(self):
  self.launch();self.samples([(1,250000,0,0,42000,41000),(2,800,0,0,42000,41000),(1,250000,0,0,42000,41000),(2,800,0,0,42000,41000),(1,250000,0,0,42000,41000)])
  n=self.finished()['npu'];self.assertEqual(n['history'][0]['window_phase'],'stable')
  self.assertEqual(n['total'],1)
 def tearDown(self):
  if hasattr(self,'p'):
   out,err=self.p.communicate('stop\n',timeout=5);self.assertEqual(self.p.returncode,0,out+err);self.assertNotIn('runtime error:',err)
  if hasattr(self,'service'):self.service.close()

class WebNpuIntegrationTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  import test_web
  cls.webtests=test_web.WebTests;cls.webtests.setUpClass()
 @classmethod
 def tearDownClass(cls):cls.webtests.tearDownClass()
 def test_real_http_bus_worker_npu_and_encrypted_snapshot(self):
  # Actual C web and unchanged C gateway engines; bus surrogate + fake NPU.
  import http.client,struct
  from cryptography.hazmat.primitives.ciphers.aead import AESGCM
  w=self.webtests('test_page_state_export_and_no_unsolicited_write');w.setUp();fake=FakeNpu()
  try:
   w.proc.terminate();self.assertEqual(w.proc.wait(6),0);w.archive_client.close()
   w.archive_client,peer=socket.socketpair()
   w.proc=subprocess.Popen([w.binary,'--archive-fd',str(peer.fileno()),'--npu-port',str(fake.port),'--bind','127.0.0.1','--peer','127.0.0.1','--port',str(w.port),'--can-port',str(w.fixture.can_port),'--rs-port',str(w.fixture.rs_port),'--udp-timeout-ms','1500'],stdout=w.logfile,stderr=subprocess.STDOUT,pass_fds=(peer.fileno(),));peer.close()
   deadline=time.monotonic()+20
   while time.monotonic()<deadline:
    try:
     code,s=w.request('GET','/api/state')
     if s['npu']['total']:break
    except (OSError,http.client.HTTPException):pass
    self.assertIsNone(w.proc.poll(),w.log_text());time.sleep(.05)
   self.assertEqual(s['npu']['total'],1);self.assertEqual(s['npu']['history'][0]['reply']['status'],'ok');self.assertTrue(s['buses']['CAN']['valid']);w.initial=s
   c=w.cmd(bus='BOTH',op='CROSS',address=31,value=0);self.assertEqual(w.submit(c)[0],202);self.assertEqual(w.wait(c)['status'],'PASS')
   def exact(n):
    b=b''
    while len(b)<n:
     a=w.archive_client.recv(n-len(b));self.assertTrue(a);b+=a
    return b
   w.archive_client.settimeout(4);w.archive_client.sendall(b'R');length=int.from_bytes(exact(8),'big');raw=exact(length);archive=json.loads(raw)
   self.assertNotIn('token',archive);self.assertEqual(archive['npu']['total'],1);self.assertEqual(archive['jobs'][0]['status'],'PASS')
   # Seal with production igw_vault using a private pseudo terminal; independently
   # decrypt PBKDF2/AESGCM and check the exact archived inference + cross record.
   import test_vault
   with tempfile.TemporaryDirectory() as t:
    inp=Path(t)/'state.json';out=Path(t)/'state.igwe';inp.write_bytes(raw)
    result=subprocess.run([w.vault_binary,'seal','--input',str(inp),'--output',str(out),'--pass-fd','0'],input=b'r12-test-private-passphrase\nr12-test-private-passphrase\n',capture_output=True,timeout=15)
    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    blob=out.read_bytes();import hashlib
    key=hashlib.pbkdf2_hmac('sha256',b'r12-test-private-passphrase',blob[16:32],int.from_bytes(blob[12:16],'big'),32)
    plain=AESGCM(key).decrypt(blob[32:44],blob[64:],blob[:64]);self.assertEqual(plain,raw)
   fake.mode='wrong_model'
   # Losing NPU does not prevent subsequent independent bus reads/writes.
   fake.close();del fake
   c=w.cmd(bus='RS485',op='WRITE',address=16,value=27182);self.assertEqual(w.submit(c)[0],202);self.assertEqual(w.wait(c)['status'],'PASS')
  finally:
   if 'fake' in locals():fake.close()
   w.tearDown()
