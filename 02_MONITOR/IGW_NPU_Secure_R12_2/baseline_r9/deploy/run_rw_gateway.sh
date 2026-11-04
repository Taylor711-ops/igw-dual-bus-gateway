#!/bin/sh
# Foreground laboratory deployment; original boot/rootfs remain untouched.
set -eu
cd "$(dirname "$0")"
igw_can_pid=''
igw_rs_pid=''
igw_owned=0
igw_lock=/tmp/igw_rw_r9.lock
igw_alive() {
    kill -0 "$1" 2>/dev/null || return 1
    if [ -r "/proc/$1/stat" ]; then
        igw_state=$(awk '{print $3}' "/proc/$1/stat" 2>/dev/null) || return 1
        [ "$igw_state" != Z ] || return 1
    fi
}
igw_cleanup() {
    trap - EXIT INT TERM HUP
    igw_stuck=0
    for igw_pid in "$igw_rs_pid" "$igw_can_pid"; do
        [ -z "$igw_pid" ] || kill -TERM "$igw_pid" 2>/dev/null || :
    done
    igw_n=0
    while [ "$igw_n" -lt 3 ]; do
        igw_any=0
        for igw_pid in "$igw_rs_pid" "$igw_can_pid"; do
            if [ -n "$igw_pid" ] && igw_alive "$igw_pid"; then igw_any=1; fi
        done
        [ "$igw_any" = 1 ] || break
        sleep 1
        igw_n=$((igw_n+1))
    done
    for igw_pid in "$igw_rs_pid" "$igw_can_pid"; do
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
    echo 'RW_R9 stopped. Original CAN autostart remains stopped.'
}
for igw_cmd in sha256sum awk devmem ip; do command -v "$igw_cmd" >/dev/null; done
sha256sum -c SHA256SUMS.txt
test -x ./igw_can_udp
test -x ./igw_rs485_rw
mkdir "$igw_lock" || { echo 'STOP: another R9 launcher owns the lock'; exit 1; }
igw_owned=1
echo "$$" > "$igw_lock/pid"
trap igw_cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
igw_hash=$(sha256sum /mnt/mmcblk0p1/BOOT.bin | awk '{print $1}')
[ "$igw_hash" = 7b81535086c3096ded1e15061118236978730d56f9132d2616bc573cc8dd734c ] || {
    echo 'STOP: not pinned R2 SD BOOT'; exit 1;
}
[ "$(devmem 0xf8800200 32)" = 0x00000005 ]
[ "$(devmem 0x1f0000020 32)" = 0x000000BF ]
[ "$(devmem 0x8040000C 32)" = 0x00000000 ]
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
        igw_can_udp|igw_rs485_udp|igw_rs485_rw|igw_ps_capture|igw_fault_onlin*|yolo_demo_hdmi*)
            echo "STOP: existing process $igw_name; stop its owning launcher first"; exit 1 ;;
    esac
done
ip link set can0 down
ip link set can0 type can bitrate 500000 sample-point 0.8 sjw 16
ip link set can0 up
./igw_can_udp --bind 0.0.0.0 --peer 192.168.31.100 --port 15000 > /tmp/igw_rw_can.log 2>&1 &
igw_can_pid=$!
./igw_rs485_rw --bind 0.0.0.0 --peer 192.168.31.100 --port 15001 > /tmp/igw_rw_rs485.log 2>&1 &
igw_rs_pid=$!
sleep 1
igw_alive "$igw_can_pid" && igw_alive "$igw_rs_pid" &&
grep -q '\[READY\]' /tmp/igw_rw_can.log && grep -q '\[READY\]' /tmp/igw_rw_rs485.log || {
    cat /tmp/igw_rw_can.log /tmp/igw_rw_rs485.log; exit 1;
}
echo 'RW_R9 READY: CAN legacy+read/write UDP15000; RS485 read/write UDP15001.'
echo 'Update slave to RW_R9 first. Start PC start_can_rw.cmd (one round first). Ctrl+C stops both.'
echo 'Logs: /tmp/igw_rw_can.log /tmp/igw_rw_rs485.log. No other CAN/RS485 senders.'
while igw_alive "$igw_can_pid" && igw_alive "$igw_rs_pid"; do sleep 1; done
echo 'A gateway exited; stopping its companion.'
cat /tmp/igw_rw_can.log /tmp/igw_rw_rs485.log
exit 1
