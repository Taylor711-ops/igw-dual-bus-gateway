#!/bin/sh
set -eu
cd "$(dirname "$0")"
sha256sum -c SHA256SUMS.txt
igw_apps=/mnt/mmcblk0p1/igw_apps
igw_base="$igw_apps/r23_sd_unit3"
igw_dest="$igw_apps/r23_can1_rs3_parallel"
[ -d "$igw_base" ] || { echo 'STOP: existing r23_sd_unit3 not found'; exit 1; }
[ ! -e "$igw_dest" ] || { echo 'STOP: destination already exists; preserve and inspect it'; exit 1; }
for igw_comm in /proc/[0-9]*/comm; do
 [ -r "$igw_comm" ] || continue
 IFS= read -r igw_name < "$igw_comm" || continue
 case "$igw_name" in igw_*|yolo_demo*) echo "STOP: $igw_name is running; stop its owning launcher first"; exit 1;; esac
done
for igw_lock in /tmp/igw_web_r10.lock /tmp/igw_rs485_once.lock; do
 [ ! -e "$igw_lock" ] || { echo "STOP: ownership lock exists: $igw_lock"; exit 1; }
done
(cd "$igw_base" && sha256sum -c SHA256SUMS.txt)
igw_tmp=$(mktemp -d "$igw_apps/.r23_can1_rs3_parallel.XXXXXX")
igw_cleanup(){ [ -z "$igw_tmp" ] || rm -rf "$igw_tmp"; }
trap igw_cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
cp -r "$igw_base/." "$igw_tmp/"
cp ./igw_file_web ./igw_rs485_file ./igw_can_block ./run_file_web.sh "$igw_tmp/"
chmod 755 "$igw_tmp/igw_file_web" "$igw_tmp/igw_rs485_file" "$igw_tmp/igw_can_block" "$igw_tmp/run_file_web.sh"
cat > "$igw_tmp/build_info.txt" <<'EOF'
R23_CAN1_RS3_PARALLEL
Nodes 1/2 require their existing R23 SD file firmware; node 3 requires IGW3 Zynq PS/PL file firmware and its own TF card.
CAN 500k for nodes 1/2; shared RS485 file bus 9600 for all three nodes.
Max 8388608 bytes per node; concurrent pair CAN node 1 and RS485 node 3 only; uploads are read back and SHA256 verified.
Parallel hardware transfer and power-loss recovery require board testing.
EOF
igw_hash_web=$(sha256sum "$igw_tmp/igw_file_web" | awk '{print $1}')
igw_hash_rs=$(sha256sum "$igw_tmp/igw_rs485_file" | awk '{print $1}')
igw_hash_can=$(sha256sum "$igw_tmp/igw_can_block" | awk '{print $1}')
igw_hash_run=$(sha256sum "$igw_tmp/run_file_web.sh" | awk '{print $1}')
igw_hash_info=$(sha256sum "$igw_tmp/build_info.txt" | awk '{print $1}')
awk -v web="$igw_hash_web" -v rs="$igw_hash_rs" -v can="$igw_hash_can" -v run="$igw_hash_run" -v info="$igw_hash_info" '
 $2=="igw_file_web"{$1=web;n++} $2=="igw_rs485_file"{$1=rs;m++} $2=="igw_can_block"{$1=can;q++} $2=="run_file_web.sh"{$1=run;r++} $2=="build_info.txt"{$1=info;k++}
 {print $1 "  " $2} END{if(n!=1||m!=1||q!=1||r!=1||k!=1)exit 1}' "$igw_tmp/SHA256SUMS.txt" > "$igw_tmp/SHA256SUMS.new"
mv "$igw_tmp/SHA256SUMS.new" "$igw_tmp/SHA256SUMS.txt"
(cd "$igw_tmp" && sha256sum -c SHA256SUMS.txt)
mv "$igw_tmp" "$igw_dest"
igw_tmp=''
sync
echo "PARALLEL_FILE_STAGED=$igw_dest"
echo 'Original r23_sd_unit3 retained. Nothing started; no flash, BOOT, DTB or slave changed.'
