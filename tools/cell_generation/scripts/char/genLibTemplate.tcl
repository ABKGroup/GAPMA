#!/usr/bin/tclsh
# This script was written and developed by ABKGroup students at UCSD.
# However, the underlying commands and reports are copyrighted by Cadence.
# We thank Cadence for granting permission to share our research to help
# promote and foster the next generation of innovators.

set input_dir [lindex $argv 0]
set result_dir [lindex $argv 1]
set libname [lindex $argv 2]
set cells [lrange $argv 3 end]

set output_filename "${result_dir}/libchar/${libname}_template.lib"
set output [open $output_filename "w"]

puts $output "library (${libname}) \{"

foreach cell $cells {
  set width 0
  set height 0
  set output_pins ""
  set input_pins ""

  set info_file "${result_dir}/cell_info/${cell}.info"
  if {![file exist $info_file]} {
    puts "ERROR: No info file for cell ${cell}"
    continue
  }

  set input [open ${info_file} "r"]
  set lines [split [read $input] \n]
  set line1 [lindex $lines 0]
  set line2 [lindex $lines 1]
  set line3 [lindex $lines 2]
  set line4 [lindex $lines 3]

  set cell_full_name $line1
  set footprint_name [lindex [split $cell_full_name "_"] 0]
  set input_pins [split $line2 " "]
  set output_pins [split $line3 " "]
  set width [lindex [split $line4 " "] 0]
  set height [lindex [split $line4 " "] 1]

  set area [expr $width*$height]

  puts $output "  cell (${cell}) \{"
  puts $output "    area : ${area};"
  puts $output "    cell_footprint : \"${footprint_name}\";"
  puts $output "  \}"

  close $input
}

puts $output "\}"
close $output
