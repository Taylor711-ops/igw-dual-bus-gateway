# From Vivado Tcl Console: source {C:/path/IGW_ZYNQ_RS4853/create_vivado_project.tcl}
set root [file normalize [file dirname [info script]]]
set project_dir [file join $root vivado_generated]
if {[file exists $project_dir]} {error "vivado_generated already exists; open its xpr or use a clean copy"}
create_project igw_rs4853 $project_dir -part xc7z020clg400-2
add_files -norecurse [list [file join $root rtl rs485_key_led.v] [file join $root rtl igw_uart_8e1.v]]
add_files -fileset constrs_1 -norecurse [file join $root rtl rs485_key_led.xdc]
add_files -fileset sim_1 -norecurse [file join $root sim tb_rs485_unit3.v]
set_property top rs485_key_led [get_filesets sources_1]
set_property top tb_rs485_unit3 [get_filesets sim_1]
update_compile_order -fileset sources_1
update_compile_order -fileset sim_1
puts "PROJECT_READY: $project_dir/igw_rs4853.xpr"
puts "Run behavioral simulation and choose Run All. Require TB_RESULT=PASS before generating bitstream."
