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
set PREVIOUS_STEP $ENDPOINT_OPT_FROM_BLOCK_NAME
set CURRENT_STEP  $ENDPOINT_OPT_BLOCK_NAME
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
set_svf ${OUTPUTS_DIR}/${ENDPOINT_OPT_BLOCK_NAME}.svf 

rm_source -file $TCL_PVT_CONFIGURATION_FILE -optional -print "TCL_PVT_CONFIGURATION_FILE"

open_lib $DESIGN_LIBRARY
copy_block -from ${DESIGN_NAME}/${PREVIOUS_STEP} -to ${DESIGN_NAME}/${CURRENT_STEP}
current_block ${DESIGN_NAME}/${CURRENT_STEP}
link_block

if {$ENDPOINT_OPT_ACTIVE_SCENARIO_LIST != ""} {
	set_scenario_status -active false [get_scenarios -filter active]
	set_scenario_status -active true $ENDPOINT_OPT_ACTIVE_SCENARIO_LIST
}

rm_source -file $TCL_MODE_CORNER_SCENARIO_MODEL_ADJUSTMENT_FILE -optional -print "TCL_MODE_CORNER_SCENARIO_MODEL_ADJUSTMENT_FILE"

rm_source -file $TCL_USER_NON_PERSISTENT_SCRIPT -optional -print "TCL_USER_NON_PERSISTENT_SCRIPT"

rm_source -file $TCL_MULTI_VT_CONSTRAINT_FILE -optional -print "TCL_MULTI_VT_CONSTRAINT_FILE"

set_app_options -name opt.common.user_instance_name_prefix -value endpoint_opt_
set_app_options -name cts.common.user_instance_name_prefix -value endpoint_opt_cts_

rm_source -file $SIDEFILE_ROUTE_OPT -optional -print "SIDEFILE_ROUTE_OPT"

if {$SET_QOR_STRATEGY_METRIC == "leakage_power"} {
   set rm_dynamic_scenarios [get_object_name [get_scenarios -filter active==true&&dynamic_power==true]]

   if {[llength $rm_dynamic_scenarios] > 0} {
      puts "RM-info: Disabling dynamic analysis for $rm_dynamic_scenarios"
      set_scenario_status -dynamic_power false [get_scenarios $rm_dynamic_scenarios]
  }
}

rm_source -file $TCL_USER_ENDPOINT_OPT_PRE_SCRIPT -optional -print "TCL_USER_ENDPOINT_OPT_PRE_SCRIPT"

redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_app_options.start {report_app_options -non_default *}
redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_lib_cell_purpose {report_lib_cell -objects [get_lib_cells] -column {full_name:20 valid_purposes}}

if {$ENABLE_INLINE_REPORT_QOR} {
   redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_qor.start {report_qor -scenarios [all_scenarios] -pba_mode [get_app_option_value -name time.pba_optimization_mode] -nosplit}
   redirect -append -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_qor.start {report_qor -summary -pba_mode [get_app_option_value -name time.pba_optimization_mode] -nosplit}
   redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_global_timing.start {report_global_timing -pba_mode [get_app_option_value -name time.pba_optimization_mode] -nosplit}
}
set check_stage_settings_cmd "check_stage_settings -stage pnr -metric \"${SET_QOR_STRATEGY_METRIC}\" -step post_route"
if {$ENABLE_REDUCED_EFFORT} {lappend check_stage_settings_cmd -reduced_effort}
if {$RESET_CHECK_STAGE_SETTINGS} {lappend check_stage_settings_cmd -reset_app_options}
redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/check_stage_settings {eval ${check_stage_settings_cmd}}

if {$DESIGN_STYLE == "hier" && $PHYSICAL_HIERARCHY_LEVEL != "bottom"} {
	set_timing_paths_disabled_blocks  -all_sub_blocks
}

if {[get_drc_error_data -quiet zroute.err] == ""} {open_drc_error_data zroute.err}
set rm_drc_before_corecmd [sizeof_collection [get_drc_errors -quiet -error_data zroute.err]]

if {![rm_source -file $TCL_USER_ENDPOINT_OPT_SCRIPT -optional -print "TCL_USER_ENDPOINT_OPT_SCRIPT"]} {
	set targeted_ep_ropt_pba_ccd_cmd "targeted_ep_ropt_pba_ccd -max_paths $ENDPOINT_OPT_MAX_PATHS -slack_lesser_than $ENDPOINT_OPT_SLACK_THRESHOLD -scenarios \"$ENDPOINT_OPT_TARGET_SCENARIOS\""
	if {$ENDPOINT_OPT_PATH_GROUP_FILTER != ""} {
		lappend targeted_ep_ropt_pba_ccd_cmd -path_group_filter $ENDPOINT_OPT_PATH_GROUP_FILTER
	}

	for {set i 1} {$i < [expr $ENDPOINT_OPT_LOOP + 1]} {incr i} {
		puts "RM-info: Running $targeted_ep_ropt_pba_ccd_cmd #${i}"
		eval $targeted_ep_ropt_pba_ccd_cmd
		puts "RM-info: Completed $targeted_ep_ropt_pba_ccd_cmd #${i}"

		if {$ENDPOINT_OPT_LOOP > 1} {save_block -as ${DESIGN_NAME}/${CURRENT_STEP}_${i}}

	}
}

