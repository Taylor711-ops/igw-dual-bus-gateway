"""Network -> gateway -> FC06 -> independent FC03 readback demonstration.
Standard-library only. No retransmission, no inference, no accuracy claim.
"""
import argparse
import datetime
import json
from pathlib import Path
import secrets
import socket
import time


def crc16(data):
    value = 0xffff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xa001 if value & 1 else 0)
    return value


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate JSON key')
        result[key] = value
    return result


def integer(value, maximum=0xffffffffffffffff):
    return type(value) is int and 0 <= value <= maximum


class Client:
    def __init__(self, host, port=15001, recorder=None):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.connect((host, port))
        self.seq = secrets.randbits(32)
        self.record = recorder or (lambda row: None)
        self.previous = None

    def close(self):
        self.sock.close()

    def request(self, operation, address, argument):
        if operation not in ('READ', 'WRITE') or not integer(address, 31):
            raise ValueError('invalid operation/address')
        if operation == 'READ' and (not integer(argument, 8) or argument < 1 or address+argument > 32):
            raise ValueError('invalid read count')
        if operation == 'WRITE' and (address < 16 or not integer(argument, 65535)):
            raise ValueError('only volatile registers16..31 may be written')
        self.seq = (self.seq+1) & 0xffffffff
        seq = self.seq
        command = f'IGW2 RS485 {operation} {seq} 1 {address} {argument}'
        start = time.perf_counter_ns()
        self.record({'event': 'request', 'monotonic_ns': start, 'request': command})
        self.sock.send(command.encode('ascii'))
        deadline = start+1_500_000_000
        while True:
            remaining = (deadline-time.perf_counter_ns())/1e9
            if remaining <= 0:
                raise TimeoutError('no matching gateway response; do not resend the write')
            self.sock.settimeout(remaining)
            raw = self.sock.recv(4096)
            received = time.perf_counter_ns()
            self.record({'event': 'datagram', 'monotonic_ns': received,
                         'request_seq': seq, 'raw': raw.hex()})
            try:
                reply = json.loads(raw.decode('ascii'), object_pairs_hook=unique_object)
            except (ValueError, UnicodeError):
                continue
            if not isinstance(reply, dict) or type(reply.get('seq')) is not int or reply['seq'] != seq:
                continue
            break
        for key, expected in {'v': 2, 'type': 'rs485_reply', 'op': operation, 'unit': 1,
                              'address': address, 'count': argument if operation == 'READ' else 1,
                              'value': argument if operation == 'WRITE' else 0}.items():
            if type(reply.get(key)) is not type(expected) or reply[key] != expected:
                raise ValueError('response identity mismatch: '+key)
        if reply.get('status') != 'ok':
            raise RuntimeError('gateway status='+str(reply.get('status'))+
                               '; write may be unconfirmed, no automatic retransmission')
        if reply.get('execution') != ('acknowledged' if operation == 'WRITE' else 'read'):
            raise ValueError('unexpected execution result')
        data = bytes.fromhex(reply['data'])
        if crc16(data) or data[:2] != bytes((1, 3 if operation == 'READ' else 6)):
            raise ValueError('invalid Modbus CRC/function/unit')
        if operation == 'WRITE':
            prefix = bytes((1, 6))+address.to_bytes(2, 'big')+argument.to_bytes(2, 'big')
            if len(data) != 8 or data[:6] != prefix or reply.get('registers') != []:
                raise ValueError('FC06 acknowledgement mismatch')
            values = []
        else:
            if len(data) != 5+2*argument or data[2] != 2*argument:
                raise ValueError('FC03 length mismatch')
            values = [int.from_bytes(data[3+2*i:5+2*i], 'big') for i in range(argument)]
            reported = reply.get('registers')
            if not isinstance(reported, list) or not all(integer(x,65535) for x in reported) or reported != values:
                raise ValueError('register list differs from raw ADU')
        stats, pl = reply.get('stats'), reply.get('pl')
        for group, names in [(stats, ['attempts','ok','timeout','tx_fail','bad_reply','exception',
                                      'busy','duplicate','ignored','udp_fail']),
                             (pl, ['good','tx','crc','uart','gap','overrun','watchdog'])]:
            if not isinstance(group, dict) or any(not integer(group.get(k)) for k in names):
                raise ValueError('missing/invalid counters')
        if not integer(reply.get('rs485_rtt_us')):
            raise ValueError('invalid gateway RTT')
        if self.previous is not None:
            old_stats, old_pl = self.previous
            if stats['attempts'] != old_stats['attempts']+1 or stats['ok'] != old_stats['ok']+1:
                raise ValueError('unexpected extra request or reset; stop other senders')
            for name in ['timeout','tx_fail','bad_reply','exception','busy','udp_fail']:
                if stats[name] != old_stats[name]:
                    raise ValueError('new gateway error: '+name)
            if pl['tx'] != (old_pl['tx']+1) & 0xffffffff:
                raise ValueError('unexpected hardware TX count')
            for name in ['crc','uart','gap','overrun','watchdog']:
                if pl[name] != old_pl[name]:
                    raise ValueError('new PL error: '+name)
        self.previous = stats.copy(), pl.copy()
        self.record({'event': 'validated', 'seq': seq, 'operation': operation,
                     'address': address, 'argument': argument, 'values': values,
                     'pc_rtt_us': (received-start)//1000,
                     'gateway_rtt_us': reply['rs485_rtt_us']})
        return values


