#!/bin/sh
set -eu
igw_here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
igw_cc=${CC:-aarch64-linux-gnu-gcc}
"$igw_cc" -std=c11 -O2 -Wall -Wextra -Werror -o "$igw_here/igw_can_pl_capture" "$igw_here/igw_can_pl_capture.c"
