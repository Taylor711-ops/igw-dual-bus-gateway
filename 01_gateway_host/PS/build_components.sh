#!/usr/bin/env bash
# Compile the organized host sources. This does not install to a board.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
prefix=${CROSS_COMPILE:-"$HOME/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-"}
cc="${prefix}gcc"
command -v "$cc" >/dev/null || { echo 'Set CROSS_COMPILE to the AArch64 Linux compiler prefix.' >&2; exit 2; }
case "$("$cc" -dumpmachine)" in aarch64*linux*) ;; *) echo 'Need an AArch64 Linux cross compiler.' >&2; exit 2;; esac
python3 "$root/04_web/generate_embedded.py"
mkdir -p "$here/build/monitor" "$here/build/file_transfer"
for app in igw_can_udp igw_can_uplink igw_rs485_rw igw_web; do
 "$cc" -std=c11 -O2 -Wall -Wextra -Wmissing-field-initializers -Werror -static -pthread "$here/monitor/src/$app.c" -lm -o "$here/build/monitor/$app"
done
"$cc" -std=c11 -O2 -Wall -Wextra -Wmissing-field-initializers -Werror "$here/monitor/src/igw_vault.c" -ldl -o "$here/build/monitor/igw_vault"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror "$here/monitor/src/igw_can_pl_capture.c" -o "$here/build/monitor/igw_can_pl_capture"
for app in igw_file_web igw_rs485_file igw_can_block; do
 "$cc" -std=gnu11 -O2 -Wall -Wextra -Werror -static "$here/file_transfer/src/$app.c" -o "$here/build/file_transfer/$app"
done
if [ -n "${IGW_ARMNN_ROOT:-}" ]; then
 test -f "$IGW_ARMNN_ROOT/include/armnn/IRuntime.hpp"
 test -f "$IGW_ARMNN_ROOT/lib/libarmnn.so"
 "${prefix}g++" -std=c++14 -O2 -Wall -Wextra -Werror -I "$IGW_ARMNN_ROOT/include" "$here/monitor/npu/src/igw_fault_online.cpp" -L"$IGW_ARMNN_ROOT/lib" -Wl,-rpath-link,"$IGW_ARMNN_ROOT/lib" -Wl,-rpath,/opt/armnn_lib/lib -larmnn -ldl -lpthread -o "$here/build/monitor/igw_fault_online"
else
 echo 'NPU build skipped: set IGW_ARMNN_ROOT to the ArmNN SDK directory.'
fi
echo 'HOST_COMPONENTS_BUILT: output is source-build only, not a board installer.'
