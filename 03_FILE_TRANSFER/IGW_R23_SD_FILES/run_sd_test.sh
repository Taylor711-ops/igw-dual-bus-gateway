#!/bin/sh
set -u
cd "$(dirname "$0")" || exit 2
sha256sum -c SHA256SUMS.txt || exit 2
igw_mode=${1:-can}
igw_size=${2:-4096}
case "$igw_mode:$igw_size" in can:4096|can:65536|can:1048576|cross:4096|rs:4096|verify:4096|generate:8388608) :;; *) echo 'Usage: sh run_sd_test.sh can 4096|65536|1048576 ; cross 4096 ; rs 4096 ; verify 4096 ; generate 8388608'; exit 2;; esac
if ps | grep -E 'igw_can_udp|igw_rs485_rw|igw_can_uplink|igw_can_pl_capture|igw_fault_online|igw_web|igw_can_file|igw_file_web|igw_rs485_file|igw_can_block' | grep -v grep >/dev/null; then
 echo 'Stop existing main-board services before testing; nothing started.'; exit 2
fi
ip -details link show can0 | grep 'bitrate 500000' >/dev/null || { echo 'Restore CAN500k first'; exit 2; }
ip link show can0 | grep 'state UP' >/dev/null || exit 2
chmod +x igw_can_block igw_rs485_file || exit 2
igw_evidence=$(mktemp -d /tmp/igw_r23_file.XXXXXX) || exit 2
echo "EVIDENCE_DIRECTORY=$igw_evidence"
cp SHA256SUMS.txt build_info.txt "$igw_evidence/" || exit 2
ip -details -statistics link show can0 > "$igw_evidence/can_before.txt"
igw_child=
igw_stop=0
stop_owned(){ igw_stop=1; if [ -n "$igw_child" ]; then kill -TERM "$igw_child" 2>/dev/null || :; fi; }
trap stop_owned INT TERM
follow_log(){
 igw_next=1
 while :; do
  igw_done=0
  [ -e "$1.done" ] && igw_done=1
  igw_lines=$(wc -l < "$1")
  if [ "$igw_lines" -ge "$igw_next" ]; then sed -n "${igw_next},${igw_lines}p" "$1"; igw_next=$((igw_lines+1)); fi
  [ "$igw_done" -eq 1 ] && break
  sleep 1
 done
}
run_owned(){
 igw_log=$1; shift
 : > "$igw_evidence/$igw_log"
 "$@" > "$igw_evidence/$igw_log" 2>&1 &
 igw_child=$!
 follow_log "$igw_evidence/$igw_log" &
 igw_follow=$!
 while :; do
  wait "$igw_child"
  igw_status=$?
  if [ "$igw_status" -ge 128 ]; then
   if kill -0 "$igw_child" 2>/dev/null; then continue; fi
   wait "$igw_child" 2>/dev/null
   igw_again=$?
   if [ "$igw_again" -ne 127 ]; then igw_status=$igw_again; fi
  fi
  break
 done
 igw_child=
 : > "$igw_evidence/$igw_log.done"
 while :; do wait "$igw_follow"; kill -0 "$igw_follow" 2>/dev/null || break; done
 [ "$igw_stop" -eq 0 ] && [ "$igw_status" -eq 0 ]
}
can_round(){
 run_owned put.log ./igw_can_block --unit 1 put "input_$igw_size.bin" &&
 run_owned get.log ./igw_can_block --unit 1 get "$igw_evidence/readback.bin" &&
 cmp "input_$igw_size.bin" "$igw_evidence/readback.bin" &&
 run_owned generated.log ./igw_can_block --unit 2 generate "$igw_size" "$igw_evidence/device_generated.bin"
}
cross_round(){
 run_owned can_put.log ./igw_can_block --unit 1 put input_4096.bin &&
 run_owned rs_get.log ./igw_rs485_file --unit 1 --baud 115200 get "$igw_evidence/can_to_rs.bin" &&
 cmp input_4096.bin "$igw_evidence/can_to_rs.bin" &&
 run_owned rs_put.log ./igw_rs485_file --unit 2 --baud 115200 put reverse_4096.bin &&
 run_owned can_get.log ./igw_can_block --unit 2 get "$igw_evidence/rs_to_can.bin" &&
 cmp reverse_4096.bin "$igw_evidence/rs_to_can.bin" &&
 run_owned node1_again.log ./igw_can_block --unit 1 get "$igw_evidence/node1_unchanged.bin" &&
 cmp input_4096.bin "$igw_evidence/node1_unchanged.bin"
}
rs_round(){
 run_owned put.log ./igw_rs485_file --unit 1 --baud 115200 put input_4096.bin &&
 run_owned get.log ./igw_rs485_file --unit 1 --baud 115200 get "$igw_evidence/readback.bin" &&
 cmp input_4096.bin "$igw_evidence/readback.bin"
}
verify_round(){
 run_owned can_after_restart.log ./igw_can_block --unit 1 get "$igw_evidence/can_after_restart.bin" &&
 cmp input_4096.bin "$igw_evidence/can_after_restart.bin" &&
 run_owned rs_after_restart.log ./igw_rs485_file --unit 1 --baud 115200 get "$igw_evidence/rs_after_restart.bin" &&
 cmp input_4096.bin "$igw_evidence/rs_after_restart.bin"
}
igw_result=0
case "$igw_mode" in verify) verify_round || igw_result=3;; can) can_round || igw_result=3;; cross) cross_round || igw_result=3;; rs) rs_round || igw_result=3;; generate) run_owned generated_8MiB.log ./igw_can_block --unit 2 generate 8388608 "$igw_evidence/device_8MiB.bin" || igw_result=3;; esac
[ "$igw_stop" -eq 0 ] || igw_result=3
trap '' INT TERM
if [ "$igw_result" -eq 0 ]; then
 echo "RESULT=PASS_R23_FILE_TEST mode=$igw_mode bytes=$igw_size" | tee "$igw_evidence/result.txt"
 sha256sum "$igw_evidence/"*.bin > "$igw_evidence/file_sha256.txt"
 cat "$igw_evidence/file_sha256.txt"
else echo "RESULT=REVIEW_R23_FILE_TEST mode=$igw_mode; inspect logs; no automatic whole-file retry" | tee "$igw_evidence/result.txt"; fi
ip -details -statistics link show can0 > "$igw_evidence/can_after.txt"
igw_sd=/mnt/mmcblk0p1/igw_evidence/r23_sd_files
mkdir -p "$igw_sd"
if cp -r "$igw_evidence" "$igw_sd/"; then sync; echo "SD_EVIDENCE=$igw_sd/$(basename "$igw_evidence")"; else echo EVIDENCE_SAVE_FAILED; igw_result=3; fi
echo 'R23 test finished. Original monitoring, NPU and file Web services remain stopped.'
exit "$igw_result"
