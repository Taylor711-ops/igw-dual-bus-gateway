#!/bin/sh
# Owns only the strict-Alnpu worker. Runtime evidence stays in volatile /tmp;
# validated results enter the web service's existing encrypted snapshot stream.
set -eu
igw_base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
igw_lock=/tmp/igw_alnpu_probe.lock
igw_pid=''
igw_ready=0
igw_reason=startup_failure
igw_uncertain=0
igw_out=/tmp/igw_npu_local_r12_$$
igw_live() {
 kill -0 "$1" 2>/dev/null || return 1
 [ "$(awk '{print $3}' "/proc/$1/stat" 2>/dev/null)" != Z ]
}
igw_hard() {
 awk '/HARD_NPU_IRQ/{for(i=2;i<=NF;i++){if($i!~/^[0-9]+$/)break;s+=$i}found=1}END{if(!found)exit 1;printf "%.0f\n",s}' /proc/interrupts
}
igw_cleanup() {
 trap - EXIT INT TERM HUP
 if [ -n "$igw_pid" ]; then
  kill -TERM "$igw_pid" 2>/dev/null || :
  igw_n=0
  while igw_live "$igw_pid" && [ "$igw_n" -lt 3 ]; do sleep 1; igw_n=$((igw_n+1)); done
  if igw_live "$igw_pid"; then igw_uncertain=1; echo 'REVIEW: NPU child still alive; lock retained; reboot before retry'; else wait "$igw_pid" 2>/dev/null || :; fi
 fi
 igw_after=$(igw_hard) || igw_after=0
 echo "NPU_STOP reason=$igw_reason HARD_AFTER=$igw_after CAPABILITY=$(devmem 0x1f0000020 32)"
 if [ "$igw_uncertain" = 0 ]; then rm -f "$igw_lock/pid";rmdir "$igw_lock" || :; fi
}
mkdir "$igw_lock" || { echo 'STOP: NPU lock already owned; stop its launcher first';exit 1; }
echo "$$" > "$igw_lock/pid"
trap igw_cleanup EXIT
trap 'igw_reason=operator_stop;exit 0' INT TERM HUP
mkdir "$igw_out"
(cd "$igw_base" && sha256sum -c SHA256SUMS.txt)
[ -x "$igw_base/igw_fault_online" ]
for igw_dev in hard_npu soft_npu cma_mem;do [ -c "/dev/$igw_dev" ];done
[ "$(devmem 0x1f0000020 32)" = 0x000000BF ]
[ "$(sha256sum /opt/armnn_lib/lib/libarmnn.so.32.1 | awk '{print $1}')" = 5def7ba75e4b59644be6f58deaf8b2bb3791c62ed38b52b6aea08b11a8f728ce ]
igw_before=$(igw_hard)
echo "NPU_START HARD_BEFORE=$igw_before LOG=$igw_out/program.log"
(cd "$igw_out" && export LD_LIBRARY_PATH=/opt/armnn_lib/lib && exec "$igw_base/igw_fault_online") > "$igw_out/program.log" 2>&1 &
igw_pid=$!
igw_n=0;igw_busy=0;igw_gated=0
while igw_live "$igw_pid";do
 igw_state=$(cat "$igw_out/state" 2>/dev/null) || igw_state=starting
 if [ "$igw_gated" = 0 ] && [ "$igw_state" = self_test_done ];then
  igw_after=$(igw_hard)
  if [ $((igw_after-igw_before)) -ne 450 ] ||
     ! grep -q '^SELF_TEST=PASS HIDDEN_CASES=150 CLASSIFIER_CASES=150' "$igw_out/program.log" ||
     [ "$(devmem 0x1f0000020 32)" != 0x000000BF ];then
   igw_reason=startup_hardware_check_failed;exit 1
  fi
  echo 'NPU_STARTUP_IRQ_DELTA HARD=450; self-test=PASS'
  : > "$igw_out/allow";igw_gated=1
 fi
 if [ "$igw_ready" = 0 ] && grep -q '^READY ONLINE_NPU ' "$igw_out/program.log";then
  igw_ready=1;echo 'NPU_LOCAL_READY UDP127.0.0.1:16002; strict Alnpu; classification transfer validation pending'
 fi
 if [ "$igw_ready" = 0 ] && [ "$igw_n" -ge 40 ];then igw_reason=startup_timeout;igw_uncertain=1;exit 1;fi
 if [ "$igw_state" = busy ];then igw_busy=$((igw_busy+1));else igw_busy=0;fi
 if [ "$igw_busy" -ge 10 ];then igw_reason=inference_watchdog;igw_uncertain=1;exit 1;fi
 igw_n=$((igw_n+1));sleep 1
done
igw_reason=unexpected_worker_exit
cat "$igw_out/program.log"
exit 1
