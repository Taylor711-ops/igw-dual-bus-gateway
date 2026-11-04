#!/usr/bin/env bash
set -euo pipefail
igw_root=$(cd "$(dirname "$0")/.." && pwd)
cd "$igw_root"
sha256sum -c SHA256SUMS.txt
igw_prefix=${CROSS_COMPILE:-"$HOME/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-"}
igw_cc="${igw_prefix}gcc"
[ -x "$igw_cc" ] || { echo 'STOP: set CROSS_COMPILE to the AArch64 Linux compiler prefix'; exit 2; }
case "$("$igw_cc" -dumpmachine)" in aarch64*linux*) ;; *) echo 'STOP: compiler is not AArch64 Linux'; exit 2;; esac
[ ! -e host/deploy ] || { echo 'STOP: host/deploy exists; preserve it and use a fresh package'; exit 2; }
python3 host/monitor/generate_web.py
mkdir host/deploy
"$igw_cc" -std=c11 -O2 -Wall -Wextra -Wmissing-field-initializers -Werror -static -pthread host/monitor/src/igw_web.c -lm -o host/deploy/igw_web
python3 - <<'PY'
from pathlib import Path
b=Path('host/deploy/igw_web').read_bytes()
assert b[:6]==b'\x7fELF\x02\x01' and int.from_bytes(b[18:20],'little')==183, 'not AArch64 ELF64'
print('AARCH64_ELF64=PASS')
PY
cp host/run_web_gateway.sh host/install_on_board.sh host/deploy/
(cd host/deploy && sha256sum igw_web run_web_gateway.sh install_on_board.sh > SHA256SUMS.txt)
echo 'RS4853_HOST_BUILD_COMPLETE: copy host/deploy contents to main SD igw_apps/r26_rs4853_install'
