#!/usr/bin/env bash
set -eu
cd "$(dirname -- "${BASH_SOURCE[0]}")"
sha256sum -c SOURCE_SHA256SUMS.txt
igw_prefix="${CROSS_COMPILE:-$HOME/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-}"
igw_cc="${igw_prefix}gcc"
case "$("$igw_cc" -dumpmachine)" in aarch64*linux*) ;; *) echo 'Need AArch64 Linux cross compiler' >&2; exit 1;; esac
for igw_app in igw_can_udp igw_rs485_rw igw_web; do
    "$igw_cc" -std=c11 -O2 -Wall -Wextra -Wmissing-field-initializers -Werror -static -pthread "src/$igw_app.c" -lm -o "deploy/$igw_app"
    "${igw_prefix}readelf" -h "deploy/$igw_app"
done
"$igw_cc" -std=c11 -O2 -Wall -Wextra -Wmissing-field-initializers -Werror src/igw_vault.c -ldl -o deploy/igw_vault
"${igw_prefix}readelf" -h deploy/igw_vault
igw_buildroot="${MLK_BUILDROOT_DIR:-$HOME/uisrc-lab-anlogic/sources/buildroot}"
igw_sdk="${IGW_ARMNN_ROOT:-$igw_buildroot/output/target/opt/armnn_lib}"
test -f "$igw_sdk/include/armnn/IRuntime.hpp" || { echo "Missing ArmNN headers: $igw_sdk/include";exit 1; }
test -f "$igw_sdk/lib/libarmnn.so" || { echo "Missing ArmNN library: $igw_sdk/lib";exit 1; }
"${igw_prefix}g++" -std=c++14 -O2 -Wall -Wextra -Werror -I "$igw_sdk/include" npu/src/igw_fault_online.cpp -L"$igw_sdk/lib" -Wl,-rpath-link,"$igw_sdk/lib" -Wl,-rpath,/opt/armnn_lib/lib -larmnn -ldl -lpthread -o deploy/igw_fault_online
"${igw_prefix}readelf" -h deploy/igw_fault_online
{
    echo 'IGW_Multi_Device_R13; addressed CAN+RS485 nodes1..16; AES256-GCM; frozen NPU model scope=node1 only; target and multi-device classification validation pending'
    "$igw_cc" --version | head -1
    sha256sum src/igw_can_udp.c src/igw_rs485_rw.c src/rw_wire.h src/igw_web.c src/json_min.h src/web_assets.h src/igw_vault.c src/npu_bridge.h src/npu_model.h web/index.html npu/src/* npu/generated/* FEATURE_CONTRACT.json
} > deploy/build_info.txt
(cd deploy && sha256sum igw_can_udp igw_rs485_rw igw_web igw_vault igw_fault_online run_web_gateway.sh run_secure_gateway.sh run_npu_local.sh install_r13.sh build_info.txt > SHA256SUMS.txt)
echo 'Build complete. Copy the entire deploy folder (eleven files) to C:\igw_multi_r13 on Windows.'
echo 'Master BOOT stays pinned R2. Slave application MUST be rebuilt from slave_r13/slave/main.c; preserve its existing FPGA design and SDK.'
