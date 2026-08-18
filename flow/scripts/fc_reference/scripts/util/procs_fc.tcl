# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.
# Copyright (C) 2014-2023 Synopsys, Inc. All rights reserved.
proc run_start {} {
  global env SEV TEV SNPS synopsys_program_name

    set SNPS(time_start) [clock seconds]
    puts "RM-info: TIME_START: $SNPS(time_start)"

   set SNPS(machine_name) [exec uname -n]
   puts "RM-info: MACHINE: $SNPS(machine_name)"

}
proc run_end {} {
  global env SEV TEV SNPS synopsys_program_name
  set SNPS(time_stop) [clock seconds]
  set runtime [expr $SNPS(time_stop) - $SNPS(time_start)]
  set runtime [expr $runtime/60]
  puts "SNPS_INFO  : METRIC    | TIMESTAMP SYS.START_TIME | $SNPS(time_start)"
  puts "SNPS_INFO  : METRIC    | TIMESTAMP SYS.STOP_TIME | $SNPS(time_stop)"
  puts "SNPS_INFO : Runtime : $runtime min"
  set memKB [get_mem]
  set memGB [expr $memKB/1000000]
  puts "SNPS_INFO : Memory : $memGB"
}

proc rm_generate_variables_for_report_parallel { args } {

  parse_proc_arguments -args $args options

  if {[info exists options(-work_directory)]} { set work_directory $options(-work_directory) }
  if {[info exists options(-file_name)]} { set file_name $options(-file_name) }

  set the_file_for_report_parallel	[file normalize "$work_directory/$file_name"]

  global REPORT_PREFIX REPORT_PARALLEL_SUBMIT_COMMAND REPORTS_DIR REPORT_QOR_REPORT_POWER REPORT_CLOCK_POWER REPORT_QOR_REPORT_CONGESTION EARLY_DATA_CHECK_POLICY REPORT_POWER_SAIF_FILE REPORT_POWER_SAIF_MAP SAIF_FILE_SOURCE_INSTANCE REPORT_VERBOSE REPORT_DEBUG REPORT_INIT_DESIGN_ACTIVE_SCENARIO_LIST REPORT_COMPILE_ACTIVE_SCENARIO_LIST REPORT_PLACE_OPT_ACTIVE_SCENARIO_LIST REPORT_CLOCK_OPT_CTS_ACTIVE_SCENARIO_LIST REPORT_CLOCK_OPT_OPTO_ACTIVE_SCENARIO_LIST REPORT_ROUTE_AUTO_ACTIVE_SCENARIO_LIST REPORT_ROUTE_OPT_ACTIVE_SCENARIO_LIST REPORT_CHIP_FINISH_ACTIVE_SCENARIO_LIST REPORT_ICV_IN_DESIGN_ACTIVE_SCENARIO_LIST REPORT_ENDPOINT_OPT_ACTIVE_SCENARIO_LIST REPORT_TIMING_ECO_ACTIVE_SCENARIO_LIST REPORT_FUNCTIONAL_ECO_ACTIVE_SCENARIO_LIST
  global WRITE_QOR_DATA WRITE_QOR_DATA_DIR COMPARE_QOR_DATA_DIR TCL_USER_SUPPLEMENTAL_REPORTS_SCRIPT
  global ENABLE_FUSA;
  global USE_ABSTRACTS_FOR_BLOCKS USE_ABSTRACTS_FOR_POWER_ANALYSIS CHECK_HIER_TIMING_CONSTRAINTS_CONSISTENCY ;
  global REPORT_STAGE REPORT_ACTIVE_SCENARIOS REPORT_DISABLE_GUI
  global SEV

  if {[info exists REPORT_PREFIX]} {
    set fid [ open ${the_file_for_report_parallel} "w" ]

    puts $fid "source [pwd]/scripts/util/procs_global.tcl"
    if {$::synopsys_program_name == "fc_shell"} {
      puts $fid "source [pwd]/scripts/util/procs_fc.tcl"
    } else {
      puts $fid "source [pwd]/scripts/util/procs_icc2.tcl"
    }
    if { [array exists SEV] } {
      foreach idx [ array names SEV ] {
        puts $fid "set SEV($idx) \"$SEV($idx)\""
      }
    }

    puts $fid "## RM Tcl variables required by report_parallel"
    puts $fid "set REPORT_PREFIX $REPORT_PREFIX"
    puts $fid "set REPORT_PARALLEL_SUBMIT_COMMAND \"$REPORT_PARALLEL_SUBMIT_COMMAND\""
    puts $fid "set REPORTS_DIR [which $REPORTS_DIR]"
    puts $fid "set REPORT_VERBOSE $REPORT_VERBOSE"
    puts $fid "set REPORT_DEBUG $REPORT_DEBUG"
    puts $fid "set REPORT_DISABLE_GUI $REPORT_DISABLE_GUI"
    puts $fid "set REPORT_CLOCK_POWER $REPORT_CLOCK_POWER"
    puts $fid "set REPORT_QOR_REPORT_POWER $REPORT_QOR_REPORT_POWER"
    puts $fid "set REPORT_POWER_SAIF_FILE \"$REPORT_POWER_SAIF_FILE\""
    puts $fid "set REPORT_POWER_SAIF_MAP \"$REPORT_POWER_SAIF_MAP\""
    puts $fid "set REPORT_INIT_DESIGN_ACTIVE_SCENARIO_LIST \"$REPORT_INIT_DESIGN_ACTIVE_SCENARIO_LIST\""
    puts $fid "set REPORT_PLACE_OPT_ACTIVE_SCENARIO_LIST \"$REPORT_PLACE_OPT_ACTIVE_SCENARIO_LIST\"" 
    puts $fid "set REPORT_CLOCK_OPT_CTS_ACTIVE_SCENARIO_LIST \"$REPORT_CLOCK_OPT_CTS_ACTIVE_SCENARIO_LIST\"" 
    puts $fid "set REPORT_CLOCK_OPT_OPTO_ACTIVE_SCENARIO_LIST \"$REPORT_CLOCK_OPT_OPTO_ACTIVE_SCENARIO_LIST\"" 
    puts $fid "set REPORT_ROUTE_AUTO_ACTIVE_SCENARIO_LIST     \"$REPORT_ROUTE_AUTO_ACTIVE_SCENARIO_LIST\"" 
    puts $fid "set REPORT_ROUTE_OPT_ACTIVE_SCENARIO_LIST      \"$REPORT_ROUTE_OPT_ACTIVE_SCENARIO_LIST\"" 
    puts $fid "set REPORT_CHIP_FINISH_ACTIVE_SCENARIO_LIST    \"$REPORT_CHIP_FINISH_ACTIVE_SCENARIO_LIST\"" 
    puts $fid "set REPORT_ICV_IN_DESIGN_ACTIVE_SCENARIO_LIST  \"$REPORT_ICV_IN_DESIGN_ACTIVE_SCENARIO_LIST\"" 
    puts $fid "set REPORT_ENDPOINT_OPT_ACTIVE_SCENARIO_LIST   \"$REPORT_ENDPOINT_OPT_ACTIVE_SCENARIO_LIST\"" 
    puts $fid "set REPORT_TIMING_ECO_ACTIVE_SCENARIO_LIST     \"$REPORT_TIMING_ECO_ACTIVE_SCENARIO_LIST\"" 
    puts $fid "set SAIF_FILE_SOURCE_INSTANCE \"$SAIF_FILE_SOURCE_INSTANCE\""
    puts $fid "set REPORT_QOR_REPORT_CONGESTION $REPORT_QOR_REPORT_CONGESTION"
    puts $fid "set EARLY_DATA_CHECK_POLICY $EARLY_DATA_CHECK_POLICY"

    puts $fid "set WRITE_QOR_DATA $WRITE_QOR_DATA"
    puts $fid "set WRITE_QOR_DATA_DIR [file normalize $WRITE_QOR_DATA_DIR]"
    puts $fid "set COMPARE_QOR_DATA_DIR [file normalize $COMPARE_QOR_DATA_DIR]"

    puts $fid "set REPORT_STAGE $REPORT_STAGE"
    puts $fid "set REPORT_ACTIVE_SCENARIOS \"$REPORT_ACTIVE_SCENARIOS\""
 
    if { $TCL_USER_SUPPLEMENTAL_REPORTS_SCRIPT != "" } { 
      puts $fid "set TCL_USER_SUPPLEMENTAL_REPORTS_SCRIPT [file normalize $TCL_USER_SUPPLEMENTAL_REPORTS_SCRIPT]"
    } else {
      puts $fid "set TCL_USER_SUPPLEMENTAL_REPORTS_SCRIPT \"\""
    }
    
    if {$::synopsys_program_name == "fc_shell"} {
	puts $fid "set REPORT_COMPILE_ACTIVE_SCENARIO_LIST \"$REPORT_COMPILE_ACTIVE_SCENARIO_LIST\""
    }

    puts $fid "set ENABLE_FUSA $ENABLE_FUSA"

    if {[info exists USE_ABSTRACTS_FOR_BLOCKS]} {
      puts $fid "set USE_ABSTRACTS_FOR_BLOCKS \"$USE_ABSTRACTS_FOR_BLOCKS\""
      puts $fid "set USE_ABSTRACTS_FOR_POWER_ANALYSIS $USE_ABSTRACTS_FOR_POWER_ANALYSIS"
      puts $fid "set CHECK_HIER_TIMING_CONSTRAINTS_CONSISTENCY $CHECK_HIER_TIMING_CONSTRAINTS_CONSISTENCY"
    }

    close $fid
  }

} ;

define_proc_attributes rm_generate_variables_for_report_parallel \
        -info "Generate and pass necessary RM variables for the report_parallel command" \
   	-define_args {
  	{-work_directory "Destination directory for the output file." AString string optional}
  	{-file_name "Name of the output file." AString string required}
	}

proc check_clock_transition { args } {

   parse_proc_arguments -args $args results
   set threshold $results(-threshold)

   set currentScenario [current_scenario]
   set i 1
   foreach_in_collection scenario [get_scenarios -filter "setup || hold && active"] {
      current_scenario $scenario 
      set time_unit [get_user_units -type time -output]
      set time_input [get_user_units -type time -input -numeric]
      set time_output [get_user_units -type time -output -numeric]
      set time_factor [expr $time_input / $time_output] 
      set fastest_clk [lindex [lsort -inc -real [get_attribute   [get_clock -filter "is_virtual==false" ] period ] ] 0]
      if {[string equal 1.00ns $time_unit]} {
         set clk_thresh 5.0
         set tran_limit 0.5
         set tran_limit_unit [expr $tran_limit / $time_factor]
         } else {
         set clk_thresh 5000
         set tran_limit 500
         set tran_limit_unit [expr $tran_limit / $time_factor]
      } 
      if {[expr $threshold * $fastest_clk] > $tran_limit} {
         set target_tran $tran_limit
         set target_tran_unit [expr $target_tran / $time_factor]
      } else {
         set target_tran [expr $threshold * $fastest_clk]
	 if {$target_tran == 0} {
	    if {[string equal 1.00ns $time_unit]} {set target_tran 0.001} else {set target_tran 1}
	    }
         set target_tran_unit [expr $target_tran / $time_factor]
      }
      if {$fastest_clk < $clk_thresh} {
         set clk_collection [get_clock -filter "is_virtual==false && period==$fastest_clk"]
         foreach_in_collection clk $clk_collection {
            set clock_sinks [sort_collection [get_clock_tree_pins -clock $clk -filter "is_sink" -quiet] constraining_max_transition]
            if {[sizeof_collection $clock_sinks]} {
               set clk_tran [get_attribute [index_collection $clock_sinks 0] constraining_max_transition]
               set clk_tran_unit [expr $clk_tran / $time_factor] 
               if {$clk_tran_unit >= $target_tran_unit} {
                   puts "RM-info: Relaxed clock max transition detected, constraining max_transition of $clk_tran_unit for clock [get_object_name $clk] in scenario [get_object_name $scenario]"
                   if {[info exists results(-apply_max_transition)]} {
                      set_max_transition $target_tran_unit [get_clocks $clk -mode [get_attribute [get_scenarios $scenario] mode]] -clock_path -scenario [get_scenarios $scenario]
                      puts "RM-info: Relaxed clock max transition detected, setting max_transition to $target_tran_unit for clock [get_object_name $clk] in scenario [get_object_name $scenario]"
                      global rm_clock
                      set rm_clock(name$i) $clk
                      set rm_clock(scenario$i) [current_scenario]
                      set rm_clock(orig_tran$i)  $clk_tran_unit
                   incr i
                   }
               }
            }
         }
     }
   }
   if {$currentScenario ne ""} {
      current_scenario $currentScenario
   }
} ;

define_proc_attributes check_clock_transition \
        -info "check_clock_transition  #Command checks the max_transtition constraint for the fastest clock per active scenario" \
        -define_args {
        {-threshold "Value between 0 and 1" "#percent of clock period" float required }
        {-apply_max_transition  "Applies max transition based on the threshold to the fastest clock" "" boolean optional}
        }

proc restore_clock_transition {args} {
   global rm_clock
   for {set i 1} {$i <= [expr [array size rm_clock] / 3]} {incr i} {
      current_scenario $rm_clock(scenario$i)
      set_max_transition $rm_clock(orig_tran$i) [get_clocks [get_object_name $rm_clock(name$i)]] -scenario $rm_clock(scenario$i) -clock_path
      puts "RM-info: Restoring clock max transition to $rm_clock(orig_tran$i) for clock [get_object_name $rm_clock(name$i)] in scenario [get_object_name $rm_clock(scenario$i)]" 
   }
} ;

proc targeted_ep_ropt_pba_ccd {args} {

  parse_proc_arguments -args $args args_array

  if [info exists args_array(-scenarios)] {
    set scenarios [ get_object_name [ get_scenarios -filter active&&setup $args_array(-scenarios) ]]
  } else {
    set scenarios [ get_object_name [ get_scenarios -filter active&&setup *]]
  }
  puts "RM-info: Operating on scenario(s): $scenarios"

  current_scenario [ lindex $scenarios 0 ] ;
  
  set path_group_filter $args_array(-path_group_filter)
  set target_groups ""
  if {$path_group_filter != ""} {
    foreach scenario [get_object_name [get_scenarios $scenarios -filter active&&setup]] {
      set target_groups [lsort -uniq [concat $target_groups [get_object_name [get_path_groups -mode [get_attribute [get_scenarios $scenario] mode] -filter $path_group_filter]]]]
    }
    set target_groups [get_object_name [get_path_groups $target_groups]] 
    puts "RM-info: Operating on path group(s): $target_groups"
  } else {
    foreach scenario [get_object_name [get_scenarios $scenarios -filter active&&setup]] {
      set target_groups [lsort -uniq [concat $target_groups [get_object_name [get_path_groups -mode [get_attribute [get_scenarios $scenario] mode]]]]]
    }
    puts "RM-info: Operating on all path group(s): $target_groups"
  }

  set max_paths $args_array(-max_paths)
  puts "RM-info: max_paths set to $max_paths"

  set slack_threshold $args_array(-slack_lesser_than)

  set user_unit [get_user_units -type time -input -numeric]
  set threshold_in_ps [expr $slack_threshold*$user_unit/(1e-12)]
  
  if {$threshold_in_ps > -1.0} {
    puts "RM-warning: slack_lesser_than threshold too small. Set to -1 ps"
    set threshold_in_ps -1.0
  }
  set_user_units -type time -value 1ps -input
  
  puts "RM-info: slack_lesser_than threshold set to $threshold_in_ps ps"
  suppress_message "TIM-010 TIM-011"

  reset_app_options ccd.targeted_ccd* ;
  set_app_options -name route_opt.flow.enable_ccd -value true ;
  set_app_options -name time.pba_optimization_mode -value path ;
  set_app_options -name time.report_use_pba_optimization_mode -value true ;
  set_app_options -name route_opt.flow.enable_targeted_ccd_wns_optimization -value true ;
  
  if {$path_group_filter !=""} {
    set target_endpoints [get_attribute [get_timing_paths -scenario $scenarios -pba_mode path -groups $target_groups -max_paths $max_paths -from [all_registers -clock_pin] -to [all_registers -data_pin] -slack_lesser_than $threshold_in_ps] endpoint ]
  } else {
    set target_endpoints [get_attribute [get_timing_paths -scenario $scenarios -pba_mode path -max_paths $max_paths -from [all_registers -clock_pin] -to [all_registers -data_pin] -slack_lesser_than $threshold_in_ps] endpoint ]
  } 

  set_user_units -type time -value $user_unit -input
  unsuppress_message "TIM-010 TIM-011"

  if {[sizeof_collection $target_endpoints ] != 0 } { 
    puts "RM-info: PBA-CCD route_opt working on [sizeof_collection $target_endpoints] target endpoints"
    set_route_opt_target_endpoints -setup_endpoints_collection $target_endpoints
    route_opt
  } else {
    puts "RM-info: No qualified endpoints found. Skip PBA-CCD route_opt"
  }
} ;

define_proc_attributes targeted_ep_ropt_pba_ccd -info {Run loop of targeted endpoint recipe on endpoints in given path groups} \
  -define_args { \
                       {-scenarios {Scenarios to target endpoints in, default all active setup scenarios} {<scenarios>} {string} {optional {default "*"}}} \
                       {-path_group_filter {Path group filter to collect paths for endpoint optimization, default "" no filtering } {<path_group_filter>} {string} {optional {default ""}}} \
                       {-max_paths {Number of paths to collect, default 10000} {<integer>} {int} {optional {default 10000}}} \
                       {-slack_lesser_than {Collect paths with slack worse than, default -0.001 ns} {<float>} {string} {optional {default -0.001}}} \
               }

