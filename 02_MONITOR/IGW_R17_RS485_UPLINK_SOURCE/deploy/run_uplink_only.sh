#!/bin/sh
# A bounded proof that uploads do not depend on PC requests or Web polling.
set -eu
cd "$(dirname "$0")"
sha256sum -c SHA256SUMS.txt
sh ./check_r15.sh
igw_lock=/tmp/igw_web_r10.lock
mkdir "$igw_lock" || { echo 'STOP: uplink-only owner already active';exit 1; }
echo "$$" > "$igw_lock/pid"
igw_upload=''
igw_capture=''
igw_rs=''
alive() {
    kill -0 "$1" 2>/dev/null || return 1
    [ "$(awk '{print $3}' "/proc/$1/stat" 2>/dev/null)" != Z ]
}
cleanup() {
    trap - EXIT
    trap '' INT TERM HUP
    for igw_pid in "$igw_rs" "$igw_upload" "$igw_capture"; do
        if [ -n "$igw_pid" ];then kill -TERM "$igw_pid" 2>/dev/null || :;fi
    done
    igw_stuck=0
    for igw_pid in "$igw_rs" "$igw_upload" "$igw_capture"; do
        [ -n "$igw_pid" ] || continue
        igw_wait=0
        while alive "$igw_pid" && [ "$igw_wait" -lt 6 ]; do sleep 1;igw_wait=$((igw_wait+1));done
        if alive "$igw_pid";then igw_stuck=1;echo "REVIEW: $igw_pid still alive; retaining ownership lock";
        else wait "$igw_pid" 2>/dev/null || :;fi
    done
    if [ "$igw_stuck" = 0 ];then rm -f "$igw_lock/pid";rmdir "$igw_lock" || :;fi
    echo 'UPLINK_ONLY_STOPPED; no Web or ordinary request polling was started'
    [ -z "$igw_upload" ] || tail -4 /tmp/igw_r16_uplink.log
    [ -z "$igw_rs" ] || tail -4 /tmp/igw_r17_rs485_uplink.log
    [ -z "$igw_capture" ] || cat /tmp/igw_r16_uplink_only_pl.stats
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
/etc/init.d/S95igw_gateway stop
for igw_comm in /proc/[0-9]*/comm; do
    [ -r "$igw_comm" ] || continue
    IFS= read -r igw_name < "$igw_comm" || continue
    case "$igw_name" in
        igw_web|igw_can_udp|igw_rs485_rw|igw_rs485_udp|igw_can_uplink|igw_can_pl_capt*|igw_fault_onlin*|yolo_demo_hdmi*)
            echo "STOP: existing $igw_name; stop its owning launcher first";exit 1;;
    esac
done
[ ! -e /tmp/igw_rs485_once.lock ] || { echo 'STOP: existing RS485 owner; stop its launcher first';exit 1; }
ip link set can0 down
ip link set can0 type can bitrate 500000 sample-point 0.8 sjw 16
ip link set can0 up
./igw_can_pl_capture --capture-seconds 600 > /tmp/igw_r16_uplink_only_pl.jsonl 2> /tmp/igw_r16_uplink_only_pl.stats &
igw_capture=$!
./igw_can_uplink --nodes 2 > /tmp/igw_r16_uplink.log 2>&1 &
igw_upload=$!
./igw_rs485_rw --uplink --nodes 2 --bind 127.0.0.1 --peer 127.0.0.1 --port 16001 > /tmp/igw_r17_rs485_uplink.log 2>&1 &
igw_rs=$!
sleep 1
alive "$igw_upload" && alive "$igw_capture" && alive "$igw_rs"
echo 'R17 UPLINK-ONLY READY; no browser or legacy read/write commands needed'
echo 'Wait 20 seconds, then Ctrl+C. Slave must be R17. Registers are volatile.'
while alive "$igw_upload" && alive "$igw_capture" && alive "$igw_rs";do
    sleep 1
done
echo 'A child stopped; stopping the other owner'
