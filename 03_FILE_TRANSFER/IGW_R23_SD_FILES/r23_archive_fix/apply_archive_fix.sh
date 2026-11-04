#!/bin/sh
set -u
igw_patch=$(CDPATH= cd "$(dirname "$0")" && pwd) || exit 2
igw_app=${1:-/mnt/mmcblk0p1/igw_apps/r23_sd_files}
(cd "$igw_patch" && sha256sum -c SHA256SUMS.patch) || exit 2
[ -d "$igw_app" ] || { echo APP_DIRECTORY_MISSING >&2; exit 2; }
grep -q '^R23_DUAL_FILE_WEB' "$igw_app/build_info.txt" || { echo EXPECTED_R23_APP >&2; exit 2; }
if ps | grep -E 'igw_can_udp|igw_rs485_rw|igw_can_uplink|igw_can_pl_capture|igw_fault_online|igw_web|igw_can_file|igw_file_web|igw_rs485_file|igw_can_block' | grep -v grep >/dev/null; then
 echo 'Stop all main-board services/clients before patching; nothing changed.' >&2; exit 2
fi
(cd "$igw_app" && sha256sum -c SHA256SUMS.txt) || exit 2
igw_old=$(sha256sum "$igw_app/run_file_web.sh" | awk '{print $1}')
igw_new=$(sha256sum "$igw_patch/run_file_web.sh" | awk '{print $1}')
if [ "$igw_old" = "$igw_new" ]; then echo ARCHIVE_FIX_ALREADY_INSTALLED; exit 0; fi
[ "$igw_old" = bdc603ade05ca849471e67912392a6eb8c38a49103b04ce8e08e2ed68550a98d ] || { echo 'Launcher differs from the R23 baseline; preserve it for review. Nothing changed.' >&2; exit 2; }
igw_backup=$(mktemp -d "$igw_app/archive_fix_backup.XXXXXX") || exit 2
cp "$igw_app/run_file_web.sh" "$igw_app/SHA256SUMS.txt" "$igw_backup/" || exit 2
awk -v hash="$igw_new" '{ if($2=="run_file_web.sh"){print hash "  run_file_web.sh";found++}else print } END{if(found!=1)exit 2}' "$igw_app/SHA256SUMS.txt" > "$igw_backup/SHA256SUMS.new" || exit 2
if cp "$igw_patch/run_file_web.sh" "$igw_app/run_file_web.sh.new" &&
   mv "$igw_app/run_file_web.sh.new" "$igw_app/run_file_web.sh" &&
   cp "$igw_backup/SHA256SUMS.new" "$igw_app/SHA256SUMS.txt.new" &&
   mv "$igw_app/SHA256SUMS.txt.new" "$igw_app/SHA256SUMS.txt" &&
   (cd "$igw_app" && sha256sum -c SHA256SUMS.txt); then
 sync
 echo "ARCHIVE_FIX_INSTALLED BACKUP=$igw_backup"
 echo 'Existing R23 Web binary is supported; no rebuild, flash or slave change needed.'
else
 cp "$igw_backup/run_file_web.sh" "$igw_app/run_file_web.sh"
 cp "$igw_backup/SHA256SUMS.txt" "$igw_app/SHA256SUMS.txt"
 sync
 echo 'PATCH_FAILED; baseline restored if copies succeeded; inspect backup before starting.' >&2
 exit 3
fi
