"""Two independent UDP clients. No automatic retries. Standard library only."""
import json
import socket
import time
from igw_client import Client as CANClient

CSV_FIELDS = ['time_local', 'channel', 'seq', 'status', 'data', 'registers',
              'pc_rtt_ms', 'bus_rtt_us', 'attempts', 'ok', 'timeout', 'tx_fail',
              'pl_good', 'pl_tx', 'pl_crc', 'pl_uart', 'pl_gap', 'pl_overrun', 'pl_watchdog']


def crc16(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
    return crc


class Client:
    def __init__(self, channel, host='192.168.31.222', port=None):
        if channel not in ('CAN', 'RS485'):
            raise ValueError('Unknown channel')
        self.channel = channel
        self.can = CANClient(host, port or 15000) if channel == 'CAN' else None
        self.sock = None
        if self.can is None:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.connect((host, port or 15001))

    def close(self):
        (self.can or self.sock).close()

    def read(self, seq):
        if type(seq) is not int or not 0 <= seq <= 0xffffffff:
            raise ValueError('seq must be uint32')
        if self.can:
            result = self.can.read(seq)
            result['channel'] = self.channel
            result['bus_rtt_us'] = result.get('can_rtt_us', 0)
            return result
        start = time.perf_counter()
        self.sock.send(f'IGW1 RS485 {seq}'.encode('ascii'))
        deadline = start + 2
        while True:
            left = deadline - time.perf_counter()
            if left <= 0:
                result = {'seq': seq, 'status': 'udp_timeout'}
                break
            self.sock.settimeout(left)
            try:
                raw = self.sock.recv(4096)
            except socket.timeout:
                result = {'seq': seq, 'status': 'udp_timeout'}
                break
            try:
                result = json.loads(raw)
            except (ValueError, UnicodeError):
                continue
            if not isinstance(result, dict) or result.get('v') != 1:
                continue
            if result.get('type') != 'rs485_reply' or result.get('seq') != seq:
                continue
            if result.get('status') == 'ok':
                try:
                    data = bytes.fromhex(result['data'])
                    if len(data) != 13 or data[:3] != b'\x01\x03\x08' or crc16(data):
                        raise ValueError('Bad Modbus frame')
                    regs = [int.from_bytes(data[i:i+2], 'big') for i in (3, 5, 7, 9)]
                    if result.get('registers') != regs:
                        raise ValueError('Registers mismatch')
                except (KeyError, TypeError, ValueError):
                    result['status'] = 'pc_mismatch'
            break
        result['channel'] = self.channel
        result['bus_rtt_us'] = result.get('rs485_rtt_us', 0)
        result['pc_rtt_ms'] = round((time.perf_counter()-start)*1000, 3)
        return result


def csv_row(result):
    row = {key: result.get(key, '') for key in CSV_FIELDS}
    row['time_local'] = time.strftime('%Y-%m-%d %H:%M:%S')
    row['registers'] = json.dumps(result.get('registers', []), separators=(',', ':'))
    stats = result.get('stats', {})
    pl = result.get('pl', {})
    for key in ('attempts', 'ok', 'timeout', 'tx_fail'):
        row[key] = stats.get(key, '')
    for key in ('good', 'tx', 'crc', 'uart', 'gap', 'overrun', 'watchdog'):
        row['pl_'+key] = pl.get(key, '')
    return row
