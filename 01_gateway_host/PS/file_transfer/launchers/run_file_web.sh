#!/bin/sh
set -u
cd "$(dirname "$0")" || exit 2
sha256sum -c SHA256SUMS.txt || exit 2
chmod +x ./igw_can_block ./igw_rs485_file ./igw_file_web || exit 2
if ps | grep -E 'igw_can_udp|igw_rs485_rw|igw_can_uplink|igw_can_pl_capture|igw_fault_online|igw_web|igw_can_file|igw_file_web|igw_rs485_file|igw_can_block' | grep -v grep >/dev/null; then
 echo 'Stop existing main-board services/file clients first. No service started.' >&2
 exit 2
fi
ip -4 addr show dev eth0 | grep '192.168.31.222/24' >/dev/null || { echo 'eth0 address must be 192.168.31.222/24' >&2; exit 2; }
ip -details link show can0 | grep 'bitrate 500000' >/dev/null || { echo 'Restore existing UP/500000 CAN setup first'; exit 2; }
ip link show can0 | grep 'state UP' >/dev/null || exit 2
igw_launch=$(mktemp -d /tmp/igw_r23_launcher.XXXXXX) || exit 2
ip -details -statistics link show can0 > "$igw_launch/can_before.txt"
igw_pid=
request_stop(){ if [ -n "$igw_pid" ]; then kill -TERM "$igw_pid" 2>/dev/null || :; fi; }
trap request_stop INT TERM
./igw_file_web --can-backend "$(pwd)/igw_can_block" --rs-backend "$(pwd)/igw_rs485_file" > "$igw_launch/service.log" 2>&1 &
igw_pid=$!
sleep 1
cat "$igw_launch/service.log"
while :; do
 wait "$igw_pid"
 igw_result=$?
 if [ "$igw_result" -ge 128 ]; then
  if kill -0 "$igw_pid" 2>/dev/null; then continue; fi
  wait "$igw_pid" 2>/dev/null
  igw_again=$?
  if [ "$igw_again" -ne 127 ]; then igw_result=$igw_again; fi
 fi
 break
done
igw_pid=
trap '' INT TERM
ip -details -statistics link show can0 > "$igw_launch/can_after.txt"
igw_evidence=$(sed -n 's/^EVIDENCE_DIRECTORY=//p' "$igw_launch/service.log" | head -n 1)
igw_saved=0
case "$igw_evidence" in
 /tmp/igw_r23_files.*|/tmp/igw_r22_files.*)
  if [ -d "$igw_evidence" ] && [ ! -L "$igw_evidence" ]; then
   if cp "$igw_launch/service.log" "$igw_launch/can_before.txt" "$igw_launch/can_after.txt" SHA256SUMS.txt build_info.txt "$igw_evidence/" &&
      mkdir -p /mnt/mmcblk0p1/igw_evidence/r23_file_web &&
      cp -r "$igw_evidence" /mnt/mmcblk0p1/igw_evidence/r23_file_web/; then
    sync
    echo "SD_EVIDENCE=/mnt/mmcblk0p1/igw_evidence/r23_file_web/$(basename "$igw_evidence")"
    igw_saved=1
   fi
  fi;;
 *) echo "ARCHIVE_REJECTED_RUNTIME=$igw_evidence" >&2;;
esac
cat "$igw_launch/service.log"
echo "R23_LAUNCHER_EXIT=$igw_result EVIDENCE_SAVED=$igw_saved"
echo 'Original monitoring/NPU services remain stopped. Files are plaintext; slave files use independent SD; board persistence validation required.'
[ "$igw_saved" -eq 1 ] || exit 3
exit "$igw_result"
