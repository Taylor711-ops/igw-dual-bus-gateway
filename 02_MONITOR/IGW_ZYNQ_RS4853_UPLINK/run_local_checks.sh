#!/bin/sh
# Native mock tests only; no Vivado, FPGA, physical bus or end-to-end latency claim.
set -eu
igw_root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
igw_tmp=$(mktemp -d "${TMPDIR:-/tmp}/igw_rs4853_check.XXXXXX")
trap 'rm -rf "$igw_tmp"' EXIT HUP INT TERM
cd "$igw_root"
sha256sum -c SHA256SUMS.txt >/dev/null
node host/monitor/tests/frontend_r26.cjs
PYTHONDONTWRITEBYTECODE=1 python3 tests/test_rs3_uplink.py
PYTHONDONTWRITEBYTECODE=1 python3 tests/test_installer.py
mkdir "$igw_tmp/fixture"
cp -r tests/host_fixture/. "$igw_tmp/fixture/"
cp host/monitor/src/igw_web.c host/monitor/src/web_assets.h "$igw_tmp/fixture/src/"
PYTHONDONTWRITEBYTECODE=1 python3 "$igw_tmp/fixture/tests/test_multi.py" MultiTests.test_rs485_only_third_node_and_isolation
PYTHONDONTWRITEBYTECODE=1 python3 "$igw_tmp/fixture/tests/test_rs3_integration.py"
printf '%s\n' 'LOCAL_MOCK_CHECKS=PASS; actual PL RTL, timing and boards NOT tested here'
