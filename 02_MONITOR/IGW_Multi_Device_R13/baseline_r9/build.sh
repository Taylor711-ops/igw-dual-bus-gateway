#!/usr/bin/env bash
set -eu
cd "$(dirname -- "${BASH_SOURCE[0]}")"
sha256sum -c SOURCE_SHA256SUMS.txt
igw_prefix="${CROSS_COMPILE:-$HOME/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-}"
igw_cc="${igw_prefix}gcc"
case "$("$igw_cc" -dumpmachine)" in
    aarch64*linux*) ;;
    *) echo 'Need AArch64 Linux cross compiler' >&2; exit 1 ;;
esac
for igw_app in igw_can_udp igw_rs485_rw; do
    "$igw_cc" -std=c11 -O2 -Wall -Wextra -Wmissing-field-initializers -Werror -static "src/$igw_app.c" -o "deploy/$igw_app"
    "${igw_prefix}readelf" -h "deploy/$igw_app"
done
{
    echo 'IGW_CAN_RS485_RW_R9'
    "$igw_cc" --version | head -1
    sha256sum src/igw_can_udp.c src/igw_rs485_rw.c src/rw_wire.h slave/main.c
} > deploy/build_info.txt
(cd deploy && sha256sum igw_can_udp igw_rs485_rw run_rw_gateway.sh build_info.txt > SHA256SUMS.txt)
echo 'Build complete. Copy ALL FIVE deploy files to C:\igw_can_rs485_rw_r9 on Windows.'
echo 'Slave main.c must be built separately in the existing baremetal SLAVE project.'
