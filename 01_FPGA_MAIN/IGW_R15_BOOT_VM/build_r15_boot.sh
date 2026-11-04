#!/bin/bash
# Uses the existing SDK's vendor packer. Does not source SDK configs or write SD.
set -euo pipefail
igw_kit=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
igw_sdk=${IGW_SDK_DIR:-"$HOME/uisrc-lab-anlogic"}
igw_albg="$igw_sdk/tools/bootgen/linux64/bin/albg"
command -v python3 >/dev/null || { echo 'ERROR: Python 3 is required in the Ubuntu VM'; exit 2; }
[ -x "$igw_albg" ] || { echo "ERROR: SDK vendor packer not executable: $igw_albg"; exit 2; }
cd "$igw_kit"
sha256sum -c SHA256SUMS.txt
python3 tools/check_inputs.py
igw_build=$(mktemp -d "$igw_kit/build_r15.XXXXXXXX")
trap 'echo "RESULT=BUILD_FAILED; keep logs in $igw_build" >&2' ERR
python3 tools/write_bif.py "$igw_build/boot_r15.bif"
echo "BUILD_DIRECTORY=$igw_build"
echo "VENDOR_PACKER=$igw_albg"
if ! "$igw_albg" -w -log info -image "$igw_build/boot_r15.bif" -arch dr1m90 -o "$igw_build/BOOT_R15.bin" > "$igw_build/albg.log" 2>&1; then
    tail -80 "$igw_build/albg.log"
    echo 'RESULT=VENDOR_PACK_FAILED' >&2
    exit 3
fi
if ! python3 tools/verify_built.py "$igw_build/BOOT_R15.bin" > "$igw_build/report.json"; then
    echo "RESULT=OUTPUT_REVIEW_FAILED; send $igw_build/albg.log" >&2
    exit 4
fi
(
    cd "$igw_build"
    sha256sum BOOT_R15.bin > SHA256SUMS.txt
    tar -czf IGW_R15_BOOT_REVIEW.tar.gz BOOT_R15.bin boot_r15.bif albg.log report.json SHA256SUMS.txt
)
cat "$igw_build/report.json"
echo 'RESULT=R15_BOOT_BUILT_OFFLINE_CHECKS_PASS'
echo "UPLOAD_REVIEW_ARCHIVE=$igw_build/IGW_R15_BOOT_REVIEW.tar.gz"
echo 'No SDK source/output or SD card was modified. Cold boot and inference remain untested.'
