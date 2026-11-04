import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

root = Path(__file__).resolve().parents[1]
env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', ASAN_OPTIONS='detect_leaks=0')
before = (root/'slave/main.c').read_bytes()
subprocess.run([sys.executable,str(root/'generate_slave.py')],check=True,env=env)
assert (root/'slave/main.c').read_bytes() == before, 'generated slave source differs'
subprocess.run(['sh','-n',str(root/'deploy/run_rw_gateway.sh')],check=True)
subprocess.run(['bash','-n',str(root/'build.sh')],check=True)
with tempfile.TemporaryDirectory(prefix='igw_r8_check_') as tmp:
    binary = str(Path(tmp)/'slave')
    subprocess.run(['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',
                    '-I'+str(root/'tests/stubs'),'-DTEST_ROLE_SLAVE',
                    str(root/'tests/test_slave_legacy.c'),'-o',binary],check=True)
    subprocess.run([binary],check=True,env=env)
suite = unittest.defaultTestLoader.discover(str(root/'tests'),pattern='*test*.py')
result = unittest.TextTestRunner(verbosity=2).run(suite)
report = {'unit_tests':result.testsRun, 'failures':len(result.failures),
          'errors':len(result.errors), 'slave_scheduler_sanitizers':'PASS',
          'shell_syntax':'PASS', 'generated_slave_reproducible':True,
          'target_arm_build':'NOT_RUN', 'hardware_rs485':'NOT_RUN',
          'rtl_simulation':'NOT_RUN', 'full_middle_target':'NOT_COMPLETE'}
(root/'VALIDATION.json').write_text(json.dumps(report,indent=2)+'\n')
raise SystemExit(0 if result.wasSuccessful() else 1)
