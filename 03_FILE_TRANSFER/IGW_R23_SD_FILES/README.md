# IGW R23 shared SD file storage

Industrial gateway project. See README_CN.md for precise staged deployment.
Replace ONLY slave_r23/slave/main.c in the existing slave SDK project; Clean,
Rebuild and link before flashing. No linker/BSP/BOOT/DTB/PL modifications.
Main Ubuntu build.sh produces eleven deploy files for igw_apps/r23_sd_files.

FatFs R0.14b is pinned to the supplied SDK (drive1 SD/MMC0).8MiB/node, bounded
~17KiB storage/protocol workspace under the native matching configuration.
Two BIN/MET generations per logical node; sync/close and fresh full-file SHA
before commit record selection. Incomplete PUT preserves last committed bank;
startup verifies records and actual data SHA. Not whole-volume crash atomicity.

Tests include actual supplied FatFs on RAM/image media, real Linux CLI protocol
fixtures, real generated scheduler plus HAL/MMIO fixtures, and HTTP/DOM/launcher
checks. No ARM link, physical SD/card removal, physical power-cut, or throughput
certification in this environment. SDK details/caveats: SDK_AUDIT.json.
Optional native checks (Python>=3.8,GCC,Node): python3 run_local_checks.py --sdk PATH.
Device cold-start persistence must be checked after successful4KiB commit and
CAN/RS485 readback, using run_sd_test.sh verify 4096 without writes.

Native-only tests/vendor_ff retains upstream FatFs copyright/license notices.
The RAM disk formatter is confined to tests; slave application never formats.
