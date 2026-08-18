#!/usr/bin/tclsh
# This script was written and developed by ABKGroup students at UCSD.
# However, the underlying commands and reports are copyrighted by Cadence.
# We thank Cadence for granting permission to share our research to help
# promote and foster the next generation of innovators.

set result_dir [lindex $argv 0]
set script_dir [lindex $argv 1]
set libname [lindex $argv 2]
set cell_list [lrange $argv 3 end]

set output_filename "${result_dir}/libchar/cells.tcl"
set output [open $output_filename "w"]

puts $output "set libname $libname"

puts $output "set cells \{ \\"

foreach cell $cell_list {
  puts $output "${cell} \\"
}

puts $output "\}"

puts $output "set SCRIPTS_DIR ${script_dir}"
puts $output "set RESULTS_DIR ${result_dir}"

close $output
