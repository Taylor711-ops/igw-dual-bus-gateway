# Run only after TB_RESULT=PASS. Builds the volatile JTAG-loadable bitstream.
# Does not program a device or write BOOT/flash/SD.
if {[current_project -quiet] eq ""} {error "Open igw_rs4853.xpr first"}
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1
set run_status [get_property STATUS [get_runs impl_1]]
puts "IMPLEMENTATION_STATUS=$run_status"
if {![string match "*Complete*" $run_status]} {error "Implementation did not finish; inspect the Vivado log"}
open_run impl_1
set paths [get_timing_paths -setup -max_paths 1]
if {[llength $paths] == 0} {error "No setup timing path: inspect clock and constraints"}
set slack [get_property SLACK [lindex $paths 0]]
puts "SETUP_WORST_SLACK_NS=$slack"
if {$slack < 0} {error "Negative setup slack; do not program the bitstream"}
puts "BUILD_REVIEW: check DRC and timing summary, then use Hardware Manager for JTAG only."
