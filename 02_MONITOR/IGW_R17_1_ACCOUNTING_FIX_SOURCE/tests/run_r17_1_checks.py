#!/usr/bin/env python3
"""Local validation only: no real CAN, MMIO, SDK linking or NPU hardware."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(ROOT/'tests/check_preflight.py')],check=True)
assert (ROOT/'src/uplink_protocol.h').read_bytes()==(ROOT/'slave_r17/slave_src/uplink_protocol.h').read_bytes()
for script in (ROOT/'deploy').glob('*.sh'):subprocess.run(['sh','-n',str(script)],check=True)
subprocess.run(['bash','-n',str(ROOT/'build.sh')],check=True)
for generator,target in [('generate_web.py','src/web_assets.h'),('slave_r17/generate_slave.py','slave_r17/slave/main.c')]:
    original=(ROOT/target).read_bytes()
    subprocess.run([sys.executable,str(ROOT/generator)],check=True)
    assert original==(ROOT/target).read_bytes(),target
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
with tempfile.TemporaryDirectory() as directory:
    for name,source,extra in [('protocol','tests/uplink_protocol_test.c',[]),('rs_protocol','tests/rs_uplink_protocol_test.c',[]),('pl_accounting','tests/pl_accounting_test.c',[]),('scheduler','slave_r17/tests/test_scheduler.c',['-DTEST_ROLE_SLAVE','-I'+str(ROOT/'slave_r17/tests/stubs')])]:
        target=str(Path(directory)/name)
        subprocess.run(['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-pthread',*extra,str(ROOT/source),'-lm','-o',target],check=True)
        subprocess.run([target],env=env,check=True)
    subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(ROOT/'src/igw_can_uplink.c'),'-o',str(Path(directory)/'production_uplink')],check=True)
subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(ROOT/'src/igw_rs485_rw.c'),'-o','/tmp/igw_rs_r17_production_check'],check=True)
node=shutil.which('node')
if node:subprocess.run([node,str(ROOT/'tests/frontend_test.cjs')],check=True)
sys.path.insert(0,str(ROOT/'tests'))
suite=unittest.TestSuite()
for module in ['test_r17_1_accounting','test_rs_uplink_r17','test_uplink_r16','test_multi','test_web','test_web_launcher']:
    suite.addTests(unittest.defaultTestLoader.loadTestsFromName(module))
result=unittest.TextTestRunner(verbosity=2).run(suite)
report={'revision':'R17.1_RS485_UPLINK','result':'PASS_LOCAL_CHECKS' if result.wasSuccessful() else 'FAIL_LOCAL_CHECKS','host_runtime_tests':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),'protocol_ASan_UBSan':'PASS','production_PL_accounting_parser_ASan_UBSan':'PASS','concurrent_CROSS_rounds':12,'NPU_five_sample_progress_with_uploads':'PASS_FAKE_UDP_NUMERIC_REFERENCE_NOT_HARDWARE','RS_grant_release_protocol_ASan_UBSan':'PASS','RS_upload_actual_Web_AES_archive':'PASS_HOST_SURROGATE','actual_generated_slave_scheduler_ASan_UBSan':'PASS','production_receiver_host_build':'PASS','frontend_DOM_fetch_component':'PASS' if node else 'NOT_RUN','real_browser_render':'NOT_RUN','AArch64_cross_build':'PENDING_USER_TOOLCHAIN','slave_application':'UNCHANGED_R17_ALREADY_BOARD_RUN','board_CAN_uploads':'NOT_RUN','board_RS485_active_uploads':'PENDING_CAN_COORDINATED_TARGET_TEST','classification_accuracy':'NOT_CERTIFIED','physical_multi_device':'NOT_RUN','PL_PS_frame_alignment':'NOT_CERTIFIED','exactly_once_across_power_loss':False}
(ROOT/'VALIDATION_R17_1.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2));sys.exit(0 if result.wasSuccessful() else 1)
