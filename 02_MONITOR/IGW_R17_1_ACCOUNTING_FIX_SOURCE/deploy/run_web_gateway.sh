#!/bin/sh
# Foreground laboratory deployment; original boot/rootfs remain untouched.
set -eu
[ "${IGW_ARCHIVE_FD:-}" = 3 ] || { echo "STOP: start run_secure_gateway.sh to unlock encryption first"; exit 1; }
cd "$(dirname "$0")"
igw_nodes=${IGW_NODES:-2}
case "$igw_nodes" in [1-9]|1[0-6]) ;; *) echo 'STOP: IGW_NODES must be 1..16'; exit 1;; esac
igw_can_pid=''
igw_rs_pid=''
igw_web_pid=''
igw_npu_pid=''
igw_capture_pid=''
igw_uplink_pid=''
igw_owned=0
igw_lock=/tmp/igw_web_r10.lock
igw_alive() {
    kill -0 "$1" 2>/dev/null || return 1
    if [ -r "/proc/$1/stat" ]; then
        igw_state=$(awk '{print $3}' "/proc/$1/stat" 2>/dev/null) || return 1
        [ "$igw_state" != Z ] || return 1
    fi
}
igw_cleanup() {
    trap - EXIT
    trap '' INT TERM HUP
    # R12.1: group TERM and parent TERM may overlap. Finish cleanup once.
    igw_stuck=0
    for igw_pid in "$igw_web_pid" "$igw_npu_pid" "$igw_rs_pid" "$igw_can_pid" "$igw_uplink_pid" "$igw_capture_pid"; do
        [ -z "$igw_pid" ] || kill -TERM "$igw_pid" 2>/dev/null || :
    done
    igw_n=0
    while [ "$igw_n" -lt 6 ]; do
        igw_any=0
        for igw_pid in "$igw_web_pid" "$igw_npu_pid" "$igw_rs_pid" "$igw_can_pid" "$igw_uplink_pid" "$igw_capture_pid"; do
            if [ -n "$igw_pid" ] && igw_alive "$igw_pid"; then igw_any=1; fi
        done
        [ "$igw_any" = 1 ] || break
        sleep 1
        igw_n=$((igw_n+1))
    done
    for igw_pid in "$igw_web_pid" "$igw_npu_pid" "$igw_rs_pid" "$igw_can_pid" "$igw_uplink_pid" "$igw_capture_pid"; do
        if [ -n "$igw_pid" ] && igw_alive "$igw_pid"; then
            echo "REVIEW: process $igw_pid did not stop after TERM; retaining ownership lock"
            igw_stuck=1
        elif [ -n "$igw_pid" ]; then
            wait "$igw_pid" 2>/dev/null || :
        fi
    done
    if [ "$igw_owned" = 1 ] && [ "$igw_stuck" = 0 ]; then
        rm -f "$igw_lock/pid"
        rmdir "$igw_lock" || :
    fi
    if [ -n "$igw_rs_pid" ]; then
        echo "FINAL_TX_AUTHORIZATION=$(devmem 0x8040000C 32)"
        echo "NPU_CAPABILITY_AFTER=$(devmem 0x1f0000020 32)"
    fi
    if [ -n "$igw_capture_pid" ]; then cat /tmp/igw_r15_plcan.stats; fi
    if [ -n "$igw_uplink_pid" ]; then tail -5 /tmp/igw_r16_uplink.log; fi
    if [ -n "$igw_rs_pid" ];then tail -4 /tmp/igw_web_rs485.log;fi
    echo 'WEB_R17.1 stopped. Original CAN autostart remains stopped.'
}
for igw_cmd in sha256sum awk devmem ip; do command -v "$igw_cmd" >/dev/null; done
sha256sum -c SHA256SUMS.txt
test -x ./igw_can_udp
test -x ./igw_rs485_rw
test -x ./igw_web
mkdir "$igw_lock" || { echo 'STOP: another R10 launcher owns the lock'; exit 1; }
igw_owned=1
echo "$$" > "$igw_lock/pid"
trap igw_cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
sh ./check_r15.sh
ip -4 addr show dev eth0 | grep -q 'inet 192\.168\.31\.222/24' || {
    echo 'STOP: eth0 must have 192.168.31.222/24'; exit 1;
}
if [ -e /tmp/igw_rs485_once.lock ]; then
    echo 'STOP: RS485 owned by another program. Stop its launcher; do not delete active locks.'
    exit 1
