import hashlib,json,os,shutil,subprocess,tempfile,time,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
TOKEN='0123456789abcdef0123456789abcdef'
class HTTP(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.build=tempfile.TemporaryDirectory(prefix='igwf_http_build_');cls.binary=Path(cls.build.name)/'driver'
  subprocess.run(['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',str(ROOT/'tests/http_driver.c'),'-o',str(cls.binary)],check=True)
 @classmethod
 def tearDownClass(cls):cls.build.cleanup()
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(prefix='igwf_http_');self.root=Path(self.tmp.name);(self.root/'runtime').mkdir(mode=0o700)
  self.backend=self.root/'backend.py';shutil.copyfile(ROOT/'tests/fake_backend.py',self.backend);self.backend.chmod(0o700);self.serial=0;self.p=None
 def tearDown(self):
  if self.p:
   self.p.stdin.close();self.assertEqual(self.p.wait(25),0);self.p.stdout.close();stderr=self.p.stderr.read();self.p.stderr.close();self.assertNotIn('runtime error:',stderr);self.assertNotIn('ERROR: AddressSanitizer',stderr)
  self.tmp.cleanup()
 def start(self,fault=''):
  self.p=subprocess.Popen([str(self.binary),str(self.root/'runtime'),str(self.backend)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',IGW_WEB_TEST_FAULT=fault))
  self.assertEqual(self.p.stdout.readline().strip(),'READY_NATIVE_HANDLER_NO_TCP')
 def raw(self,data):
  self.serial+=1;q=self.root/('q%d'%self.serial);r=self.root/('r%d'%self.serial);q.write_bytes(data)
  self.p.stdin.write(str(q)+' '+str(r)+'\n');self.p.stdin.flush()
  while True:
   line=self.p.stdout.readline();self.assertTrue(line,'driver exited')
   if line.strip()=='DONE':break
  head,body=r.read_bytes().split(b'\r\n\r\n',1);code=int(head.split()[1]);headers=dict(line.split(b':',1) for line in head.split(b'\r\n')[1:]);self.assertEqual(int(headers[b'Content-Length']),len(body));return code,body
 def request(self,method,target,body=b'',token=TOKEN,extra='',host='192.168.31.222:8081'):
  h='%s %s HTTP/1.1\r\nHost: %s\r\n'%(method,target,host)
  if token is not None:h+='X-IGW-Token: '+token+'\r\n'
  if method=='POST':h+='Content-Length: %d\r\n'%len(body)
  return self.raw((h+extra+'\r\n').encode()+body)
 def submit(self,op,unit=1,size=0,data=b'',bus='can'):
  code,body=self.request('POST','/api/file?op=%s&unit=%d&size=%d&bus=%s'%(op,unit,size,bus),data);return code,json.loads(body)
 def listing(self):
  code,b=self.request('GET','/api/jobs');self.assertEqual(code,200);return json.loads(b)
 def finish(self,job_id):
  end=time.monotonic()+12
  while time.monotonic()<end:
   d=self.listing();j=next(j for j in d['jobs'] if j['job_id']==job_id)
   if j['done']:return j
   time.sleep(.03)
  self.fail('worker did not finish')
 def api(self,*a,**kw):
  code,j=self.submit(*a,**kw);self.assertEqual(code,202,j);return self.finish(j['job_id'])
 def test_actual_handler_fork_upload_readback_zero_to_8m_both_buses(self):
  self.start()
  for bus in ['can','rs485']:
   for n in [0,4096,65536,1048576,8388608]:
    data=bytes((i*137+(i>>7)*21)&255 for i in range(n));j=self.api('put',size=n,data=data,bus=bus);self.assertTrue(j['ok'],j);self.assertEqual(j['sha256'],hashlib.sha256(data).hexdigest())
    code,out=self.request('GET','/api/download?id='+j['job_id']);self.assertEqual(code,200);self.assertEqual(out,data)
 def test_cross_bus_and_logical_node_isolation(self):
  self.start();a=b'can to rs\x00\xff';b=b'reverse independent'
  self.assertTrue(self.api('put',unit=1,size=len(a),data=a)['ok']);j=self.api('get',unit=1,bus='rs485');self.assertEqual(j['sha256'],hashlib.sha256(a).hexdigest())
  self.assertTrue(self.api('put',unit=2,size=len(b),data=b,bus='rs485')['ok']);j=self.api('get',unit=2);self.assertEqual(j['sha256'],hashlib.sha256(b).hexdigest())
  j=self.api('get',unit=1);self.assertEqual(j['sha256'],hashlib.sha256(a).hexdigest())
  self.assertEqual((self.root/'calls.txt').read_text().splitlines(),['can 1 put','can 1 get','rs485 1 get','rs485 2 put','rs485 2 get','can 2 get','can 1 get'])
 def test_async_progress_busy_guard_and_not_yet_downloadable(self):
  self.start('slow');began=time.monotonic();code,j=self.submit('generate',size=65536);self.assertEqual(code,202);self.assertLess(time.monotonic()-began,.5)
  deadline=time.monotonic()+3
  while True:
   d=self.listing()
   if d['jobs'][0]['progress_bytes']==32768:break
   self.assertLess(time.monotonic(),deadline);time.sleep(.02)
  self.assertTrue(d['busy']);self.assertEqual(d['jobs'][0]['progress_bytes'],32768);self.assertFalse(d['jobs'][0]['done']);self.assertNotIn(TOKEN,json.dumps(d))
  code,_=self.submit('generate',bus='rs485');self.assertEqual(code,409);code,_=self.request('GET','/api/download?id='+j['job_id']);self.assertEqual(code,404)
  self.assertTrue(self.finish(j['job_id'])['ok'])
 def test_cancel_waits_for_owned_backend_cleanup_and_blocks_restart(self):
  self.start('slow');code,j=self.submit('generate',size=65536,bus='rs485');self.assertEqual(code,202)
  deadline=time.monotonic()+3
  while not (self.root/'ready.txt').exists():
   self.assertLess(time.monotonic(),deadline);time.sleep(.02)
  code,_=self.request('POST','/api/cancel');self.assertEqual(code,200);self.assertFalse(self.finish(j['job_id'])['ok']);self.assertTrue((self.root/'cleanup.txt').is_file());self.assertTrue(self.listing()['blocked']);code,_=self.submit('get');self.assertEqual(code,503)
 def test_failed_backend_and_bad_hash_unavailable(self):
  self.start('hash');j=self.api('generate',size=4096);self.assertFalse(j['ok']);code,_=self.request('GET','/api/download?id='+j['job_id']);self.assertEqual(code,404);code,_=self.submit('get');self.assertEqual(code,503)
 def test_upload_readback_corruption_rejected(self):
  self.start('corrupt');j=self.api('put',size=3,data=b'abc',bus='rs485');self.assertFalse(j['ok']);self.assertTrue(self.listing()['blocked'])
 def test_no_remote_file_failure(self):
  self.start();j=self.api('get');self.assertFalse(j['ok']);self.assertTrue(self.listing()['blocked'])
 def test_session_and_guards(self):
  self.start();code,b=self.request('GET','/',token=None);self.assertEqual(code,200);self.assertIn('RS485'.encode(),b);code,b=self.request('GET','/api/session',token=None);self.assertEqual(json.loads(b)['max_bytes'],8388608)
  for kw in [{'token':None},{'token':'bad'},{'extra':'Origin: http://evil.example\r\n'},{'host':'evil.example'}]:
   code,_=self.request('POST','/api/file?op=generate&unit=1&size=1&bus=can',**kw);self.assertEqual(code,403)
  code,_=self.request('GET','/api/download?id=../../etc/passwd');self.assertEqual(code,400);self.assertFalse((self.root/'calls.txt').exists())
 def test_malformed_request_limits_and_bus_validation(self):
  self.start()
  for raw in [b'POST / HTTP/1.1\r\nHost: 192.168.31.222:8081\r\nContent-Length: 8388609\r\n\r\n',b'POST / HTTP/1.1\r\nHost: 192.168.31.222:8081\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n',b'GET / HTTP/1.1\r\nHost: \r\nHost: 192.168.31.222:8081\r\n\r\n',b'POST / HTTP/1.1\r\nHost: 192.168.31.222:8081\r\nTransfer-Encoding: chunked\r\n\r\n']:
   code,_=self.raw(raw);self.assertIn(code,[400,413])
  for suffix in ['op=generate&unit=1&size=4294967296&bus=can','op=get&unit=4294967297&size=0&bus=can','op=generate&unit=1&size=-1&bus=can','op=get&unit=1&size=0&bus=can&extra=1','op=get&unit=1&size=0&bus=ethernet','op=get&unit=1&size=0']:
   code,_=self.request('POST','/api/file?'+suffix);self.assertEqual(code,400)
  self.assertFalse((self.root/'calls.txt').exists())
 def test_session_limit(self):
  self.start()
  for _ in range(16):self.assertTrue(self.api('generate')['ok'])
  code,_=self.submit('generate');self.assertEqual(code,507)
if __name__=='__main__':unittest.main(verbosity=2)
