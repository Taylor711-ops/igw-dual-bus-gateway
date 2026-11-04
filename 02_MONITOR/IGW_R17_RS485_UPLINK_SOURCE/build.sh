#!/usr/bin/env bash
# Build only the changed Web program and new CAN upload receiver.
set -euo pipefail
cd "$(dirname -- "${BASH_SOURCE[0]}")"
sha256sum -c SOURCE_SHA256SUMS.txt
igw_prefix=${CROSS_COMPILE:-$HOME/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-}
igw_cc=${igw_prefix}gcc
case "$("$igw_cc" -dumpmachine)" in aarch64*linux*) ;; *) echo 'Need AArch64 Linux compiler' >&2;exit 1;; esac
for igw_app in igw_web igw_can_uplink igw_rs485_rw;do
    "$igw_cc" -std=c11 -O2 -Wall -Wextra -Wmissing-field-initializers -Werror -static -pthread "src/$igw_app.c" -lm -o "deploy/$igw_app"
    "${igw_prefix}readelf" -h "deploy/$igw_app"
done
{
    echo 'R17_RS485_UPLINK; R15 hardware+FIX1; CAN host R16 plus CAN-coordinated RS485 uploads to host R17. Board validation pending.'
    echo 'Builds Web, CAN uplink receiver and extended single-owner RS485 gateway. Existing R16 CAN/vault/NPU/capture binaries are copied on board.'
    "$igw_cc" --version | head -1
    sha256sum src/igw_web.c src/igw_can_uplink.c src/uplink_protocol.h src/uplink_snapshot.h src/igw_rs485_rw.c src/rs_uplink_protocol.h src/rs_uplink_host.h src/npu_bridge.h src/npu_model.h src/web_assets.h slave_r17/slave/main.c
} > deploy/build_info_r17.txt
(cd deploy && sha256sum igw_web igw_can_uplink igw_rs485_rw check_r15.sh run_web_gateway.sh run_secure_gateway.sh run_uplink_only.sh run_npu_local.sh install_on_board.sh build_info_r17.txt > SHA256SUMS.txt)
echo 'BUILD_COMPLETE_R17: copy entire deploy folder to main SD igw_apps/r17_rs485_uplink_install'
echo 'Slave: slave_r17/slave/main.c; rebuild ONLY the existing slave application with its own SDK.'
echo 'Main BOOT.bin/system.dtb and all FPGA bitstreams stay unchanged.'
