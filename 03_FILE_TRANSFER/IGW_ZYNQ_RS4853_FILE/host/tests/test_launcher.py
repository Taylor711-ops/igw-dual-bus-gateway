import hashlib,os,re,shutil,signal,subprocess,tempfile,time,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class Launcher(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.build=tempfile.TemporaryDirectory(prefix='r23_archive_build_');cls.probe=Path(cls.build.name)/'probe'
  subprocess.run(['gcc','-std=gnu11','-O2','-Wall','-Wextra','-Werror',str(ROOT/'tests/runtime_probe.c'),'-o',str(cls.probe)],check=True)
 @classmethod
 def tearDownClass(cls):cls.build.cleanup()
 def run_case(self,legacy=False,rejected=False):
  with tempfile.TemporaryDirectory(prefix='igw_r23_launch_test_') as tmp:
   r=Path(tmp);deploy=r/'deploy';tools=r/'bin';deploy.mkdir();tools.mkdir();runtime=None
   def exe(p,text):p.write_text(text);p.chmod(0o700)
   exe(tools/'ps','#!/bin/sh\necho "1 root init"\n')
   exe(tools/'ip','#!/bin/sh\necho "can0 state UP bitrate 500000"\necho "inet 192.168.31.222/24"\n')
   for name in ['igw_can_block','igw_rs485_file']:exe(deploy/name,'#!/bin/sh\nexit 0\n')
   if legacy or rejected:
    runtime=Path(tempfile.mkdtemp(prefix='igw_r22_files.' if legacy else 'igw_foreign.'));(runtime/'probe.output').write_bytes(b'verified fixture')
    exe(deploy/'igw_file_web',"""#!/usr/bin/env python3
import signal,time
stopped=False
def handler(*args):
 global stopped
 stopped=True
signal.signal(signal.SIGTERM,handler)
print('R23 FILE WEB READY fixture only',flush=True)
print('EVIDENCE_DIRECTORY=%s',flush=True)
while not stopped:time.sleep(.01)
time.sleep(.25)
print('R23_STOP jobs=1 blocked=0',flush=True)
"""%runtime)
   else:shutil.copy2(self.probe,deploy/'igw_file_web')
   (deploy/'build_info.txt').write_text('R23 fixture')
   script=(ROOT/'run_file_web.sh').read_text().replace('/mnt/mmcblk0p1',str(r/'sd'))
   (deploy/'run_file_web.sh').write_text(script)
   (deploy/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256((deploy/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in ['igw_can_block','igw_rs485_file','igw_file_web','run_file_web.sh','build_info.txt']))
   log=r/'launcher.log'
   with log.open('w') as out:
    p=subprocess.Popen(['sh',str(deploy/'run_file_web.sh')],stdout=out,stderr=subprocess.STDOUT,env=dict(os.environ,PATH=str(tools)+':'+os.environ['PATH']),start_new_session=True)
    try:
     deadline=time.monotonic()+5
     while time.monotonic()<deadline and 'FILE WEB READY' not in log.read_text():time.sleep(.02)
     self.assertIn('FILE WEB READY',log.read_text());text=log.read_text();runtime=Path(re.search(r'^EVIDENCE_DIRECTORY=(.+)$',text,re.M).group(1))
     if not legacy and not rejected:self.assertTrue(str(runtime).startswith('/tmp/igw_r23_files.'))
     p.terminate();time.sleep(.03)
     if p.poll() is None:p.terminate()
     self.assertEqual(p.wait(8),3 if rejected else 0,log.read_text())
     text=log.read_text();self.assertIn('EVIDENCE_SAVED=0' if rejected else 'EVIDENCE_SAVED=1',text)
     saved=r/'sd/igw_evidence/r23_file_web'/runtime.name
     if rejected:self.assertFalse(saved.exists());self.assertIn('ARCHIVE_REJECTED_RUNTIME=',text)
     else:self.assertEqual((saved/'probe.output').read_bytes(),b'verified fixture');self.assertTrue((saved/'service.log').is_file())
    finally:
     if p.poll() is None:p.kill();p.wait()
     try:os.killpg(p.pid,signal.SIGTERM)
     except ProcessLookupError:pass
     if runtime and str(runtime).startswith(('/tmp/igw_r22_files.','/tmp/igw_r23_files.','/tmp/igw_foreign.')):shutil.rmtree(runtime)
 def test_actual_C_runtime_and_launcher_contract(self):self.run_case()
 def test_legacy_R22_runtime_from_deployed_R23_binary_is_saved(self):self.run_case(legacy=True)
 def test_unrelated_runtime_path_is_rejected(self):self.run_case(rejected=True)
if __name__=='__main__':unittest.main(verbosity=2)
