"""Concurrent real protocol code with two independent simulated boards, no hardware."""
import hashlib,os,subprocess,tempfile,time
from pathlib import Path
from tests.test_http import HTTP

ROOT=Path(__file__).resolve().parents[1]
PKG=ROOT.parent
ENV=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')

class ParallelProtocols(HTTP):
 @classmethod
 def setUpClass(cls):
  super().setUpClass();cls.compiled=tempfile.TemporaryDirectory(prefix='igw_protocol_pair_');b=Path(cls.compiled.name)
  cls.can=b/'can';cls.rs=b/'rs';cls.fixture1=b/'fixture1';cls.fixture3=b/'fixture3'
  common=['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
  include=['-I'+str(ROOT/'src'),'-I'+str(ROOT/'tests/stubs'),'-I'+str(ROOT/'tests/vendor_ff')]
  for src,out,flags in [(ROOT/'src/igw_can_block.c',cls.can,['-DCBF_TEST_TRANSPORT']),
                        (ROOT/'src/igw_rs485_file.c',cls.rs,['-DRSF_TEST_TRANSPORT']),
                        (ROOT/'tests/fixture.c',cls.fixture1,include+[str(ROOT/'tests/vendor_ff/ff.c'),str(ROOT/'tests/vendor_ff/ffunicode.c')]),
                        (PKG/'tests/fixture3.c',cls.fixture3,include+['-I'+str(PKG/'zynq/src'),str(ROOT/'tests/vendor_ff/ff.c'),str(ROOT/'tests/vendor_ff/ffunicode.c')])]:
   subprocess.run(common+flags+[str(src),'-o',str(out)],check=True)
 @classmethod
 def tearDownClass(cls):cls.compiled.cleanup();super().tearDownClass()
 def setUp(self):
  super().setUp();a,b=os.pipe();c,d=os.pipe();e,f=os.pipe();g,h=os.pipe()
  self.can_r,self.can_w=c,b;self.fds=[c,b,g,f]
  self.s1=subprocess.Popen([str(self.fixture1),str(a),str(d),str(e),str(h),'none',str(self.root/'disk1.img')],pass_fds=(a,d,e,h),stderr=subprocess.PIPE,env=ENV)
  for fd in (a,d,e,h):os.close(fd)
  a,b=os.pipe();c,d=os.pipe();self.rs_r,self.rs_w=c,b;self.fds.extend([c,b])
  self.s3=subprocess.Popen([str(self.fixture3),str(a),str(d),str(self.root/'disk3.img')],pass_fds=(a,d),stderr=subprocess.PIPE,env=ENV)
  os.close(a);os.close(d)
  self.backend.write_text('#!/usr/bin/env python3\nimport os,sys\na=sys.argv[1:]\nif a[:2]==["--can","can0"]:\n p='+repr(str(self.can))+'; r='+str(self.can_r)+'; w='+str(self.can_w)+'\nelse:\n p='+repr(str(self.rs))+'; r='+str(self.rs_r)+'; w='+str(self.rs_w)+'\nos.execv(p,[p,"--test-read-fd",str(r),"--test-write-fd",str(w)]+a)\n')
  self.backend.chmod(0o700)
 def start(self,fault=''):
  self.p=subprocess.Popen([str(self.binary),str(self.root/'runtime'),str(self.backend)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=ENV,pass_fds=tuple(self.fds))
  self.assertEqual(self.p.stdout.readline().strip(),'READY_NATIVE_HANDLER_NO_TCP')
 def tearDown(self):
  try:
   # The web process must release its inherited pipe handles first.
   super().tearDown()
  finally:
   for fd in self.fds:os.close(fd)
   for s in (self.s1,self.s3):
    _,err=s.communicate(timeout=30);self.assertEqual(s.returncode,0,err.decode(errors='replace'))
 def test_real_can1_and_rs3_protocols_run_concurrently_and_verify(self):
  self.start();n=262144
  data1=bytes((i*31+17)&255 for i in range(n));data3=bytes((i*71+13)&255 for i in range(n))
  code,a=self.submit('put',unit=3,size=n,data=data3,bus='rs485');self.assertEqual(code,202,a)
  code,b=self.submit('put',unit=1,size=n,data=data1,bus='can');self.assertEqual(code,202,b)
  self.assertEqual(self.listing()['active_count'],2)
  for j,data in ((a,data3),(b,data1)):
   end=time.monotonic()+60
   while True:
    state=next(x for x in self.listing()['jobs'] if x['job_id']==j['job_id'])
    if state['done']:break
    self.assertLess(time.monotonic(),end);time.sleep(.05)
   self.assertTrue(state['ok'],state);self.assertEqual(state['sha256'],hashlib.sha256(data).hexdigest())
   code,out=self.request('GET','/api/download?id='+j['job_id']);self.assertEqual(code,200);self.assertEqual(out,data)

if __name__=='__main__':
 import unittest
 unittest.main(defaultTest='ParallelProtocols.test_real_can1_and_rs3_protocols_run_concurrently_and_verify',verbosity=2)
