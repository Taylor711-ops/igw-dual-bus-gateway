#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname -- "${BASH_SOURCE[0]}")"
sha256sum -c SOURCE_SHA256SUMS.txt
igw_prefix=${CROSS_COMPILE:-$HOME/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-}
igw_cc=${igw_prefix}gcc
case "$("$igw_cc" -dumpmachine)" in aarch64*linux*) ;; *) echo 'Need AArch64 Linux compiler' >&2; exit 1;; esac
[ ! -e deploy ] || { echo 'STOP: deploy already exists; retaining it'; exit 1; }
mkdir deploy
"$igw_cc" -std=c11 -O2 -Wall -Wextra -Werror -static src/igw_can_pl_capture.c -o deploy/igw_can_pl_capture
"${igw_prefix}readelf" -h deploy/igw_can_pl_capture
cp overlay/*.sh deploy/
(
    cd deploy
    sha256sum igw_can_pl_capture check_r15.sh run_web_gateway.sh run_secure_gateway.sh install_on_board.sh > SHA256SUMS.txt
)
echo 'BUILD_COMPLETE: copy entire deploy folder to main SD igw_apps/r15_bringup_install'
echo 'This builds one diagnostic collector only. No BOOT/DTB/slave/SDK output was modified.'
