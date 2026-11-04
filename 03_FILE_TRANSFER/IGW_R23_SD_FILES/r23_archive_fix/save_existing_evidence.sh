#!/bin/sh
set -u
igw_src=${1:-/tmp/igw_r22_files.dSpmSX}
case "$igw_src" in /tmp/igw_r22_files.*|/tmp/igw_r23_files.*) :;; *) echo 'Refusing unrelated source path' >&2; exit 2;; esac
[ -d "$igw_src" ] && [ ! -L "$igw_src" ] || { echo 'Source missing; do not create an empty replacement.' >&2; exit 2; }
if ps | grep -E 'igw_file_web|igw_can_block|igw_rs485_file' | grep -v grep >/dev/null; then echo 'Stop active file service/clients before saving a final snapshot.' >&2; exit 2; fi
igw_sd=/mnt/mmcblk0p1/igw_evidence/r23_file_web
mkdir -p "$igw_sd" || exit 2
igw_dest=$(mktemp -d "$igw_sd/recovered_$(basename "$igw_src").XXXXXX") || exit 2
cp -r "$igw_src/." "$igw_dest/" || exit 3
igw_matched=0
for igw_launch in /tmp/igw_r23_launcher.*; do
 [ -f "$igw_launch/service.log" ] || continue
 if grep -Fqx "EVIDENCE_DIRECTORY=$igw_src" "$igw_launch/service.log"; then
  mkdir -p "$igw_dest/launcher_records/$(basename "$igw_launch")" || exit 3
  cp -r "$igw_launch/." "$igw_dest/launcher_records/$(basename "$igw_launch")/" || exit 3
  igw_matched=$((igw_matched+1))
 fi
done
{
 echo 'R23 evidence manually recovered after automatic archive path mismatch.'
 echo "ORIGINAL_RUNTIME=$igw_src"
 echo "MATCHED_LAUNCHER_RECORDS=$igw_matched"
 echo 'This is a copied evidence snapshot, not a transfer PASS or proof of prior automatic save.'
} > "$igw_dest/RECOVERY_NOTE.txt" || exit 3
(cd "$igw_dest" && find . -type f ! -name RECOVERY_SHA256SUMS.txt | sort | while IFS= read -r igw_file; do sha256sum "$igw_file" || exit 1; done) > "$igw_dest/RECOVERY_SHA256SUMS.txt" || exit 3
(cd "$igw_dest" && sha256sum -c RECOVERY_SHA256SUMS.txt) || exit 3
sync
echo "SD_EVIDENCE_RECOVERED=$igw_dest"
if [ -f "$igw_dest/49278cdcdbe5dc30d1ebd736d08767fa.log" ]; then tail -20 "$igw_dest/49278cdcdbe5dc30d1ebd736d08767fa.log"; fi
