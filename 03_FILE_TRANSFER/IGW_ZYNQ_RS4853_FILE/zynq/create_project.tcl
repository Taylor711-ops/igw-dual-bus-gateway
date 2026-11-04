# Vivado 2020.2. Creates a separate project; does not program the board.
set root [file normalize [file dirname [info script]]]
set dir [file join $root vivado_generated]
if {[file exists $dir]} {error "vivado_generated already exists; open it or use a clean copy"}
create_project igw3_files $dir -part xc7z020clg400-2
# From the user's 16_can_key_led example: DDR, SDIO0 and UART0 pin configuration.
source [file join $root vendor system_bd.tcl]
set ps [get_bd_cells processing_system7_0]
# The earlier board example enabled CAN; this RS485-only image must not drive it.
delete_bd_objs [get_bd_intf_ports CAN]
set_property CONFIG.PCW_CAN0_PERIPHERAL_ENABLE {0} $ps
set_property -dict [list CONFIG.PCW_UART1_PERIPHERAL_ENABLE {1} \
  CONFIG.PCW_UART1_UART1_IO {EMIO} CONFIG.PCW_UART1_GRP_FULL_ENABLE {0}] $ps
if {[llength [get_bd_intf_pins -quiet processing_system7_0/UART_1]]!=1} {
  error "UART1 EMIO interface was not created; do not continue"
}
create_bd_intf_port -mode Master -vlnv xilinx.com:interface:uart_rtl:1.0 UART1
connect_bd_intf_net [get_bd_intf_ports UART1] [get_bd_intf_pins processing_system7_0/UART_1]
validate_bd_design
save_bd_design
set wrapper [make_wrapper -files [get_files system.bd] -top]
add_files -norecurse $wrapper
add_files -norecurse [list [file join $root rtl igw_uart_8e1.v] \
  [file join $root rtl rs485_key_led.v] [file join $root rtl file_tx_guard.v] \
  [file join $root rtl igw3_system.v]]
add_files -fileset constrs_1 -norecurse [file join $root rtl igw3_system.xdc]
add_files -fileset sim_1 -norecurse [list [file join $root sim tb_rs485_unit3.v] [file join $root sim tb_file_tx_guard.v]]
set_property top igw3_system [get_filesets sources_1]
set_property top tb_file_tx_guard [get_filesets sim_1]
update_compile_order -fileset sources_1
update_compile_order -fileset sim_1
puts "PROJECT_READY: $dir/igw3_files.xpr"
puts "Run BOTH simulations, then build and export the XSA. No programming has occurred."
