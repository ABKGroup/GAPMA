# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.

puts "\[extract_features\] =================================================="
puts "\[extract_features\] Design:  $env(FEAT_DESIGN_NAME)"
puts "\[extract_features\] Netlist: $env(FEAT_NETLIST_V)"
puts "\[extract_features\] Out:     $env(FEAT_OUT_CSV)"
puts "\[extract_features\] =================================================="

set db_list {}
foreach db [split $env(FEAT_DB_FILES) ":"] {
    set db [string trim $db]
    if {$db ne ""} {
        lappend db_list $db
    }
}
set feat_lib_dir [file join [file dirname $env(FEAT_OUT_CSV)] "feat_extract_[pid].ndm"]
puts "\[extract_features\] DB files: $db_list"
puts "\[extract_features\] Creating design library: $feat_lib_dir"
create_lib $feat_lib_dir -ref_libs $db_list

if {[info exists env(FEAT_PARASITIC_SETUP_FILE)] && $env(FEAT_PARASITIC_SETUP_FILE) ne ""} {
    source $env(FEAT_PARASITIC_SETUP_FILE)
}

puts "\[extract_features\] Reading netlist ..."
read_verilog -top $env(FEAT_DESIGN_NAME) $env(FEAT_NETLIST_V)
set_top_module $env(FEAT_DESIGN_NAME)

set sdc_clean_path [file join [file dirname $env(FEAT_OUT_CSV)] "feat_extract_clean_[pid].sdc"]
set ifh [open $env(FEAT_SDC_FILE) r]
set ofh [open $sdc_clean_path w]
while {[gets $ifh line] >= 0} {
    if {[regexp {^\s*(current_design|set_dont_use|set_wire_load_mode)\s} $line]} continue
    regsub {\[remove_from_collection \[all_inputs\] \[get_ports [^\]]+\]\]} $line {[all_inputs]} line
    puts $ofh $line
}
close $ifh
close $ofh
puts "\[extract_features\] Reading SDC (filtered) ..."
read_sdc $sdc_clean_path
file delete $sdc_clean_path

if {[info exists env(FEAT_CLOCK_PERIOD_PS)] && $env(FEAT_CLOCK_PERIOD_PS) ne ""} {
    set override_ns [expr {double($env(FEAT_CLOCK_PERIOD_PS)) / 1000.0}]
    puts "\[extract_features\] Overriding clock period to ${override_ns} ns ($env(FEAT_CLOCK_PERIOD_PS) ps)"
    foreach_in_collection clk [all_clocks] {
        set clkname  [get_attribute $clk full_name]
        set clksrcs  [get_attribute $clk sources]
        create_clock -name $clkname -period $override_ns $clksrcs
    }
}

if {[info exists env(FEAT_DEF_FILE)] && $env(FEAT_DEF_FILE) ne "" &&
        [file exists $env(FEAT_DEF_FILE)]} {
    puts "\[extract_features\] Reading DEF: $env(FEAT_DEF_FILE)"
    read_def $env(FEAT_DEF_FILE)
}

if {[info exists env(FEAT_SPEF_FILE)] && $env(FEAT_SPEF_FILE) ne "" &&
        [file exists $env(FEAT_SPEF_FILE)]} {
    puts "\[extract_features\] Reading SPEF: $env(FEAT_SPEF_FILE)"
    read_parasitics -format spef $env(FEAT_SPEF_FILE)
}

puts "\[extract_features\] Running update_timing ..."
update_timing
puts "\[extract_features\] Timing update complete."

set _core [file join [file dirname [info script]] extract_cell_features_core.tcl]
if {![file exists $_core]} {
    error "extract_cell_features_core.tcl not found at: $_core"
}
source $_core

exit
