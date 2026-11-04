#!/bin/sh
# Stage only. Does not replace BOOT.bin, stop services or access a bus.
set -eu
igw_root=/mnt/mmcblk0p1/igw_apps
igw_dest=$igw_root/multi_r13
igw_url=http://192.168.31.100:8019
grep -q ' /mnt/mmcblk0p1 ' /proc/mounts || { echo 'STOP: SD not mounted'; exit 1; }
igw_install_lock=/tmp/igw_r13_install.lock
mkdir "$igw_install_lock" || { echo 'STOP: another R13 installer owns the lock'; exit 1; }
trap 'rmdir "$igw_install_lock"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
if [ -e "$igw_dest" ]; then
    echo 'Existing R13 directory: verifying without replacing files.'
    (cd "$igw_dest" && sha256sum -c SHA256SUMS.txt)
    echo "STAGED=$igw_dest; services were not started"
    exit 0
fi
mkdir -p "$igw_root"
igw_stage=$igw_root/multi_r13.incoming.$$
mkdir "$igw_stage"
cd "$igw_stage"
for igw_file in igw_can_udp igw_rs485_rw igw_web igw_vault igw_fault_online run_web_gateway.sh run_secure_gateway.sh run_npu_local.sh install_r13.sh build_info.txt SHA256SUMS.txt; do
    wget -O "$igw_file" "$igw_url/$igw_file"
done
sha256sum -c SHA256SUMS.txt
chmod +x igw_can_udp igw_rs485_rw igw_web igw_vault igw_fault_online
sync
mv "$igw_stage" "$igw_dest"
sync
echo "STAGED=$igw_dest; services were not started; slave application must be R13"
