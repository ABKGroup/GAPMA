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

set PREVIOUS_STEP $FUNCTIONAL_ECO_FROM_BLOCK_NAME
set CURRENT_STEP  $FUNCTIONAL_ECO_BLOCK_NAME

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

rm_source -file $TCL_PVT_CONFIGURATION_FILE -optional -print "TCL_PVT_CONFIGURATION_FILE"

open_lib $DESIGN_LIBRARY
copy_block -from ${DESIGN_NAME}/${FUNCTIONAL_ECO_FROM_BLOCK_NAME} -to ${DESIGN_NAME}/${FUNCTIONAL_ECO_BLOCK_NAME}
current_block ${DESIGN_NAME}/${FUNCTIONAL_ECO_BLOCK_NAME}
link_block

if {![file exists [which $FUNCTIONAL_ECO_VERILOG_FILE]]} {
        puts "RM-error: FUNCTIONAL_ECO_VERILOG_FILE is not specified or invalid. Exiting...."
        exit
}

if {$FUNCTIONAL_ECO_ACTIVE_SCENARIO_LIST != ""} {
	set_scenario_status -active false [get_scenarios -filter active]
	set_scenario_status -active true $FUNCTIONAL_ECO_ACTIVE_SCENARIO_LIST
}

rm_source -file $TCL_MODE_CORNER_SCENARIO_MODEL_ADJUSTMENT_FILE -optional -print "TCL_MODE_CORNER_SCENARIO_MODEL_ADJUSTMENT_FILE"

rm_source -file $SIDEFILE_FUNCTIONAL_ECO -optional -print "SIDEFILE_FUNCTIONAL_ECO"

rm_source -file $TCL_USER_NON_PERSISTENT_SCRIPT -optional -print "TCL_USER_NON_PERSISTENT_SCRIPT"

rm_source -file $TCL_USER_FUNCTIONAL_ECO_PRE_SCRIPT -optional -print "TCL_USER_FUNCTIONAL_ECO_PRE_SCRIPT"

redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_app_options.start {report_app_options -non_default *}
redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_lib_cell_purpose {report_lib_cell -objects [get_lib_cells] -column {full_name:20 valid_purposes}}

if {$ENABLE_INLINE_REPORT_QOR} {
   redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_qor.start {report_qor -scenarios [all_scenarios] -pba_mode [get_app_option_value -name time.pba_optimization_mode] -nosplit}
   redirect -append -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_qor.start {report_qor -summary -pba_mode [get_app_option_value -name time.pba_optimization_mode] -nosplit}
   redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_global_timing.start {report_global_timing -pba_mode [get_app_option_value -name time.pba_optimization_mode] -nosplit}
}

remove_attribute [get_cell -quiet -hier -filter "defined(eco_change_status)"] eco_change_status

if {$FUNCTIONAL_ECO_MODE == "freeze_silicon"} {

	rm_source -file $TCL_USER_PSC_AUTO_DERIVE_MAPPING_RULE_FILE -optional -print "TCL_USER_PSC_AUTO_DERIVE_MAPPING_RULE_FILE"

	puts "RM-info: Running freeze silicon Functional ECO flow"

	eco_netlist -by_verilog_file $FUNCTIONAL_ECO_VERILOG_FILE -write_changes eco_changes.tcl

	set_app_options -name design.eco_freeze_silicon_mode -value true

	rm_source -file eco_changes.tcl

	set_app_options -name design.eco_freeze_silicon_mode -value false
	
	redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/check_freeze_silicon {check_freeze_silicon}

	place_freeze_silicon
	
} else {

	puts "RM-info: Running MPI Functional ECO flow"

	eco_netlist -by_verilog_file $FUNCTIONAL_ECO_VERILOG_FILE -write_changes eco_changes.tcl

	rm_source -file eco_changes.tcl

	redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_eco_physical_changes.pre_eco_place.rpt {report_eco_physical_changes -type all}

	place_eco_cells -eco_changed_cells -no_legalize

	set place_eco_cells_cmd "place_eco_cells -eco_changed_cells -legalize_only -legalize_mode minimum_physical_impact -displacement_threshold $FUNCTIONAL_ECO_DISPLACEMENT_THRESHOLD"
	if {$CHIP_FINISH_METAL_FILLER_LIB_CELL_LIST != "" || $CHIP_FINISH_NON_METAL_FILLER_LIB_CELL_LIST != ""} {
		lappend place_eco_cells_cmd -remove_filler_references "$CHIP_FINISH_METAL_FILLER_LIB_CELL_LIST $CHIP_FINISH_NON_METAL_FILLER_LIB_CELL_LIST"
	}
	puts "RM-info: $place_eco_cells_cmd"
	eval ${place_eco_cells_cmd}

	redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_eco_physical_changes.post_eco_place.rpt {report_eco_physical_changes -type all}
}

