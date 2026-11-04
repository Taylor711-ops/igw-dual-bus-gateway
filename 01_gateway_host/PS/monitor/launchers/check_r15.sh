#!/bin/sh
# Read-only preflight. Check files/live DT before any MMIO access.
set -eu
igw_fail() { echo "STOP: $*" >&2; exit 1; }
igw_hash=$(sha256sum /mnt/mmcblk0p1/BOOT.bin | awk '{print $1}')
[ "$igw_hash" = 83a016557c5e93f9bf26ef5017fce67eb4221a28322c24bdf8b19c81e8075215 ] || igw_fail 'not the reviewed R15 BOOT'
igw_hash=$(sha256sum /mnt/mmcblk0p1/system.dtb | awk '{print $1}')
[ "$igw_hash" = 771d4692c3de93c00095515359f769becf2cba8aee144752905dec984745a1af ] || igw_fail 'not R15 DTB FIX1'
for igw_node in vdmafb@80100000 axi_dynclk@80101000 vdma@80200000 mipicam@80300000; do
    igw_status=$(tr -d '\000' < "/sys/firmware/devicetree/base/soc/$igw_node/status")
    [ "$igw_status" = disabled ] || igw_fail "live $igw_node is not disabled"
done
igw_expect() {
    igw_value=$(devmem "$1" 32)
    [ "$igw_value" = "$2" ] || igw_fail "$1 expected $2 got $igw_value"
}
igw_expect 0xf8800200 0x00000005
igw_expect 0x80400000 0x32574749
igw_expect 0x80400004 0x00020000
igw_expect 0x80400008 0x0000001F
igw_expect 0x8040000C 0x00000000
igw_expect 0x80400018 0x02FAF080
igw_expect 0x80400800 0x4E414349
igw_expect 0x80400804 0x00010000
igw_expect 0x80400808 0x0000001F
igw_expect 0x80400818 0x02FAF080
igw_expect 0x8040081C 0x0007A120
igw_expect 0x80400844 0x00000010
igw_expect 0x1f0000020 0x000000BF
echo 'R15_PREFLIGHT=PASS (static checks only; no inference or frame-accuracy claim)'
