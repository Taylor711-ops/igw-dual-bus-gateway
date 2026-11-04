from pathlib import Path
import json,os,subprocess,sys,tempfile,unittest
r=Path(__file__).resolve().parents[1]
original=(r/'src/web_assets.h').read_bytes()
subprocess.run([sys.executable,str(r/'generate_web.py')],check=True)
assert original==(r/'src/web_assets.h').read_bytes()
subprocess.run(['bash','-n',str(r/'build.sh')],check=True)
subprocess.run(['sh','-n',str(r/'deploy/run_web_gateway.sh')],check=True)
subprocess.run(['sh','-n',str(r/'deploy/run_secure_gateway.sh')],check=True)
with tempfile.TemporaryDirectory() as t:
 binary=str(Path(t)/'json_check')
 subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',str(r/'tests/json_selftest.c'),'-o',binary],check=True)
 subprocess.run([binary],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),check=True)
with tempfile.TemporaryDirectory() as t:
 binary=str(Path(t)/'model_ref')
 subprocess.run(['g++','-std=c++14','-O2','-Wall','-Wextra','-Werror',str(r/'tests/model_reference.cpp'),'-o',binary],check=True)
 subprocess.run([binary],check=True)
node=__import__('shutil').which('node')
if node:subprocess.run([node,str(r/'tests/frontend_test.cjs')],check=True)
# Compile the actual generated slave scheduler with sanitizers and test both
# buses concurrently, including node isolation and cross-protocol readback.
with tempfile.TemporaryDirectory() as t:
 binary=str(Path(t)/'slave_scheduler')
 subprocess.run(['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-I'+str(r/'slave_r16/tests/stubs'),'-DTEST_ROLE_SLAVE',str(r/'slave_r16/tests/test_scheduler.c'),'-o',binary],check=True)
 subprocess.run([binary],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),check=True)
before=(r/'slave_r16/slave/main.c').read_bytes()
subprocess.run([sys.executable,str(r/'slave_r16/generate_slave.py')],check=True)
assert before==(r/'slave_r16/slave/main.c').read_bytes()
suite=unittest.defaultTestLoader.discover(str(r/'tests'),pattern='test*.py')
result=unittest.TextTestRunner(verbosity=2).run(suite)
validation={'source_revision':'R13','host_tests':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),'json_parser_asan_ubsan_100000_fuzz_cases':'PASS','frontend_dom_fetch_component':'PASS' if node else 'NOT_RUN','static_web_and_dynamic_vault_host_build':'PASS','crypto_host_runtime':'OpenSSL 3.0.13','cross_implementation_AESGCM_PBKDF2':'PASS','target_openssl_1_1':'NOT_RUN','target_arm64_build':'PENDING_USER_TOOLCHAIN','hardware_r13':'NOT_RUN','sd_fat_rename_fsync':'NOT_RUN','power_cycle_recovery':'NOT_RUN','npu_concurrent_r13':'NOT_RUN','classification_transfer_accuracy':'PENDING_BOARD_DOMAIN_TEST','frozen_reference_150':'PASS','full_middle_target':'NOT_COMPLETE','multi_node_host_protocol':'16_LOGICAL_NODES','slave_scheduler_asan_ubsan':'PASS','physical_multi_device':'NOT_RUN','PL_CAN_parser':'NOT_IMPLEMENTED'}
(r/'VALIDATION.json').write_text(json.dumps(validation,indent=2)+'\n')
raise SystemExit(0 if result.wasSuccessful() else 1)
