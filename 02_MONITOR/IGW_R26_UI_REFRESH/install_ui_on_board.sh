#!/bin/sh
set -eu
cd "$(dirname "$0")"
sha256sum -c SHA256SUMS.txt
igw_apps=/mnt/mmcblk0p1/igw_apps
igw_r26="$igw_apps/r26_bidir_demo"
igw_r23="$igw_apps/r23_sd_files"
igw_new26="$igw_apps/r26_bidir_ui"
igw_new23="$igw_apps/r23_sd_ui"
[ -d "$igw_r26" ] && [ -d "$igw_r23" ] || { echo 'STOP: original R26 or R23 directory missing'; exit 1; }
[ ! -e "$igw_new26" ] && [ ! -e "$igw_new23" ] || { echo 'STOP: UI destination already exists; preserve it and inspect'; exit 1; }
for igw_comm in /proc/[0-9]*/comm; do
 [ -r "$igw_comm" ] || continue
 IFS= read -r igw_name < "$igw_comm" || continue
 case "$igw_name" in igw_*|yolo_demo*) echo "STOP: $igw_name is active; stop its owning launcher"; exit 1;; esac
done
for igw_lock in /tmp/igw_web_r10.lock /tmp/igw_rs485_once.lock; do
 [ ! -e "$igw_lock" ] || { echo "STOP: ownership lock exists: $igw_lock; inspect owner"; exit 1; }
done
(cd "$igw_r26" && sha256sum -c SHA256SUMS.txt)
(cd "$igw_r23" && sha256sum -c SHA256SUMS.txt)
[ "$(sha256sum "$igw_r26/igw_web" | awk '{print $1}')" = 2547617b5c34373a860e50ffd9c0b4ee45ea93fc74e2cb3ea5bf1c3f077781c3 ] || { echo 'STOP: R26 baseline web binary differs from reviewed source'; exit 1; }
[ "$(sha256sum "$igw_r23/igw_file_web" | awk '{print $1}')" = ca12c2df6f25af3b3e77f452f4f51770f1f503d03f69ee58ffbfa786e75c21dd ] || { echo 'STOP: R23 baseline web binary differs from reviewed source'; exit 1; }
igw_tmp26=$(mktemp -d "$igw_apps/.r26_ui.XXXXXX")
igw_tmp23=''
igw_cleanup(){ [ -z "$igw_tmp26" ] || rm -rf "$igw_tmp26"; [ -z "$igw_tmp23" ] || rm -rf "$igw_tmp23"; }
trap igw_cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
igw_tmp23=$(mktemp -d "$igw_apps/.r23_ui.XXXXXX")
cp -r "$igw_r26/." "$igw_tmp26/"
cp -r "$igw_r23/." "$igw_tmp23/"
cp ./igw_web "$igw_tmp26/igw_web"
cp ./igw_file_web "$igw_tmp23/igw_file_web"
chmod 755 "$igw_tmp26/igw_web" "$igw_tmp23/igw_file_web"
igw_hash26=$(sha256sum "$igw_tmp26/igw_web" | awk '{print $1}')
igw_hash23=$(sha256sum "$igw_tmp23/igw_file_web" | awk '{print $1}')
awk -v h="$igw_hash26" '$2=="igw_web"{$1=h;n++} {print $1 "  " $2} END{if(n!=1)exit 1}' "$igw_tmp26/SHA256SUMS.txt" > "$igw_tmp26/SHA256SUMS.new"
mv "$igw_tmp26/SHA256SUMS.new" "$igw_tmp26/SHA256SUMS.txt"
awk -v h="$igw_hash23" '$2=="igw_file_web"{$1=h;n++} {print $1 "  " $2} END{if(n!=1)exit 1}' "$igw_tmp23/SHA256SUMS.txt" > "$igw_tmp23/SHA256SUMS.new"
mv "$igw_tmp23/SHA256SUMS.new" "$igw_tmp23/SHA256SUMS.txt"
(cd "$igw_tmp26" && sha256sum -c SHA256SUMS.txt)
(cd "$igw_tmp23" && sha256sum -c SHA256SUMS.txt)
mv "$igw_tmp26" "$igw_new26"; igw_tmp26=''
mv "$igw_tmp23" "$igw_new23"; igw_tmp23=''
sync
echo 'UI_STAGE_PASS monitor=/mnt/mmcblk0p1/igw_apps/r26_bidir_ui file=/mnt/mmcblk0p1/igw_apps/r23_sd_ui'
echo 'Original R26/R23 directories retained. No service started; no BOOT, DTB, PL or slave changed.'
