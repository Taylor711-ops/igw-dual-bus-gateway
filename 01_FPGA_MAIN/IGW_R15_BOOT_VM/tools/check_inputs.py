import hashlib, json, sys
from pathlib import Path
from boot_layout import parse_image, elf_load_payload

ROOT = Path(__file__).resolve().parents[1]
PINNED = {
    'inputs/soc_fsbl.elf': 'c513d9b9da5e57d8797e55bee6e6b1cd6309c64412801b708b97ea218a7ab701',
    'inputs/system_r15.bit': '4eb5a7519e3238853a1e15bf7cfe374182948579791a4cc63d1ea76a420b72aa',
    'inputs/u-boot.bin': '145ab113e4c29cca292bc50b3d882989fa25c2d57134a9eb3395651a6cfdc87f',
    'inputs/system_r15_gateway.dtb': '5c9b621f3bc61cf36cf1fdc68f383aa89fd71517c493904f4268be6bcbbdd184',
    'reference/BOOT_R2_REFERENCE.bin': '7b81535086c3096ded1e15061118236978730d56f9132d2616bc573cc8dd734c',
}

def check(root=ROOT):
    root = Path(root)
    for name, expected in PINNED.items():
        if hashlib.sha256((root/name).read_bytes()).hexdigest() != expected:
            raise ValueError('Input SHA256 mismatch: '+name)
    data, parts = parse_image(root/'reference/BOOT_R2_REFERENCE.bin')
    if parts[0]['payload'] != elf_load_payload(root/'inputs/soc_fsbl.elf'):
        raise ValueError('FSBL load bytes differ from the working reference BOOT')
    if parts[2]['payload'] != (root/'inputs/u-boot.bin').read_bytes():
        raise ValueError('U-Boot bytes differ from the working reference BOOT')
    expected = [(0x61000000,0x61000000,0x820), (0xffffffff,0x200000,0x8a0),
                (0x200000,0x200000,0x820)]
    for p,e in zip(parts,expected):
        if (p['entry'],p['load'],p['attributes']) != e:
            raise ValueError('Unexpected reference partition attributes')
    return parts

if __name__ == '__main__':
    try:
        check()
        print('INPUTS_PASS FSBL_LOAD_MATCH=YES UBOOT_MATCH=YES R15_BIT_SHA=PINNED')
    except (ValueError,OSError,KeyError) as error:
        print('INPUTS_FAIL: '+str(error),file=sys.stderr);sys.exit(2)
