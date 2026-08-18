# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.
# Copyright (C) 2014-2023 Synopsys, Inc. All rights reserved.

source ./scripts/util/procs_global.tcl 
source ./scripts/util/procs_fc.tcl 
rm_source -file ./configs/design_setup.tcl
rm_source -file sidefile_setup.tcl -after_file technology_override.tcl
if {$HPC_CORE != ""} {
	if {$DESIGN_STYLE == "hier"} {rm_source -file ./flow_override.tcl}
	rm_source -file ./rm_hpc_core_scripts/sidefile_setup_hpc_core.tcl
}
set PREVIOUS_STEP $CLOCK_OPT_CTS_BLOCK_NAME
set CURRENT_STEP $CLOCK_OPT_OPTO_BLOCK_NAME
if { [info exists env(RM_VARFILE)] } {
	if { [file exists $env(RM_VARFILE)] } {
		rm_source -file $env(RM_VARFILE)
	} else {
		puts "RM-error: env(RM_VARFILE) specified but not found"
	}
}

set REPORT_PREFIX $CURRENT_STEP
file mkdir ${REPORTS_DIR}/${REPORT_PREFIX}
puts "RM-info: PREVIOUS_STEP = $PREVIOUS_STEP"
puts "RM-info: CURRENT_STEP  = $CURRENT_STEP"
puts "RM-info: REPORT_PREFIX = $REPORT_PREFIX"
redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/run_start.rpt {run_start}
set_svf ${OUTPUTS_DIR}/${CLOCK_OPT_OPTO_BLOCK_NAME}.svf 

rm_source -file $TCL_PVT_CONFIGURATION_FILE -optional -print "TCL_PVT_CONFIGURATION_FILE"

open_lib $DESIGN_LIBRARY
copy_block -from ${DESIGN_NAME}/${PREVIOUS_STEP} -to ${DESIGN_NAME}/${CURRENT_STEP}
current_block ${DESIGN_NAME}/${CURRENT_STEP}
link_block

if {$DESIGN_STYLE == "hier"} {
	if {$USE_ABSTRACTS_FOR_BLOCKS != "" && ($BLOCK_ABSTRACT_FOR_CLOCK_OPT_OPTO != $BLOCK_ABSTRACT_FOR_CLOCK_OPT_CTS)} {
		puts "RM-info: Swapping from [lindex $BLOCK_ABSTRACT_FOR_CLOCK_OPT_CTS 0] to [lindex $BLOCK_ABSTRACT_FOR_CLOCK_OPT_OPTO 0] abstracts for all blocks."
		change_abstract -references $USE_ABSTRACTS_FOR_BLOCKS -label [lindex $BLOCK_ABSTRACT_FOR_CLOCK_OPT_OPTO 0] -view [lindex $BLOCK_ABSTRACT_FOR_CLOCK_OPT_OPTO 1]
		report_abstracts
	}
	if {$USE_ABSTRACTS_FOR_BLOCKS != "" && $PROMOTE_CLOCK_BALANCE_POINTS} {
		rm_source -file $PROMOTE_ABSTRACT_CLOCK_DATA_FILE
	}
}

if {$CLOCK_OPT_OPTO_ACTIVE_SCENARIO_LIST != ""} {
	set_scenario_status -active false [get_scenarios -filter active]
	set_scenario_status -active true $CLOCK_OPT_OPTO_ACTIVE_SCENARIO_LIST
}

rm_source -file $TCL_MODE_CORNER_SCENARIO_MODEL_ADJUSTMENT_FILE -optional -print "TCL_MODE_CORNER_SCENARIO_MODEL_ADJUSTMENT_FILE"

if {$CLOCK_OPT_OPTO_ACTIVE_SCENARIO_LIST != "" || $TCL_MODE_CORNER_SCENARIO_MODEL_ADJUSTMENT_FILE != ""} {
        synthesize_clock_trees -propagate_only
        compute_clock_latency
}

rm_source -file $TCL_LIB_CELL_PURPOSE_FILE -optional -print "TCL_LIB_CELL_PURPOSE_FILE"

rm_source -file $TCL_USER_NON_PERSISTENT_SCRIPT -optional -print "TCL_USER_NON_PERSISTENT_SCRIPT"

rm_source -file $TCL_MULTI_VT_CONSTRAINT_FILE -optional -print "TCL_MULTI_VT_CONSTRAINT_FILE"