proc rm_report_drc {args} {
  set arg(-err_data) zroute.err
	parse_proc_arguments -args $args arg

	if {[info exists arg(-output_file)]} {
    if {[catch {open $arg(-output_file) w} FH]} {
      puts stderr "Error:  Can't open file $arg(-output_file) for write"
      return
    }
  } else {
    set FH stdout
  }

  set closeFile false
  if {[set errData [get_drc_error_data -quiet $arg(-err_data)]] eq ""} {
    set errData [open_drc_error_data $arg(-err_data)]
    if {$errData eq ""} {
      puts stderr "Error: Can't open DRC error data file $arg(-err_data)"
      return
    }
    set closeFile true
  }

	if {[info exists arg(-type)]} {
    foreach type $arg(-type) {set typ($type) ""}
	} else {
    foreach_in_collection itm [get_drc_error_types -error_data $errData] {set typ([get_attribute $itm name]) ""}
	}

	if {[info exists arg(-ignore_type)]} {
		foreach type $arg(-ignore_type) {array unset typ($type)}
	}
	if {[array size typ] == 0} {
		puts $FH "\n#################\nNo DRCs to report\n#################\n"
    return
  }

	if {[info exists arg(-layer)]} {
    foreach layer $arg(-layer) {set lyr($layer) ""}
    set lyr_order $arg(-layer)
  } else {
    if {[info exists arg(-ignore_vias)]} {
      set lst [get_object_name [get_layers -filter layer_type==interconnect]]
    } else {
      set lst [get_object_name [get_layers -filter {layer_type==interconnect || layer_type==via_cut}]]
    }
    if {![info exists arg(-include_nonrouting)]} {
      redirect -variable tmp {report_ignored_layers}
      set min [lindex [regsub {.*Min Routing Layer\s+} $tmp ""] 0]
      set max [lindex [regsub {.*Max Routing Layer\s+} $tmp ""] 0]
      set lst [lrange $lst [lsearch -exact $lst $min] [lsearch -exact $lst $max]]
    }
    foreach layer $lst {set lyr($layer) ""}
    set lyr_order $lst
	}

	if {[info exists arg(-ignore_layer)]} {
		foreach layer $arg(-ignore_layer) {array unset lyr($layer)}
    set lyr_order [lsearch -all -inline -not -exact $lyr_order $layer]
	}
	if {[array size lyr] == 0} {
		puts $FH "\n#################\nNo DRCs to report\n#################\n"
    return
  }

  array unset cnt
  array unset totals
  set maxlen 17
  foreach type [array names typ] {
    foreach layer [array names lyr] {
      set cnt($type,$layer) 0
      set totals($layer) 0
    }
    set totals($type) 0
    if {[set new [string length $type]] > $maxlen} {set maxlen $new}
  }
  set totals(all) 0

  foreach_in_collection mkr [get_drc_errors -error_data $errData] {
    set type [get_attribute $mkr type_name]
    if {![info exists typ($type)]} {continue} 
    foreach_in_collection itm [get_attribute $mkr layers] {
      set layer [get_object_name $itm]
      if {![info exists lyr($layer)]} {continue} 
      incr cnt($type,$layer)
    }
    incr totals($layer)
    incr totals($type)
    incr totals(all)
  }

  if {$closeFile} {close_drc_error_data -force $arg(-err_data)}

  set layer_count 0
  puts $FH "DRC Violation Summary Report for $arg(-err_data)"
  puts -nonewline $FH [format "%${maxlen}s |" ""]
  foreach layer $lyr_order {
    if {$totals($layer) > 0} {puts -nonewline $FH [format "%6s" $layer]; incr layer_count}
  }
  puts $FH [format " |%17s" "TOTALS BY MARKER"]
  set sep [string repeat "-" [expr $maxlen + ($layer_count*6) + 21]]
  puts $FH $sep

  foreach type [lsort [array name typ]] {
    if {$totals($type) > 0} {
    puts -nonewline $FH [format "%${maxlen}s |" $type]  
      foreach layer $lyr_order {
        if {$totals($layer) == 0} {continue}
        if {$cnt($type,$layer) > 0} {
          puts -nonewline $FH [format "%6d" $cnt($type,$layer)]
        } else {
          puts -nonewline $FH [format "%6s" "-"]
        }
      }
    puts -nonewline $FH [format " |%6d" $totals($type)]
    puts $FH ""
    }
  }

  puts $FH $sep
  puts -nonewline $FH [format "%${maxlen}s |" ""]
  foreach layer $lyr_order {
    if {$totals($layer) > 0} {puts -nonewline $FH [format "%6s" $layer]; incr layer_count}
  }
  puts $FH [format " |%6d" $totals(all)]

  puts -nonewline $FH [format "%${maxlen}s |" "TOTALS BY LAYER"]
  foreach layer $lyr_order {
    if {$totals($layer) > 0} {puts -nonewline $FH [format "%6s" $totals($layer)]}
  }
  puts -nonewline $FH [format " |"]

  puts $FH "\n"

	if {[info exists arg(-output_file)]} {close $FH}

} ;

define_proc_attributes rm_report_drc \
	-info "Report routing DRCs by Layer and/or DRC_type\n" \
	-define_args {
		{-layer "Layer list on which to report DRCs.  Default all routing and all via layers." layer list optional}
		{-type "List of DRC violation types to report. eg: -type {{Short} {Less than NDR width}}" type list optional}
		{-ignore_layer "Layers to exclude from report. eg: -ignore_layer {M2 M7} " ignore_layer list optional}
		{-ignore_type "DRC violation types to ignore. eg: -ignore_type {{Short} {Less than NDR width}}" ignore_type list optional}
		{-ignore_vias "Remove via cut layers in default reporting layers." "" boolean optional}
		{-include_nonrouting "Include non-routing layers in default reporting layers." "" boolean optional}
		{-err_data "Source error data file.  Default: zroute.err" err_data string optional}
		{-output_file "Write results to file.  Default: stdout" output_file string optional}
	}

