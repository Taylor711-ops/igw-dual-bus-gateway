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
    echo 'IGW_NPU_Secure_R12_2; frozen R6 model, new feature domain unvalidated; AES256-GCM archives; unchanged R9 CAN and R8 RS485 engines; slave remains R9'
    "$igw_cc" --version | head -1
    sha256sum src/igw_can_udp.c src/igw_rs485_rw.c src/rw_wire.h src/igw_web.c src/json_min.h src/web_assets.h src/igw_vault.c src/npu_bridge.h src/npu_model.h web/index.html npu/src/* npu/generated/* FEATURE_CONTRACT.json
} > deploy/build_info.txt
(cd deploy && sha256sum igw_can_udp igw_rs485_rw igw_web igw_vault igw_fault_online run_web_gateway.sh run_secure_gateway.sh run_npu_local.sh build_info.txt > SHA256SUMS.txt)
echo 'Build complete. Copy ALL TEN deploy files to C:\igw_npu_secure_r12_2 on Windows.'
echo 'Slave stays RW_R9. No new BOOT or FPGA image required.'
