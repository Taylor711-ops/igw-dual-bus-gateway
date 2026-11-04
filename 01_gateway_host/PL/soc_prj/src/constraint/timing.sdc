# R15 gateway-only. Preserve physical PS/PLL configuration and derive actual clocks.
derive_clocks
# Original PS asynchronous-reset source exception, preserved from the baseline.
set_false_path -from [get_nets {design_0_inst/design_0_ARM_Processor_System_0_Inst/p2f_rst0_n}]
# No PWM/video FIFO/HDMI exceptions: those logic blocks are not instantiated.
# CAN and RS485 external receive pins are asynchronous protocol inputs. Their
# synchronizer structures remain in the preserved bus RTL; no fake input clock.
