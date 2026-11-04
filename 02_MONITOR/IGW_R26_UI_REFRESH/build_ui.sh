#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
sha256sum -c SOURCE_SHA256SUMS.txt
igw_prefix=${CROSS_COMPILE:-"$HOME/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-"}
igw_cc="${igw_prefix}gcc"
[ -x "$igw_cc" ] || { echo 'STOP: set CROSS_COMPILE to your AArch64 Linux compiler prefix'; exit 2; }
case "$("$igw_cc" -dumpmachine)" in aarch64*linux*) ;; *) echo 'STOP: compiler is not AArch64 Linux'; exit 2;; esac
[ ! -e deploy ] || { echo 'STOP: deploy already exists; keep it and unpack a fresh source directory'; exit 2; }
python3 monitor/generate_web.py
python3 file/embed_page.py
mkdir deploy
"$igw_cc" -std=c11 -O2 -Wall -Wextra -Wmissing-field-initializers -Werror -static -pthread monitor/src/igw_web.c -lm -o deploy/igw_web
"$igw_cc" -std=gnu11 -O2 -Wall -Wextra -Werror -static file/src/igw_file_web.c -o deploy/igw_file_web
python3 - <<'PY'
from pathlib import Path
for name in ('igw_web','igw_file_web'):
 b=(Path('deploy')/name).read_bytes();assert b[:6]==b'\x7fELF\x02\x01' and int.from_bytes(b[18:20],'little')==183, 'not AArch64 ELF64'
print('AARCH64_ELF64=PASS')
PY
cp install_ui_on_board.sh deploy/
(cd deploy && sha256sum igw_web igw_file_web install_ui_on_board.sh > SHA256SUMS.txt)
echo 'UI_BUILD_COMPLETE: copy the complete deploy directory to main SD igw_apps/r26_ui_install'
