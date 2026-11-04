#!/bin/sh
# NOT for the currently deployed R13 BOOT. Only after R15 timing/DT/boot review.
set -eu
igw_expected=${R15_BOOT_SHA256:-}
case "$igw_expected" in *[!0123456789abcdef]*|'') echo 'Set reviewed R15_BOOT_SHA256 after installing and cold-booting R15.' >&2; exit 2;; esac
[ "${#igw_expected}" -eq 64 ] || exit 2
# Known R13 image cannot contain this new peripheral.
[ "$igw_expected" != '7b81535086c3096ded1e15061118236978730d56f9132d2616bc573cc8dd734c' ] || { echo 'Refusing old R13 BOOT' >&2; exit 2; }
igw_actual=$(sha256sum /mnt/mmcblk0p1/BOOT.bin)
igw_actual=${igw_actual%% *}
[ "$igw_actual" = "$igw_expected" ] || { echo 'BOOT file SHA mismatch' >&2; exit 2; }
[ "${R15_COLD_BOOT_CONFIRMED:-}" = YES ] || { echo 'Cold boot of that R15 image must be confirmed first.' >&2; exit 2; }
igw_here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$igw_here/igw_can_pl_capture" --capture-seconds "${1:-10}"
