#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
root=$(cd .. && pwd)
(cd "$root" && sha256sum -c SHA256SUMS.txt)
igw_cc=${IGW_AARCH64_CC:-"$HOME/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-gcc"}
if [[ ! -x "$igw_cc" ]]; then
  echo 'AArch64 compiler not found. Set IGW_AARCH64_CC to its full path.' >&2
  exit 2
fi
if [[ -e deploy ]]; then echo 'deploy already exists; preserve it or use a fresh extraction.' >&2; exit 2; fi
python3 embed_page.py
mkdir deploy
for name in igw_file_web igw_rs485_file; do
  "$igw_cc" -std=gnu11 -O2 -Wall -Wextra -Werror -static "src/$name.c" -o "deploy/$name"
done
cp install_on_board.sh deploy/
python3 - <<'PY'
from pathlib import Path
import hashlib
r=Path('deploy')
for name in ('igw_file_web','igw_rs485_file'):
    b=(r/name).read_bytes()
    assert b[:6]==b'\x7fELF\x02\x01' and int.from_bytes(b[18:20],'little')==183, 'Expected AArch64 ELF64 LE'
names=('igw_file_web','igw_rs485_file','install_on_board.sh')
(r/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256((r/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in names),encoding='utf-8')
print('AARCH64_ELF64=PASS')
PY
echo 'IGW3_HOST_BUILD_COMPLETE: copy host/deploy contents to main SD igw_apps/r23_sd_unit3_install'