source ./configs/route_app_option.tcl
set set_stage_cmd "set_stage -step post_cts_opto"
puts "RM-info: Running ${set_stage_cmd}"
redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/set_stage.post_cts_opto {eval ${set_stage_cmd} -report}
eval ${set_stage_cmd}

if {$ROUTE_FOCUSED_SCENARIO != ""} {
	set_app_options -name route.common.focus_scenario -value $ROUTE_FOCUSED_SCENARIO
}

if {$HPC_CORE != "" } {
	set HPC_STAGE "clock_opt_opto"
        puts "RM-info: HPC_CORE is being set to $HPC_CORE; Loading HPC settings for stage $HPC_STAGE"
        redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/${DESIGN_NAME}.set_hpc_options {set_hpc_options -core $HPC_CORE -stage $HPC_STAGE -report_only}
        set_hpc_options -core $HPC_CORE -stage $HPC_STAGE
}

set_app_options -name opt.common.user_instance_name_prefix -value clock_opt_opto_
set_app_options -name cts.common.user_instance_name_prefix -value clock_opt_opto_cts_

if {$SET_QOR_STRATEGY_METRIC == "timing"} {
   set rm_leakage_scenarios [get_object_name [get_scenarios -filter active==true&&leakage_power==true]]
   set rm_dynamic_scenarios [get_object_name [get_scenarios -filter active==true&&dynamic_power==true]]

   if {[llength $rm_leakage_scenarios] > 0 || [llength $rm_dynamic_scenarios] > 0} {
      puts "RM-info: Disabling leakage analysis for $rm_leakage_scenarios"
      puts "RM-info: Disabling dynamic analysis for $rm_dynamic_scenarios"
      set_scenario_status -leakage_power false -dynamic_power false [get_scenarios "$rm_leakage_scenarios $rm_dynamic_scenarios"]
   }
} elseif {$SET_QOR_STRATEGY_METRIC == "leakage_power"} {
   set rm_dynamic_scenarios [get_object_name [get_scenarios -filter active==true&&dynamic_power==true]]

   if {[llength $rm_dynamic_scenarios] > 0} {
      puts "RM-info: Disabling dynamic analysis for $rm_dynamic_scenarios"
      set_scenario_status -dynamic_power false [get_scenarios $rm_dynamic_scenarios]
  }
}

if {$ENABLE_IRDP} {
	rm_source -file $TCL_IRDP_CONFIG_FILE -print "ENABLE_IRDP requires a proper TCL_IRDP_CONFIG_FILE"
}

rm_source -file $TCL_USER_CLOCK_OPT_OPTO_PRE_SCRIPT -optional -print "TCL_USER_CLOCK_OPT_OPTO_PRE_SCRIPT"

redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_app_options.start {report_app_options -non_default *}
redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_lib_cell_purpose {report_lib_cell -objects [get_lib_cells] -column {full_name:20 valid_purposes}}

if {$ENABLE_INLINE_REPORT_QOR} {
   redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_qor.start {report_qor -scenarios [all_scenarios] -pba_mode [get_app_option_value -name time.pba_optimization_mode] -nosplit}
   redirect -append -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_qor.start {report_qor -summary -pba_mode [get_app_option_value -name time.pba_optimization_mode] -nosplit}
   redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_global_timing.start {report_global_timing -pba_mode [get_app_option_value -name time.pba_optimization_mode] -nosplit}
}

set check_stage_settings_cmd "check_stage_settings -stage pnr -metric \"${SET_QOR_STRATEGY_METRIC}\" -step post_cts_opto"
if {$ENABLE_REDUCED_EFFORT} {lappend check_stage_settings_cmd -reduced_effort}
if {$RESET_CHECK_STAGE_SETTINGS == "true"} {lappend check_stage_settings_cmd -reset_app_options}
if {$NON_DEFAULT_CHECK_STAGE_SETTINGS == "true"} {lappend check_stage_settings_cmd -all_non_default}
redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/check_stage_settings {eval ${check_stage_settings_cmd}}

if {$DESIGN_STYLE == "hier" && $PHYSICAL_HIERARCHY_LEVEL != "bottom"} {
	set_timing_paths_disabled_blocks  -all_sub_blocks
}

if {![rm_source -file $TCL_USER_CLOCK_OPT_OPTO_SCRIPT -optional -print "TCL_USER_CLOCK_OPT_OPTO_SCRIPT"]} {

	puts "RM-info: Running clock_opt -from final_opto -to final_opto command"
	clock_opt -from final_opto -to final_opto

}