connect_pg_net
redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/check_legality {check_legality -verbose}

set_app_options -name route.detail.eco_route_use_soft_spacing_for_timing_optimization -value false

set RM_route_global_timing_driven [get_app_option_value -name route.global.timing_driven]
set_app_options -name route.global.timing_driven    -value false
set RM_route_track_timing_driven [get_app_option_value -name route.track.timing_driven]
set_app_options -name route.track.timing_driven     -value false
set RM_route_detail_timing_driven [get_app_option_value -name route.detail.timing_driven]
set_app_options -name route.detail.timing_driven    -value false 
set RM_route_global_crosstalk_driven [get_app_option_value -name route.global.crosstalk_driven]
set_app_options -name route.global.crosstalk_driven -value false 
set RM_route_track_crosstalk_driven [get_app_option_value -name route.track.crosstalk_driven]
set_app_options -name route.track.crosstalk_driven  -value false 
	
set route_eco_cmd "route_eco -utilize_dangling_wires true -reroute modified_nets_first_then_others -open_net_driven true"
puts "RM-info: $route_eco_cmd"
eval ${route_eco_cmd}

set_app_options -name route.global.timing_driven -value $RM_route_global_timing_driven
set_app_options -name route.track.timing_driven -value $RM_route_track_timing_driven
set_app_options -name route.detail.timing_driven -value $RM_route_detail_timing_driven
set_app_options -name route.global.crosstalk_driven -value $RM_route_global_crosstalk_driven
set_app_options -name route.track.crosstalk_driven -value $RM_route_track_crosstalk_driven

redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_eco_physical_changes.post_eco_route.rpt {report_eco_physical_changes -type all}

if {([sizeof_collection [get_cells -quiet ${CHIP_FINISH_METAL_FILLER_PREFIX}*]] > 0) && ($PT_ECO_MODE!="freeze_silicon")} {
	puts "RM-info: Filler cells were detected in the source design.  Performing refill..."
	if {$CHIP_FINISH_METAL_FILLER_LIB_CELL_LIST != ""} {
		set create_stdcell_filler_metal_lib_cell_sorted [get_object_name [sort_collection -descending [get_lib_cells $CHIP_FINISH_METAL_FILLER_LIB_CELL_LIST] area]]
		set create_stdcell_filler_metal_cmd "create_stdcell_filler -lib_cell [list $create_stdcell_filler_metal_lib_cell_sorted] -post_eco"
		if {$CHIP_FINISH_METAL_FILLER_PREFIX != ""} {
			lappend create_stdcell_filler_metal_cmd -prefix $CHIP_FINISH_METAL_FILLER_PREFIX
		}
		puts "RM-info: Running $create_stdcell_filler_metal_cmd"
		eval ${create_stdcell_filler_metal_cmd}
		connect_pg_net
		remove_stdcell_fillers_with_violation -post_eco true ;
	}

	if {$CHIP_FINISH_NON_METAL_FILLER_LIB_CELL_LIST != ""} {
		set create_stdcell_filler_non_metal_lib_cell_sorted [get_object_name [sort_collection -descending [get_lib_cells $CHIP_FINISH_NON_METAL_FILLER_LIB_CELL_LIST] area]]
		set create_stdcell_filler_non_metal_cmd "create_stdcell_filler -lib_cell [list $create_stdcell_filler_non_metal_lib_cell_sorted]"
		if {$CHIP_FINISH_NON_METAL_FILLER_PREFIX != ""} {
			lappend create_stdcell_filler_non_metal_cmd -prefix $CHIP_FINISH_NON_METAL_FILLER_PREFIX
		}
		puts "RM-info: Running $create_stdcell_filler_non_metal_cmd"
		eval ${create_stdcell_filler_non_metal_cmd}
		connect_pg_net
	}
} else {
	puts "RM-info: Skipping filler cell reinsertion as none detected in source design."
}

