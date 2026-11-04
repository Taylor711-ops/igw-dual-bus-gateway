#!/usr/bin/env python3
"""Native real FatFs tests, generated scheduler and actual CLI pipe tests.
No physical SD, CAN, RS485, AArch64 link or abrupt power-cut certification.
"""
import argparse,json,os,subprocess,sys,tempfile
from pathlib import Path
r=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--sdk');a=p.parse_args()
def check(cmd):
 print('CHECK '+str(cmd),flush=True);subprocess.run(cmd,cwd=str(r),check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
check([sys.executable,'embed_page.py']);check([sys.executable,'slave_r23/generate_slave.py'])
for name in ['igw_can_block','igw_rs485_file','igw_file_web']:check(['gcc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-fsyntax-only','src/'+name+'.c'])
for name in ['run_file_web.sh','run_sd_test.sh']:check(['sh','-n',name])
check(['bash','-n','build.sh'])
with tempfile.TemporaryDirectory(prefix='igw_sd_core_') as tmp:
 binary=str(Path(tmp)/'core')
 check(['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-Itests/stubs','-Itests/vendor_ff','-Isrc','tests/core.c','tests/vendor_ff/ff.c','tests/vendor_ff/ffunicode.c','-o',binary]);check([binary])
for task in ['slave_r23/run_checks.py','tests/test_transfer.py','tests/test_http.py','tests/test_launcher.py','tests/test_archive_patch.py','tests/test_sd_launcher.py']:check([sys.executable,task])
check(['node','tests/frontend.cjs','file_page.html'])
if a.sdk:check([sys.executable,'slave_r23/check_slave_sdk.py','--sdk',str(Path(a.sdk).resolve())])
report={'revision':'R23_SD_FILE','result':'PASS_NATIVE_CHECKS','storage_tests':'real SDK FatFs R0.14b on native RAM/mapped disk; streaming8MiB, generations, restart, incomplete PUT, digest/metadata corruption, disk I/O error, foreign directory refusal','slave_workspace_bytes_native':16704,'two_node_protocol_state_bytes_native':1008,'actual_CLI_and_SD_wire_tests':7,'Web_HTTP_worker_fixture_tests':10,'web_launcher_tests':3,'archive_installer_recovery_tests':5,'CLI_launcher_tests':4,'frontend_DOM':'PASS','generated_slave_scheduler':'PASS both logging modes; real FatFs plus HAL/MMIO fixtures','SDK_header_check':'PASS supplied actual platform headers' if a.sdk else 'NOT_RUN','sanitizers':'ASan+UBSan; leak detection off under ptrace','AArch64_compile_link':'PENDING_USER_SDK','physical_SD_mount_read_write':'PENDING','physical_reboot_persistence':'PENDING','physical8MiB_transfer':'PENDING','arbitrary_power_cut_atomicity':'NOT_CERTIFIED','SDK_driver_caveats':['disk_ioctl CTRL_SYNC returns success without issuing an explicit card-cache barrier','HAL read/write Timeout argument is unused; lower device layer handles transfer timeouts, and some initialization loops are not bounded','No SD hot-unplug robustness claim'],'scope':'No physical hardware performance claim; no persistent partial-file resume; no other project-wide acceptance claim'}
(r/'VALIDATION_R23.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print('RESULT=PASS_R23_NATIVE_CHECKS; target compile/link, SD bringup and persistence tests pending')
