"""Offline parser/comparator regression; never invokes albg or publishes a BOOT.

The positive comparison fixture changes a reference image payload in temporary
storage only. Its vendor SHA2 bytes are deliberately not rebuilt; it is not a
bootable image, and it is not a substitute for the vendor packaging test.
"""
from pathlib import Path
import sys,tempfile,json,array,struct,zlib,hashlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from check_inputs import check
from boot_layout import parse_image
from verify_built import verify

def reject(fn):
    try:fn()
    except (ValueError,OSError):return
    raise AssertionError('Invalid fixture was accepted')

check()
reference,parts=parse_image(ROOT/'reference/BOOT_R2_REFERENCE.bin')
reject(lambda:verify(ROOT/'reference/BOOT_R2_REFERENCE.bin'))
bit=(ROOT/'inputs/system_r15.bit').read_bytes();raw=bit[bit.index(b'\n\n')+2:]
words=array.array('I');words.frombytes(raw);words.byteswap();encoded=words.tobytes()
assert len(encoded)==parts[1]['actual']
fixture=bytearray(reference);offset=parts[1]['data_offset'];fixture[offset:offset+len(encoded)]=encoded
with tempfile.TemporaryDirectory() as directory:
    f=Path(directory)/'NOT_BOOTABLE_TEST_ONLY.bin';f.write_bytes(fixture)
    report=verify(f);assert report['vendor_sha2_digest_independently_verified'] is False
    for name,position in [('boot_header',0x64),('partition_header',parts[0]['header_offset']),
                          ('FSBL',parts[0]['data_offset']+16),('UBoot',parts[2]['data_offset']+16),
                          ('R15_PL',parts[1]['data_offset']+16)]:
        altered=bytearray(fixture);altered[position]^=1;f.write_bytes(altered);reject(lambda:verify(f))
    for name,header_field in [('bad_next_header',3),('overlap',8),('oversize',2)]:
        altered=bytearray(fixture);position=parts[1]['header_offset']
        value={'bad_next_header':position,'overlap':parts[0]['data_offset'],'oversize':0xffffffff}[name]
        struct.pack_into('<I',altered,position+4*header_field,value)
        struct.pack_into('<I',altered,position+60,zlib.crc32(altered[position:position+60]))
        f.write_bytes(altered);reject(lambda:verify(f))
result={'result':'PASS_GUARD_TESTS_ONLY','reference_headers_and_inputs':'PASS',
        'old_PL_rejected':True,'changed_FSBL_UBoot_PL_rejected':True,
        'bad_headers_chain_ranges_rejected':True,
        'comparison_positive_fixture':'TEMPORARY_NONBOOTABLE_ONLY',
        'vendor_albg_executed':False,'board_boot_verified':False}
(ROOT/'validation/guard_tests.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
