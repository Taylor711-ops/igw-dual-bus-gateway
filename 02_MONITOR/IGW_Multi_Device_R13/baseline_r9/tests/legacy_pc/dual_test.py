"""Concurrent CAN/RS485 rounds, one in-flight request per channel, CSV evidence."""
import argparse
import csv
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import secrets
import time
from dual_client import Client, CSV_FIELDS, csv_row


def run(host, count, channels, stop, emit, path):
    clients = {}
    try:
        for channel in channels:
            clients[channel] = Client(channel, host)
        with path.open('x', newline='', encoding='utf-8-sig') as stream, ThreadPoolExecutor(max_workers=2) as pool:
            writer = csv.DictWriter(stream, fieldnames=CSV_FIELDS)
            writer.writeheader()
            seq = secrets.randbits(32)
            for _ in range(count):
                if stop.is_set():
                    break
                start = time.perf_counter()
                pending = [(ch, pool.submit(client.read, seq)) for ch, client in clients.items()]
                for channel, future in pending:
                    try:
                        result = future.result()
                    except Exception as exc:
                        result = {'channel': channel, 'seq': seq, 'status': 'client_error', 'error': str(exc)}
                    writer.writerow(csv_row(result))
                    stream.flush()
                    emit(result)
                seq = (seq+1) & 0xffffffff
                stop.wait(max(0, 1-(time.perf_counter()-start)))
    finally:
        for client in clients.values():
            client.close()


if __name__ == '__main__':
    import threading
    import signal
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', default='192.168.31.222')
    parser.add_argument('--count', type=int, default=10)
    parser.add_argument('--channel', choices=['CAN', 'RS485', 'BOTH'], default='BOTH')
    args = parser.parse_args()
    if not 1 <= args.count <= 1000000:
        parser.error('count must be 1..1000000')
    channels = ['CAN', 'RS485'] if args.channel == 'BOTH' else [args.channel]
    stop = threading.Event()
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    totals = {ch: [0, 0] for ch in channels}
    def emit(result):
        t = totals[result['channel']]
        t[0] += 1
        t[1] += result.get('status') == 'ok'
        print(f"{result['channel']} seq={result['seq']} {result.get('status')} "
              f"PC_RTT={result.get('pc_rtt_ms')}ms BUS_RTT={result.get('bus_rtt_us')}us", flush=True)
    path = Path(f'igw_dual_{time.strftime("%Y%m%d_%H%M%S")}_{secrets.token_hex(2)}.csv').resolve()
    run(args.host, args.count, channels, stop, emit, path)
    print('RESULTS', totals, 'CSV', path)
    raise SystemExit(0 if all(v == [args.count, args.count] for v in totals.values()) else 1)