if {!([compare_collections [get_shapes -hier -quiet] [get_shapes -hier -include_fill -quiet]]=="0")} {
	puts "RM-info: Metal fill was detected in the source design.  Performing refill..."
	save_block

	redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}.report_app_options.signoff.create_metal_fill.rpt {report_app_options signoff.create_metal_fill*}
	
	if {$ICV_IN_DESIGN_METAL_FILL_ECO_THRESHOLD!=""} {
        	 set_app_options   -name  signoff.create_metal_fill.auto_eco_threshold_value -value $ICV_IN_DESIGN_METAL_FILL_ECO_THRESHOLD
	}
	
	set create_metal_fill_cmd "signoff_create_metal_fill"
	
	if {$ICV_IN_DESIGN_METAL_FILL_TRACK_BASED != "off"} {
	
		if {$ICV_IN_DESIGN_METAL_FILL_TRACK_BASED != "generic"} {
			lappend create_metal_fill_cmd "-track_fill $ICV_IN_DESIGN_METAL_FILL_TRACK_BASED -fill_all_tracks true"
		} else {
			lappend create_metal_fill_cmd "-track_fill $ICV_IN_DESIGN_METAL_FILL_TRACK_BASED"
		}
	
		if {$ICV_IN_DESIGN_METAL_FILL_TRACK_BASED_PARAMETER_FILE != "auto" && [file exists [which $ICV_IN_DESIGN_METAL_FILL_TRACK_BASED_PARAMETER_FILE]]} {
			lappend create_metal_fill_cmd -track_fill_parameter_file $ICV_IN_DESIGN_METAL_FILL_TRACK_BASED_PARAMETER_FILE
		}
	}
	
	if {$ICV_IN_DESIGN_METAL_FILL_ECO_THRESHOLD!="0"} {
		lappend create_metal_fill_cmd -auto_eco true
	}
	
	puts "RM-info: Running $create_metal_fill_cmd"
	eval $create_metal_fill_cmd
} else {
	puts "RM-info: Skipping metal fill reinsertion as none detected in source design."
}

rm_source -file $TCL_USER_FUNCTIONAL_ECO_POST_SCRIPT -optional -print "TCL_USER_FUNCTIONAL_ECO_POST_SCRIPT"

if {![rm_source -file $TCL_USER_CONNECT_PG_NET_SCRIPT -optional -print "TCL_USER_CONNECT_PG_NET_SCRIPT"]} {
	connect_pg_net
}

save_block
save_lib

if {$REPORT_PARALLEL_SUBMIT_COMMAND != ""} {
        set REPORT_STAGE post_route
        set REPORT_ACTIVE_SCENARIOS $REPORT_FUNCTIONAL_ECO_ACTIVE_SCENARIO_LIST
	rm_generate_variables_for_report_parallel -work_directory ${REPORTS_DIR}/${REPORT_PREFIX} -file_name rm_tcl_var.tcl

	report_parallel -work_directory ${REPORTS_DIR}/${REPORT_PREFIX} -submit_command ${REPORT_PARALLEL_SUBMIT_COMMAND} -max_cores ${REPORT_PARALLEL_MAX_CORES} -user_scripts [list "${REPORTS_DIR}/${REPORT_PREFIX}/rm_tcl_var.tcl" "[which report_qor.tcl]"]
} else {
	rm_source -file report_qor.tcl
}
redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/run_end.rpt {run_end}

report_msg -summary
print_message_info -ids * -summary
rm_logparse $LOGS_DIR/functional_eco.log
echo [date] > functional_eco

exit