proc proc_qor {args} {

  set version 2.07
  proc proc_mysort_hash {args} {

    parse_proc_arguments -args ${args} opt

    upvar $opt(hash) myarr

    set given    "[info exists opt(-values)][info exists opt(-dict)][info exists opt(-reverse)]"

    set key_list  [array names myarr]

    switch $given {
      000 { return [lsort -real $key_list] }
      001 { return [lsort -real -decreasing $key_list] }
      010 { return [lsort -dictionary $key_list] }
      011 { return [lsort -dictionary -decreasing $key_list] }
    }
  
    foreach {a b} [array get myarr] { lappend full_list [list $a $b] }

    switch $given {
      100 { set sfull_list [lsort -real -index 1 $full_list] }
      101 { set sfull_list [lsort -real -index 1 -decreasing $full_list] }
      110 { set sfull_list [lsort -index 1 -dictionary $full_list] }
      111 { set sfull_list [lsort -index 1 -dictionary -decreasing $full_list] }

    }

    foreach i $sfull_list { lappend sorted_key_list [lindex $i 0] }
    return $sorted_key_list
  }

  define_proc_attributes proc_mysort_hash -info "USER PROC:sorts a hash based on options and returns sorted keys list\nUSAGE: set sorted_keys \[proc_mysort_hash hash_name_without_dollar\]" \
        -define_args { \
                    { -reverse 	"reverse sort"      			""              	boolean optional }
                    { -dict 	"dictionary sort, default numerical"	""              	boolean optional }
                    { -values 	"sort values, default keys"      	""              	boolean optional }
                    { hash   	"hash"         				"hash"            	list    required }
                    }

  echo "\nVersion $version\n"
  parse_proc_arguments -args $args results
  set skew_flag [info exists results(-skew)]
  set scenario_flag [info exists results(-scenarios)]
  set pba_flag  [info exists results(-pba_mode)]
  set file_flag [info exists results(-existing_qor_file)]
  set no_hist_flag [info exists results(-no_histogram)]
  set unit_flag [info exists results(-units)]
  set no_pg_flag   [info exists results(-no_pathgroup_info)]
  set sort_by_tns_flag   [info exists results(-sort_by_tns)]
  set uncert_flag [info exists results(-signoff_uncertainty_adjustment)]
  if {[info exists results(-tee)]} {set tee "-tee -var" } else { set tee "-var" }
  if {[info exists results(-csv_file)]} {set csv_file $results(-csv_file)} else { set csv_file "qor.csv" }
  if {$file_flag&&$skew_flag} { echo "Error!! -skew cannot be used with -existing_qor_file" ; return }
  if {$file_flag&&$no_hist_flag} { echo "Warning!! -no_histogram flag is ignored when -existing_qor_file is used" }
  if {$file_flag} { 
    if {[file exists $results(-existing_qor_file)]} { 
      set qor_file  $results(-existing_qor_file) 
    } else { 
      echo "Error!! Cannot find given -existing_qor_file $results(-existing_qor_file)" 
      return
    }
  }
  if {[info exists results(-units)]} {set unit $results(-units)}
  if {[info exists results(-pba_mode)]} {
    if { $::synopsys_program_name != "pt_shell" && $::synopsys_program_name != "icc2_shell" && $::synopsys_program_name != "fc_shell" } { echo "Error!! -pba_mode supported only in pt_shell, icc2_shell, and fc_shell" ; return}
  }
  if {[info exists results(-pba_mode)]} {set pba_mode $results(-pba_mode)} else { set pba_mode "none" }
  if {[info exists results(-pba_mode)]&&$file_flag} { echo "-pba_mode ignored when -existing_qor_file is used" }

  set nil "~"

  if {$uncert_flag} {
    echo "-signoff_uncertainty_adjustment only changes Frequency Column, report still sorted by WNS"
    set signoff_uncert $results(-signoff_uncertainty_adjustment)
  }

  if {$file_flag} {
    set tmp [open $qor_file "r"]
    set x [read $tmp]
    close $tmp
    if {[regexp {\(max_delay/setup|\(min_delay/hold} $x]} { set pt_file 1 } else { set pt_file 0 }
  } else {
    if {$::synopsys_program_name == "pt_shell"} {
          if {$::pt_shell_mode=="primetime_master"} {echo "Error!! proc_qor not supported in DMSA Master" ; return }
          set pt_file 1
          set orig_uncons $::timing_report_unconstrained_paths
          if {[info exists ::timing_report_union_tns]} { set orig_union  $::timing_report_union_tns } else { set orig_union true }
          set ::timing_report_union_tns true
          if {[regsub -all {[A-Z\-\.]} $::sh_product_version {}]>=201506} {
            echo -n "Running report_qor -pba_mode $pba_mode ; report_qor -pba_mode $pba_mode -summary ... "
            redirect {*}$tee x { report_qor -pba_mode $pba_mode ; report_qor -pba_mode $pba_mode -summary }
          } else {
            echo -n "Running report_qor ; report_qor -summary ... "
            redirect {*}$tee x { report_qor ; report_qor -summary }
          }
          echo "Done"
      } else {
        set pt_file 0
        if {$scenario_flag} {
          if { $::synopsys_program_name == "icc2_shell" || $::synopsys_program_name == "dcrt_shell" || $::synopsys_program_name == "fc_shell" } {
            if {[regsub -all {[A-Z\-\.]} $::sh_product_version {}]>=201709} {
              echo -n "Running report_qor -pba_mode $pba_mode -nosplit -scenarios $results(-scenarios) ; report_qor -pba_mode $pba_mode -nosplit -summary ... "
              redirect {*}$tee x { report_qor -pba_mode $pba_mode -nosplit -scenarios $results(-scenarios) ; report_qor -pba_mode $pba_mode -nosplit -summary }
            } else {
              echo -n "Running report_qor -nosplit -scenarios $results(-scenarios) ; report_qor -nosplit -summary ... "
              redirect {*}$tee x { report_qor -nosplit -scenarios $results(-scenarios) ; report_qor -nosplit -summary }
            }
          } else {
            echo -n "Running report_qor -nosplit -scenarios $results(-scenarios) ... "
            redirect {*}$tee x { report_qor -nosplit -scenarios $results(-scenarios) }
          }
          echo "Done"
        } else {
          if { $::synopsys_program_name == "icc2_shell" || $::synopsys_program_name == "dcrt_shell" || $::synopsys_program_name == "fc_shell" } {
            if {[regsub -all {[A-Z\-\.]} $::sh_product_version {}]>=201709} {
              echo -n "Running report_qor -pba_mode $pba_mode -nosplit ; report_qor -pba_mode $pba_mode -nosplit -summary ... "
              redirect {*}$tee x { report_qor -pba_mode $pba_mode -nosplit ; report_qor -pba_mode $pba_mode -nosplit -summary }
            } else {
              echo -n "Running report_qor -nosplit ; report_qor -nosplit -summary ... "
              redirect {*}$tee x { report_qor -nosplit ; report_qor -nosplit -summary }
            }
          } else {
            echo -n "Running report_qor -nosplit ... "
            redirect {*}$tee x { report_qor -nosplit }
          }
          echo "Done"
        }
    }
  }
  
  if {$unit_flag} {
    if {[string match $unit "ps"]} { set unit 1000000 } else { set unit 1000 }
  } else {
    catch {redirect -var y {report_units}}
    if {[regexp {(\S+)\s+Second} $y match unit]} {
      if {[regexp {e-12} $unit]} { set unit 1000000 } else { set unit 1000 }
    } elseif {[regexp {ns} $y]} { set unit 1000
    } elseif {[regexp {ps} $y]} { set unit 1000000 }
  }

  if {![info exists unit]} { set unit 1000 }
  
  set drc 0
  set cella 0
  set buf 0
  set leaf 0
  set tnets 0
  set cbuf 0
  set seqc 0
  set tran 0
  set cap 0
  set fan 0
  set combc 0
  set macroc 0
  set comba 0
  set seqa 0
  set desa 0
  set neta 0
  set netl 0
  set netx 0
  set nety 0
  set hierc 0
  if {![file writable [file dir $csv_file]]} {
    echo "$csv_file not writable, Writing to /dev/null instead"
    set csv_file "/dev/null"
  }
  set csv [open $csv_file "w"]

  if {!$pt_file} {
  set i 0
  set group_just_set 0
  foreach line [split $x "\n"] {
  
    incr i

    if {[regexp {^\s*Scenario\s+\'(\S+)\'} $line match scenario]} {
    } elseif {[regexp {^\s*Timing Path Group\s+\'(\S+)\'} $line match group]} {
      if {[info exists scenario]} { set group ${group}($scenario) }
      set GROUPS($group) 1
      set group_just_set 1
      unset -nocomplain ll cpl wns cp tns nvp wnsh tnsh nvph fr
    } elseif {[regexp {^\s*------\s*$} $line]} {
      if {$group_just_set} {
        continue 
      } else {
        set group_just_set 0
        unset -nocomplain group scenario
      }
    } elseif {[regexp {^\s*Levels of Logic\s*:\s*(\S+)\s*$} $line match ll]} {
      set GROUP_LL($group) $ll
    } elseif {[regexp {^\s*Critical Path Length\s*:\s*(\S+)\s*$} $line match cpl]} {
      set GROUP_CPL($group) $cpl
    } elseif {[regexp {^\s*Critical Path Slack\s*:\s*(\S+)\s*$} $line match wns]} { 
      if {![string is double $wns]} { set wns 0.0 }
      set GROUP_WNS($group) $wns 
    } elseif {[regexp {^\s*Critical Path Clk Period\s*:\s*(\S+)\s*$} $line match cp]} { 
      if {![string is double $cp]} { set cp 0.0 }
      set GROUP_CP($group) $cp
    } elseif {[regexp {^\s*Total Negative Slack\s*:\s*(\S+)\s*$} $line match tns]} {
      set GROUP_TNS($group) $tns
    } elseif {[regexp {^\s*No\. of Violating Paths\s*:\s*(\S+)\s*$} $line match nvp]} {
      set GROUP_NVP($group) $nvp
    } elseif {[regexp {^\s*Worst Hold Violation\s*:\s*(\S+)\s*$} $line match wnsh]} {
      if {![string is double $wnsh]} { set wnsh 0.0 }
      set GROUP_WNSH($group) $wnsh
    } elseif {[regexp {^\s*Total Hold Violation\s*:\s*(\S+)\s*$} $line match tnsh]} {
      set GROUP_TNSH($group) $tnsh
    } elseif {[regexp {^\s*No\. of Hold Violations\s*:\s*(\S+)\s*$} $line match nvph]} {
      set GROUP_NVPH($group) $nvph

    } elseif {[regexp {^\s*Hierarchical Cell Count\s*:\s*(\S+)\s*$} $line match hierc]} {
    } elseif {[regexp {^\s*Hierarchical Port Count\s*:\s*(\S+)\s*$} $line match hierp]} {
    } elseif {[regexp {^\s*Leaf Cell Count\s*:\s*(\S+)\s*$} $line match leaf]} {
      set leaf [expr {$leaf/1000}]
    } elseif {[regexp {^\s*Buf/Inv Cell Count\s*:\s*(\S+)\s*$} $line match buf]} {
      set buf [expr {$buf/1000}]
    } elseif {[regexp {^\s*CT Buf/Inv Cell Count\s*:\s*(\S+)\s*$} $line match cbuf]} {
    } elseif {[regexp {^\s*Combinational Cell Count\s*:\s*(\S+)\s*$} $line match combc]} {
      set combc [expr $combc/1000]
    } elseif {[regexp {^\s*Sequential Cell Count\s*:\s*(\S+)\s*$} $line match seqc]} {
    } elseif {[regexp {^\s*Macro Count\s*:\s*(\S+)\s*$} $line match macroc]} {
 
    } elseif {[regexp {^\s*Combinational Area\s*:\s*(\S+)\s*$} $line match comba]} {
      set comba [expr {int($comba)}]
    } elseif {[regexp {^\s*Noncombinational Area\s*:\s*(\S+)\s*$} $line match seqa]} {
      set seqa [expr {int($seqa)}]
    } elseif {[regexp {^\s*Net Area\s*:\s*(\S+)\s*$} $line match neta]} {
      set neta [expr {int($neta)}]
    } elseif {[regexp {^\s*Net XLength\s*:\s*(\S+)\s*$} $line match netx]} {
    } elseif {[regexp {^\s*Net YLength\s*:\s*(\S+)\s*$} $line match nety]} {
    } elseif {[regexp {^\s*Cell Area\s*.*:\s*(\S+)\s*$} $line match cella]} {
      set cella [expr {int($cella)}]
    } elseif {[regexp {^\s*Design Area\s*:\s*(\S+)\s*$} $line match desa]} {
      set desa [expr {int($desa)}]
    } elseif {[regexp {^\s*Net Length\s*:\s*(\S+)\s*$} $line match netl]} {
      set netl [expr {int($netl)}]

    } elseif {[regexp {^\s*Total Number of Nets\s*:\s*(\S+)\s*$} $line match tnets]} {
      set tnets [expr {$tnets/1000}]
    } elseif {[regexp {^\s*Nets With Violations\s*:\s*(\S+)\s*$} $line match drc]} {
    } elseif {[regexp {^\s*Max Trans Violations\s*:\s*(\S+)\s*$} $line match tran]} {
    } elseif {[regexp {^\s*Max Cap Violations\s*:\s*(\S+)\s*$} $line match cap]} {
    } elseif {[regexp {^\s*Max Fanout Violations\s*:\s*(\S+)\s*$} $line match fan]} {

    } elseif {[regexp {^\s*Scenario:\s*(\S+)\s+\s+WNS:\s*(\S+)\s*TNS:\s*(\S+).*Paths:\s*(\S+)} $line match scenario wns tns nvp]} {
      set SETUP_SCENARIOS($scenario) 1
      set SETUP_SCENARIO_WNS($scenario) $wns
      set SETUP_SCENARIO_TNS($scenario) $tns
      set SETUP_SCENARIO_NVP($scenario) $nvp
    } elseif {[regexp {^\s*Scenario:\s*(\S+)\s+\(Hold\)\s+WNS:\s*(\S+)\s*TNS:\s*(\S+).*Paths:\s*(\S+)} $line match scenario wns tns nvp]} {
      set HOLD_SCENARIOS($scenario) 1
      set HOLD_SCENARIO_WNS($scenario) $wns
      set HOLD_SCENARIO_TNS($scenario) $tns
      set HOLD_SCENARIO_NVP($scenario) $nvp
    } elseif {[regexp {^\s*Design\s+WNS:\s*(\S+)\s*TNS:\s*(\S+).*Paths:\s*(\S+)} $line match setup_wns setup_tns setup_nvp]} {
      if {![string is double $setup_wns]} { set setup_wns 0.0 }
      if {![string is double $setup_tns]} { set setup_tns 0.0 }
      if {![string is double $setup_nvp]} { set setup_nvp 0 }
    } elseif {[regexp {^\s*Design\s+\(Hold\)\s*WNS:\s*(\S+)\s*TNS:\s*(\S+).*Paths:\s*(\S+)} $line match hold_wns hold_tns hold_nvp]} {
      if {![string is double $hold_wns]} { set hold_wns 0.0 }
      if {![string is double $hold_tns]} { set hold_tns 0.0 }
      if {![string is double $hold_nvp]} { set hold_nvp 0 }
    } elseif {[regexp {^\s*Design\s+\(Setup\)\s+(\S+)\s+(\S+)\s+(\d+)\s*$} $line match setup_wns setup_tns setup_nvp]} {
      if {![string is double $setup_wns]} { set setup_wns 0.0 }
      if {![string is double $setup_tns]} { set setup_tns 0.0 }
      if {![string is double $setup_nvp]} { set setup_nvp 0 }
    } elseif {[regexp {^\s*Design\s+\(Hold\)\s+(\S+)\s+(\S+)\s+(\d+)\s*$} $line match hold_wns hold_tns hold_nvp]} {
      if {![string is double $hold_wns]} { set hold_wns 0.0 }
      if {![string is double $hold_tns]} { set hold_tns 0.0 }
      if {![string is double $hold_nvp]} { set hold_nvp 0 }
    } elseif {[regexp {^\s*Error\:} $line]} {
      echo "Error: found in report_qor. Exiting ..."
      return
    }

  }
  if {$drc==0} { set drc [expr $tran+$cap+$fan] }
  }

  if {$pt_file} {
  set i 0
  set group_just_set 0
  foreach line [split $x "\n"] {
  
    incr i

    if {[regexp {^\s*Scenario\s+\'(\S+)\'} $line match scenario]} {
    } elseif {[regexp {^\s*Timing Path Group\s+\'(\S+)\'\s*\(max_delay} $line match group]} {
      if {[info exists scenario]} { set group ${group}($scenario) }
      set GROUPS($group) 1
      set group_just_set 1
      set group_is_setup 1
      unset -nocomplain ll cpl wns cp tns nvp wnsh tnsh nvph fr
    } elseif {[regexp {^\s*Timing Path Group\s+\'(\S+)\'\s*\(min_delay} $line match group]} {
      if {[info exists scenario]} { set group ${group}($scenario) }
      set GROUPS($group) 1
      set group_just_set 1
      set group_is_setup 0
      unset -nocomplain ll cpl wns cp tns nvp wnsh tnsh nvph fr
    } elseif {[regexp {^\s*------\s*$} $line]} {
      if {$group_just_set} {
        continue 
      } else {
        set group_just_set 0
        unset -nocomplain group scenario
      }
    } elseif {[regexp {^\s*Levels of Logic\s*:\s*(\S+)\s*$} $line match ll]} {
      set GROUP_LL($group) $ll
    } elseif {[regexp {^\s*Critical Path Length\s*:\s*(\S+)\s*$} $line match cpl]} {
      set GROUP_CPL($group) $cpl
    } elseif {[regexp {^\s*Critical Path Slack\s*:\s*(\S+)\s*$} $line match wns]} {
      if {![string is double $wns]} { set wns 0.0 } 
      if {$group_is_setup} { set GROUP_WNS($group) $wns } else { set GROUP_WNSH($group) $wns }
    } elseif {[regexp {^\s*Critical Path Clk Period\s*:\s*(\S+)\s*$} $line match cp]} {
      if {![string is double $cp]} { set cp 0.0 }
      set GROUP_CP($group) $cp
    } elseif {[regexp {^\s*Total Negative Slack\s*:\s*(\S+)\s*$} $line match tns]} {
      if {$group_is_setup} { set GROUP_TNS($group) $tns } else { set GROUP_TNSH($group) $tns }
    } elseif {[regexp {^\s*No\. of Violating Paths\s*:\s*(\S+)\s*$} $line match nvp]} {
      if {$group_is_setup} { set GROUP_NVP($group) $nvp } else { set GROUP_NVPH($group) $nvp }

    } elseif {[regexp {^\s*Hierarchical Cell Count\s*:\s*(\S+)\s*$} $line match hierc]} {
    } elseif {[regexp {^\s*Hierarchical Port Count\s*:\s*(\S+)\s*$} $line match hierp]} {
    } elseif {[regexp {^\s*Leaf Cell Count\s*:\s*(\S+)\s*$} $line match leaf]} {
      set leaf [expr {$leaf/1000}]
    } elseif {[regexp {^\s*Buf/Inv Cell Count\s*:\s*(\S+)\s*$} $line match buf]} {
      set buf [expr {$buf/1000}]
    } elseif {[regexp {^\s*CT Buf/Inv Cell Count\s*:\s*(\S+)\s*$} $line match cbuf]} {
    } elseif {[regexp {^\s*Combinational Cell Count\s*:\s*(\S+)\s*$} $line match combc]} {
      set combc [expr $combc/1000]
    } elseif {[regexp {^\s*Sequential Cell Count\s*:\s*(\S+)\s*$} $line match seqc]} {
    } elseif {[regexp {^\s*Macro Count\s*:\s*(\S+)\s*$} $line match macroc]} {
 
    } elseif {[regexp {^\s*Combinational Area\s*:\s*(\S+)\s*$} $line match comba]} {
      set comba [expr {int($comba)}]
    } elseif {[regexp {^\s*Noncombinational Area\s*:\s*(\S+)\s*$} $line match seqa]} {
      set seqa [expr {int($seqa)}]
    } elseif {[regexp {^\s*Net Interconnect area\s*:\s*(\S+)\s*$} $line match neta]} {
      set neta [expr {int($neta)}]
    } elseif {[regexp {^\s*Net XLength\s*:\s*(\S+)\s*$} $line match netx]} {
    } elseif {[regexp {^\s*Net YLength\s*:\s*(\S+)\s*$} $line match nety]} {
    } elseif {[regexp {^\s*Total cell area\s*.*:\s*(\S+)\s*$} $line match cella]} {
      set cella [expr {int($cella)}]
    } elseif {[regexp {^\s*Design Area\s*:\s*(\S+)\s*$} $line match desa]} {
      set desa [expr {int($desa)}]
    } elseif {[regexp {^\s*Net Length\s*:\s*(\S+)\s*$} $line match netl]} {
      set netl [expr {int($netl)}]

    } elseif {[regexp {^\s*Total Number of Nets\s*:\s*(\S+)\s*$} $line match tnets]} {
      set tnets [expr {$tnets/1000}]
    } elseif {[regexp {^\s*Nets With Violations\s*:\s*(\S+)\s*$} $line match drc]} {
    } elseif {[regexp {^\s*max_transition Count\s*:\s*(\S+)\s*$} $line match tran]} {
    } elseif {[regexp {^\s*max_capacitance Count\s*:\s*(\S+)\s*$} $line match cap]} {
    } elseif {[regexp {^\s*max_fanout Count\s*:\s*(\S+)\s*$} $line match fan]} {

    } elseif {[regexp {^\s*Scenario:\s*(\S+)\s+\s+WNS:\s*(\S+)\s*TNS:\s*(\S+).*Paths:\s*(\S+)} $line match scenario wns tns nvp]} {
      set SETUP_SCENARIOS($scenario) 1
      set SETUP_SCENARIO_WNS($scenario) $wns
      set SETUP_SCENARIO_TNS($scenario) $tns
      set SETUP_SCENARIO_NVP($scenario) $nvp
    } elseif {[regexp {^\s*Scenario:\s*(\S+)\s+\(Hold\)\s+WNS:\s*(\S+)\s*TNS:\s*(\S+).*Paths:\s*(\S+)} $line match scenario wns tns nvp]} {
      set HOLD_SCENARIOS($scenario) 1
      set HOLD_SCENARIO_WNS($scenario) $wns
      set HOLD_SCENARIO_TNS($scenario) $tns
      set HOLD_SCENARIO_NVP($scenario) $nvp
    } elseif {[regexp {^\s*Setup\s+WNS:\s*(\S+)\s*TNS:\s*(\S+).*Paths:\s*(\S+)} $line match setup_wns setup_tns setup_nvp]} {
      if {![string is double $setup_wns]} { set setup_wns 0.0 }
      if {![string is double $setup_tns]} { set setup_tns 0.0 }
      if {![string is double $setup_nvp]} { set setup_nvp 0 }
    } elseif {[regexp {^\s*Hold\s*WNS:\s*(\S+)\s*TNS:\s*(\S+).*Paths:\s*(\S+)} $line match hold_wns hold_tns hold_nvp]} {
      if {![string is double $hold_wns]} { set hold_wns 0.0 }
      if {![string is double $hold_tns]} { set hold_tns 0.0 }
      if {![string is double $hold_nvp]} { set hold_nvp 0 }
    } elseif {[regexp {^\s*Error\:} $line]} {
      echo "Error: found in report_qor. Exiting ..."
      return
    }

  }
  if {$drc==0} { set drc [expr $tran+$cap+$fan] }
  }

  if {![info exists GROUPS]} {
    echo "Error!! no QoR data found to reformat"
    return
  }

  if {$skew_flag} {

    if { $::synopsys_program_name == "icc2_shell" || $::synopsys_program_name == "dcrt_shell" || $::synopsys_program_name == "fc_shell" } {
      if {![get_app_option -name time.remove_clock_reconvergence_pessimism]} { echo "WARNING!! crpr is not turned on, skew values reported could be pessimistic" }
    } else {
      if {$::timing_remove_clock_reconvergence_pessimism=="false"} { echo "WARNING!! crpr is not turned on, skew values reported could be pessimistic" }
    }
    echo "Skews numbers reported include any ocv derates, crpr value is close, but may not match report_timing UITE-468"
    echo "Getting setup timing paths for skew analysis"
    if { $::synopsys_program_name != "pt_shell" && $::synopsys_program_name != "icc2_shell" && $::synopsys_program_name != "fc_shell" } {
      redirect /dev/null {set paths [get_timing_paths -slack_less 0 -max_paths 100000] } 
    } else { 
      redirect /dev/null {set paths [get_timing_paths -slack_less 0 -max_paths 100000 -pba_mode $pba_mode] } 
    }

    foreach_in_collection p $paths {

      set g [get_attribute [get_attribute -quiet $p path_group] full_name]
      set scenario [get_attribute -quiet $p scenario]
      if {[regexp {^_sel\d+$} $scenario]} { set scenario [get_object_name $scenario] }
      if {$scenario !=""} { set g ${g}($scenario) }
      if { $::synopsys_program_name == "icc2_shell" || $::synopsys_program_name == "dcrt_shell" || $::synopsys_program_name == "fc_shell" } {
        set e_arr [get_attribute -quiet $p endpoint_clock_close_edge_arrival]
        set e_val [get_attribute -quiet $p endpoint_clock_close_edge_value]
        if {$e_arr!=""&&$e_val!=""} { set e [expr {$e_arr-$e_val}] ; if {$e<0} { set e 0.0 } }
        set s_arr [get_attribute -quiet $p startpoint_clock_open_edge_arrival]
        set s_val [get_attribute -quiet $p startpoint_clock_open_edge_value]
        if {$s_arr!=""&&$s_val!=""} { set s [expr {$s_arr-$s_val}] ; if {$s<0} { set s 0.0 } }
      } else {
        set e [get_attribute -quiet $p endpoint_clock_latency]
        set s [get_attribute -quiet $p startpoint_clock_latency]
      }

      if { $::synopsys_program_name == "pt_shell" || $::synopsys_program_name == "icc2_shell" || $::synopsys_program_name == "dcrt_shell" || $::synopsys_program_name == "fc_shell" } { 
        set crpr [get_attribute -quiet $p common_path_pessimism]
      } else {
        set crpr [get_attribute -quiet $p crpr_value]
      }
      if {$crpr==""} { set crpr 0 }

      if {$e!=""&&$s!=""} { set skew [expr {$e-$s}] } else { set skew 0 }

      if {$skew<0}       { set skew [expr {$skew+$crpr}]
      } elseif {$skew>0} { set skew [expr {$skew-$crpr}]
      } elseif {$skew==0} {}

      if {![info exists SKEW_WNS($g)]} { set SKEW_WNS($g) $skew }
      if {![info exists SKEW_TNS($g)]} { set SKEW_TNS($g) $skew } else { set SKEW_TNS($g) [expr {$SKEW_TNS($g)+$skew}] }
    }

    echo "Getting hold  timing paths for skew analysis"
    if {$::synopsys_program_name != "pt_shell"} {
      redirect /dev/null { set paths [get_timing_paths -slack_less 0 -max_paths 100000 -delay min] }
    } else { 
      redirect /dev/null { set paths [get_timing_paths -pba_mode $pba_mode -slack_less 0 -max_paths 100000 -delay min] }
    }

    foreach_in_collection p $paths {

      set g [get_attribute [get_attribute -quiet $p path_group] full_name]
      set scenario [get_attribute -quiet $p scenario]
      if {[regexp {^_sel\d+$} $scenario]} { set scenario [get_object_name $scenario] }
      if {$scenario !=""} { set g ${g}($scenario) }
      if { $::synopsys_program_name == "icc2_shell" || $::synopsys_program_name == "dcrt_shell" || $::synopsys_program_name == "fc_shell" } { 
        set e_arr [get_attribute -quiet $p endpoint_clock_close_edge_arrival]
        set e_val [get_attribute -quiet $p endpoint_clock_close_edge_value]
        if {$e_arr!=""&&$e_val!=""} { set e [expr {$e_arr-$e_val}] ; if {$e<0} { set e 0.0 } }
        set s_arr [get_attribute -quiet $p startpoint_clock_open_edge_arrival]
        set s_val [get_attribute -quiet $p startpoint_clock_open_edge_value]
        if {$s_arr!=""&&$s_val!=""} { set s [expr {$s_arr-$s_val}] ; if {$s<0} { set s 0.0 } }
      } else {
        set e [get_attribute -quiet $p endpoint_clock_latency]
        set s [get_attribute -quiet $p startpoint_clock_latency]
      }

      if { $::synopsys_program_name == "pt_shell" || $::synopsys_program_name == "icc2_shell" || $::synopsys_program_name == "dcrt_shell" || $::synopsys_program_name == "fc_shell" } { 
        set crpr [get_attribute -quiet $p common_path_pessimism]
      } else {
        set crpr [get_attribute -quiet $p crpr_value]
      }
      if {$crpr==""} { set crpr 0 }

      if {$e!=""&&$s!=""} { set skew [expr {$e-$s}] } else { set skew 0 }

      if {$skew<0}       { set skew [expr {$skew+$crpr}]
      } elseif {$skew>0} { set skew [expr {$skew-$crpr}]
      } elseif {$skew==0} {}

      if {![info exists SKEW_WNSH($g)]} { set SKEW_WNSH($g) $skew }
      if {![info exists SKEW_TNSH($g)]} { set SKEW_TNSH($g) $skew } else { set SKEW_TNSH($g) [expr {$SKEW_TNSH($g)+$skew}] }
    }

    foreach g [array names GROUPS] {

      if {![info exists SKEW_WNS($g)]} { 
        set SKEW_WNS($g) 0.0
        set SKEW_TNS($g) 0.0
      } else {
        set SKEW_TNS($g) [expr {$SKEW_TNS($g)/$GROUP_NVP($g)}]
        if {![info exists maxskew]} { set maxskew $SKEW_WNS($g) }
        if {![info exists maxavg]} { set maxavg $SKEW_TNS($g) }
        if {$maxskew>$SKEW_WNS($g)} { set maxskew $SKEW_WNS($g) }
        if {$maxavg>$SKEW_TNS($g)} { set maxavg $SKEW_TNS($g) }
      }

      if {![info exists SKEW_WNSH($g)]} {
        set SKEW_WNSH($g) 0.0
        set SKEW_TNSH($g) 0.0
      } else {
        set SKEW_TNSH($g) [expr {$SKEW_TNSH($g)/$GROUP_NVPH($g)}]
        if {![info exists maxskewh]} { set maxskewh $SKEW_WNSH($g) }
        if {![info exists maxavgh]} { set maxavgh $SKEW_TNSH($g) }
        if {$maxskewh<$SKEW_WNSH($g)} { set maxskewh $SKEW_WNSH($g) }
        if {$maxavgh<$SKEW_TNSH($g)} { set maxavgh $SKEW_TNSH($g) }
      }

    }

    if {![info exists maxskew]} { set maxskew 0.0 }
    if {![info exists maxavg]} { set maxavg 0.0 }
    if {![info exists maxskewh]} { set maxskewh 0.0 }
    if {![info exists maxavgh]} { set maxavgh 0.0 }

    set maxskew  [format "%10.3f" $maxskew]
    set maxavg   [format "%10.3f" $maxavg]
    set maxskewh [format "%10.3f" $maxskewh]
    set maxavgh  [format "%10.3f" $maxavgh]

  }

  if {[info exists GROUP_WNS]} {
    foreach g [proc_mysort_hash -values GROUP_WNS] {
  
      set wns  [expr {double($GROUP_WNS($g))}]
      if {$pt_file&&!$file_flag} {
        if {![info exists GROUP_CP($g)]} { 
          redirect /dev/null { set cp [get_attr -quiet [get_timing_path -group $g -pba_mode $pba_mode] endpoint_clock.period] }
          if {$cp!=""} { set GROUP_CP($g) $cp }
        }
      }
      if {![info exists GROUP_CP($g)]} { continue }
      set per  [expr {double($GROUP_CP($g))}]
      if {$wns >= $per} { set freq 0.0
      } else {
        if {$uncert_flag} {
          set freq [expr {1.0/($per-$wns-$signoff_uncert)*$unit}]
        } else {
          set freq [expr {1.0/($per-$wns)*$unit}] 
        }
      }
      if {![info exists wfreq]} { set wfreq [format "% 7.0fMHz" $freq] }
      set GROUP_FREQ($g) $freq

    }
  }

  if {![info exists wfreq]} { set wfreq [format "% 7.0fMhz" 0.0] }

  set ttns  0.0
  set tnvp  0
  set ttnsh 0.0
  set tnvph 0

  foreach g [array names GROUPS] {

    if {[info exists GROUP_TNS($g)]}  { set ttns  [expr {$ttns+$GROUP_TNS($g)}] }
    if {[info exists GROUP_NVP($g)]}  { set tnvp  [expr {$tnvp+$GROUP_NVP($g)}] }
    if {[info exists GROUP_TNSH($g)]} { set ttnsh [expr {$ttnsh+$GROUP_TNSH($g)}] }
    if {[info exists GROUP_NVPH($g)]} { set tnvph [expr {$tnvph+$GROUP_NVPH($g)}] }

    if {[info exists GROUP_WNS($g)]}  { set GROUP_WNS_F($g)  [format "% 10.3f" $GROUP_WNS($g)] }  else { set GROUP_WNS_F($g)  [format "% 10s" $nil] }
    if {[info exists GROUP_TNS($g)]}  { set GROUP_TNS_F($g)  [format "% 10.1f" $GROUP_TNS($g)] }  else { set GROUP_TNS_F($g)  [format "% 10s" $nil] }
    if {[info exists GROUP_NVP($g)]}  { set GROUP_NVP_F($g)  [format "% 7.0f"  $GROUP_NVP($g)] }  else { set GROUP_NVP_F($g)  [format "% 7s" $nil] }
    if {[info exists GROUP_WNSH($g)]} { set GROUP_WNSH_F($g) [format "% 10.3f" $GROUP_WNSH($g)] } else { set GROUP_WNSH_F($g) [format "% 10s" $nil] }
    if {[info exists GROUP_TNSH($g)]} { set GROUP_TNSH_F($g) [format "% 10.1f" $GROUP_TNSH($g)] } else { set GROUP_TNSH_F($g) [format "% 10s" $nil] }
    if {[info exists GROUP_NVPH($g)]} { set GROUP_NVPH_F($g) [format "% 7.0f"  $GROUP_NVPH($g)] } else { set GROUP_NVPH_F($g) [format "% 7s" $nil] }
    if {[info exists GROUP_FREQ($g)]} { set GROUP_FREQ_F($g) [format "% 7.0fMHz"  $GROUP_FREQ($g)] } else { set GROUP_FREQ_F($g) [format "% 10s" $nil] }

    if {[info exists SKEW_WNS($g)]}  { set SKEW_WNS_F($g)  [format "% 10.3f"  $SKEW_WNS($g)] }  else { set SKEW_WNS_F($g)  [format "% 10s" $nil] }
    if {[info exists SKEW_TNS($g)]}  { set SKEW_TNS_F($g)  [format "% 10.3f"  $SKEW_TNS($g)] }  else { set SKEW_TNS_F($g)  [format "% 10s" $nil] }
    if {[info exists SKEW_WNSH($g)]} { set SKEW_WNSH_F($g) [format "% 10.3f"  $SKEW_WNSH($g)] } else { set SKEW_WNSH_F($g) [format "% 10s" $nil] }
    if {[info exists SKEW_TNSH($g)]} { set SKEW_TNSH_F($g) [format "% 10.3f"  $SKEW_TNSH($g)] } else { set SKEW_TNSH_F($g) [format "% 10s" $nil] }
  }

  if {[info exists setup_tns]} { set ttns $setup_tns }
  if {[info exists setup_nvp]} { set tnvp $setup_nvp }
  if {[info exists hold_tns]} { set ttnsh $hold_tns }
  if {[info exists hold_nvp]} { set tnvph $hold_nvp }
  set ttns [format "% 10.1f" $ttns]
  set tnvp [format "% 7.0f" $tnvp]
  set ttnsh [format "% 10.1f" $ttnsh]
  set tnvph [format "% 7.0f" $tnvph]

  set maxl 0
  foreach g [array names GROUPS] {
    set l [string length $g]
    if {$maxl < $l} { set maxl $l }
  }
  set maxl [expr {$maxl+2}]
  if {$maxl < 20} { set maxl 20 }
  set drccol [expr {$maxl-13}]

  for {set i 0} {$i<$maxl} {incr i} { append bar - }
  if {$skew_flag} { 
    set bar "${bar}-------------------------------------------------------------------------------------------------------------------" 
  } else {
    set bar "${bar}-----------------------------------------------------------------------"
  }

  if {$skew_flag} {

    echo ""
    echo "SKEW      - Skew on WNS Path"
    echo "AVGSKW    - Average Skew on TNS Paths"
    echo "NVP       - No. of Violating Paths"
    echo "FREQ      - Estimated Frequency, not accurate in some cases, multi/half-cycle, etc"
    echo "WNS(H)    - Hold WNS"
    echo "SKEW(H)   - Skew on Hold WNS Path"
    echo "TNS(H)    - Hold TNS"
    echo "AVGSKW(H) - Average Skew on Hold TNS Paths"
    echo "NVP(H)    - Hold NVP"
    echo ""

    puts $csv "Path Group, WNS, SKEW, TNS, AVGSKW, NVP, FREQ, WNS(H), SKEW(H), TNS(H), AVGSKW(H), NVP(H)"
    echo [format "%-${maxl}s % 10s % 10s % 10s % 10s % 7s % 9s    % 8s % 10s % 10s % 10s % 7s" \
    "Path Group" "WNS" "SKEW" "TNS" "AVGSKW" "NVP" "FREQ" "WNS(H)" "SKEW(H)" "TNS(H)" "AVGSKW(H)" "NVP(H)"]
    echo "$bar"

  } else {

    echo ""
    echo "NVP    - No. of Violating Paths"
    echo "FREQ   - Estimated Frequency, not accurate in some cases, multi/half-cycle, etc"
    echo "WNS(H) - Hold WNS"
    echo "TNS(H) - Hold TNS"
    echo "NVP(H) - Hold NVP"
    echo ""

    puts $csv "Path Group, WNS, TNS, NVP, FREQ, WNS(H), TNS(H), NVP(H)"
    echo [format "%-${maxl}s % 10s % 10s % 7s % 9s    % 8s % 10s % 7s" \
    "Path Group" "WNS" "TNS" "NVP" "FREQ" "WNS(H)" "TNS(H)" "NVP(H)"]
    echo "$bar"

  }

  unset -nocomplain wwns wwnsh
  if {[info exists setup_wns]} {
    set wwns [format "%10.3f" $setup_wns]
  } else { if {[info exists GROUP_WNS]} { foreach g [proc_mysort_hash -values GROUP_WNS] { if {![info exists wwns]} { set wwns $GROUP_WNS_F($g) } } } }
  if {![info exists wwns]} { set wwns [format "% 10s" $nil] }

  if {[info exists hold_wns]} { 
    set wwnsh [format "%10.3f" $hold_wns]
  } else { if {[info exists GROUP_WNSH]} { foreach g [proc_mysort_hash -values GROUP_WNSH] { if {![info exists wwnsh]} { set wwnsh $GROUP_WNSH_F($g) } } } }
  if {![info exists wwnsh]} { set wwnsh [format "% 10s" $nil] }

  if {$sort_by_tns_flag} {
    set setup_sort_group GROUP_TNS
    set hold_sort_group  GROUP_TNSH
  } else {
    set setup_sort_group GROUP_WNS
    set hold_sort_group  GROUP_WNSH
  }

  if {[info exists GROUP_WNS]} {
    foreach g [proc_mysort_hash -values $setup_sort_group] {

      if {$skew_flag} {
        puts $csv "[format "%-${maxl}s" $g], $GROUP_WNS_F($g), $SKEW_WNS_F($g), $GROUP_TNS_F($g), $SKEW_TNS_F($g), $GROUP_NVP_F($g), $GROUP_FREQ_F($g), $GROUP_WNSH_F($g), $SKEW_WNSH_F($g), $GROUP_TNSH_F($g), $SKEW_TNSH_F($g), $GROUP_NVPH_F($g)\n"
      } else {
        puts $csv "[format "%-${maxl}s" $g], $GROUP_WNS_F($g), $GROUP_TNS_F($g), $GROUP_NVP_F($g), $GROUP_FREQ_F($g), $GROUP_WNSH_F($g), $GROUP_TNSH_F($g), $GROUP_NVPH_F($g)\n"
      }

      if {!$no_pg_flag} {
        if {$skew_flag} {
          echo      "[format "%-${maxl}s" $g] $GROUP_WNS_F($g) $SKEW_WNS_F($g) $GROUP_TNS_F($g) $SKEW_TNS_F($g) $GROUP_NVP_F($g) $GROUP_FREQ_F($g) $GROUP_WNSH_F($g) $SKEW_WNSH_F($g) $GROUP_TNSH_F($g) $SKEW_TNSH_F($g) $GROUP_NVPH_F($g)"
        } else {
          echo      "[format "%-${maxl}s" $g] $GROUP_WNS_F($g) $GROUP_TNS_F($g) $GROUP_NVP_F($g) $GROUP_FREQ_F($g) $GROUP_WNSH_F($g) $GROUP_TNSH_F($g) $GROUP_NVPH_F($g)"
        }
      }
      set PRINTED($g) 1

    }
  }

  if {[info exists GROUP_WNSH]} {
    foreach g [proc_mysort_hash -values $hold_sort_group] {

      if {[info exists PRINTED($g)]} { continue }

      if {$skew_flag} {
        puts $csv "[format "%-${maxl}s" $g], $GROUP_WNS_F($g), $SKEW_WNS_F($g), $GROUP_TNS_F($g), $SKEW_TNS_F($g), $GROUP_NVP_F($g), $GROUP_FREQ_F($g), $GROUP_WNSH_F($g), $SKEW_WNSH_F($g), $GROUP_TNSH_F($g), $SKEW_TNSH_F($g), $GROUP_NVPH_F($g)\n"
      } else {
        puts $csv "[format "%-${maxl}s" $g], $GROUP_WNS_F($g), $GROUP_TNS_F($g), $GROUP_NVP_F($g), $GROUP_FREQ_F($g), $GROUP_WNSH_F($g), $GROUP_TNSH_F($g), $GROUP_NVPH_F($g)\n"
      }

      if {!$no_pg_flag} {
        if {$skew_flag} {
          echo      "[format "%-${maxl}s" $g] $GROUP_WNS_F($g) $SKEW_WNS_F($g) $GROUP_TNS_F($g) $SKEW_TNS_F($g) $GROUP_NVP_F($g) $GROUP_FREQ_F($g) $GROUP_WNSH_F($g) $SKEW_WNSH_F($g) $GROUP_TNSH_F($g) $SKEW_TNSH_F($g) $GROUP_NVPH_F($g)"
        } else {
          echo      "[format "%-${maxl}s" $g] $GROUP_WNS_F($g) $GROUP_TNS_F($g) $GROUP_NVP_F($g) $GROUP_FREQ_F($g) $GROUP_WNSH_F($g) $GROUP_TNSH_F($g) $GROUP_NVPH_F($g)"
        }
      }
      set PRINTED($g) 1
    }
  }

  if {!$no_pg_flag} {
    echo "$bar"
  }

  if {$skew_flag} {
    puts $csv "Summary, $wwns, $maxskew, $ttns, $maxavg, $tnvp, $wfreq, $wwnsh, $maxskewh, $ttnsh, $maxavgh, $tnvph"
  } else {
    puts $csv "Summary, $wwns, $ttns, $tnvp, $wfreq, $wwnsh, $ttnsh, $tnvph"
  }

  if {$skew_flag} {
    echo "[format "%-${maxl}s" "Summary"] $wwns $maxskew $ttns $maxavg $tnvp $wfreq $wwnsh $maxskewh $ttnsh $maxavgh $tnvph"
  } else {
    echo "[format "%-${maxl}s" "Summary"] $wwns $ttns $tnvp $wfreq $wwnsh $ttnsh $tnvph"
  }
  echo "$bar"

  puts $csv "CAP, FANOUT, TRAN, TDRC, CELLA, BUFS, LEAFS, TNETS, CTBUF, REGS"

  if {$skew_flag} {
    echo [format "% 7s % 7s % 7s % ${drccol}s % 10s % 10s % 10s % 7s % 10s % 10s" \
     "CAP" "FANOUT" "TRAN" "TDRC" "CELLA" "BUFS" "LEAFS" "TNETS" "CTBUF" "REGS"]
  } else {
    echo [format "% 7s % 7s % 7s % ${drccol}s % 10s % 7s % 9s % 11s % 10s % 7s" \
    "CAP" "FANOUT" "TRAN" "TDRC" "CELLA" "BUFS" "LEAFS" "TNETS" "CTBUF" "REGS"]
  }
  echo "$bar"

  if {$buf==0}   { set buf   $nil }
  if {$tnets==0} { set tnets $nil }
  if {$cbuf==0}  { set cbuf  $nil }
  if {$seqc==0}  { set seqc  $nil }

  puts $csv "$cap, $fan, $tran, $drc, $cella, ${buf}K, ${leaf}K, ${tnets}K, $cbuf, $seqc"

  if {$skew_flag} {
    echo [format "% 7s % 7s % 7s % ${drccol}s % 10s % 9sK % 9sK % 6sK % 10s % 10s" \
    $cap $fan $tran $drc $cella $buf $leaf $tnets $cbuf $seqc]
  } else {
    echo [format "% 7s % 7s % 7s % ${drccol}s % 10s % 6sK % 8sK % 10sK % 10s % 7s" \
    $cap $fan $tran $drc $cella $buf $leaf $tnets $cbuf $seqc]
  }
  echo "$bar"

  if {![info exists setup_tns]} { echo "#Union TNS/NVP not found in report_qor, Summary line will report pessimistic summation TNS/NVP" }

  close $csv
  if {$::synopsys_program_name == "pt_shell"&&!$file_flag} {
          set ::timing_report_unconstrained_paths $orig_uncons
          set ::timing_report_union_tns $orig_union
  }
  echo "Written $csv_file"

  if {!$file_flag&&!$no_hist_flag} { 
    if {$pba_mode=="none"} {
      proc_histogram
    } else {
      proc_histogram -pba_mode $pba_mode
    }
  }
  rename proc_mysort_hash ""

} ;

define_proc_attributes proc_qor -info "USER PROC: reformats report_qor" \
          -define_args {
          {-tee     "Optional - displays the output of under-the-hood report_qor command" "" boolean optional}
          {-no_histogram "Optional - Skips printing text histogram for setup corner" "" boolean optional}
          {-existing_qor_file "Optional - Existing report_qor file to reformat" "<report_qor file>" string optional}
          {-scenarios "Optional - report qor on specified set of scenarios, skip on inactive scenarios" "{ scenario_name1 scenario_name2 ... }" string optional}
          {-no_pathgroup_info "Optional - to suppress individual pathgroup info" "" boolean optional}
          {-sort_by_tns "Optional - to sort by tns instead of wns" "" boolean optional}
          {-skew     "Optional - reports skew and avg skew on failing path groups" "" boolean optional}
          {-csv_file "Optional - Output csv file name, default is qor.csv" "<output csv file>" string optional}
          {-units    "Optional - override the automatic units calculation" "<ps or ns>" one_of_string {optional value_help {values {ps ns}}}}
          {-pba_mode "Optional - pba mode when in PrimeTime, ICC2, and FC" "<path, exhaustive, none>" one_of_string {optional value_help {values {path exhaustive none}}}}
          {-signoff_uncertainty_adjustment "Optional - adjusts ONLY the frequency column with signoff uncertainty, default 0." "" float optional}
          }

proc proc_histogram {args} {

set version 1.13
set ::timing_save_pin_arrival_and_slack true
echo "\nStarting  Histogram (proc_histogram) $version\n"

parse_proc_arguments -args $args results

set s_flag  [info exists results(-slack_lesser_than)]
set gs_flag [info exists results(-slack_greater_than)]
set path_flag [info exists results(-paths)]
set h_flag [info exists results(-hold)]
set pba_mode "none"

if {[info exists results(-number_of_bins)]} { set numbins $results(-number_of_bins) } else { set numbins 10 }
if {[info exists results(-slack_lesser_than)]} { set slack $results(-slack_lesser_than) } else { set slack 0.0 }
if {[info exists results(-slack_greater_than)]} { set gslack $results(-slack_greater_than) }
if {[info exists results(-hold)]} { set attr "min_slack" } else { set attr "max_slack" }
if {[info exists results(-number_of_critical_hierarchies)]} { set number $results(-number_of_critical_hierarchies) } else { set number 10 }

if {[info exists results(-pba_mode)]} {
  if { $::synopsys_program_name != "pt_shell" && $::synopsys_program_name != "icc2_shell" && $::synopsys_program_name != "fc_shell" } { echo "Error!! -pba_mode supported in pt_shell, icc2_shell, and fc_shell" ; return }
  set pba_mode $results(-pba_mode)
}

if {$gs_flag&&!$s_flag} { echo "Error!! -slack_greater_than can only be used with -slack_lesser_than ....Exiting\n" ; return }
if {$gs_flag&&$gslack>$slack} { echo "Error!! -slack_greater_than should be more than -slack_lesser_than ....Exiting\n" ; return }

if {[info exists results(-clock)]} {
  set clock [get_clocks -quiet $results(-clock)]
  if {[sizeof_collection $clock]!=1} { echo "Error!! provided -clock value did not results in 1 clock" ; return }
  set clock_arg "-clock [get_object_name $clock]"
  set clock_per [get_attribute $clock period]
} else {
  set clock_arg ""
}

foreach_in_collection clock [all_clocks] { if {[get_attribute -quiet $clock sources] != "" } { append_to_collection -unique real_clocks $clock } }
set min_period [lindex [lsort -real [get_attribute -quiet $real_clocks period]] 0]

catch {redirect -var y {report_units}}
if {[regexp {(\S+)\s+Second} $y match unit]} {
  if {[regexp {e-12} $unit]} { set unit 1000000 } else { set unit 1000 }
} elseif {[regexp {ns} $y]} { set unit 1000
} elseif {[regexp {ps} $y]} { set unit 1000000 }

if {![info exists unit]} { set unit 1000 }

if {[info exists clock_per]} { set min_period $clock_per }
if {$min_period<=0} { echo "Error!! Failed to calculate min_period of real clocks .... Exiting\n" ; return }

if {$path_flag} {

  set paths $results(-paths)
  if {[sizeof_collection $paths]<2} { echo "Error! Not enough -paths [sizeof_collection $paths] given for histogram" ; return }

  set paths [filter_collection $paths "slack!=INFINITY"]
  if {[sizeof_collection $paths]<2} { echo "Error! Not enough -paths [sizeof_collection $paths] with real slack given for histogram" ; return }

  set path_type [lsort -unique [get_attribute -quiet $paths path_type]]
  if {[llength $path_type]!=1} { echo "Error! please provide only max paths or min paths - not both" ; return }
  if {$path_type=="min"} { set attr "min_slack" ; set h_flag 1 } else { set attr "max_slack" ; set h_flag 0 }

  echo "Analyzing given [sizeof_collection $paths] path collection - ignores REGOUT\n"
  set coll $paths 
  set endpoint_coll [get_pins -quiet [get_attribute -quiet $paths endpoint]]
  if {[sizeof_collection $endpoint_coll]<2} { echo "\nNo Violations or Not enough Violations Found" ; return }
  set check_attr "slack"
}

if {!$path_flag} {

  if {$pba_mode =="none"} {
    set type "GBA"
  } elseif {$pba_mode =="path"} {
    set type "PBA Path"
  } elseif {$pba_mode =="exhaustive"} {
    set type "PBA Exhaustive"
  }

  if {$gs_flag} {
    echo -n "Acquiring $type Endpoints ($gslack > Slack < $slack) - ignores REGOUT ... "
  } else {
    echo -n "Acquiring $type Endpoints (Slack < $slack) - ignores REGOUT ... "
  }

  set coll   [sort_collection [filter_collection [eval all_registers -data_pins $clock_arg] "$attr<$slack"] $attr]
  if {$gs_flag} { set coll [sort_collection [filter_collection $coll "$attr>$gslack"] $attr] }

  if {[sizeof_collection $coll]<2} { echo "\nNo Violations or Not enough Violations Found" ; return }
  set endpoint_coll $coll

  if {$pba_mode!="none"} {
    set check_attr "slack"
    if {$gs_flag} {
      redirect /dev/null {set coll [get_timing_path -to $coll -pba_mode $pba_mode -max_paths [sizeof_collection $coll] -slack_lesser $slack -slack_greater $gslack] }
      set endpoint_coll [get_attribute -quiet $coll endpoint]
    } else {
      redirect /dev/null {set coll [get_timing_path -to $coll -pba_mode $pba_mode -max_paths [sizeof_collection $coll] -slack_lesser $slack] }
      set endpoint_coll [get_attribute -quiet $coll endpoint]
    }
  } else {
    set check_attr $attr
  }

  echo "Done\n"
}

if {[sizeof_collection $coll]<2} { echo "\nNo Violations or Not enough Violations Found" ; return }

echo -n "Initializing Histogram ... "
set values [lsort -real [get_attribute -quiet $coll $check_attr]]
set min    [lindex $values 0]
set max    [lindex $values [expr {[llength $values]-1}]]
set new_max    [expr $max+0.1] ;
set range  [expr {$max-$min}]
set width  [expr {$range/$numbins}]

for {set i 1} {$i<=$numbins} {incr i} { 
  set compare($i) [expr {$min+$i*$width}] 
  set histogram($i) 0
  set tns_histogram($i) 0
}
set compare($i) $new_max

echo -n "Populating Bins ... "
foreach v $values {
  for {set i 1} {$i<=$numbins} {incr i} {
    if {$v<=$compare($i)} {
      incr histogram($i)
      if {$v<0} { set tns_histogram($i) [expr {$tns_histogram($i)+$v}] }
      break
    }
  }
}
echo "Done - TNS can be slightly off\n"

set tot_tns 0
for {set i 1} {$i<=$numbins} {incr i} { set tot_tns [expr $tot_tns+$tns_histogram($i)] }

echo "========================================================================="
echo "          WNS RANGE        -          Endpoints                       TNS"
echo "========================================================================="
if {[llength $values]>1} {
  for {set i $numbins} {$i>=1} {incr i -1} {
    set low [expr {$min+$i*$width}]
    set high [expr {$min+($i-1)*$width}]
    set f_low [format %.3f $low]
    set f_high [format %.3f $high]
    set pct [expr {100.0*$histogram($i)/[llength $values]}]
    echo -n "[format "% 10s" $f_low] to [format "% 10s" $f_high]   -  [format %9i $histogram($i)] ([format %4.1f $pct]%)"
    if {$attr=="max_slack"} {
      if {[expr {($min_period-$high)*$unit}]>0} { set freq [expr {(1.0/($min_period-$high))*$unit}] } else { set freq 0.0 }
      echo -n " - [format %4.0f ${freq}]Mhz"
    }
    if {$h_flag} { echo " [format "% 25.1f" $tns_histogram($i)]" } else { echo " [format "% 15.1f" $tns_histogram($i)]" }
  }
}
echo "========================================================================="
echo "Total Endpoints            - [format %10i [llength $values]] [format "% 33.1f" $tot_tns]"
if {$attr=="max_slack"} { echo "Clock Frequency            - [format %10.0f [expr (1.0/$min_period)*$unit]]Mhz (estimated)" }
echo "========================================================================="
echo ""

if { $::synopsys_program_name == "icc2_shell" || $::synopsys_program_name == "pt_shell" || $::synopsys_program_name == "dcrt_shell" || $::synopsys_program_name == "fc_shell" } {
  set allicgs [get_cells -quiet -hierarchical -filter "is_hierarchical==false&&is_integrated_clock_gating_cell==true"]
} else {
  set allicgs [get_cells -quiet -hierarchical -filter "is_hierarchical==false&&clock_gating_integrated_cell=~*"]
}
set slkff [remove_from_coll [get_cells -quiet -of $endpoint_coll] $allicgs]

foreach c [get_attribute -quiet $slkff full_name] {
  set cell $c
  for {set i 1} {$i<20} {incr i} {
    set parent [file dir $cell]
    if {$parent=="."} { break }
    set parent_coll [get_cells -quiet $parent -filter "is_hierarchical==true"]
    if {[sizeof_collection $parent_coll]<1} { set cell $parent ; continue }
    if {[info exists hier_repeat($parent)]} { incr hier_repeat($parent) } else { set hier_repeat($parent) 1 }
    set cell $parent
  }
}

echo "========================================================================="
echo " Viol.   $number Critical"
echo " Count - Hierarchies - ignores ICGs"
echo "========================================================================="

if {![array exists hier_repeat]} { echo "No Critial Hierarchies found" ; return }

foreach {a b} [array get hier_repeat] { lappend repeat_list [list $a $b] }

set cnt 0
foreach i [lsort -real -decreasing -index 1 $repeat_list] { 
  echo "[format %6i [lindex $i 1]] - [lindex $i 0]" 
  incr cnt
  if {$cnt==$number} { break }
}
echo "========================================================================="
echo ""

} ;

define_proc_attributes proc_histogram -info "USER_PROC: Prints histogram of setup or hold slack endpoints" \
  -define_args { \
  {-number_of_bins      "Optional - number of bins for histgram, default 10"			"<int>"               int  optional}
  {-slack_lesser_than   "Optional - histogram for endpoints with slack less than, default 0" 	"<float>"               float  optional}
  {-slack_greater_than  "Optional - histogram for endpoints with slack greater than, can only be used with -slack_greater_than, default wns" 	"<float>"               float  optional}
  {-hold		"Optional - Generates histogram for hold slack, default is setup"	""                      boolean  optional}
  {-number_of_critical_hierarchies      "Optional - number of critical hierarchies to display viol. count, default 10" "<int>" int  optional}
  {-clock      		"Optional - Generates histogram only for the specified clock endpoints, default all clocks" "<clock>" string  optional}
  {-pba_mode 		"Optional - PBA mode supported in PrimeTime, ICC2 and FC" "<path or exhaustive>" one_of_string {optional value_help {values {path exhaustive none}}}}
  {-paths 		"Optional - Generates histogram for given user path collection" "<path coll>" string optional}
}
proc rm_generate_variables_for_eco { args } {

  parse_proc_arguments -args $args options

  if {[info exists options(-work_directory)]} { set work_directory $options(-work_directory) }
  if {[info exists options(-file_name)]} { set file_name $options(-file_name) }

  set the_file_for_eco_opt_var_definition	[file normalize "$work_directory/$file_name"]
  
  file delete -force $the_file_for_eco_opt_var_definition
  global REPORT_PREFIX REPORTS_DIR
  global PRIME_ECO_DRC_BUFFS PRIME_ECO_HOLD_BUFFS ECO_OPT_PHYSICAL_MODE ECO_OPT_WITH_PBA ECO_OPT_CUSTOM_OPTIONS eco_opt_type eco_count 

  set fid [ open ${the_file_for_eco_opt_var_definition} "w" ]

  puts $fid "## RM Tcl variables required by eco_opt"
    
  puts $fid "set PRIME_ECO_DRC_BUFFS \"$PRIME_ECO_DRC_BUFFS\""
  puts $fid "set PRIME_ECO_HOLD_BUFFS \"$PRIME_ECO_HOLD_BUFFS\""
  puts $fid "set eco_opt_types \"$eco_opt_type\""
  puts $fid "set eco_count $eco_count"
  puts $fid "set ECO_OPT_PHYSICAL_MODE \"$ECO_OPT_PHYSICAL_MODE\""
  puts $fid "set ECO_OPT_WITH_PBA \"$ECO_OPT_WITH_PBA\""
  puts $fid "set ECO_OPT_CUSTOM_OPTIONS \"$ECO_OPT_CUSTOM_OPTIONS\""

  close $fid

} ;

define_proc_attributes rm_generate_variables_for_eco \
 -info "Generate and pass necessary RM variables for the PrimeECO based eco_optcommand" \
 -define_args {
 {-work_directory "Destination directory for the output file." AString string optional}
 {-file_name "Name of the output file." AString string required}
}

proc rm_open_design { args } {

  set options(-from_lib) ""
  set options(-block_name) ""
  set options(-from_label) ""
  set options(-to_label) ""
  set options(-view) "design"
  set options(-dp_block_refs) ""
  set options(-backup_snapshot) "1"
  set options(-verbose) "0"
  
  if {$options(-verbose)} {
    unsuppress_message FILE-007 ;
    unsuppress_message UNDO-016 ;
    unsuppress_message NDMUI-064 ;
  }

  parse_proc_arguments -args $args options

  global LIBRARY_SUFFIX

  set from_block_name ${options(-block_name)}/${options(-from_label)}.$options(-view) ;
  set to_block_name   ${options(-block_name)}/${options(-to_label)}.$options(-view) ;
  if {$options(-dp_block_refs) != ""} {
    set ref_libs_for_edit "-ref_libs_for_edit"
  } else {
    set ref_libs_for_edit ""
  }

if { 0 } {

  open_lib $options(-from_lib) $ref_libs_for_edit ;

  set from_block [get_blocks -all -quiet ${from_block_name}] ;
  set to_block [get_blocks -all -quiet ${to_block_name}] ;

  if { [sizeof_collection $from_block] == 0} {
    set rc 0 ;
    print_err "From block; $from_block_name is not found, please check block name." ;
    return 1 ;
  }
  set backup_suffix "backup_[exec date +%y%m%d_%H%M]" ;
  if { [sizeof_collection $to_block] } {
    set backup_block_name ${options(-block_name)}/${options(-to_label)}_${backup_suffix}.$options(-view) ;
    if { $options(-backup_snapshot) } {
      puts "RM-info : Top block: $to_block_name found as specified distination, creating back up as $backup_block_name ..." ;
      rename_block -hierarchical -force -from_block ${to_block_name} -to_block ${backup_block_name} ;
      remove_block -force ${options(-block_name)}/${options(-to_label)} ;
      set block_libs [get_libs -quiet -filter "use_hier_ref_libs==true"] ;
      if { [sizeof_collection $block_libs] } {
        set blocks_remove [get_blocks -quiet -all -of_objects $block_libs -filter "label_name==$options(-to_label)"] ;
        if { [sizeof_collection $blocks_remove] } {
   
          remove_block -force $blocks_remove ; 
        }
      }
    } else {
      puts "RM-warning : Top block: $to_block_name found as specified distination, $backup_block_name will be overwritten." ;
    }
  }
  puts "RM-info : Copying design from $from_block_name to $to_block_name ..."
  rename_block -hierarchical -force -from_block ${from_block_name} -to_block ${to_block_name} ;
  save_lib -all ;

  puts "RM-info : Opening block: $to_block_name" ;
  open_block $to_block_name $ref_libs_for_edit ;
} else {
  puts "RM-info : Opening library $options(-from_lib)"
  open_lib $options(-from_lib) $ref_libs_for_edit

  foreach block $options(-dp_block_refs) {
    if {[sizeof_collection [get_blocks -quiet -all ${block}${LIBRARY_SUFFIX}:${block}/${options(-to_label)}.abstract]] > 0} {
      remove_block -force ${block}${LIBRARY_SUFFIX}:${block}/${options(-to_label)}.abstract
    }
  }

  open_block ${from_block_name}
  save_block -hier -force -label ${options(-to_label)}

  if {$options(-view) != "outline"} {
    foreach block "$options(-dp_block_refs) $options(-block_name)" {
      if {[sizeof_collection [get_blocks -quiet -all ${block}${LIBRARY_SUFFIX}:${block}/${options(-to_label)}.outline]] > 0} {
        remove_block -force ${block}${LIBRARY_SUFFIX}:${block}/${options(-to_label)}.outline
      }
    }
  }

  close_lib -purge -force -all
  puts "RM-info : Opening block $options(-from_lib):$to_block_name"
  open_block $options(-from_lib):$to_block_name $ref_libs_for_edit

  if {$options(-view) == "outline"} {
    link -force
  }
}

  if { $options(-verbose) } { list_blocks }

  return 1 ;
}

define_proc_attributes rm_open_design \
  -info "Opens design lib and block as new name from specified design_lib and block." \
  -define_args {
    {-from_lib           "Design library name to open"     "design lib name"          string required}
    {-block_name         "Block name to open"              "block name"               string required}
    {-from_label         "Block label name to copy from"   "previous step label name" string required}
    {-to_label           "Block label name to open"        "current step label name"  string required}
    {-dp_block_refs      "Child block reference names"     ""                         string optional}
    {-no_backup_snapshot "No NDM back-up snapshot created" ""                         boolean optional}
    {-view               "View name to open"               "view" one_of_string {optional value_help {values {information design outline}}}}
    {-verbose            "" "" boolean optional}
  }

proc report_dp_flow_strategy_settings {app_option_cmd_list run_cmd_list dp_stage dp_flow hier_fp_style stage} {

  set option_sets_list [list] ;
  set max_str_length 1 ;

  foreach app_option_cmd $app_option_cmd_list {
    set app_option_cmd [split $app_option_cmd] ; 
    if {[lindex $app_option_cmd 0] == "set_app_options"} {
      set option_name [lindex $app_option_cmd 2] ;
      set target_valu [lindex $app_option_cmd 4] ;
    } elseif {[lindex $app_option_cmd 0] == "reset_app_option"} {
      set option_name [lindex $app_option_cmd 1] ;
      set target_valu DP_RESET ;
    } 
    set option_name [string trimleft  $option_name "\{"] ;
    set option_name [string trimright $option_name "\}"] ;
    set target_valu [string trimleft  $target_valu "\{"] ;
    set target_valu [string trimright $target_valu "\}"] ;

    set default_value [get_app_option_value -name "$option_name" -system_default] ;
    set current_value [get_app_option_value -name "$option_name"] ;
    if { $target_valu == "DP_RESET" } {
      set target_valu [get_app_option_value -name "$option_name" -user_default] ; 
      if { $target_valu == "" } {
        set target_valu [get_app_option_value -name "$option_name" -system_default] ;
      }
    }
    set option_sets [list $option_name $current_value $default_value $target_valu] ;
    lappend option_sets_list $option_sets ;

    set option_name_len [string length $option_name] ;
    if { $option_name_len > $max_str_length } {
      set max_str_length $option_name_len ;
    } 
  }

  set separater_0 "---------------" ;
  set separater_1 "" ;
  for {set idx 0} {$idx < [expr $max_str_length + 2]} {incr idx} { 
    append separater_1 "-" ;
  }
  puts "dp_stage      : $dp_stage" ;
  puts "dp_flow       : $dp_flow" ;
  puts "hier_fp_style : $hier_fp_style" ;
  puts "stage         : $stage" ;
  puts [format "+%${max_str_length}s+%13s+%13s+%13s+" $separater_1 $separater_0 $separater_0 $separater_0] ;
  puts [format "| %-${max_str_length}s | %-13s | %-13s | %-13s |" "App-Option Name" "Current Value" "Default Value" "Target Value"] ;
  puts [format "+%${max_str_length}s+%13s+%13s+%13s+" $separater_1 $separater_0 $separater_0 $separater_0] ;
  foreach option_sets $option_sets_list {
    puts [format "| %-${max_str_length}s | %-13s | %-13s | %-13s |" [lindex $option_sets 0] [lindex $option_sets 1] [lindex $option_sets 2] [lindex $option_sets 3]] ;
  }
  puts [format "+%${max_str_length}s+%13s+%13s+%13s+" $separater_1 $separater_0 $separater_0 $separater_0] ;

  puts [format "+%${max_str_length}s-%13s-%13s-%13s+" $separater_1 $separater_0 $separater_0 $separater_0] ;
  puts [format "| %-[expr ${max_str_length} + 48]s |" "Executed Commands"] ;
  puts [format "+%${max_str_length}s-%13s-%13s-%13s+" $separater_1 $separater_0 $separater_0 $separater_0] ;
  foreach cmd $run_cmd_list {
    puts [format "| %s" $cmd]  ;
  }
  puts [format "+%${max_str_length}s-%13s-%13s-%13s+" $separater_1 $separater_0 $separater_0 $separater_0] ;
}

proc report_dp_flow_strategy_diff {dp_stage dp_flow hier_fp_style stage} {
  puts "RM-warning: -diff_only option is not implemented yet" ;
}

proc set_dp_flow_run_cmd_settings {dp_cmd_list dp_stage dp_flow hier_fp_style stage} {

  set cmd [list] ;

  foreach cur_cmd_set $dp_cmd_list {
    if {$dp_stage == "early"} {
      if {$stage == "synthesis"} {
        if {[lindex $cur_cmd_set 8] == "set"} {
          if {[lindex $cur_cmd_set 1] != {}} {
            lappend cmd "[lindex $cur_cmd_set 0] [lindex $cur_cmd_set 1]" ;
          }
        }
      } elseif {$stage=="pnr"} {
        if {[lindex $cur_cmd_set 7] == "set"} {
          if {[lindex $cur_cmd_set 1] != {}} {
            lappend cmd "[lindex $cur_cmd_set 0] [lindex $cur_cmd_set 1]" ;
          }
        }
      }
    } elseif {$dp_stage == "final"} {
      if {$stage == "synthesis"} {
        if {[lindex $cur_cmd_set 8] == "set"} {
          if {[lindex $cur_cmd_set 2] != {}} {
            lappend cmd "[lindex $cur_cmd_set 0] [lindex $cur_cmd_set 2]" ;
          }
        }
      } elseif {$stage=="pnr"} {
        if {[lindex $cur_cmd_set 7] == "set"} {
          if {[lindex $cur_cmd_set 2] != {}} {
            lappend cmd "[lindex $cur_cmd_set 0] [lindex $cur_cmd_set 2]" ;
          }
        }
      }
    }
    if {$dp_flow == "flat"} {
      if {$stage == "synthesis"} {
        if {[lindex $cur_cmd_set 8] == "set"} {
          if {[lindex $cur_cmd_set 3] != {}} {
            lappend cmd "[lindex $cur_cmd_set 0] [lindex $cur_cmd_set 3]" ;
          }
        }
      } elseif {$stage=="pnr"} {
        if {[lindex $cur_cmd_set 7] == "set"} {
          if {[lindex $cur_cmd_set 3] != {}} {
            lappend cmd "[lindex $cur_cmd_set 0] [lindex $cur_cmd_set 3]" ;
          }
        }
      }
    } elseif {$dp_flow == "hierarchical" } {
      if {$stage == "synthesis"} {
        if {[lindex $cur_cmd_set 8] == "set"} {
          if {[lindex $cur_cmd_set 4] != {}} {
            lappend cmd "[lindex $cur_cmd_set 0] [lindex $cur_cmd_set 4]" ;
          }
        }
      } elseif {$stage=="pnr"} {
        if {[lindex $cur_cmd_set 7] == "set"} {
          if {[lindex $cur_cmd_set 4] != {}} {
            lappend cmd "[lindex $cur_cmd_set 0] [lindex $cur_cmd_set 4]" ;
          }
        }
      }
    }
    if {$dp_flow == "hierarchical" } { 
      if {$hier_fp_style == "channel"} {
        if {$stage == "synthesis"} {
          if {[lindex $cur_cmd_set 8] == "set"} {
            if {[lindex $cur_cmd_set 5] != {}} {
              lappend cmd "[lindex $cur_cmd_set 0] [lindex $cur_cmd_set 5]" ;
            }
          }
        } elseif {$stage=="pnr"} {
          if {[lindex $cur_cmd_set 7] == "set"} {
            if {[lindex $cur_cmd_set 5] != {}} {
              lappend cmd "[lindex $cur_cmd_set 0] [lindex $cur_cmd_set 5]" ;
            }
          }
        }
      } elseif {$hier_fp_style == "abutted"} {
        if {$stage == "synthesis"} {
          if {[lindex $cur_cmd_set 8] == "set"} {
            if {[lindex $cur_cmd_set 6] != {}} {
              lappend cmd "[lindex $cur_cmd_set 0] [lindex $cur_cmd_set 6]" ;
            }
          }
        } elseif {$stage=="pnr"} {
          if {[lindex $cur_cmd_set 7] == "set"} {
            if {[lindex $cur_cmd_set 6] != {}} {
              lappend cmd "[lindex $cur_cmd_set 0] [lindex $cur_cmd_set 6]" ;
            }
          }
        }
      }
    }
  }
  return $cmd ;
}

proc set_dp_flow_app_option_settings {dp_app_option_list dp_stage dp_flow hier_fp_style stage} {

  set cmd [list] ; ;

  foreach cur_app_option_set $dp_app_option_list {
    if {$dp_stage == "early"} {
      if {$stage == "synthesis"} {
        if {[lindex $cur_app_option_set 8] == "set"} {
          if {[lindex $cur_app_option_set 1] != {}} {
            lappend cmd "set_app_options -name \{[lindex $cur_app_option_set 0]\} -value \{[lindex $cur_app_option_set 1]\}" ;
          }
        }
      } elseif {$stage=="pnr"} {
        if {[lindex $cur_app_option_set 7] == "set"} { 
          if {[lindex $cur_app_option_set 1] != {}} {
            lappend cmd "set_app_options -name \{[lindex $cur_app_option_set 0]\} -value \{[lindex $cur_app_option_set 1]\}" ;
          }
        }
      }
    } elseif {$dp_stage == "final"} {
      if {$stage == "synthesis"} {
        if {[lindex $cur_app_option_set 8] == "set"} {
          if {[lindex $cur_app_option_set 2] != {}} {
            if {[lindex $cur_app_option_set 2] == "reset"} {
              lappend cmd "reset_app_option \{[lindex $cur_app_option_set 0]\}" ;
            } else {
              lappend cmd "set_app_options -name \{[lindex $cur_app_option_set 0]\} -value \{[lindex $cur_app_option_set 2]\}" ;
            }
          }
        }
      } elseif {$stage=="pnr"} {
        if {[lindex $cur_app_option_set 7] == "set"} {
          if {[lindex $cur_app_option_set 2] != {}} {
            if {[lindex $cur_app_option_set 2] == "reset"} {
              lappend cmd "reset_app_option \{[lindex $cur_app_option_set 0]\}" ;
            } else {
              lappend cmd "set_app_options -name \{[lindex $cur_app_option_set 0]\} -value \{[lindex $cur_app_option_set 2]\}" ;
            }
          }
        }
      }
    }
    if {$dp_flow == "flat"} {
      if {$stage == "synthesis"} {
        if {[lindex $cur_app_option_set 8] == "set"} {
          if {[lindex $cur_app_option_set 3] != {}} {
            lappend cmd "set_app_options -name \{[lindex $cur_app_option_set 0]\} -value \{[lindex $cur_app_option_set 3]\}" ; 
          }
        }
      } elseif {$stage=="pnr"} {
        if {[lindex $cur_app_option_set 7] == "set"} {
          if {[lindex $cur_app_option_set 3] != {}} {
            lappend cmd "set_app_options -name \{[lindex $cur_app_option_set 0]\} -value \{[lindex $cur_app_option_set 3]\}" ;
          }
        }
      }
    } elseif {$dp_flow == "hierarchical" } {
      if {$stage == "synthesis"} {
        if {[lindex $cur_app_option_set 8] == "set"} {
          if {[lindex $cur_app_option_set 4] != {}} {
            lappend cmd "set_app_options -name \{[lindex $cur_app_option_set 0]\} -value \{[lindex $cur_app_option_set 4]\}" ;
          }
        }
      } elseif {$stage=="pnr"} {
        if {[lindex $cur_app_option_set 7] == "set"} {
          if {[lindex $cur_app_option_set 4] != {}} {
            lappend cmd "set_app_options -name \{[lindex $cur_app_option_set 0]\} -value \{[lindex $cur_app_option_set 4]\}" ;
          }
        }
      }
    }
    if {$dp_flow == "hierarchical" } {
      if {$hier_fp_style == "channel"} {
        if {$stage == "synthesis"} {
          if {[lindex $cur_app_option_set 8] == "set"} {
            if {[lindex $cur_app_option_set 5] != {}} {
              lappend cmd "set_app_options -name \{[lindex $cur_app_option_set 0]\} -value \{[lindex $cur_app_option_set 5]\}" ;
            } 
          }
        } elseif {$stage=="pnr"} {
          if {[lindex $cur_app_option_set 7] == "set"} {
            if {[lindex $cur_app_option_set 5] != {}} {
              lappend cmd "set_app_options -name \{[lindex $cur_app_option_set 0]\} -value \{[lindex $cur_app_option_set 5]\}" ;
            }
          }
        }
      } elseif {$hier_fp_style == "abutted"} {
        if {$stage == "synthesis"} {
          if {[lindex $cur_app_option_set 8] == "set"} {
            if {[lindex $cur_app_option_set 6] != {}} {
              lappend cmd "set_app_options -name \{[lindex $cur_app_option_set 0]\} -value \{[lindex $cur_app_option_set 6]\}" ;
            }
          }
        } elseif {$stage=="pnr"} {
          if {[lindex $cur_app_option_set 7] == "set"} {
            if {[lindex $cur_app_option_set 6] != {}} {
              lappend cmd "set_app_options -name \{[lindex $cur_app_option_set 0]\} -value \{[lindex $cur_app_option_set 6]\}" ;
            }
          }
        }
      }
    }
  }
  return $cmd ;
}

proc rm_set_dp_flow_strategy { args } {

  set options(-dp_stage) "early"
  set options(-dp_flow) "hier"
  set options(-hier_fp_style) "channel"
  set options(-report_only) "0"
  set options(-diff_only) "0"
  parse_proc_arguments -args $args options

  set shell_name [get_app_option_value -name {shell.common.shell_name}] ;
  if {$shell_name == "fc_shell"} {
    set stage synthesis ; 
  } elseif {$shell_name == "icc2_shell"} {
    set stage pnr ;
  }

  set dp_app_option_list [list] ;
  set dp_cmd_list        [list] ;
  lappend dp_app_option_list [list {plan.pins.exclude_clocks_from_feedthroughs}        {}      {}        {}    {}     {true}  {false}  {set} {set}  ] ;
  lappend dp_app_option_list [list {plan.pins.synthesize_abutted_pins}                 {}      {}        {}    {}     {false} {true}   {set} {set}  ] ;
  lappend dp_app_option_list [list {place.coarse.continue_on_missing_scandef}          {true}  {true}    {}    {}      {}      {}      {set} {set}  ] ;
  lappend dp_cmd_list        [list set_block_pin_constraints                           {}      {}        {}    {}      {-allow_feedthroughs false} {-allow_feedthroughs true} {set} {set}] ;
 
  set app_options_list [set_dp_flow_app_option_settings $dp_app_option_list $options(-dp_stage) $options(-dp_flow) $options(-hier_fp_style) $stage] ;
  set run_cmds_list    [set_dp_flow_run_cmd_settings   $dp_cmd_list        $options(-dp_stage) $options(-dp_flow) $options(-hier_fp_style) $stage] ;

  if { $options(-report_only) } {
    report_dp_flow_strategy_settings $app_options_list $dp_app_option_list $options(-dp_stage) $options(-dp_flow) $options(-hier_fp_style) $stage ;
  } elseif { $options(-diff_only) } {
    report_dp_flow_strategy_diff $options(-dp_stage) $options(-dp_flow) $options(-hier_fp_style) $stage ;
  } else {
    foreach cmd1 $app_options_list {
      eval $cmd1 ;
    }
    puts "RM-info: All app options set completed." ;
    foreach cmd2 $run_cmds_list {
      eval $cmd2 ;
    }
    if { [llength $run_cmds_list] } {
      puts "RM-info: All commands set completed." ;
    }
    report_dp_flow_strategy_settings $app_options_list $run_cmds_list $options(-dp_stage) $options(-dp_flow) $options(-hier_fp_style) $stage ;
  }

  return 1 ;
}

define_proc_attributes rm_set_dp_flow_strategy \
  -info "Apply design planning flow, stage and type specific application settings to current design." \
  -define_args {
    {-dp_stage      "Specify design planning stage"            "" one_of_string {required value_help {values {information early final}}}}
    {-dp_flow       "Specify design planning flow type"        "" one_of_string {required value_help {values {information flat hierarchical}}}}
    {-hier_fp_style "Specify hierarchical floor planning type" "" one_of_string {required value_help {values {information channel abutted}}}}
    {-report_only   "Report flow specific app option settings in a table without applying them to the design" "" boolean optional}
    {-diff_only     "Report the difference between current and proposed setting in tabular format"           "" boolean optional} 
  }

proc auto_multi_vth_constraint {args} {
    
    parse_proc_arguments -args $args results
    
    if {[info exists results(-cost)]} {
        set COST $results(-cost)
    } else {
        set COST "cell_count"
    }

    suppress_message ATTR-11 
    if {[info exists results(-percentage)]} {
        set PERCENT $results(-percentage)
    } else {
        set vt_groups [lsort -unique [get_attribute -quiet [get_lib_cells -quiet */* ] threshold_voltage_group]] ;
        if {[llength $vt_groups ] == 0} {
            puts "RM-warning: There are no threshold voltage groups defined for the reference libraries"
        }
        set open_vt_groups ""
        foreach grp $vt_groups {
            set tmp_vt_groups [sizeof_collection [get_lib_cells -quiet */* -filter threshold_voltage_group==$grp&&dont_use==false&&valid_purposes=~*optimization*&&design_type==lib_cell]] ;
            if {$tmp_vt_groups > 0} {
                lappend open_vt_groups vt_groups
            }
        }
        if {[llength $open_vt_groups] == 2} {
            set PERCENT 90 ;
        } elseif {[llength $open_vt_groups] > 2} {
            set PERCENT 30 ;
        } elseif {[llength $open_vt_groups] == 1} {
            puts "RM-warning: Only one threshold voltage group $open_vt_groups is open for optimization. set_multi_vth_constraint will not be applied"
            set PERCENT ""
        } elseif {[llength $open_vt_groups] == 0} {
            puts "RM-warning: No threshold voltage groups are open for optimization. set_multi_vth_constraint will not be applied"
            set PERCENT ""
        }
        if {$PERCENT ne "" } {
            puts "RM-info: Using ${PERCENT}% low_vt percentage"
        } 
    }
    
    redirect -var mvt_rpt {report_multi_vth_constraint}
    regexp -indices "Percentage vth flow  :" $mvt_rpt loc1
    regexp -indices -start [lindex $loc1 1] "\n" $mvt_rpt loc1End
    regexp -indices "Percentage vth limit :" $mvt_rpt loc2
    regexp -indices -start [lindex $loc2 1] {\(} $mvt_rpt loc2End
    regexp -indices "Percentage vth cost  :" $mvt_rpt loc3
    regexp -indices -start [lindex $loc3 1] "\n" $mvt_rpt loc3End
    
    set vt_flow [string trim [string range $mvt_rpt [expr [lindex $loc1 1]+1] [lindex $loc1End 0]]]
    set vt_limit [string trim [string range $mvt_rpt [expr [lindex $loc2 1]+1] [expr [lindex $loc2End 0]-1]]]
    set vt_cost [string trim [string range $mvt_rpt [expr [lindex $loc3 1]+1] [lindex $loc3End 0]]]
    
    set low_vt [get_lib_cells -quiet -filter threshold_voltage_group_type==low_vt]
    if {[sizeof_collection $low_vt] != 0} {
        puts "RM-info: There are [sizeof_collection $low_vt] low_vt cells for the multi vth constraint defined"
    } else {
        if {[info exists results(-apply)]} {
            puts "RM-warning: There are no low_vt cells defined by set_threshold_voltage_group_type. set_multi_vth_constraint will not be applied"
            return 0
        } else {
            puts "RM-info: There are no low_vt cells defined by set_threshold_voltage_group_type"
        }
    }
    
    if {$vt_flow eq "disabled"} {
        puts "RM-info: No prior VTH constraint applied."
    } elseif {$vt_flow eq "enabled"} {
        if {$vt_limit == $PERCENT} {
            puts "RM-info: Existing VTH constraints already set at $vt_limit"
        } elseif {$vt_limit != $PERCENT} {
            puts "RM-warning: Existing VTH constraint of $vt_limit is different than proposed $PERCENT."
            if {![info exists results(-force)]} {
                puts "RM-warning: New constraint of $PERCENT by $COST is not applied. Use -force to enforce the new limit" 
                return 
            } 
        }
    }
    
    if {[info exists results(-apply)] && $PERCENT ne ""} {
        puts "RM-info: Executing set_multi_vth_constraint -cost $COST -low_vt_percentage $PERCENT"
        set_multi_vth_constraint -cost $COST -low_vt_percentage $PERCENT
    }
    unsuppress_message ATTR-11     
    return 
}

define_proc_attributes auto_multi_vth_constraint -info "Automatically assigns a multi vth constraint on the design" \
  -define_args {
    {-force "Apply constraint even if previously set" "" boolean optional}
    {-apply "Add the constraint to the design" "" boolean optional}
    {-cost "Which cost to use for auto applying multi vth constraint. (Default: cell_count)" cost one_of_string {optional value_help {values {"cell_count" "area"}}}}
    {-percentage "Percentage of LVT to constraint (Default 30 if >=3 VT groups. 90 if only 2 VT groups)" "percentage" int optional}

}

proc rm_check_floorplan_initialzation {} {
  set rc "" ;

  set carea   [get_attribute [current_block] core_area_area -quiet] ;
  set tracks  [get_tracks -quiet] ;
  set sarrays [get_site_arrays -quiet] ;
  set srows   [get_site_rows -quiet] ;

  if { $carea == "" } {
    set rc "initialize_floorplan" ;
  }
  if { ![sizeof_collection $tracks] } { 
    set rc "initialize_floorplan" ;
  }
  if { ![sizeof_collection $sarrays] && ![sizeof_collection $srows] } {
    set rc "initialize_floorplan" ;
  }

  return $rc ;
}

proc rm_check_io_placement {} {
  set rc "" ;

  set unplaced_io_cells [get_cells -quiet -physical_context -filter "design_type==pad&&physical_status==unplaced"] ;
  if { [sizeof_collection $unplaced_io_cells] } {
    set rc "io_placement" ;
  }

  return $rc ;
}

proc rm_check_macro_placement {} {
  set rc "" ;

  set unplaced_macro_cells [get_cells -quiet -hierarchical -physical_context -filter "is_hard_macro==true&&physical_status==unplaced"] ;
  if { [sizeof_collection $unplaced_macro_cells] } {
    set rc "macro_placement" ;
  }
  return $rc ;
}

proc rm_check_bump_placement {} {
  set rc "" ;

  set unplaced_bump_cells [get_cells -quiet -physical_context -filter "design_type==flip_chip_pad&&physical_status==unplaced"] ;
  if { [sizeof_collection $unplaced_bump_cells] } {
    set rc "bump_placement" ;
  }

  return $rc ;
}

proc rm_check_block_shaping {} {
  set rc "" ;

  set unshaped_blocks [get_cells -quiet -hierarchical -filter "is_soft_macro==true&&physical_status==unplaced"] ;
  if { [sizeof_collection $unshaped_blocks] } {
    set rc "block_shaping" ;
  }

  return $rc ;
}

proc rm_check_va_shaping {} {
  set rc "" ;

  set power_dains [get_power_domains -quiet -hierarchical] ;
  set voltage_areas [get_voltage_areas -quiet -hierarchical] ;

  if { [sizeof_collection $power_dains] > [sizeof_collection $voltage_areas] } {
    set rc "va_shaping" ;
  }
  return $rc ;
}

proc rm_check_top_pin_placement {} {
  set rc "" ;

  set io_cells [get_cells -quiet -physical_context -filter "design_type==pad"] ;
  if { ![sizeof_collection $io_cells] } {
    set top_ports [get_ports -quiet -filter "physical_status==unplaced"] ;
    if { [sizeof_collection $top_ports] } {
      set rc "top_pin_placement" ;
    }
  }
  return $rc ;
}

proc rm_check_block_pin_pllcement {} {
  set rc "" ;

  set blocks [get_cells -quiet -hierarchical -filter "is_soft_macro==true"] ;
  if { [sizeof_collection $blocks] } {
    set block_pins [get_ports -quiet -of_objects $blocks -filter "physical_status==unplaced"] ;
    if { [sizeof_collection $block_pins] } {
      set rc "block_pin_placement" ;
    }
  }

  return $rc ;
}

proc rm_check_tap_cell_insertion { tap_lib_cell_name } {
  set rc "" ;

  if { $tap_lib_cell_name == "" } {
    set tap_cells [get_cells -quiet -physical_context -filter "design_type==well_tap"] ;
  } else {
    set tap_cells [get_cells -quiet -physical_context -filter "ref_name=~$tap_lib_cell_name"] ;
  }
  if { ![sizeof_collection $tap_cells] } {
    set rc "tap_cell_insertion" ;
  }
  return $rc ;
}

proc rm_check_boundary_cell_insertion {boundary_lib_cell_name } {
  set rc "" ;

  if { $boundary_lib_cell_name == "" } {
    set boundary_cells [get_cells -quiet -physical_context -filter "design_type==end_cap"] ;
  } else {
    set boundary_cells [get_cells -quiet -physical_context -filter "ref_name=~$boundary_lib_cell_name"] ;
  }
  if { ![sizeof_collection $boundary_cells] } {
    set rc "boundary_cell_insertion" ;
  }

  return $rc ;
}

proc rm_detect_fp_valid_operations { args } {

  set rc [list] ;

  set valid_operations [list] ;
  lappend valid_operations initialize_floorplan ;
  lappend valid_operations io_placement ;
  lappend valid_operations bump_placement ;
  lappend valid_operations macro_placement ;
  lappend valid_operations block_shaping ;
  lappend valid_operations va_shaping ;
  lappend valid_operations top_pin_placement ;
  lappend valid_operations block_pin_placement ;
  lappend valid_operations tap_cell_insertion ;
  lappend valid_operations boundary_cell_insertion ;
  set tap_lib_cell_name "" ;
  set boundary_lib_cell_name "" ;

  parse_proc_arguments -args $args results ;
  if { [info exists results(-operations)] } {
    set valid_operations $results(-operations) ;
  }
  if { [info exists results(-well_tap_lib_cell_name)] } {
    set tap_lib_cell_name $results(-well_tap_lib_cell_name) ;
  }
  if { [info exists results(-boundary_lib_cell_name)] } {
    set boundary_lib_cell_name $results(-boundary_lib_cell_name) ;
  }

  set init_fp      [rm_check_floorplan_initialzation] ;
  set io_place     [rm_check_io_placement] ;
  set bump_place   [rm_check_bump_placement] ;
  set macro_place  [rm_check_macro_placement] ;
  set block_shape  [rm_check_block_shaping] ;
  set va_shape     [rm_check_va_shaping] ;
  set tpin_place   [rm_check_top_pin_placement] ;
  set bpin_place   [rm_check_block_pin_pllcement] ;
  set tap_ins      [rm_check_tap_cell_insertion $tap_lib_cell_name] ;
  set boundary_ins [rm_check_boundary_cell_insertion $boundary_lib_cell_name] ;

  if { [lsearch $valid_operations "initialize_floorplan"] > -1 && $init_fp != "" } {
    lappend rc $init_fp ;
  }
  if { [lsearch $valid_operations "io_placement"] > -1 && $io_place != "" } {
    lappend rc $io_place ;
  }
  if { [lsearch $valid_operations "bump_placement"] > -1 && $bump_place != "" } {
    lappend rc $bump_place :
  }
  if { [lsearch $valid_operations "macro_placement"] > -1 && $macro_place != "" } {
    lappend rc $macro_place ;
  }
  if { [lsearch $valid_operations "block_shaping"] > -1 && $block_shape != "" } {
    lappend rc $block_shape ;
  }
  if { [lsearch $valid_operations "va_shaping"] > -1 && $va_shape != "" } {
    lappend rc $va_shape ;
  }
  if { [lsearch $valid_operations "top_pin_placement"] > -1 && $tpin_place != "" } { 
    lappend rc $tpin_place ;
  }
  if { [lsearch $valid_operations "block_pin_placement"] > -1 && $bpin_place != "" } {
    lappend rc $bpin_place ;
  }
  if { [lsearch $valid_operations "tap_cell_insertion"] > -1 && $tap_ins != "" } {
    lappend rc $tap_ins ;
  }
  if { [lsearch $valid_operations "boundary_cell_insertion"] > -1 && $boundary_ins != ""  } {
    lappend rc $boundary_ins ;
  }

  return $rc ;
}

define_proc_attributes rm_detect_fp_valid_operations \
  -info "detects floorplaning valid operartions" \
  -define_args {
    {-operations "Specifies vaild operation list" "valid operation list are \'initialize_floorplan io_placement bump_placement macro_placement block_shaping va_shaping top_pin_placement block_pin_placement tap_cell_insertion boundary_cell_insertion" list optional}
    {-well_tap_lib_cell_name "Specifies lib_cell name" "<name>" string optional}
    {-boundary_lib_cell_name "Specifies lib_cell name" "<name>" string optional}
  }

proc rm_create_power_domain_map { args } {

  parse_proc_arguments -args $args results ;

  set VAs [get_voltage_areas] ;
  if { [sizeof_collection $VAs] == 0 } { 
    puts "RM-warning: No votage area found, No VA map created."
    return 0 
  }

  set map_file [open $results(-map_file) w] ;
  foreach_in_collection VA $VAs {
    set VA_name [get_attribute $VA full_name] ;
    set PD_name [get_attribute $VA power_domains.full_name] ;
    puts $map_file "$VA_name	$PD_name" ;
  }
  close $map_file ;
  puts "RM-info: power_domin mapfile: $results(-map_file) written succesfully." ;

  return 1 ;
}

define_proc_attributes rm_create_power_domain_map\
  -info "Create map for power_domain and voltage_area" \
  -define_args {
    {-map_file "Specifies map_file name" "<file name>" string required}
  }

proc rm_push_down_voltage_areas { args } {

  set write_script_only 0 ;
  set keep_top_va 0 ;

  parse_proc_arguments -args $args results ;
  set target_cell $results(-cell) ;
  set mapfile      $results(-map_file) ;
  if {[info exists results(-write_script_only)]} {
    set write_script_only 1 
  }
  if {[info exists results(-keep_top_voltage_area)]} {
    set keep_top_va 1 ;
  }
 
  puts "RM-info: Prameter Sets" ;
  puts "         target cell           : [get_attribute $target_cell full_name]" ;
  puts "         map-file              : $mapfile" ; ;
  puts "         write_script_only     : $write_script_only" ;
  puts "         keep_top_voltage_area : $keep_top_va" ;

  if { ![file exists $mapfile] } { 
    puts "RM-error: No map_file: $mapfile found." ;
    return 0 ;
  } else {
    set map [open $mapfile r] ;
    set va_names [list] ;
    set pd_names [list] ;
    while { [gets $map line ] >= 0 } {
      lappend va_names [lindex $line 0] ;
      lappend pd_names [lindex $line 1] ;
    }
  }

  set block_boundary [get_attribute $target_cell boundary] ;
  set target_cell_name [get_attribute $target_cell full_name] ;
  set VAs [get_voltage_areas -quiet -within $block_boundary] ; 

  if { ![sizeof_collection $VAs] } {
     puts "RM-warning: No voltage area found in $target_cell_name boundary." 
     return 0 ;
  }

  set write_script_name "push_down_va_${target_cell_name}.tcl" ;
  
  set cmd_cre_va "" ;
  append cmd_cre_va "########################################################################\n" ;
  append cmd_cre_va "# Remove existing top level voltage area(s). \n" ;
  append cmd_cre_va "########################################################################\n" ;
  append cmd_cre_va "set del_vas \[get_voltage_areas -within \{$block_boundary\}\] ;\n" ;
  append cmd_cre_va "set del_va_shapes \[get_voltage_area_shapes -of_objects \$del_vas\] ;\n"
  append cmd_cre_va "remove_voltage_areas \$del_vas ;\n" ; 
  append cmd_cre_va "\n" ;
  append cmd_cre_va "########################################################################\n" ;
  append cmd_cre_va "# Create voltage area into block level.\n"
  append cmd_cre_va "########################################################################\n" ;
  append cmd_cre_va "set target_cell_name $target_cell_name ;\n" ;
  append cmd_cre_va "set target_cell \[get_cells \$target_cell_name\] ;\n"
  append cmd_cre_va "set_working_design -push \$target_cell ;\n\n" ;

  foreach_in_collection cur_VA $VAs {
    set cur_VA_name [get_attribute $cur_VA name] ;
    set idx [lsearch $va_names $cur_VA_name] ;
    set PD_name [lindex $pd_names $idx] ;
    set target_PD_name [string trimleft $PD_name "${target_cell_name}/"] ;
    set target_PD [get_power_domain $PD_name] ;
    set va_bounding_box   [get_attribute $cur_VA bounding_box] ;
    set va_region         [get_attribute $cur_VA region] ;
    set va_guard_bands     [get_attribute $cur_VA guard_bands] ;
    set cell_bounding_box [get_attribute $target_cell boundary_bounding_box] ;
    set primary_power     [get_attribute $target_PD primary_power.name] ;
    set primary_ground    [get_attribute $target_PD primary_ground.name] ;
    
    set str_len [string length $va_region] ;
    set va_region [string range $va_region 1 [expr $str_len -2]]

    set cell_llx [get_attribute $cell_bounding_box ll_x] ;
    set cell_lly [get_attribute $cell_bounding_box ll_y] ;

    set new_poly_list [list] ;
    foreach cur_coord $va_region {
      set new_x [expr [lindex $cur_coord 0] - $cell_llx] ;
      set new_y [expr [lindex $cur_coord 1] - $cell_lly] ;
      set new_coord [list] ;
      lappend new_coord $new_x ;
      lappend new_coord $new_y ;
      lappend new_poly_list $new_coord ;
    }

    append cmd_cre_va "set pd \[get_power_domains -quiet $target_PD_name\] ;\n" ;
    append cmd_cre_va "if \{\[sizeof_collection \$pd\]\} \{\n" ;
    append cmd_cre_va "  if \{\[get_attribute \$pd associated_by_missing_va\] == true\} \{\n" ;
    append cmd_cre_va "    puts \"RMM-info: Creating VA for power domain: $target_PD_name ... \" ;\n"
    if { $va_guard_bands == "" } {
      append cmd_cre_va "    create_voltage_area -power_domain $target_PD_name -region \{$new_poly_list\} ;\n" ;
    } else {
      append cmd_cre_va "    create_voltage_area -power_domain $target_PD_name -region \{$new_poly_list\} -guard_band \{$va_guard_bands\} ;\n" ;
    }
    append cmd_cre_va "  \} else \{\n" ;
    append cmd_cre_va "    puts \"RM-warning: VA already exists as DEFAULT_VA, DEFAULT_VA shape can not be modified.\""
    append cmd_cre_va "  \}\n" ;
    append cmd_cre_va "\} else \{\n" ;
    append cmd_cre_va "  puts \"RM-warning: power domain: $target_PD_name not found, Skipping VA push down.\" ;\n" ;
    append cmd_cre_va "\}\n" ;
  }

  append cmd_cre_va "\nset_working_design -pop -level 0 ;\n" ;

  set out_file [open $write_script_name w] ;
  if { $write_script_only == 0 } {
    puts $out_file $cmd_cre_va ;
    eval $cmd_cre_va ;
  } else {
    puts $out_file $cmd_cre_va ;
  }
  puts "RM-info: VA push down tcl script ($write_script_name) was written."
  close $out_file ;

  return 1 ;
}

define_proc_attributes rm_push_down_voltage_areas \
  -info "Push down voltage_areas into overlapped hierarchical block" \
  -define_args {
    {-cell              "Specifies block"     "collection"  string  required}
    {-map_file          "Specifies map_file"  "<file name>" string  required}
    {-write_script_only ""                    ""            boolean  optional}
    {-keep_top_voltage_area "" ""                           boolean optional}
  }

proc rm_extract_block_constraint_types { map_info } {

  set rc [list] ;
  set idx 0 ;
  foreach line [split $map_info "\n"] {
    set key_list [split $line " "] ;
    if { [llength $key_list] == 3 } {
      set cur_type [lindex $key_list 1] ;
      if { [lsearch $rc $cur_type] < 0 } {
        lappend  rc $cur_type ;
      }
    }

    incr idx ;
  }

  return $rc ;
}

proc rm_get_block_constraint_types { args } {

  parse_proc_arguments -args $args results ;

  redirect -variable map_info {report_constraint_mapping_file} ;

  set const_types [rm_extract_block_constraint_types $map_info] ;
}

define_proc_attributes rm_get_block_constraint_types \
  -info "Gets constraint types from block constraints mapping." \
  -define_args {
  }

proc rm_logparse {logfile} {
    set cmd "scripts/util/logparse.pl"
    append cmd " $logfile"
    puts "\nRM-info: Running  $cmd\n"
    eval exec $cmd
}

proc rm_check_design { args } {

  parse_proc_arguments -args $args results

  if {[info exists results(-step)]} { set step $results(-step) }

  if {[current_design] != ""} {
     if { [string equal ${step} init_design] } {
         set error_detected 0 ;

         if {[sizeof_collection [get_site_rows -quiet]] == 0 && [sizeof_collection [get_site_arrays -quiet]] == 0} {
                 set error_detected 1
                 puts "RM-error: Design has no site rows or site arrays. Please fix it before you continue!"
         }
         if {[sizeof_collection [get_terminals -filter "port.port_type==signal" -quiet]] == 0} {
                 set error_detected 1
                 puts "RM-error: Design has no signal terminals. Please fix it before you continue!"
         }
         if {[sizeof_collection [get_tracks -quiet]] == 0} {
                 set error_detected 1
                 puts "RM-error: Design has no tracks. Please fix it before you continue!"
         }
         if {[sizeof_collection [get_shapes -filter "net_type==power"]] == 0 || [sizeof_collection [get_shapes -filter "net_type==ground"]] == 0} {
                 puts "RM-warning: Design does not contain any PG shapes. You do not have proper PG structure. If this is unexpected, please double check before you continue!"
         }
         if {[sizeof_collection [get_cells -hier -filter "is_hard_macro&&!is_placed"]]} {
                 set error_detected 1
                 puts "RM-error: Design has unplaced hard macros. Please fix it before you continue!"
         }
         if {[sizeof_collection [get_cells -hier -filter "is_physical_only&&(design_type=~*cap||design_type=~*tap)"]] == 0} {
                 puts "RM-warning: Design has no boundary or tap cells. If this is unexpected, please double check before you continue!"
         }
         if {[sizeof_collection [get_cells -hier -filter "is_physical_only&&(design_type=~*cap||design_type=~*tap)&&(!is_placed||!is_fixed)"]]} {
                 set error_detected 1
                 puts "RM-error: Design has unplaced boundary or tap cells. Please fix it before you continue!"
         }
         return $error_detected ;
      } 
   } else {
    puts "RM-error: No current design"
   }
}

define_proc_attributes rm_check_design \
        -info "rm_check_design #Command design settings for different steps in the RM flow" \
   	-define_args {
	{-step "Values - init_design|compile|place_opt|cts|post_cts_opto|route|post_route" "#flow step" string required }
	}     

proc rm_report_qor { args } {

   global env search_path synopsys_program_name CURRENT_STEP REPORTS_DIR REPORT_PREFIX REPORT_DEBUG REPORT_CLOCK_POWER REPORT_QOR_REPORT_POWER REPORT_POWER_SAIF_FILE REPORT_POWER_SAIF_MAP SAIF_FILE_SOURCE_INSTANCE REPORT_QOR_REPORT_CONGESTION REPORT_VERBOSE TCL_USER_SUPPLEMENTAL_REPORTS_SCRIPT
   global EARLY_DATA_CHECK_POLICY WRITE_QOR_DATA WRITE_QOR_DATA_DIR COMPARE_QOR_DATA_DIR REPORT_PARALLEL_SUBMIT_COMMAND REPORT_PARALLEL_MAX_CORES
   global REPORT_INIT_DESIGN_ACTIVE_SCENARIO_LIST REPORT_COMPILE_ACTIVE_SCENARIO_LIST REPORT_PLACE_OPT_ACTIVE_SCENARIO_LIST REPORT_CLOCK_OPT_CTS_ACTIVE_SCENARIO_LIST REPORT_CLOCK_OPT_OPTO_ACTIVE_SCENARIO_LIST
   global REPORT_ROUTE_AUTO_ACTIVE_SCENARIO_LIST REPORT_ROUTE_OPT_ACTIVE_SCENARIO_LIST REPORT_CHIP_FINISH_ACTIVE_SCENARIO_LIST REPORT_ICV_IN_DESIGN_ACTIVE_SCENARIO_LIST REPORT_ENDPOINT_OPT_ACTIVE_SCENARIO_LIST REPORT_TIMING_ECO_ACTIVE_SCENARIO_LIST
  global INIT_DESIGN_BLOCK_NAME PLACE_OPT_BLOCK_NAME CLOCK_OPT_CTS_BLOCK_NAME CLOCK_OPT_OPTO_BLOCK_NAME ROUTE_AUTO_BLOCK_NAME ROUTE_OPT_BLOCK_NAME CHIP_FINISH_BLOCK_NAME ICV_IN_DESIGN_BLOCK_NAME TIMING_ECO_BLOCK_NAME FUNCTIONAL_ECO_BLOCK_NAME ENDPOINT_OPT_BLOCK_NAME
  global COMPILE_BLOCK_NAME ENABLE_FUSA
  global USE_ABSTRACTS_FOR_BLOCKS USE_ABSTRACTS_FOR_POWER_ANALYSIS CHECK_HIER_TIMING_CONSTRAINTS_CONSISTENCY
  global REPORT_STAGE REPORT_ACTIVE_SCENARIOS REPORT_DISABLE_GUI
  global SEV

   parse_proc_arguments -args $args results
   if {![info exists CURRENT_STEP]} { 
      if {![info exists results(-stage)] || ![info exists results(-reports_directory)] || ![info exists results(-report_prefix)]} {
         return -code error "Detected that rm_report_qor is running standalone. Note: -stage, -reports_directory, -report_prefix , and -setup_path are required when running standalone"
         }
   }

   if {[info exists results(-setup_path)]} {set SETUP_PATH $results(-setup_path)} else {set SETUP_PATH ""}

   if {$synopsys_program_name == "icc2_shell"} {
      set search_path [list ./scripts/util ./scripts/plugins ./scripts/tech ./scripts/flow ./configs ./examples]
      if {$SETUP_PATH != ""} {
         set search_path "$search_path $SETUP_PATH"
         }
         lappend search_path .
   } elseif {$synopsys_program_name == "fc_shell"} {
      set search_path "$search_path ./scripts/util ./scripts/plugins ./scripts/tech ./scripts/flow ./configs ./examples"
      if {$SETUP_PATH != ""} {
         set search_path "$search_path $SETUP_PATH"
      }
   }

   if {![info exists CURRENT_STEP]} {
      source ./scripts/util/procs_global.tcl
      if {$synopsys_program_name == "fc_shell"} {source ./scripts/util/procs_fc.tcl} else {source ./scripts/util/procs_icc2.tcl}
      source ./configs/design_setup.tcl
      }   
   if {[info exists results(-stage)]} { 
        set REPORT_STAGE $results(-stage)
      } 

   if {![info exists REPORT_ACTIVE_SCENARIOS]} { 
      if {[info exists results(-active_scenarios)]} {set REPORT_ACTIVE_SCENARIOS $results(-active_scenarios)} else {set REPORT_ACTIVE_SCENARIOS ""}} 
   if {[info exists results(-debug)]} {set REPORT_DEBUG $results(-debug)}
   if {[info exists results(-verbose)]} {set REPORT_VERBOSE $results(-verbose)} 
   if {[info exists results(-power_saif_file)]} {set REPORT_POWER_SAIF_FILE $results(-power_saif_file)}
   if {[info exists results(-power_saif_map)]} {set REPORT_POWER_SAIF_MAP $results(-power_saif_map)}
   if {[info exists results(-saif_source_instance)]} {set SAIF_FILE_SOURCE_INSTANCE $results(-saif_source_instance)}
   if {[info exists results(-clock_power)]} {set REPORT_CLOCK_POWER $results(-clock_power)}
   if {[info exists results(-disable_total_power)]} {set REPORT_QOR_REPORT_POWER false}
   if {[info exists results(-disable_congestion)]} {set REPORT_QOR_REPORT_CONGESTION false}
   if {[info exists results(-abstracts_for_blocks)]} {set USE_ABSTRACTS_FOR_BLOCKS $results(-abstracts_for_blocks)} 
   if {[info exists results(-abstracts_for_power)]} {set USE_ABSTRACTS_FOR_POWER_ANALYSIS $results(-abstracts_for_power)}
   if {[info exists results(-fusa)]} {set ENABLE_FUSA $results(-fusa)}
   if {[info exists results(-early_data_check_policy)]} {set EARLY_DATA_CHECK_POLICY $results(-early_data_check_policy)}
   if {[info exists results(-disable_write_qor_data)]} {set WRITE_QOR_DATA false}
   if {[info exists results(-write_qor_data_dir)]} {set WRITE_QOR_DATA_DIR $results(-write_qor_data_dir)}
   if {[info exists results(-compare_qor_data_dir)]} {set COMPARE_QOR_DATA_DIR $results(-compare_qor_data_dir)}
   if {[info exists results(-submit_command)]} {set REPORT_PARALLEL_SUBMIT_COMMAND $results(-submit_command)}
   if {[info exists results(-user_reporting_script)]} {set TCL_USER_SUPPLEMENTAL_REPORTS_SCRIPT $results(-user_reporting_script)}

   if {![info exists results(-disable_write_qor_data)] && ![info exists CURRENT_STEP]} {
      if {![info exists results(-write_qor_data_dir)] || ![info exists results(-compare_qor_data_dir)]} {
          return -code error "When running standalone, if write_qor_data is enabled, -write_qor_data_dir and -compare_qor_data_dir are required" 
         }
      }

   if {[info exists REPORTS_DIR]} {set REPORTS_DIR_ORIGINAL $REPORTS_DIR } 
   if {[info exists REPORT_PREFIX]} {set REPORT_PREFIX_ORIGINAL $REPORT_PREFIX } 
   if {[info exists results(-reports_directory)]} {set REPORTS_DIR $results(-reports_directory)} 
   if {[info exists results(-report_prefix)]} {set REPORT_PREFIX $results(-report_prefix)}
   if {[file isdirectory ${REPORTS_DIR}/${REPORT_PREFIX}]} {
          return -code error "Specified directory ${REPORTS_DIR}/${REPORT_PREFIX} exists, to avoid overwritting any reports please specify a different report path"
        } else {
           file mkdir ${REPORTS_DIR}/${REPORT_PREFIX}
        }
   
   if {[info exists results(-block)]} {set BLOCK $results(-block)}
   if {$REPORT_PARALLEL_SUBMIT_COMMAND != ""} {
           rm_generate_variables_for_report_parallel -work_directory ${REPORTS_DIR}/${REPORT_PREFIX} -file_name rm_tcl_var.tcl

	   if {[info exists results(-block)]} {
              report_parallel -work_directory ${REPORTS_DIR}/${REPORT_PREFIX} -submit_command ${REPORT_PARALLEL_SUBMIT_COMMAND} -max_cores ${REPORT_PARALLEL_MAX_CORES} -user_scripts [list "${REPORTS_DIR}/${REPORT_PREFIX}/rm_tcl_var.tcl" "[which report_qor.tcl]"] -block $results(-block)
	      } else {
	      report_parallel -work_directory ${REPORTS_DIR}/${REPORT_PREFIX} -submit_command ${REPORT_PARALLEL_SUBMIT_COMMAND} -max_cores ${REPORT_PARALLEL_MAX_CORES} -user_scripts [list "${REPORTS_DIR}/${REPORT_PREFIX}/rm_tcl_var.tcl" "[which report_qor.tcl]"]
	      }
      } else {
           rm_source -file report_qor.tcl
      }
  if {[info exists REPORTS_DIR_ORIGINAL]} {set REPORTS_DIR $REPORTS_DIR_ORIGINAL}
  if {[info exists REPORT_PREFIX_ORIGINAL]} {set REPORT_PREFIX $REPORT_PREFIX_ORIGINAL}
}
define_proc_attributes rm_report_qor \
        -info "report_qor_rm  #Command executes a series of reporting commands based on the specified stage of the design" \
        -define_args {
        {-stage "Values - init_design|mapped|synthesis|placement|cts|post_cts_opt|route|post_route" "#Required if rm_report_qor run standalone" string required}
	{-reports_directory "Name of report directory" "#Required if rm_report_qor run standalone" string optional}	
	{-report_prefix "Name of report sub-directory" "#Required if rm_report_qor run standalone" string optional}	
	{-setup_path "Path to RM directories: configs, scripts/flow, scripts/util etc" "" string optional}  
        {-verbose  "Runs additional report_timing with -max_paths equal to 300 and -slack_lesser_than 0" "" boolean optional}
	{-debug "Generates zero interconnect timing if stage is init_design.  If stage is route_auto generates non-SI timing reports" "" boolean optional}
	{-disable_congestion "Disables report congestion with route_global -congestion_map_only true" "" boolean optional}
	{-disable_total_power "Disables report_power during reporting" "" boolean optional}
	{-clock_power "Enables report_clock_qor -type power" "" boolean optional}
	{-active_scenarios "Scenarios to activate for reporting" "#list of scenarios" string optional}
	{-power_saif_file "Specify a SAIF file for report_power" "#path to saif" string optional}
	{-power_saif_map "Specify a SAIF map for report_power" "#path to saif map" string optional}
	{-saif_source_instance "name of the instance of the current design as it appears in SAIF file" "#name of instance" string optional}
	{-fusa "Specify to enable report_safety_status" ""  boolean optional}
	{-abstracts_for_blocks "design names of the physical blocks in the next lower level that will be bound to abstracts" "#list of design names of physical blocks" string optional}
       	{-abstracts_for_power "set this switch to perform power analysis inside subblocks modeled as abstracts" "" boolean optional} 
	{-early_data_check_policy "Values - none|lenient|strict" "#specify policy for set_early_data_check_policy command" string optional}
        {-disable_write_qor_data "Disables write_qor_data command to generate data for QoR HTML file" "" boolean optional}	
	{-write_qor_data_dir "Specify write_qor_data directory" "#Required with -write_qor_data option" string optional}
	{-compare_qor_data_dir "Specify compare_qor_data directory" "#Required with -write_qor_data option" string optional}
	{-user_reporting_script "Specify user reporting script" "" string optional}
	{-submit_command "Full job submission command.  Note syntax sensititivity. Ex : \"qsub -P bnormal -cwd -V -N report_parallel -pe mt 3 -l \\\"mem_free=4000M\\\"\"" "" string optional}
	{-block "Name of design and label to open when running report_parallel" "" string optional}   
	}
