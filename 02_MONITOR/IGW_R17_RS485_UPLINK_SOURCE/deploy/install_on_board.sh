#!/bin/sh
# Copy existing verified R15 baseline; install new Web/uplink in a new directory.
set -eu
cd "$(dirname "$0")"
sha256sum -c SHA256SUMS.txt
igw_base=/mnt/mmcblk0p1/igw_apps/r16_can_uplink
igw_dest=/mnt/mmcblk0p1/igw_apps/r17_rs485_uplink
grep -q ' /mnt/mmcblk0p1 ' /proc/mounts
[ ! -e "$igw_dest" ] || { echo 'STOP: R17 directory exists; keeping it unchanged';exit 1; }
(cd "$igw_base" && sha256sum -c SHA256SUMS.txt)
igw_stage=$igw_dest.incoming.$$
mkdir "$igw_stage"
for igw_file in igw_can_udp igw_vault igw_fault_online igw_can_pl_capture run_npu_local.sh build_info.txt; do
    cp "$igw_base/$igw_file" "$igw_stage/$igw_file"
done
cp "$igw_base/SHA256SUMS.txt" "$igw_stage/R16_BASELINE_SHA256SUMS.txt"
for igw_file in igw_web igw_can_uplink igw_rs485_rw check_r15.sh run_web_gateway.sh run_secure_gateway.sh run_uplink_only.sh build_info_r17.txt; do
    cp "$igw_file" "$igw_stage/$igw_file"
done
chmod +x "$igw_stage"/igw_*
(
    cd "$igw_stage"
    sha256sum igw_can_udp igw_rs485_rw igw_web igw_vault igw_fault_online igw_can_pl_capture igw_can_uplink check_r15.sh run_web_gateway.sh run_secure_gateway.sh run_npu_local.sh run_uplink_only.sh build_info.txt build_info_r17.txt R16_BASELINE_SHA256SUMS.txt > SHA256SUMS.txt
    sha256sum -c SHA256SUMS.txt
)
mv "$igw_stage" "$igw_dest"
sync
echo "R17_STAGED=$igw_dest; nothing started; original R16 preserved"
