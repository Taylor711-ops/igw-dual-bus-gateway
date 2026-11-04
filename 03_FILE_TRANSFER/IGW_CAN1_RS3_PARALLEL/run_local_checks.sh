#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/igw3_local.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT
export ASAN_OPTIONS=detect_leaks=0
gcc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -fno-omit-frame-pointer -no-pie -Ihost/tests/stubs -Ihost/tests/vendor_ff -Izynq/src \
 tests/file_core.c host/tests/vendor_ff/ff.c host/tests/vendor_ff/ffunicode.c -o "$test_dir/file_core"
"$test_dir/file_core"
python3 tests/test_unit3_transfer.py -v
python3 tests/test_install.py -v
node host/tests/frontend.cjs host/file_page.html
(
 cd host
 python3 -m unittest -v \
  tests.test_http.HTTP.test_unit3_rs485_only_and_roundtrip \
  tests.test_http.HTTP.test_cross_bus_and_logical_node_isolation \
  tests.test_http.HTTP.test_async_progress_busy_guard_and_not_yet_downloadable \
  tests.test_http.HTTP.test_malformed_request_limits_and_bus_validation \
  tests.test_http.HTTP.test_only_can1_and_rs3_overlap_in_either_order \
  tests.test_http.HTTP.test_cancel_stops_both_owned_workers \
  tests.test_http.HTTP.test_one_failure_does_not_report_other_task_as_failed \
  tests.test_managed_lock \
  tests.test_parallel_protocols.ParallelProtocols.test_real_can1_and_rs3_protocols_run_concurrently_and_verify \
  tests.test_transfer.Transfer.test_cross_bus_both_directions_one_mib
)
sh -n host/install_on_board.sh
bash -n host/build_on_ubuntu.sh
echo 'LOCAL_TESTS=PASS: native simulations only; no FPGA, target link or hardware test'
