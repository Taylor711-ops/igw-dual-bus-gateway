# Provenance and verification scope

- This package extends the prior IGW_ZYNQ_RS4853 project and keeps its board pin mapping and 50 MHz clock. The base XDC came from the user-provided `20_rs485_key_led.zip` (SHA-256 `44a65ae19a790b29580ccda1016522abaca6da79e78d199e115a88b1df692407`); the XDC SHA-256 is `17a58603dfa4325a8c89fee093c176f5e9ebbfab03ebb7e7d949a554d799519a`. The design is an adaptation and not a vendor-certified bitstream.
- The user previously reported passing the old RTL simulation, successful programming, RS485 online and two independent device 3 write/readbacks. This package changes the RTL and has not been tested on that board.
- Unit 3's new FC45/FC46 mechanism is a private, host-scheduled one-shot event. Units 1/2 retain the existing CAN RTS/grant/DONE coordination. It is not arbitrary unsolicited RS485 transmission or standard Modbus.
- Native mock tests execute the modified C owner, web frontend, original node paths, simulated MMIO and simulated node3 responses. The mocks are not HDL simulation, timing, electrical testing, AArch64 cross-build, hardware programming, or physical concurrency evidence.
- `sim/tb_rs485_unit3.v` is supplied for user execution in Vivado. No Vivado/XSim, hardware, target toolchain or physical board was available in the authoring environment.
