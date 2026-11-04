"""Host syntax/type check against actual igw_can_plat headers; not ARM linking."""
import argparse
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk',required=True)
args = parser.parse_args()
sdk = Path(args.sdk).resolve()
board = sdk/'dr1m90_fpsoc_cpu0'
lwip = sdk/'3rdparty/lwip-2.1.3'
includes = [sdk/'platform/inc',board/'inc',board/'chip/dr1x90/dr1m90/inc',
            board/'chip/dr1x90/common/inc',board/'arch/common/inc',
            board/'arch/arm/common/inc',board/'arch/arm/armv8/aarch64/cortex-a/inc',
            board/'arch/arm/common/gic_v3/inc',lwip/'src/include',lwip/'ports',
            lwip/'ports/netif/netif_gbe']
includes += list((board/'driver/ps_driver').glob('*/inc'))
includes += list((board/'lib').glob('*/inc'))+list((board/'lib').glob('*/api/inc'))
command = ['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-fsyntax-only',
           '-D__aarch64__','-DENABLE_MMU=1','-DAL_CHIP_DR1M90','-DLOG_DEV=AL_LOG_UART1']
if not (sdk/'platform/inc/soc_plat.h').is_file():
    parser.error('not an igw_can_plat SDK directory')
for path in includes:
    if path.exists():
        command += ['-isystem',str(path)]
subprocess.run(command+[str(root/'slave/main.c')],check=True)
print('SLAVE_REAL_SDK_SYNTAX=PASS (ARM compile/link and board execution not performed)')
