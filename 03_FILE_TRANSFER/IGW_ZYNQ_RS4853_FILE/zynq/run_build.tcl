if {[current_project -quiet] eq ""} {error "Open igw3_files.xpr first"}
set root [file normalize [file dirname [info script]]]
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1
set status [get_property STATUS [get_runs impl_1]]
puts "IMPLEMENTATION_STATUS=$status"
if {![string match "*Complete*" $status]} {error "Implementation failed; inspect logs"}
open_run impl_1
foreach mode {setup hold} {
  set paths [get_timing_paths -$mode -max_paths 1]
  if {![llength $paths]} {error "No $mode timing paths; inspect constraints"}
  set slack [get_property SLACK [lindex $paths 0]]
  puts "${mode}_WORST_SLACK_NS=$slack"
  if {$slack<0} {error "Negative $mode slack; stop"}
}
write_hw_platform -fixed -include_bit -force -file [file join $root igw3_files.xsa]
puts "XSA_READY: [file join $root igw3_files.xsa]"
puts "Review DRC/timing. Neither JTAG nor nonvolatile memory was programmed."
