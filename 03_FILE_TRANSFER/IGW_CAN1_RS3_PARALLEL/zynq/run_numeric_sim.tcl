if {[current_project -quiet] eq ""} {error "Open igw3_files.xpr first"}
set_property top tb_rs485_unit3 [get_filesets sim_1]
launch_simulation -simset sim_1 -mode behavioral
run -all
