#!/usr/bin/env python3
"""Independent structural comparison of the vendor DTB and staged DTB.

No board access, filesystem repair, boot-file writes or DTB modifications.
The candidate itself was produced with the standard libfdt fdtput utility.
"""
from pathlib import Path
import struct, hashlib, json, zlib

ROOT = Path(__file__).resolve().parents[1]
TARGETS = {'/soc/vdma@80200000', '/soc/vdmafb@80100000', '/soc/mipicam@80300000', '/soc/axi_dynclk@80101000'}
ORIGINAL_SHA = '8f319401d8c658f5e04a60c7927029fd9af4913377477b95040aeaba5c4b6867'

def parse(path):
    b = Path(path).read_bytes()
    assert len(b) >= 40
    h = struct.unpack_from('>10I', b)
    magic, total, off, stroff, rsv, version, last, cpu, ssize, tsize = h
    assert magic == 0xd00dfeed and version == 17 and last <= version
    assert total == len(b) and off % 4 == 0 and rsv % 8 == 0
    assert off + tsize <= total and stroff + ssize <= total
    strings = b[stroff:stroff + ssize]
    reserves = []
    while True:
        assert rsv + 16 <= total
        a, size = struct.unpack_from('>QQ', b, rsv); rsv += 16
        if (a, size) == (0, 0): break
        reserves.append((a, size))
    pos = off; stack = []; nodes = {}; ended = False
    while pos < off + tsize:
        t = struct.unpack_from('>I', b, pos)[0]; pos += 4
        if t == 1:
            end = b.index(0, pos, off + tsize)
            name = b[pos:end].decode('utf-8'); pos = (end + 4) & ~3
            stack.append(name); key = '/' + '/'.join(stack[1:])
            assert key not in nodes
            nodes[key] = {}
        elif t == 2:
            assert stack; stack.pop()
        elif t == 3:
            assert stack and pos + 8 <= off + tsize
            size, idx = struct.unpack_from('>II', b, pos); pos += 8
            assert idx < len(strings) and pos + size <= off + tsize
            name = strings[idx:strings.index(0, idx)].decode('utf-8')
            props = nodes['/' + '/'.join(stack[1:])]
            assert name not in props
            props[name] = b[pos:pos+size]; pos = (pos + size + 3) & ~3
        elif t == 4: pass
        elif t == 9:
            assert not stack; ended = True; break
        else: raise ValueError('Invalid FDT token: ' + str(t))
    assert ended and pos == off + tsize
    return {'nodes': nodes, 'reserves': reserves, 'cpu': cpu, 'version': version, 'last': last}

def main():
    old_path = ROOT/'reference/system_r2_original.dtb'
    new_path = ROOT/'system.dtb'
    assert hashlib.sha256(old_path.read_bytes()).hexdigest() == ORIGINAL_SHA
    old, new = parse(old_path), parse(new_path)
    assert old.keys() == new.keys()
    for k in ['reserves', 'cpu', 'version', 'last']: assert old[k] == new[k], k
    assert old['nodes'].keys() == new['nodes'].keys()
    changes = []
    for node in old['nodes']:
        a, b = old['nodes'][node], new['nodes'][node]
        for prop in a.keys() | b.keys():
            if a.get(prop) != b.get(prop):
                assert node in TARGETS and prop == 'status', (node, prop)
                assert b[prop] == b'disabled\0'
                changes.append({'node': node, 'property': prop,
                                'before': a.get(prop, b'<absent>').rstrip(b'\0').decode(),
                                'after': 'disabled'})
    assert len(changes) == 4 and {c['node'] for c in changes} == TARGETS
    for path in ['/soc/can@0xF8408000', '/soc/ethernet0@F8100000',
                 '/soc/hard_npu@63F00000', '/soc/soft_npu@1f0000000',
                 '/soc/mmc@f8049000', '/soc/serial@F8401000', '/memory', '/chosen']:
        assert old['nodes'][path] == new['nodes'][path], path
    script = (ROOT/'reference/boot_original.scr').read_bytes()
    fields = struct.unpack_from('>7I4B32s', script)
    assert fields[0] == 0x27051956 and fields[9] == 6 and fields[10] == 0
    header = bytearray(script[:64]); header[4:8] = b'\0'*4
    assert zlib.crc32(header) == fields[1]
    data = script[64:]
    assert len(data) == fields[3] and zlib.crc32(data) == fields[6]
    first, terminator = struct.unpack_from('>II', data)
    assert terminator == 0 and first == len(data) - 8
    commands = data[8:].decode('ascii')
    assert 'load ${devtype} ${devnum}:${distro_bootpart} 0x14000000 ${prefix}system.dtb\n' in commands
    assert 'bootm 0x10000000 0x15000000 0x14000000\n' in commands
    print(json.dumps({'result': 'PASS_OFFLINE_DTB_AND_SCRIPT',
                      'nodes_preserved': len(new['nodes']), 'changes': changes,
                      'candidate_sha256': hashlib.sha256(new_path.read_bytes()).hexdigest(),
                      'candidate_bytes': len(new_path.read_bytes()),
                      'boot_script_crc': 'PASS', 'board_boot_verified': False}, indent=2))

if __name__ == '__main__': main()