fi
/etc/init.d/S95igw_gateway stop
for igw_comm in /proc/[0-9]*/comm; do
    [ -r "$igw_comm" ] || continue
    IFS= read -r igw_name < "$igw_comm" || continue
    case "$igw_name" in
        igw_web|igw_can_udp|igw_rs485_udp|igw_rs485_rw|igw_ps_capture|igw_fault_onlin*|yolo_demo_hdmi*|igw_can_uplink|igw_can_pl_capt*)
            echo "STOP: existing process $igw_name; stop its owning launcher first"; exit 1 ;;
    esac
done
./igw_can_pl_capture --capture-seconds 600 > /tmp/igw_r15_plcan.jsonl 2> /tmp/igw_r15_plcan.stats 3>&- &
igw_capture_pid=$!
sleep 1
igw_alive "$igw_capture_pid" && grep -q '^CAPTURE_READY ' /tmp/igw_r15_plcan.stats || {
    cat /tmp/igw_r15_plcan.stats; exit 1;
}
ip link set can0 down
ip link set can0 type can bitrate 500000 sample-point 0.8 sjw 16
ip link set can0 up
./igw_can_uplink --nodes "$igw_nodes" > /tmp/igw_r16_uplink.log 2>&1 3>&- &
igw_uplink_pid=$!
sleep 1
igw_alive "$igw_uplink_pid" && grep -q '^\[READY\]' /tmp/igw_r16_uplink.log || {
    cat /tmp/igw_r16_uplink.log; exit 1;
}
./igw_can_udp --nodes "$igw_nodes" --bind 127.0.0.1 --peer 127.0.0.1 --port 16000 > /tmp/igw_web_can.log 2>&1 3>&- &
igw_can_pid=$!
./igw_rs485_rw --uplink --nodes "$igw_nodes" --bind 127.0.0.1 --peer 127.0.0.1 --port 16001 > /tmp/igw_web_rs485.log 2>&1 3>&- &
igw_rs_pid=$!
sleep 1
igw_alive "$igw_can_pid" && igw_alive "$igw_rs_pid" &&
grep -q '\[READY\]' /tmp/igw_web_can.log && grep -q '\[READY\]' /tmp/igw_web_rs485.log || {
    cat /tmp/igw_web_can.log /tmp/igw_web_rs485.log; exit 1;
}
sh ./run_npu_local.sh > /tmp/igw_npu_local_launcher.log 2>&1 3>&- &
igw_npu_pid=$!
./igw_web --nodes "$igw_nodes" --npu-port 16002 --bind 192.168.31.222 --peer 192.168.31.100 --port 8080 --archive-fd 3 > /tmp/igw_web_service.log 2>&1 &
igw_web_pid=$!
sleep 1
igw_alive "$igw_web_pid" && grep -q '\[READY\]' /tmp/igw_web_service.log || {
    cat /tmp/igw_web_service.log; exit 1;
}
igw_wait=0
while ! grep -q '^NPU_LOCAL_READY ' /tmp/igw_npu_local_launcher.log; do
    if ! igw_alive "$igw_npu_pid" || [ "$igw_wait" -ge 45 ]; then cat /tmp/igw_npu_local_launcher.log; exit 1; fi
    sleep 1; igw_wait=$((igw_wait+1))
done
cat /tmp/igw_npu_local_launcher.log
echo 'R17.1 CAN+RS485 DEVICE-UPLOAD + DUAL BUS + NPU + SECURE WEB READY: http://192.168.31.222:8080'
echo "Configured logical nodes=$igw_nodes. Slave MUST run R17 autonomous-upload firmware with independent banks."
echo 'Open the page on PC 192.168.31.100; close old UDP panels. NPU scope=device1 only.'
echo 'Backends are private loopback ports16000/16001. Browser writes use independent readback.'
echo 'Ctrl+C stops six owned services; session limit 600s. PL capture: /tmp/igw_r15_plcan.jsonl and /tmp/igw_r15_plcan.stats'
echo 'CAN uploads: /tmp/igw_r16_uplink.log; RS485 controlled uploads: /tmp/igw_r17_rs485_uplink.json. CAN coordination required; NPU unchanged.'
igw_npu_failed=0
while igw_alive "$igw_can_pid" && igw_alive "$igw_rs_pid" && igw_alive "$igw_web_pid" && igw_alive "$igw_capture_pid" && igw_alive "$igw_uplink_pid"; do
    if [ "$igw_npu_failed" = 0 ] && ! igw_alive "$igw_npu_pid";then
        igw_npu_failed=1;echo 'REVIEW: NPU stopped; communication and encrypted archive remain running; do not restart NPU during this session.'
    fi
    sleep 1
done
echo 'A service exited; stopping its companions.'
cat /tmp/igw_web_can.log /tmp/igw_web_rs485.log /tmp/igw_web_service.log
exit 1
