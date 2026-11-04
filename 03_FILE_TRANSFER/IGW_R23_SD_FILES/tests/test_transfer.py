from pathlib import Path
import hashlib,os,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class Transfer(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.build=tempfile.TemporaryDirectory(prefix='cbf_build_');cls.can=Path(cls.build.name)/'can';cls.rs=Path(cls.build.name)/'rs';cls.fixture=Path(cls.build.name)/'fixture'
  for source,target,flags in [('src/igw_can_block.c',cls.can,['-DCBF_TEST_TRANSPORT']),('src/igw_rs485_file.c',cls.rs,['-DRSF_TEST_TRANSPORT']),('tests/fixture.c',cls.fixture,[])]:
   cmd=['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-I'+str(ROOT/'src'),'-I'+str(ROOT/'tests/stubs'),'-I'+str(ROOT/'tests/vendor_ff')]+flags+[str(ROOT/source)]
   if source=='tests/fixture.c':cmd += [str(ROOT/'tests/vendor_ff/ff.c'),str(ROOT/'tests/vendor_ff/ffunicode.c')]
   subprocess.run(cmd+['-o',str(target)],check=True)

 @classmethod
 def tearDownClass(cls):cls.build.cleanup()
 def setUp(self):self.tmp=tempfile.TemporaryDirectory(prefix='cbf_case_');self.path=Path(self.tmp.name);self.server=None
 def tearDown(self):
  if self.server:
   for fd in self.fds:os.close(fd)
   _,err=self.server.communicate(timeout=10);self.assertEqual(self.server.returncode,0,err)
  self.tmp.cleanup()
 def start(self,fault='none'):
  a,b=os.pipe();c,d=os.pipe();e,f=os.pipe();g,h=os.pipe();self.r,self.w,self.rr,self.rw=c,b,g,f;self.fds=[c,b,g,f]
  self.server=subprocess.Popen([str(self.fixture),str(a),str(d),str(e),str(h),fault,str(self.path/("disk%d.img"%getattr(self,"image_serial",0)))],pass_fds=(a,d,e,h),stderr=subprocess.PIPE,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
  for fd in (a,d,e,h):os.close(fd)
 def run_tool(self,bus,unit,*args,good=True):
  tool=self.can if bus=='can' else self.rs;r,w=(self.r,self.w) if bus=='can' else (self.rr,self.rw)
  cmd=[str(tool),'--test-read-fd',str(r),'--test-write-fd',str(w),'--unit',str(unit)]
  if bus=='rs':cmd+=['--baud','115200']
  p=subprocess.run(cmd+list(map(str,args)),pass_fds=(r,w),capture_output=True,text=True,timeout=300,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
  if good:self.assertEqual(p.returncode,0,p.stdout+p.stderr);self.assertIn('RESULT=PASS_',p.stdout)
  else:self.assertNotEqual(p.returncode,0,p.stdout+p.stderr)
  self.assertNotIn('AddressSanitizer',p.stderr);self.assertNotIn('runtime error:',p.stderr);return p
 def data(self,n):return bytes((i*137+(i>>7)*21)&255 for i in range(n))
 def test_can_roundtrip_boundaries(self):
  self.start()
  for n in [0,1,191,192,193,4096,65536,1048576]:
   data=self.data(n);source=self.path/'in';source.write_bytes(data);out=self.path/('out'+str(n));p=self.run_tool('can',1,'put',source);self.assertIn(hashlib.sha256(data).hexdigest(),p.stdout);self.run_tool('can',1,'get',out);self.assertEqual(out.read_bytes(),data)
 def test_cross_bus_both_directions_one_mib(self):
  self.start();source=self.path/'in';source.write_bytes(self.data(1048576));self.run_tool('can',1,'put',source);out=self.path/'rs_get';self.run_tool('rs',1,'get',out);self.assertEqual(source.read_bytes(),out.read_bytes())
  source.write_bytes(b'REVERSE'+self.data(65536));self.run_tool('rs',2,'put',source);out=self.path/'can_get';self.run_tool('can',2,'get',out);self.assertEqual(source.read_bytes(),out.read_bytes())
 def test_generated_eight_mib(self):
  self.start();out=self.path/'generated';p=self.run_tool('can',2,'generate',8388608,out);self.assertEqual(out.stat().st_size,8388608);self.assertIn(hashlib.sha256(out.read_bytes()).hexdigest(),p.stdout)
 def test_fault_recovery(self):
  for fault in ['drop_request','drop_ack','drop_meta','drop_reply','stale']:
   with self.subTest(fault=fault):
    # A fresh fixture per fault; preserve shared server lifetime inside each case.
    if self.server:
     for fd in self.fds:os.close(fd)
     self.server.communicate(timeout=10);self.assertEqual(self.server.returncode,0);self.server=None
    self.image_serial=getattr(self,"image_serial",0)+1;self.start(fault);source=self.path/'in';source.write_bytes(self.data(4096));self.run_tool('can',1,'put',source);out=self.path/fault;self.run_tool('can',1,'get',out);self.assertEqual(source.read_bytes(),out.read_bytes())
 def test_corrupted_file_not_published(self):
  self.start('corrupt_file');source=self.path/'in';source.write_bytes(self.data(4096));self.run_tool('can',1,'put',source);out=self.path/'bad';p=self.run_tool('can',1,'get',out,good=False);self.assertIn('FILE_SHA256_MISMATCH',p.stderr);self.assertFalse(out.exists());self.assertFalse(list(self.path.glob('*.part.*')))
 def test_process_restart_reloads_committed_sd_file(self):
  self.start();source=self.path/'persistent';source.write_bytes(self.data(65536));self.run_tool('rs',1,'put',source)
  for fd in self.fds:os.close(fd)
  _,err=self.server.communicate(timeout=10);self.assertEqual(self.server.returncode,0,err);self.server=None
  self.start();out=self.path/'after_restart';self.run_tool('can',1,'get',out);self.assertEqual(out.read_bytes(),source.read_bytes())
 def test_no_file_and_existing_output(self):
  self.start();out=self.path/'no';self.run_tool('can',1,'get',out,good=False);self.assertFalse(out.exists());out.write_bytes(b'KEEP');self.run_tool('can',1,'get',out,good=False);self.assertEqual(out.read_bytes(),b'KEEP')
if __name__=='__main__':unittest.main(verbosity=2)
