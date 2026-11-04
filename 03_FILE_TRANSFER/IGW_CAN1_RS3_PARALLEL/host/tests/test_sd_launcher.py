from pathlib import Path
import hashlib,os,signal,subprocess,tempfile,time,unittest,selectors
ROOT=Path(__file__).resolve().parents[1]
FAKE='''#!/usr/bin/env python3
from pathlib import Path
import signal,sys,time,os
args=sys.argv[1:];unit=1
while args[0].startswith('--'):
 key,value=args[:2];args=args[2:]
 if key=='--unit':unit=int(value)
op=args[0]; store=Path('store_'+str(unit))
if os.environ.get('WAIT_FIXTURE'):
 signal.signal(signal.SIGTERM,lambda a,b:sys.exit(3))
 Path('started').write_text('yes')
 while True:time.sleep(.02)
if os.environ.get('LIVE_FIXTURE'):
 print('START_FIXTURE',flush=True);time.sleep(2.5)
if op=='put':store.write_bytes(Path(args[1]).read_bytes())
elif op=='get':Path(args[1]).write_bytes(store.read_bytes())
elif op=='generate':Path(args[2]).write_bytes(bytes([91])*int(args[1]))
else:sys.exit(2)
print('RESULT=PASS_FIXTURE_ONLY')
'''
class Launcher(unittest.TestCase):
 def case(self,mode='can',interrupted=False,live=False):
  with tempfile.TemporaryDirectory(prefix='cbf_smoke_') as tmp:
   r=Path(tmp);(r/'bin').mkdir();sd=r/'sd'
   for name in ['ip','ps']:
    p=r/'bin'/name;p.write_text('#!/bin/sh\n'+("echo '2: can0 state UP bitrate 500000'\n" if name=='ip' else '')+'exit 0\n');p.chmod(0o755)
   for name in ['igw_can_block','igw_rs485_file']:(r/name).write_text(FAKE);(r/name).chmod(0o755)
   script=(ROOT/'run_sd_test.sh').read_text().replace('/mnt/mmcblk0p1/igw_evidence/r23_sd_files',str(sd));(r/'run_sd_test.sh').write_text(script)
   (r/'input_4096.bin').write_bytes(bytes(range(256))*16);(r/'reverse_4096.bin').write_bytes(bytes(reversed(range(256)))*16);(r/'build_info.txt').write_text('fixture')
   names=['igw_can_block','igw_rs485_file','run_sd_test.sh','input_4096.bin','reverse_4096.bin','build_info.txt'];(r/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256((r/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in names))
   env=dict(os.environ,PATH=str(r/'bin')+':'+os.environ['PATH'])
   if interrupted:env['WAIT_FIXTURE']='1'
   if live:env['LIVE_FIXTURE']='1'
   process=subprocess.Popen(['sh','run_sd_test.sh',mode,'8388608' if mode=='generate' else '4096'],cwd=str(r),env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
   early=''
   if live:
    # Read one byte at a time to avoid text-buffer readiness hiding lines.
    with selectors.DefaultSelector() as selector:
     selector.register(process.stdout,selectors.EVENT_READ);deadline=time.monotonic()+2.2
     while 'START_FIXTURE\n' not in early and time.monotonic()<deadline:
      if selector.select(.1):early+=os.read(process.stdout.fileno(),1).decode()
    self.assertIn('START_FIXTURE',early);self.assertIsNone(process.poll())
   if interrupted:
    for _ in range(100):
     if (r/'started').exists():break
     time.sleep(.02)
    self.assertTrue((r/'started').exists());process.send_signal(signal.SIGTERM);time.sleep(.01)
    if process.poll() is None:process.send_signal(signal.SIGTERM)
   out,err=process.communicate(timeout=15);out=early+out;self.assertEqual(process.returncode,3 if interrupted else 0,out+err)
   self.assertIn('SD_EVIDENCE=',out);saved=list(sd.glob('igw_r23_file.*'));self.assertEqual(len(saved),1)
   self.assertIn('REVIEW_R23' if interrupted else 'PASS_R23',(saved[0]/'result.txt').read_text())
   if mode=='cross' and not interrupted:self.assertEqual((saved[0]/'node1_unchanged.bin').read_bytes(),(r/'input_4096.bin').read_bytes());self.assertEqual((saved[0]/'rs_to_can.bin').read_bytes(),(r/'reverse_4096.bin').read_bytes())
 def test_normal(self):self.case()
 def test_cross_bus(self):self.case('cross')
 def test_repeated_term_preserves_evidence(self):self.case(interrupted=True)
 def test_live_progress_before_completion(self):self.case('generate',live=True)
if __name__=='__main__':unittest.main(verbosity=2)
