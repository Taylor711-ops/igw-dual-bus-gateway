import array, hashlib, json, sys
from pathlib import Path
from boot_layout import parse_image
from check_inputs import ROOT, check

def verify(image, root=ROOT):
    root = Path(root); reference = check(root)
    data, parts = parse_image(image)
    for old,new in zip(reference,parts):
        for key in ['entry','load','attributes']:
            if old[key] != new[key]: raise ValueError('Changed partition '+key)
    for index in [0,2]:
        if parts[index]['payload'] != reference[index]['payload']:
            raise ValueError('Changed FSBL/U-Boot partition payload')
    bit = (root/'inputs/system_r15.bit').read_bytes()
    end = bit.index(b'\n\n')+2
    header,raw = bit[:end],bit[end:]
    if b'# Package: DR1M90GEG400\n' not in header or b'# Design name: system_top\n' not in header:
        raise ValueError('Unexpected bitstream target')
    if len(raw)%4: raise ValueError('Unaligned bitstream')
    words = array.array('I');words.frombytes(raw);words.byteswap()
    if parts[1]['payload'] != words.tobytes():
        raise ValueError('PL payload does not match the pinned R15 bitstream after word conversion')
    return {'result':'PASS_VENDOR_BOOT_OFFLINE_REVIEW_CHECKS',
            'BOOT_sha256': hashlib.sha256(data).hexdigest(), 'BOOT_bytes':len(data),
            'reference_BOOT_sha256':hashlib.sha256((root/'reference/BOOT_R2_REFERENCE.bin').read_bytes()).hexdigest(),
            'boot_header_crc32':'PASS','partition_header_crc32':'PASS',
            'partition_load_entry_attributes':'MATCH_REFERENCE',
            'FSBL_payload':'BYTE_IDENTICAL_TO_REFERENCE',
            'UBOOT_payload':'BYTE_IDENTICAL_TO_REFERENCE',
            'PL_payload':'MATCHES_PINNED_R15_BITSTREAM',
            'vendor_checksum_attribute':'sha2',
            'vendor_sha2_digest_independently_verified':False,
            'cold_boot_verified':False,'NPU_inference_verified':False,
            'PL_CAN_board_verified':False,
            'partitions':[{k:v for k,v in p.items() if k!='payload'} for p in parts]}

if __name__ == '__main__':
    try: print(json.dumps(verify(sys.argv[1]),indent=2))
    except (ValueError,OSError,KeyError,IndexError) as error:
        print('BOOT_REVIEW_FAIL: '+str(error),file=sys.stderr);sys.exit(3)