def write_and_verify(client, address=16, desired=None):
    before = client.request('READ', 4, 4)
    if before[0] != 0x0801:
        raise ValueError('RW_R8 slave revision absent; update the slave application first')
    old = client.request('READ', address, 1)[0]
    value = old ^ 0x5a5a if desired is None else desired
    if value == old:
        raise ValueError('choose a different value to prove the register changed; no write sent')
    client.request('WRITE', address, value)
    actual = client.request('READ', address, 1)[0]
    after = client.request('READ', 4, 4)
    if actual != value or after != [0x0801, (before[1]+1) & 0xffff, address, value]:
        raise ValueError('readback or slave write metadata mismatch')
    result = {'event': 'write_verified', 'address': address, 'before': old,
              'written': value, 'readback': actual, 'slave_write_count': after[1]}
    client.record(result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='192.168.31.222')
    parser.add_argument('--port', type=int, default=15001)
    parser.add_argument('--address', type=int, choices=range(16,32), default=16)
    parser.add_argument('--value', type=lambda s: int(s,0))
    parser.add_argument('--count', type=int, choices=range(1,101), default=1)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or (args.value is not None and not 0 <= args.value <= 65535):
        parser.error('port/value out of range')
    if args.value is not None and args.count != 1:
        parser.error('--value is supported only with --count 1')
    stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    path = Path(__file__).resolve().parent/f'rs485_rw_{stamp}_{secrets.token_hex(3)}.jsonl'
    passed = 0
    with path.open('x', encoding='utf-8') as log:
        def record(row):
            log.write(json.dumps(row, ensure_ascii=False)+'\n')
            log.flush()
        info = time.get_clock_info('perf_counter')
        record({'event':'clock', 'tool_revision':'RW_R8_PC_CLOCK1',
                'clock':'perf_counter_ns', 'implementation':info.implementation,
                'resolution_seconds':info.resolution, 'monotonic':info.monotonic,
                'rtt_scope':'PC application RTT including local request logging and scheduling'})
        print('PC_TOOL=RW_R8_PC_CLOCK1 CLOCK=perf_counter_ns resolution_seconds='+str(info.resolution))
        client = Client(args.host, args.port, record)
        try:
            for i in range(args.count):
                row = write_and_verify(client, args.address, args.value)
                passed += 1
                print(f'PASS {passed}/{args.count} register={row["address"]} '
                      f'before={row["before"]} written={row["written"]} readback={row["readback"]}', flush=True)
                if i+1 < args.count:
                    time.sleep(1)
            record({'event':'summary', 'result':'PASS_RS485_WRITE_READBACK', 'verified':passed})
            print('RESULT=PASS_RS485_WRITE_READBACK')
            print('Scope: volatile unit1 test registers; CAN write, multi-device, storage and web pending.')
            return 0
        except (OSError, ValueError, RuntimeError, KeyError, TypeError, KeyboardInterrupt) as error:
            record({'event':'summary', 'result':'REVIEW', 'verified':passed, 'reason':str(error)})
            print('RESULT=REVIEW reason='+str(error))
            return 1
        finally:
            client.close()
            print('EVIDENCE='+str(path))


if __name__ == '__main__':
    raise SystemExit(main())
