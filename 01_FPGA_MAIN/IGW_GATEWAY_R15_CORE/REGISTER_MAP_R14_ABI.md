# R14 CAN observer AXI map

Base `0x80400800`, 256-byte subwindow inside existing GP0 RS485 page. Clock 50 MHz; Classical CAN base frames 500 kbit/s. RX-only; no transmitter. FIFO_DEPTH is configured to 16 in this release.

| Local offset | Read contents |
|---|---|
| 00 | magic 0x4E414349, byte order ICAN |
| 04 | version 0x00010000 |
| 08 | capabilities 0x1F: bit0 passive, bit1 standard, bit2 RTR, bit3 CRC15, bit4 FIFO |
| 0C | write-only POP: full WSTRB=0xF, WDATA=1, nonempty FIFO required |
| 10 | status: bit4 idle-ready, bit3 active, bit2 FIFO-full, bit1 nonempty; bit0 reserved |
| 14 | FIFO record count |
| 18 | CLOCK_HZ=50000000; parameter, not measured frequency |
| 1C | BITRATE=500000; parameter |
| 20 | accepted-frame counter (including dropped records) |
| 24 | dropped-newest counter |
| 28 | stuff errors |
| 2C | CRC errors |
| 30 | format errors |
| 34 | unsupported frames |
| 38 | missing ACK errors |
| 3C | incomplete-frame timeouts |
| 40 | successful POP counter |
| 44 | FIFO depth=16 |
| 80..9C | eight 32-bit head-record words |

All other reads and all writes except valid POP return AXI SLVERR. Reads of an empty head return SLVERR. Counter additions wrap at 32 bits; elapsed differences use unsigned modular subtraction. Hardware reset clears all counters and the FIFO. Error counters classify receiver rejection events; after losing framing, a single malformed stream may generate more than one event. They are not one-for-one ISO error-frame statistics.

| Record word | Bits/content |
|---|---|
| 0 | sequence number, starts at 0; increments even if FIFO drops the frame |
| 1 | standard CAN ID bits10:0; upper bits zero |
| 2 | DLC3:0, RTR4, EXT5=0, ACK6, CRC-valid7=1, reserved15:8=0, CRC15 in bits30:16, bit31=0 |
| 3 | SOF detected timestamp low32 |
| 4 | SOF detected timestamp high32 |
| 5 | payload bytes0..3 in little-endian byte lanes |
| 6 | payload bytes4..7 in little-endian byte lanes |
| 7 | duration from detected SOF to final EOF sample, PL clock cycles |

Timestamp is a local free-running 50 MHz tick counter and includes the input synchronizer/detection delay. It is not a wall clock or a calibrated wire timestamp. Divide tick differences by 50,000,000 for seconds; host and PC clocks require separate synchronization. RTR records retain the requested DLC but contain no payload. Unused bytes are zero.

Head record remains stable until POP. One consumer must read all eight words before POP; never POP first. RDATA and RRESP remain latched through AXI backpressure. Full+POP+PUSH retains the arriving record without a drop; empty+invalid POP+PUSH retains it and reports write SLVERR. No hardware clear command is implemented.

The 16-record FIFO is finite. A slow polling consumer can lose records at high bus load, even while the PS CAN controller continues working. Report drops and sequence gaps; do not claim lossless capture unless measurements support it. Host output includes initial FIFO backlog, whereas GOOD/DROP deltas describe only the capture interval, so those values need not equal CAPTURED. Frames left in FIFO at stop are explicitly reported.

Primary protocol references: [CiA CAN CC](https://www.can-cia.org/can-knowledge/can-cc), [CiA CRC](https://www.can-cia.org/can-knowledge/cyclic-redundancy-check-crc-in-can-frames), [TI CAN synchronization](https://www.ti.com/lit/pdf/spra500). The implemented subset and tested drift range are limited as specified in README_FIRST_CN.md.