rm_source -file $TCL_USER_CLOCK_OPT_OPTO_POST_SCRIPT -optional -print "TCL_USER_CLOCK_OPT_OPTO_POST_SCRIPT"
if {([check_license -quiet "Fusion-Compiler-BE-NX"] || [check_license -quiet "Fusion-Compiler-NX"]) && [llength $TCL_PRIMEPOWER_CONFIG_FILE]> 0  && [lsearch $INDESIGN_PRIMEPOWER_STAGES "AFTER_CLOCK_OPT_OPTO"] >= 0} {
        rm_source -file $TCL_PRIMEPOWER_CONFIG_FILE -print "ENABLE_PRIMEPOWER requires a proper TCL_PRIMEPOWER_CONFIG_FILE"
	set update_indesign_cmd "update_indesign_activity"
	if {$KEEP_INDESIGN_SAIF_FILE} {lappend update_indesign_cmd -keep_saif -saif_suffix clock_opt_opto}
	puts "RM-info: Running ${update_indesign_cmd}"
	eval ${update_indesign_cmd}
}

if {![rm_source -file $TCL_USER_CONNECT_PG_NET_SCRIPT -optional -print "TCL_USER_CONNECT_PG_NET_SCRIPT"]} {
	connect_pg_net
}

if {[info exists rm_leakage_scenarios] && [llength $rm_leakage_scenarios] > 0} {
   puts "RM-info: Reenabling leakage power analysis for $rm_leakage_scenarios"
   set_scenario_status -leakage_power true [get_scenarios $rm_leakage_scenarios]
}
if {[info exists rm_dynamic_scenarios] && [llength $rm_dynamic_scenarios] > 0} {
   puts "RM-info: Reenabling dynamic power analysis for $rm_dynamic_scenarios"
   set_scenario_status -dynamic_power true [get_scenarios $rm_dynamic_scenarios]
}

save_block

if {$HPC_CORE != ""} {set_scenario_status [get_scenarios $ALL_SCENARIOS] -active true}
if {$DESIGN_STYLE == "hier" && $PHYSICAL_HIERARCHY_LEVEL != "top" && !$SKIP_ABSTRACT_GENERATION} {
	if {$USE_ABSTRACTS_FOR_POWER_ANALYSIS == "true"} {
	        if {$HPC_CORE != ""} {rm_source -file $HPC_REPORT_QOR_POWER -optional -print "HPC_REPORT_QOR_POWER"}
	        set_app_options -name abstract.annotate_power -value true
	}
	if { $PHYSICAL_HIERARCHY_LEVEL == "bottom" } {
	        create_abstract -read_only
	        create_frame -block_all true
	} elseif { $PHYSICAL_HIERARCHY_LEVEL == "intermediate"} {
	    if { $ABSTRACT_TYPE_FOR_MPH_BLOCKS == "nested"} {
	        create_abstract -read_only
	        create_frame -block_all true
	    } elseif { $ABSTRACT_TYPE_FOR_MPH_BLOCKS == "flattened"} {
	        create_abstract -read_only -preserve_block_instances false
	        create_frame -block_all true
	    }
	}
}

if {$REPORT_QOR} {
	set REPORT_STAGE post_cts_opt
        set REPORT_ACTIVE_SCENARIOS $REPORT_CLOCK_OPT_OPTO_ACTIVE_SCENARIO_LIST
	if {$REPORT_PARALLEL_SUBMIT_COMMAND != ""} {
		rm_generate_variables_for_report_parallel -work_directory ${REPORTS_DIR}/${REPORT_PREFIX} -file_name rm_tcl_var.tcl

		report_parallel -work_directory ${REPORTS_DIR}/${REPORT_PREFIX} -submit_command ${REPORT_PARALLEL_SUBMIT_COMMAND} -max_cores ${REPORT_PARALLEL_MAX_CORES} -user_scripts [list "${REPORTS_DIR}/${REPORT_PREFIX}/rm_tcl_var.tcl" "[which report_qor.tcl]"]
	} else {
		rm_source -file report_qor.tcl
	}
}
redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/run_end.rpt {run_end}

write_qor_data -report_list "performance host_machine report_app_options" -label $REPORT_PREFIX -output $WRITE_QOR_DATA_DIR

report_msg -summary
print_message_info -ids * -summary
rm_logparse $LOGS_DIR/clock_opt_opto.log
echo [date] > clock_opt_opto

exit 
