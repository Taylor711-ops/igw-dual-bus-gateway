"""Read-only checks for the pinned DR1M90 image layout in this handoff.

Image creation is exclusively performed by the SDK's vendor albg tool.
This module neither creates nor rewrites partition headers or checksums.
"""
import struct
import zlib
from pathlib import Path

def parse_image(path):
    data = Path(path).read_bytes()
    if len(data) < 0x400 or len(data) > 64*1024*1024:
        raise ValueError('Unexpected BOOT image size')
    if struct.unpack_from('<I', data, 0)[0] != 0x12345678 or data[0x64:0x68] != b'ALGC':
        raise ValueError('Unexpected DR1 image header')
    if zlib.crc32(data[0x60:0x9c]) != struct.unpack_from('<I', data, 0x9c)[0]:
        raise ValueError('Boot header CRC32 mismatch')
    first, count = struct.unpack_from('<II', data, 0x94)
    if count != 3:
        raise ValueError('Expected exactly FSBL, PL and U-Boot partitions')
    parts = []; seen = set(); position = first
    for index in range(count):
        if position < 0xa0 or position % 4 or position + 64 > len(data) or position in seen:
            raise ValueError('Invalid partition header chain')
        seen.add(position)
        values = struct.unpack_from('<16I', data, position)
        if zlib.crc32(data[position:position+60]) != values[15]:
            raise ValueError('Partition header CRC32 mismatch: '+str(index))
        length, actual, total, next_header = values[:4]
        offset, checksum_offset = values[8], values[11]
        if not (0 < actual <= length <= total and offset % 4 == 0 and offset+total <= len(data)):
            raise ValueError('Invalid partition bounds: '+str(index))
        if not (offset+length <= checksum_offset and checksum_offset+32 <= offset+total):
            raise ValueError('Missing expected vendor checksum storage: '+str(index))
        parts.append({'index': index, 'header_offset': position, 'data_offset': offset,
                      'length': length, 'actual': actual, 'total': total,
                      'entry': values[4] | (values[5]<<32),
                      'load': values[6] | (values[7]<<32),
                      'attributes': values[9], 'checksum_offset': checksum_offset,
                      'payload': data[offset:offset+actual]})
        position = next_header
    if position != 0:
        raise ValueError('Unexpected extra partition')
    intervals = sorted((p['data_offset'],p['data_offset']+p['total']) for p in parts)
    for a,b in zip(intervals,intervals[1:]):
        if a[1] > b[0]: raise ValueError('Overlapping payloads')
    for p in parts:
        for q in seen:
            if p['data_offset'] < q+64 and q < p['data_offset']+p['total']:
                raise ValueError('Header overlaps payload')
    return data, parts

def elf_load_payload(path):
    data = Path(path).read_bytes()
    if data[:6] != b'\x7fELF\x02\x01': raise ValueError('Expected ELF64 little-endian FSBL')
    header = struct.unpack_from('<16sHHIQQQIHHHHHH',data)
    if header[1] != 2 or header[2] != 183 or header[4] != 0x61000000:
        raise ValueError('Unexpected FSBL architecture or entry')
    loads = []
    for index in range(header[10]):
        item = struct.unpack_from('<IIQQQQQQ',data,header[5]+index*header[9])
        kind,flags,offset,vaddr,paddr,filesz,memsz,align = item
        if kind == 1 and filesz:
            if not (offset+filesz <= len(data) and memsz >= filesz): raise ValueError('Bad ELF range')
            loads.append((paddr,data[offset:offset+filesz]))
    if len(loads) != 1 or loads[0][0] != 0x61000000:
        raise ValueError('Unexpected FSBL load layout')
    return loads[0][1]
