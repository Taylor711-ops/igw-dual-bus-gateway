#!/bin/sh
# Native mock transport tests only, no access to boards or electrical signals.
set -eu
igw_root=$(cd "$(dirname "$0")" && pwd)
igw_tmp=$(mktemp -d "${TMPDIR:-/tmp}/igw_can3_checks.XXXXXX")
trap 'rm -rf "$igw_tmp"' EXIT HUP INT TERM
cd "$igw_root"
sha256sum -c SHA256SUMS.txt >/dev/null
gcc -std=c11 -O2 -Wall -Wextra -Werror -I tests/mock_include tests/node3_test.c -o "$igw_tmp/node3_test"
"$igw_tmp/node3_test"
node host/monitor/tests/frontend_r26.cjs
python3 tests/test_installer.py
mkdir "$igw_tmp/fixture"
cp -r tests/host_fixture/. "$igw_tmp/fixture/"
cp host/monitor/src/igw_web.c host/monitor/src/web_assets.h "$igw_tmp/fixture/src/"
gcc -std=c11 -O2 -Wall -Wextra -Werror -shared -fPIC -I tests/mock_include tests/zynq_bridge.c -o "$igw_tmp/zynq_can3_bridge.so"
export CAN3_BRIDGE_PATH="$igw_tmp/zynq_can3_bridge.so"
python3 "$igw_tmp/fixture/tests/test_multi.py" MultiTests.test_can_only_third_node_healthy_and_no_rs485_poll
python3 "$igw_tmp/fixture/tests/test_uplink_r16.py" UplinkTests.test_zynq_node3_can_uplink_real_gateway_protocol
printf '%s\n' 'LOCAL_CHECKS=PASS; simulated transport only, no target build or physical board test'
