"""Standard-library UDP client. All sequence words are uint32, CAN payload LE."""
import json
import socket
import struct
import time

CSV_FIELDS = ["time_local", "seq", "status", "data", "pc_rtt_ms", "can_rtt_us",
              "attempts", "ok", "timeout", "tx_fail", "mismatch", "resets", "reset_fail"]


class Client:
    def __init__(self, host="192.168.31.222", port=15000, timeout=2.0):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.connect((host, port))
        self.timeout = timeout

    def close(self):
        self.sock.close()

    def read(self, seq):
        if not isinstance(seq, int) or not 0 <= seq <= 0xFFFFFFFF:
            raise ValueError("seq must be uint32")
        start = time.perf_counter()
        self.sock.send(f"IGW1 READ {seq}".encode("ascii"))
        deadline = start + self.timeout
        while True:
            left = deadline - time.perf_counter()
            if left <= 0:
                return self._failure(seq, "udp_timeout", start)
            self.sock.settimeout(left)
            try:
                raw = self.sock.recv(4096)
            except socket.timeout:
                return self._failure(seq, "udp_timeout", start)
            try:
                result = json.loads(raw)
            except (ValueError, UnicodeError):
                continue
            if not isinstance(result, dict) or result.get("v") != 1:
                continue
            if result.get("type") != "can_reply" or result.get("seq") != seq:
                continue
            result["pc_rtt_ms"] = round((time.perf_counter() - start) * 1000, 3)
            if result.get("status") == "ok":
                expected = struct.pack("<II", seq, 0x52535000 ^ seq).hex().upper()
                if result.get("data") != expected:
                    result["status"] = "pc_mismatch"
            return result

    @staticmethod
    def _failure(seq, status, start):
        return {"v": 1, "seq": seq, "status": status, "data": "",
                "can_rtt_us": 0, "pc_rtt_ms": round((time.perf_counter()-start)*1000, 3),
                "stats": {}}


def csv_row(result):
    stats = result.get("stats", {})
    if not isinstance(stats, dict):
        stats = {}
    return {"time_local": time.strftime("%Y-%m-%d %H:%M:%S"),
            **{key: result.get(key, "") for key in CSV_FIELDS[1:6]},
            **{key: stats.get(key, "") for key in CSV_FIELDS[6:]}}
