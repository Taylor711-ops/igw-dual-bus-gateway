"""CLI acceptance helper. No automatic UDP retries or hidden CAN successes."""
import argparse
import collections
import csv
import secrets
import sys
import time
from pathlib import Path
from igw_client import Client, CSV_FIELDS, csv_row


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="192.168.31.222")
    p.add_argument("--port", type=int, default=15000)
    p.add_argument("--count", type=int, default=100)
    p.add_argument("--interval", type=float, default=1.0)
    p.add_argument("--timeout", type=float, default=2.0)
    p.add_argument("--start-seq", type=lambda s: int(s, 0), default=None)
    p.add_argument("--csv", default=None)
    a = p.parse_args()
    if not 1 <= a.count <= 1000000 or a.interval < 0 or a.timeout <= 0:
        p.error("count=1..1000000, interval>=0, timeout>0 required")
    if a.start_seq is not None and not 0 <= a.start_seq <= 0xFFFFFFFF:
        p.error("start-seq must be uint32")
    seq = secrets.randbits(32) if a.start_seq is None else a.start_seq
    path = Path(a.csv or time.strftime("igw_can_udp_%Y%m%d_%H%M%S.csv"))
    counts = collections.Counter()
    rtts = []
    interrupted = False
    client = Client(a.host, a.port, a.timeout)
    print(f"Target={a.host}:{a.port} count={a.count} CSV={path.resolve()}", flush=True)
    try:
        with path.open("w", newline="", encoding="utf-8-sig") as stream:
            writer = csv.DictWriter(stream, fieldnames=CSV_FIELDS)
            writer.writeheader()
            for i in range(a.count):
                start = time.perf_counter()
                try:
                    result = client.read(seq)
                except OSError as exc:
                    result = {"seq": seq, "status": "socket_error", "stats": {}}
                    print(f"Socket error: {exc}", flush=True)
                status = result.get("status", "invalid_response")
                counts[status] += 1
                if status == "ok":
                    rtts.append(result["pc_rtt_ms"])
                writer.writerow(csv_row(result))
                stream.flush()
                print(f"[{i+1}/{a.count}] seq={seq} status={status} "
                      f"PC_RTT={result.get('pc_rtt_ms','-')}ms "
                      f"CAN_RTT={result.get('can_rtt_us','-')}us", flush=True)
                seq = (seq + 1) & 0xFFFFFFFF
                if i + 1 < a.count:
                    time.sleep(max(0, a.interval - (time.perf_counter()-start)))
    except KeyboardInterrupt:
        interrupted = True
        print("Stopped by user; partial CSV retained.")
    finally:
        client.close()
    print("SUMMARY", dict(counts), "CSV=", path.resolve())
    if rtts:
        print(f"PC RTT successful requests only: min={min(rtts):.3f} "
              f"avg={sum(rtts)/len(rtts):.3f} max={max(rtts):.3f} ms")
    return 130 if interrupted else 0 if counts["ok"] == a.count else 1


if __name__ == "__main__":
    sys.exit(main())
