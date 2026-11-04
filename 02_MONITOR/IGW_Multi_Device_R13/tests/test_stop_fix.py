"""Deterministic repeated-TERM regression, no FPGA or NPU hardware.
Inject a cleanup marker/pause only; production child cleanup/lock removal follows.
Old R12 must reproduce stale lock; fixed R12.1 must complete cleanup.
"""
import hashlib,os,signal,subprocess,tempfile,time,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OLD=ROOT/'baseline_stop_r12'
class RepeatedTermTests(unittest.TestCase):
 def fixture(self,fixed,group_first=False):
  with tempfile.TemporaryDirectory() as tmp:
   r=Path(tmp);deploy=r/'deploy';tools=r/'bin';runtime=r/'runtime';deploy.mkdir();tools.mkdir();runtime.mkdir();irq=r/'interrupts';irq.write_text('40: 0 0 GIC HARD_NPU_IRQ\n')
   def executable(p,s):p.write_text(s);p.chmod(0o755)
   executable(tools/'devmem','#!/bin/sh\necho 0x000000BF\n')
   executable(tools/'sha256sum','#!/bin/sh\ncase "$1" in\n*/libarmnn.so.32.1) echo "5def7ba75e4b59644be6f58deaf8b2bb3791c62ed38b52b6aea08b11a8f728ce  $1";;\n*) exec /usr/bin/sha256sum "$@";;\nesac\n')
   executable(tools/'sleep','#!/bin/sh\n/bin/sleep .02\n')
   executable(deploy/'igw_fault_online','''#!/usr/bin/env python3
import os,signal,time
from pathlib import Path
Path(os.environ['PID_FILE']).write_text(str(os.getpid()))
signal.signal(signal.SIGTERM,lambda *_:exit(0))
print('SELF_TEST=PASS HIDDEN_CASES=150 CLASSIFIER_CASES=150',flush=True)
Path(os.environ['IRQ_FILE']).write_text('40: 300 150 GIC HARD_NPU_IRQ\\n')
Path('state').write_text('self_test_done')
while not Path('allow').exists():time.sleep(.01)
Path('state').write_text('idle');print('READY ONLINE_NPU UDP=127.0.0.1:16002',flush=True)
while True:time.sleep(.02)
''')
   source=ROOT/'deploy/run_npu_local.sh' if fixed else OLD/'run_npu_local.sh'
   script=source.read_text().replace('/tmp/',str(runtime)+'/').replace('/proc/interrupts',str(irq)).replace('[ -c "/dev/$igw_dev" ]',':')
   script=script.replace('[ "$(awk \'{print $3}\' "/proc/$1/stat" 2>/dev/null)" != Z ]','return 0')
   marker=r/'cleanup_entered'
   # Pause AFTER traps are changed but before lock removal. No production
   # semantics changed: a second TERM now demonstrates exactly the race.
   key=' if [ -n "$igw_pid" ]; then'
   assert script.count(key)==1
   script=script.replace(key,f' : > "{marker}"\n /bin/sleep .25\n'+key)
   executable(deploy/'run_npu_local.sh',script)
   (deploy/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256((deploy/f).read_bytes()).hexdigest()+'  '+f+'\n' for f in ['igw_fault_online','run_npu_local.sh']))
   log=r/'log';h=log.open('w');pidfile=r/'child.pid'
   p=subprocess.Popen(['sh',str(deploy/'run_npu_local.sh')],env=dict(os.environ,PATH=str(tools)+':'+os.environ['PATH'],IRQ_FILE=str(irq),PID_FILE=str(pidfile)),stdout=h,stderr=subprocess.STDOUT,start_new_session=True)
   try:
    deadline=time.monotonic()+3
    while time.monotonic()<deadline and 'NPU_LOCAL_READY' not in log.read_text():time.sleep(.01)
    self.assertIn('NPU_LOCAL_READY',log.read_text())
    if group_first:os.killpg(p.pid,signal.SIGTERM)
    else:p.terminate()
    deadline=time.monotonic()+2
    while time.monotonic()<deadline and not marker.exists():time.sleep(.005)
    self.assertTrue(marker.exists())
    p.terminate() # supervisor's second delivery while its cleanup is active
    ret=p.wait(5);h.flush();return ret,log.read_text(),(runtime/'igw_alnpu_probe.lock').exists()
   finally:
    if p.poll() is None:p.kill();p.wait(3)
    # Reap fixture descendants when intentionally reproducing old R12 failure.
    try:os.killpg(p.pid,signal.SIGTERM)
    except ProcessLookupError:pass
    if pidfile.exists():
     try:os.kill(int(pidfile.read_text()),signal.SIGTERM)
     except ProcessLookupError:pass
    h.close()
 def test_old_r12_reproduces_aborted_cleanup_and_stale_lock(self):
  ret,log,lock=self.fixture(False);self.assertEqual(ret,-signal.SIGTERM,log);self.assertTrue(lock)
 def test_fixed_repeated_term_finishes_cleanup_and_releases_lock(self):
  ret,log,lock=self.fixture(True);self.assertEqual(ret,0,log);self.assertIn('NPU_STOP reason=operator_stop',log);self.assertFalse(lock,log)
 def test_fixed_group_term_plus_parent_term_finishes_cleanup(self):
  ret,log,lock=self.fixture(True,True);self.assertEqual(ret,0,log);self.assertFalse(lock,log)
if __name__=='__main__':unittest.main(verbosity=2)
