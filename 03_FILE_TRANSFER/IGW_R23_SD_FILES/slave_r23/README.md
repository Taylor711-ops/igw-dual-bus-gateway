# R23 slave generated application

Copy ONLY slave/main.c into the actual existing slave application. Back up the
previous file outside the SDK app tree; clean/rebuild/link, then flash using the
same application flow as R21.1. Keep the original linker/platform unchanged.
Requires actual FatFs headers/library and HAVE_MMCPS_DRIVER from the supplied SDK.
SD drive1 (MMC0), FAT16/32, owned IGW23 directory. No formatter or RAM fallback.

SDK-compatible syntax checked with check_slave_sdk.py; actual ARM link and board
mount/persistence are pending. Run ../../README_CN.md staged steps.

Edit slave_src and regenerate with generate_slave.py; default2 logical nodes.
No source/test files other than the self-contained generated main.c need copying
to the SDK. Old REFERENCE files document prior revisions, not R23 certifications.
