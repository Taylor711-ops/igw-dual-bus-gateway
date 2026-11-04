import sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
if any(c.isspace() for c in str(root)):
    raise SystemExit('Move this kit to a path without spaces before building')
lines = [
    '//arch=dr1m90; split=false; format=BIN',
    'the_ROM_image:', '{', '    image {',
    '        [bootloader,destination_cpu=apu-0,checksum=sha2] '+str(root/'inputs/soc_fsbl.elf'),
    '        [destination_device=pl,partition_owner=fsbl,encryption=none,checksum=sha2,load=0x200000] '+str(root/'inputs/system_r15.bit'),
    '        [destination_device=ps,destination_cpu=apu-0,org_endian,checksum=sha2,load=0x200000,exec=0x200000,partition_owner=fsbl] '+str(root/'inputs/u-boot.bin'),
    '    }', '}', ''
]
Path(sys.argv[1]).write_text('\n'.join(lines),encoding='ascii')
