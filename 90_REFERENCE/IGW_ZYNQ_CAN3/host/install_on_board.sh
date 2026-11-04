#!/bin/sh
set -eu
cd "$(dirname "$0")"
sha256sum -c SHA256SUMS.txt
igw_apps=/mnt/mmcblk0p1/igw_apps
igw_base="$igw_apps/r26_bidir_ui"
igw_dest="$igw_apps/r26_can3_demo"
grep -q ' /mnt/mmcblk0p1 ' /proc/mounts || { echo 'STOP: main SD is not mounted'; exit 1; }
[ -d "$igw_base" ] || { echo 'STOP: reviewed UI baseline directory missing'; exit 1; }
[ ! -e "$igw_dest" ] || { echo 'STOP: destination exists; preserve it'; exit 1; }
for igw_comm in /proc/[0-9]*/comm; do
 [ -r "$igw_comm" ] || continue
 IFS= read -r igw_name < "$igw_comm" || continue
 case "$igw_name" in igw_*|yolo_demo*) echo "STOP: $igw_name is active; stop its owning launcher"; exit 1;; esac
done
for igw_lock in /tmp/igw_web_r10.lock /tmp/igw_rs485_once.lock; do
 [ ! -e "$igw_lock" ] || { echo "STOP: ownership lock exists: $igw_lock; inspect its owner"; exit 1; }
done
(cd "$igw_base" && sha256sum -c SHA256SUMS.txt)
igw_stage=$(mktemp -d "$igw_apps/.r26_can3.XXXXXX")
igw_cleanup(){ [ -z "$igw_stage" ] || rm -rf "$igw_stage"; }
trap igw_cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
cp -r "$igw_base/." "$igw_stage/"
cp igw_web run_web_gateway.sh "$igw_stage/"
chmod 755 "$igw_stage/igw_web" "$igw_stage/run_web_gateway.sh"
igw_webhash=$(sha256sum "$igw_stage/igw_web" | awk '{print $1}')
igw_runhash=$(sha256sum "$igw_stage/run_web_gateway.sh" | awk '{print $1}')
awk -v wh="$igw_webhash" -v rh="$igw_runhash" '$2=="igw_web"{$1=wh;a++} $2=="run_web_gateway.sh"{$1=rh;b++} {print $1 "  " $2} END{if(a!=1||b!=1)exit 1}' "$igw_stage/SHA256SUMS.txt" > "$igw_stage/SHA256SUMS.new"
mv "$igw_stage/SHA256SUMS.new" "$igw_stage/SHA256SUMS.txt"
(cd "$igw_stage" && sha256sum -c SHA256SUMS.txt)
mv "$igw_stage" "$igw_dest";igw_stage=''
sync
echo "CAN3_HOST_STAGED=$igw_dest; no service started; original UI retained"
