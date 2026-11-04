#!/bin/sh
# Read-only platform collection. No devmem, driver unbind, boot write or TX.
set -eu
igw_uptime=$(cut -d. -f1 /proc/uptime)
igw_collect_dir="/tmp/igw_r15_linux_${igw_uptime}_$$"
mkdir -p "$igw_collect_dir"
{
    echo 'PROFILE=R15_GATEWAY_ONLY_PREDEPLOY_READONLY'
    uname -a
    echo '--- SD mounted filesystems ---'
    awk '$2 ~ /^\/mnt\/mmcblk/ {print}' /proc/mounts
    echo '--- SD boot-directory file list ---'
    ls -l /mnt/mmcblk0p1
    echo '--- Existing SD BOOT file hash (does not prove running image identity) ---'
    if [ -f /mnt/mmcblk0p1/BOOT.bin ]; then sha256sum /mnt/mmcblk0p1/BOOT.bin; fi
    echo '--- Platform devices and bound drivers ---'
    for igw_dev in /sys/bus/platform/devices/*; do
        [ -d "$igw_dev" ] || continue
        igw_dev_name=${igw_dev##*/}
        case "$igw_dev_name" in
            *80100000*|*80200000*|*80300000*|*80400000*|*npu*|*NPU*|*vdma*|*video*|*frame*|*mipi*|*imx*|*hdmi*)
                echo "DEVICE=$igw_dev_name"
                if [ -L "$igw_dev/driver" ]; then readlink "$igw_dev/driver"; fi
                if [ -f "$igw_dev/modalias" ]; then cat "$igw_dev/modalias"; fi
                ;;
        esac
    done
    echo '--- Framebuffer names ---'
    for igw_fb in /sys/class/graphics/fb*/name; do
        [ -f "$igw_fb" ] || continue
        echo "$igw_fb"; cat "$igw_fb"
    done
    echo '--- Relevant kernel messages ---'
    dmesg | grep -Ei 'vdma|fvdma|video|hdmi|mipi|framebuffer|imx334|hard_npu|soft_npu|80100000|80200000|80300000' || :
} > "$igw_collect_dir/report.txt" 2>&1
if [ -r /sys/firmware/fdt ]; then
    cp /sys/firmware/fdt "$igw_collect_dir/live.dtb"
fi
if [ -d /sys/firmware/devicetree/base ]; then
    # Capture the live tree as a raw tar; this Buildroot tar has no -z support.
    tar -cf "$igw_collect_dir/live_tree.tar" -C /sys/firmware/devicetree/base .
fi
tar -cf "$igw_collect_dir.tar" -C "$igw_collect_dir" .
cat "$igw_collect_dir/report.txt"
echo "EVIDENCE_ARCHIVE=$igw_collect_dir.tar"
echo 'RESULT=COLLECTED_READONLY; current running gateways and BOOT files unchanged'
