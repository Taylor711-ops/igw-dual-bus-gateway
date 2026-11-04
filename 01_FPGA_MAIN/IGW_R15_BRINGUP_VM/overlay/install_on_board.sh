#!/bin/sh
# Copy verified R13 applications into a separate R15 test directory.
# Does not start services or write hardware/BOOT/DTB.
set -eu
cd "$(dirname "$0")"
sha256sum -c SHA256SUMS.txt
igw_base=/mnt/mmcblk0p1/igw_apps/multi_r13
igw_dest=/mnt/mmcblk0p1/igw_apps/r15_bringup
grep -q ' /mnt/mmcblk0p1 ' /proc/mounts || { echo 'STOP: SD not mounted'; exit 1; }
[ ! -e "$igw_dest" ] || { echo 'STOP: r15_bringup already exists; keeping it unchanged'; exit 1; }
[ -f "$igw_base/SHA256SUMS.txt" ] || { echo 'STOP: verified multi_r13 installation not found'; exit 1; }
(cd "$igw_base" && sha256sum -c SHA256SUMS.txt)
grep -q 'R13' "$igw_base/build_info.txt" || { echo 'STOP: expected R13 baseline'; exit 1; }
igw_stage=$igw_dest.incoming.$$
mkdir "$igw_stage"
trap 'echo "Installation interrupted; incomplete files retained in $igw_stage" >&2' EXIT
for igw_file in igw_can_udp igw_rs485_rw igw_web igw_vault igw_fault_online run_npu_local.sh build_info.txt; do
    cp "$igw_base/$igw_file" "$igw_stage/$igw_file"
done
cp "$igw_base/SHA256SUMS.txt" "$igw_stage/R13_BASELINE_SHA256SUMS.txt"
for igw_file in check_r15.sh run_web_gateway.sh run_secure_gateway.sh igw_can_pl_capture; do
    cp "$igw_file" "$igw_stage/$igw_file"
done
chmod +x "$igw_stage/igw_can_udp" "$igw_stage/igw_rs485_rw" "$igw_stage/igw_web" "$igw_stage/igw_vault" "$igw_stage/igw_fault_online" "$igw_stage/igw_can_pl_capture"
(
    cd "$igw_stage"
    sha256sum igw_can_udp igw_rs485_rw igw_web igw_vault igw_fault_online igw_can_pl_capture run_npu_local.sh run_web_gateway.sh run_secure_gateway.sh check_r15.sh build_info.txt R13_BASELINE_SHA256SUMS.txt > SHA256SUMS.txt
    sha256sum -c SHA256SUMS.txt
)
mv "$igw_stage" "$igw_dest"
sync
trap - EXIT
echo "R15_STAGED=$igw_dest; no services started; R13 directory unchanged"
