"""NPU supervisor lifecycle in isolated fake /proc + MMIO fixtures, no hardware."""
import hashlib,os,subprocess,tempfile,time,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class NpuLauncherTests(unittest.TestCase):
 def run_case(self,mode):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);deploy=root/'deploy';tools=root/'bin';runtime=root/'runtime';deploy.mkdir();tools.mkdir();runtime.mkdir();irq=root/'interrupts';irq.write_text('40: 0 0 GIC HARD_NPU_IRQ\n')
   def executable(p,s):p.write_text(s);p.chmod(0o755)
   executable(tools/'devmem','#!/bin/sh\necho 0x000000BF\n')
   executable(tools/'sha256sum','#!/bin/sh\ncase "$1" in\n*/libarmnn.so.32.1) echo "5def7ba75e4b59644be6f58deaf8b2bb3791c62ed38b52b6aea08b11a8f728ce  $1";;\n*) exec /usr/bin/sha256sum "$@";;\nesac\n')
   executable(tools/'sleep','#!/bin/sh\n/bin/sleep 0.02\n')
   executable(deploy/'igw_fault_online','''#!/bin/sh
trap 'exit 0' TERM INT
if [ "$MODE" = startup_hang ];then while :;do /bin/sleep .02;done;fi
echo 'SELF_TEST=PASS HIDDEN_CASES=150 CLASSIFIER_CASES=150'
if [ "$MODE" != missing_irq ];then echo '40: 300 150 GIC HARD_NPU_IRQ' > "$IRQ_FILE";fi
echo self_test_done > state
while [ ! -f allow ];do /bin/sleep .01;done
if [ "$MODE" = bind_fail ];then exit 2;fi
echo 'READY ONLINE_NPU UDP=127.0.0.1:16002'
if [ "$MODE" = busy_hang ];then echo busy > state;else echo idle > state;fi
while :;do /bin/sleep .02;done
''')
   script=(ROOT/'deploy/run_npu_local.sh').read_text().replace('/tmp/',str(runtime)+'/').replace('/proc/interrupts',str(irq)).replace('[ -c "/dev/$igw_dev" ]',':')
   # Host executor PID namespaces differ; only fixture lookup is replaced.
   script=script.replace('[ "$(awk \'{print $3}\' "/proc/$1/stat" 2>/dev/null)" != Z ]','return 0')
   executable(deploy/'run_npu_local.sh',script)
   (deploy/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256((deploy/f).read_bytes()).hexdigest()+'  '+f+'\n' for f in ['igw_fault_online','run_npu_local.sh']))
   log=root/'log';h=log.open('w');p=subprocess.Popen(['sh',str(deploy/'run_npu_local.sh')],env=dict(os.environ,PATH=str(tools)+':'+os.environ['PATH'],MODE=mode,IRQ_FILE=str(irq)),stdout=h,stderr=subprocess.STDOUT)
   try:
    if mode=='normal':
     deadline=time.monotonic()+3
     while time.monotonic()<deadline and 'NPU_LOCAL_READY' not in log.read_text():
      if p.poll() is not None:break
      time.sleep(.01)
     self.assertIn('NPU_LOCAL_READY',log.read_text());p.terminate()
    ret=p.wait(5);h.close();return ret,log.read_text(),(runtime/'igw_alnpu_probe.lock').exists()
   finally:
    if p.poll() is None:p.terminate();p.wait(5)
    h.close()
 def test_correct_450_irq_gate_and_signal_cleanup(self):
  ret,log,lock=self.run_case('normal');self.assertEqual(ret,0,log);self.assertIn('NPU_STARTUP_IRQ_DELTA HARD=450',log);self.assertFalse(lock,log)
 def test_missing_hardware_irq_never_opens_service(self):
  ret,log,lock=self.run_case('missing_irq');self.assertNotEqual(ret,0);self.assertNotIn('NPU_LOCAL_READY',log);self.assertFalse(lock,log)
 def test_worker_bind_failure_refuses_ready(self):
  ret,log,lock=self.run_case('bind_fail');self.assertNotEqual(ret,0);self.assertNotIn('NPU_LOCAL_READY',log);self.assertFalse(lock,log)
 def test_startup_watchdog_retains_lock(self):
  ret,log,lock=self.run_case('startup_hang');self.assertNotEqual(ret,0);self.assertIn('startup_timeout',log);self.assertTrue(lock)
 def test_busy_watchdog_retains_lock(self):
  ret,log,lock=self.run_case('busy_hang');self.assertNotEqual(ret,0);self.assertIn('inference_watchdog',log);self.assertTrue(lock)