if {[get_drc_error_data -quiet zroute.err] == ""} {open_drc_error_data zroute.err}
set rm_drc_after_corecmd [sizeof_collection [get_drc_errors -quiet -error_data zroute.err]]

if { [info exists rm_drc_before_corecmd] && [info exists rm_drc_after_corecmd] } {
	set incr_route_detail_cmd "route_detail -incremental true -initial_drc_from_input true"
	if {$INCR_ROUTE_DETAIL_MAX_ITERATIONS != ""} {lappend incr_route_detail_cmd -max_number_iterations $INCR_ROUTE_DETAIL_MAX_ITERATIONS}
	if { [string equal -nocase $INCR_ROUTE_DETAIL_MODE "true"] } {
		puts "RM-info : INCR_ROUTE_DETAIL_MODE = true. Running $incr_route_detail_cmd"	
		eval $incr_route_detail_cmd
	} elseif { [string equal -nocase $INCR_ROUTE_DETAIL_MODE "false"] } {
		puts "RM-info : INCR_ROUTE_DETAIL_MODE = false. Skipping $incr_route_detail_cmd"
	} elseif {[string equal -nocase $INCR_ROUTE_DETAIL_MODE "auto"]} {
		if { ($rm_drc_after_corecmd > $rm_drc_before_corecmd) && \
		     ($rm_drc_before_corecmd < $INCR_ROUTE_DETAIL_DRC_THRESHOLD_MAX) && \
		     ($rm_drc_after_corecmd > $INCR_ROUTE_DETAIL_DRC_THRESHOLD_MIN) && \
		     ([expr (${rm_drc_after_corecmd}-${rm_drc_before_corecmd})] > [expr (${INCR_ROUTE_DETAIL_DRC_INCREASE_THRESHOLD_MIN}*${rm_drc_before_corecmd})]) } {
			puts "RM-info : INCR_ROUTE_DETAIL_MODE = auto and conditions are met. Running $incr_route_detail_cmd"	
			eval $incr_route_detail_cmd
		}
	}
}

if {$HPC_CORE != ""} {
	if {$CURRENT_STEP == "endpoint_opt_flat" && [info exists HPC_ENDPOINT_OPT_FLAT_POST_SCRIPT]} {
		rm_source -file $HPC_ENDPOINT_OPT_FLAT_POST_SCRIPT -optional -print "HPC_ENDPOINT_OPT_FLAT_POST_SCRIPT"
	} else {
		rm_source -file $TCL_USER_ENDPOINT_OPT_POST_SCRIPT -optional -print "TCL_USER_ENDPOINT_OPT_POST_SCRIPT" 
	}
} else {
	rm_source -file $TCL_USER_ENDPOINT_OPT_POST_SCRIPT -optional -print "TCL_USER_ENDPOINT_OPT_POST_SCRIPT" 
}

if {![rm_source -file $TCL_USER_CONNECT_PG_NET_SCRIPT -optional -print "TCL_USER_CONNECT_PG_NET_SCRIPT"]} {
	connect_pg_net
}

redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/check_routes {check_routes}

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
                derive_hier_antenna_property -design ${DESIGN_NAME}/${ENDPOINT_OPT_BLOCK_NAME}
                save_block ${DESIGN_NAME}/${ENDPOINT_OPT_BLOCK_NAME}.frame
	} elseif { $PHYSICAL_HIERARCHY_LEVEL == "intermediate"} {
		if { $ABSTRACT_TYPE_FOR_MPH_BLOCKS == "nested"} {
			create_abstract -read_only
		} elseif { $ABSTRACT_TYPE_FOR_MPH_BLOCKS == "flattened"} {
			create_abstract -read_only -preserve_block_instances false
		}
                create_frame -block_all true
                derive_hier_antenna_property -design ${DESIGN_NAME}/${ENDPOINT_OPT_BLOCK_NAME}
		save_block ${DESIGN_NAME}/${ENDPOINT_OPT_BLOCK_NAME}.frame
	}
}

if {$REPORT_QOR} {
        set REPORT_STAGE post_route
        set REPORT_ACTIVE_SCENARIOS $REPORT_ENDPOINT_OPT_ACTIVE_SCENARIO_LIST
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
rm_logparse $LOGS_DIR/endpoint_opt.log
echo [date] > endpoint_opt

exit 
