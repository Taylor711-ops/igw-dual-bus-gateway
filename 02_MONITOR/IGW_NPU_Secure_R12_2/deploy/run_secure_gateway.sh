#!/bin/sh
set -eu
cd "$(dirname "$0")"
sha256sum -c SHA256SUMS.txt
[ -x ./igw_vault ]
[ -d /mnt/mmcblk0p1 ]
grep -q ' /mnt/mmcblk0p1 ' /proc/mounts || { echo 'STOP: SD partition is not mounted'; exit 1; }
echo 'R12: enter a private passphrase in this terminal; first start asks twice.'
echo 'The passphrase is NOT saved. Do not send it in screenshots or chat.'
exec ./igw_vault serve --vault /mnt/mmcblk0p1/igw_vault_r11 --run-script ./run_web_gateway.sh --interval 30
