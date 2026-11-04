#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
igw_compiler=${IGW_AARCH64_CC:-"$HOME/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-gcc"}
[ -x "$igw_compiler" ] || { echo 'Set IGW_AARCH64_CC to the existing AArch64 Linux GCC'; exit 2; }
python3 embed_page.py
mkdir -p deploy
for name in igw_can_block igw_rs485_file igw_file_web; do
 "$igw_compiler" -std=gnu11 -O2 -Wall -Wextra -Werror -static "src/$name.c" -o "deploy/$name"
done
python3 - <<'PY'
from pathlib import Path
import hashlib,shutil
r=Path('deploy')
for name in ['igw_can_block','igw_rs485_file','igw_file_web']:
 b=(r/name).read_bytes();assert b[:6]==b'\x7fELF\x02\x01' and int.from_bytes(b[18:20],'little')==183,'Not AArch64 ELF64 LE'
shutil.copy2('run_file_web.sh',str(r/'run_file_web.sh'))
shutil.copy2('run_sd_test.sh',str(r/'run_sd_test.sh'))
for n in [4096,65536,1048576]:
 (r/('input_'+str(n)+'.bin')).write_bytes(bytes((i*137+(i>>7)*21)&255 for i in range(n)))
(r/'reverse_4096.bin').write_bytes(bytes(((i*137+(i>>7)*21)&255)^165 for i in range(4096)))
(r/'build_info.txt').write_text('R23_DUAL_FILE_WEB\nSlave R23 SD_FILE_R23 required. CAN500k / RS485 negotiated115200.\nMaximum8388608 bytes/node, shared CAN/RS485 SD generations. One worker globally.\nUpload automatically reads back over the selected bus. GET is hash verified.\nHTTP192.168.31.222:8081, allowed peer192.168.31.100. Plaintext trusted-LAN prototype.\nAsync progress and verified downloads; restart after stop/failure. No resume.\nSlave uses independent SD; target persistence/recovery remains to be tested. Slave application changes; BOOT/PL/DTB unchanged.\n',encoding='utf-8')
names=['igw_can_block','igw_rs485_file','igw_file_web','run_file_web.sh','run_sd_test.sh','input_4096.bin','input_65536.bin','input_1048576.bin','reverse_4096.bin','build_info.txt']
(r/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256((r/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in names),encoding='utf-8')
print('AARCH64_ELF64=PASS THREE_TOOLS')
PY
echo 'BUILD_COMPLETE_R23; copy all eleven deploy files to main SD igw_apps/r23_sd_files. Slave SDK: slave_r23/slave/main.c.'
