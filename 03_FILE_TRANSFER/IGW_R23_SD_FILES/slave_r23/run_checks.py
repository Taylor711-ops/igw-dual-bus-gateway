#!/usr/bin/env python3
"""Host mocks only; no claim of ARM linking, UART latency or target performance."""
from pathlib import Path
import hashlib,json,os,subprocess,tempfile

root=Path(__file__).resolve().parent
original=(root/'slave/main.c').read_bytes()
subprocess.run(['python3',str(root/'generate_slave.py')],check=True)
assert original==(root/'slave/main.c').read_bytes(), 'Generated file is stale'
results=[]
with tempfile.TemporaryDirectory(prefix='igw_r172_') as tmp:
    for mode in (0,1):
        target=Path(tmp)/('scheduler_'+str(mode))
        command=['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
                 '-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',
                 '-DTEST_ROLE_SLAVE','-DIGW_RUNTIME_LOGGING='+str(mode),
                 '-I'+str(root/'tests/stubs'),'-I'+str(root/'../tests/vendor_ff'),str(root/'tests/test_scheduler.c'),str(root/'../tests/vendor_ff/ff.c'),str(root/'../tests/vendor_ff/ffunicode.c'),'-o',str(target)]
        subprocess.run(command,check=True)
        # LeakSanitizer cannot enumerate threads under the hosted ptrace runtime.
        # Address/undefined checks stay enabled; leak detection is explicitly off.
        result=subprocess.run([str(target)],check=True,text=True,capture_output=True,
                              env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
        print(result.stdout,end='')
        results.append({'periodic_uart':mode,'result':'PASS','stdout':result.stdout})
report={'revision':'R23_SD_SLAVE','result':'PASS_HOST_CHECKS',
        'generated_source_reproducible':True,'ASan_UBSan':True,
        'LeakSanitizer':'NOT_RUN_PTRACE_ENVIRONMENT','tests':results,
        'ARM_compile_link':'PENDING_USER_SDK','board_latency':'NOT_TESTED',
        'file_transfer':'PASS_NATIVE_GENERATED_SCHEDULER_WITH_REAL_FATFS_RAMDISK_NOT_TARGET_HARDWARE'}
(root/'VALIDATION.json').write_text(json.dumps(report,indent=2)+'\n')
print('RESULT=PASS_HOST_CHECKS; target build and A/B latency test still required')
