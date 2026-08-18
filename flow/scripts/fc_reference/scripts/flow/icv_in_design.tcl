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
set PREVIOUS_STEP $ICV_IN_DESIGN_FROM_BLOCK_NAME
set CURRENT_STEP  $ICV_IN_DESIGN_BLOCK_NAME
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

rm_source -file $TCL_USER_LIBRARY_SETUP_SCRIPT -optional -print "TCL_USER_LIBRARY_SETUP_SCRIPT"

rm_source -file $TCL_PVT_CONFIGURATION_FILE -optional -print "TCL_PVT_CONFIGURATION_FILE"

open_lib $DESIGN_LIBRARY
copy_block -from ${DESIGN_NAME}/${PREVIOUS_STEP} -to ${DESIGN_NAME}/${CURRENT_STEP}
current_block ${DESIGN_NAME}/${CURRENT_STEP}
link_block

if {$DESIGN_STYLE == "hier"} {
	if {$USE_ABSTRACTS_FOR_BLOCKS != "" && ($BLOCK_ABSTRACT_FOR_ICV_IN_DESIGN != $BLOCK_ABSTRACT_FOR_CHIP_FINISH)} {
		puts "RM-info: Swapping from [lindex $BLOCK_ABSTRACT_FOR_CHIP_FINISH 0] to [lindex $BLOCK_ABSTRACT_FOR_ICV_IN_DESIGN 0] abstracts for all blocks."
		change_abstract -references $USE_ABSTRACTS_FOR_BLOCKS -label [lindex $BLOCK_ABSTRACT_FOR_ICV_IN_DESIGN 0] -view [lindex $BLOCK_ABSTRACT_FOR_ICV_IN_DESIGN 1]
		report_abstracts
	}
}

if {$ICV_IN_DESIGN_ACTIVE_SCENARIO_LIST != ""} {
	set_scenario_status -active false [get_scenarios -filter active]
	set_scenario_status -active true $ICV_IN_DESIGN_ACTIVE_SCENARIO_LIST
}

rm_source -file $TCL_MODE_CORNER_SCENARIO_MODEL_ADJUSTMENT_FILE -optional -print "TCL_MODE_CORNER_SCENARIO_MODEL_ADJUSTMENT_FILE"
rm_source -file $TCL_USER_NON_PERSISTENT_SCRIPT -optional -print "TCL_USER_NON_PERSISTENT_SCRIPT"

rm_source -file $TCL_USER_ICV_IN_DESIGN_PRE_SCRIPT -optional -print "TCL_USER_ICV_IN_DESIGN_PRE_SCRIPT"

redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_app_options.start {report_app_options -non_default *}
redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_lib_cell_purpose {report_lib_cell -objects [get_lib_cells] -column {full_name:20 valid_purposes}}

if {$DESIGN_STYLE == "hier" && $PHYSICAL_HIERARCHY_LEVEL != "bottom"} {
	set_timing_paths_disabled_blocks  -all_sub_blocks
}
if {$ENABLE_INLINE_REPORT_QOR} {
   redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_qor.start {report_qor -scenarios [all_scenarios] -pba_mode [get_app_option_value -name time.pba_optimization_mode] -nosplit}
   redirect -append -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_qor.start {report_qor -summary -pba_mode [get_app_option_value -name time.pba_optimization_mode] -nosplit}
   redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_global_timing.start {report_global_timing -pba_mode [get_app_option_value -name time.pba_optimization_mode] -nosplit}
}

if {$ICV_IN_DESIGN_DRC} {
	if {[file exists [which $ICV_IN_DESIGN_DRC_CHECK_RUNSET]]} {
		puts "RM-info: Setting signoff.check_drc.runset to [which $ICV_IN_DESIGN_DRC_CHECK_RUNSET]"
		set_app_options -name signoff.check_drc.runset -value [which $ICV_IN_DESIGN_DRC_CHECK_RUNSET]
	} else {
		puts "RM-error: ICV_IN_DESIGN_DRC_CHECK_RUNSET is either unspecified or the path is incorrect. Exiting..."
                save_block
                exit
	}
}
if {$ICV_IN_DESIGN_METAL_FILL} {
	if {$ICV_IN_DESIGN_METAL_FILL_TRACK_BASED == "off"} {
		if {[file exists [which $ICV_IN_DESIGN_METAL_FILL_RUNSET]]} {
			puts "RM-info: Setting signoff.create_metal_fill.runset to [which $ICV_IN_DESIGN_METAL_FILL_RUNSET]"
			set_app_options -name signoff.create_metal_fill.runset -value [which $ICV_IN_DESIGN_METAL_FILL_RUNSET]
		} else {
			puts "RM-error: ICV_IN_DESIGN_METAL_FILL_RUNSET is either unspecified or the path is incorrect. Exiting..."
                        save_block
                	exit
		}
	}
}
if { $ICV_IN_DESIGN_BASE_FILL } {
		if {[file exists [which $ICV_IN_DESIGN_BASE_FILL_RUNSET]]} {
			puts "RM-info: Setting signoff.create_metal_fill.runset to [which $ICV_IN_DESIGN_BASE_FILL_RUNSET]"
			set_app_options -name signoff.create_metal_fill.base_layer_runset -value [which $ICV_IN_DESIGN_BASE_FILL_RUNSET]
		} else {
			puts "RM-error: ICV_IN_DESIGN_BASE_FILL_RUNSET is either unspecified or the path is incorrect. Exiting..."
                        save_block
                	exit
		}
}
if {$WRITE_GDS_LAYER_MAP_FILE != ""} {
	set_app_options -name signoff.physical.layer_map_file -value $WRITE_GDS_LAYER_MAP_FILE
} elseif {$WRITE_OASIS_LAYER_MAP_FILE != ""} {
	set_app_options -name signoff.physical.layer_map_file -value $WRITE_OASIS_LAYER_MAP_FILE
} else {
	puts "RM-warning: The layer mapping file is needed to map to GDS/OASIS layers (i.e.WRITE_GDS_LAYER_MAP_FILE/WRITE_OASIS_LAYER_MAP_FILE)"
}
set_app_options -name signoff.physical.merge_stream_files -value $STREAM_FILES_FOR_MERGE ;
redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_app_options.signoff.physical.rpt {report_app_options signoff.physical.*} ;

save_block ;

rm_source -file $SIDEFILE_ICV_IN_DESIGN_1 -optional -print "SIDEFILE_ICV_IN_DESIGN_1"

if {$ICV_IN_DESIGN_DRC} {

        set drc_select_rules $ICV_IN_DESIGN_DRC_SELECT_RULES
        set drc_unselect_rules $ICV_IN_DESIGN_DRC_UNSELECT_RULES
        set excluded_cell_types $ICV_IN_DESIGN_DRC_EXCLUDED_CELL_TYPES
        set ignore_child_cell_errors $ICV_IN_DESIGN_DRC_IGNORE_CHILD_CELL_ERRORS

	set_app_options -name signoff.check_drc.run_dir -value $ICV_IN_DESIGN_DRC_CHECK_RUNDIR ;
	set_app_options -name signoff.check_drc.excluded_cell_types -value $excluded_cell_types ;
	set_app_options -name signoff.check_drc.ignore_child_cell_errors -value $ignore_child_cell_errors ;
        if { $ICV_IN_DESIGN_DRC_USER_DEFINED_OPTIONS != "" } {
		set_app_options -name signoff.check_drc.user_defined_options -value $ICV_IN_DESIGN_DRC_USER_DEFINED_OPTIONS ;
	}
	set_app_options -name signoff.check_drc.fill_view_data -value $ICV_IN_DESIGN_DRC_FILL_VIEW_DATA

	if {$ICV_IN_DESIGN_DRC_CELL_VIEWS == "layout"} {
		set_app_options -name signoff.check_drc.read_layout_views -value {*}
	} elseif {$ICV_IN_DESIGN_DRC_CELL_VIEWS == "design"}  {
		set_app_options -name signoff.check_drc.read_design_views -value {*}
	} else {
		puts "RM-info: Frame views will be read in for analysis."
		reset_app_options signoff.check_drc.read_layout_views
		reset_app_options signoff.check_drc.read_design_views
	}

	rm_source -file $SIDEFILE_ICV_IN_DESIGN_2 -optional -print "SIDEFILE_ICV_IN_DESIGN_2"

	save_block
}

if {$ICV_IN_DESIGN_METAL_FILL} {
	if {$SIDEFILE_ICV_IN_DESIGN_CUSTOM_METAL_FILL != "" } {
		rm_source -file $SIDEFILE_ICV_IN_DESIGN_CUSTOM_METAL_FILL
		if {$ICV_IN_DESIGN_DRC} {
			set_app_options -name signoff.check_drc.run_dir -value $ICV_IN_DESIGN_POST_METAL_FILL_RUNDIR ;
			puts "RM-info: Running $signoff_check_drc_cmd"
			eval $signoff_check_drc_cmd

		} else {
			puts "RM-info: The signoff_check_drc command after signoff_create_metal_fill is skipped."
		}	
	
	} else {
		if {$ICV_IN_DESIGN_METAL_FILL_RUNDIR != ""} {
			puts "RM-info: Setting signoff.create_metal_fill.run_dir to $ICV_IN_DESIGN_METAL_FILL_RUNDIR"
			set_app_options -name signoff.create_metal_fill.run_dir -value $ICV_IN_DESIGN_METAL_FILL_RUNDIR
		}
		set_app_options -name signoff.create_metal_fill.user_defined_options -value $ICV_IN_DESIGN_METAL_FILL_USER_DEFINED_OPTIONS ;
		set_app_options -name signoff.create_metal_fill.fix_density_errors -value $ICV_IN_DESIGN_METAL_FILL_FIX_DENSITY_ERRORS

		if {$ICV_IN_DESIGN_METAL_FILL_TRACK_BASED == "off"} {
			set create_metal_fill_cmd "signoff_create_metal_fill"
		} else {

			if {$ICV_IN_DESIGN_METAL_FILL_TRACK_BASED != "generic"} {
				set create_metal_fill_cmd "signoff_create_metal_fill -track_fill $ICV_IN_DESIGN_METAL_FILL_TRACK_BASED -fill_all_tracks true"
			} else {
				set create_metal_fill_cmd "signoff_create_metal_fill -track_fill $ICV_IN_DESIGN_METAL_FILL_TRACK_BASED"
			}

			if {$ICV_IN_DESIGN_METAL_FILL_TRACK_BASED_PARAMETER_FILE != "auto" && [file exists [which $ICV_IN_DESIGN_METAL_FILL_TRACK_BASED_PARAMETER_FILE]]} {
				lappend create_metal_fill_cmd -track_fill_parameter_file $ICV_IN_DESIGN_METAL_FILL_TRACK_BASED_PARAMETER_FILE
			}
		}

		if {$ICV_IN_DESIGN_METAL_FILL_TIMING_DRIVEN_THRESHOLD != ""} {
			lappend create_metal_fill_cmd -timing_preserve_setup_slack_threshold $ICV_IN_DESIGN_METAL_FILL_TIMING_DRIVEN_THRESHOLD

			set_extraction_options -real_metalfill_extraction none

		}

		if {$ICV_IN_DESIGN_METAL_FILL_SELECT_LAYERS != ""} {
			lappend create_metal_fill_cmd -select_layers $ICV_IN_DESIGN_METAL_FILL_SELECT_LAYERS
		}

		if {$ICV_IN_DESIGN_METAL_FILL_COORDINATES != ""} {
			lappend create_metal_fill_cmd -coordinates $ICV_IN_DESIGN_METAL_FILL_COORDINATES	
		}

		redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_app_options.signoff.create_metal_fill.rpt {report_app_options signoff.create_metal_fill.*} ;

		save_block
		puts "RM-info: Running $create_metal_fill_cmd"
		eval $create_metal_fill_cmd
		save_block

		if {$ICV_IN_DESIGN_DRC} {
			set_app_options -name signoff.check_drc.run_dir -value $ICV_IN_DESIGN_POST_METAL_FILL_RUNDIR ;
			puts "RM-info: Running $signoff_check_drc_cmd"
			eval $signoff_check_drc_cmd

		} else {
			puts "RM-info: The signoff_check_drc command after signoff_create_metal_fill is skipped."
		}

		set_extraction_options -real_metalfill_extraction floating -virtual_metalfill_extraction none
  	}
	save_block
}
if { $ICV_IN_DESIGN_BASE_FILL } {

	if {$ICV_IN_DESIGN_BASE_FILL_RUNDIR != ""} {
		puts "RM-info: Setting signoff.create_metal_fill.run_dir to $ICV_IN_DESIGN_BASE_FILL_RUNDIR"
		set_app_options -name signoff.create_metal_fill.run_dir -value $ICV_IN_DESIGN_BASE_FILL_RUNDIR
	}

        if { $ICV_IN_DESIGN_BASE_FILL_FOUNDRY_NODE != "" } {
		set create_base_fill_cmd "signoff_create_metal_fill -mode add -foundry_fill_type feol -foundry_for_feol_fill $ICV_IN_DESIGN_BASE_FILL_FOUNDRY_NODE"
		puts "RM-info: Running $create_base_fill_cmd"
		eval $create_base_fill_cmd
        } else {
		set create_base_fill_cmd "signoff_create_metal_fill -mode add -all_runset_layers"
		puts "RM-info: Running $create_base_fill_cmd"
		eval $create_base_fill_cmd
        }
}

if {$DESIGN_STYLE == "hier"} {
        if {$USE_ABSTRACTS_FOR_POWER_ANALYSIS == "true"} {
                set_app_options -name abstract.annotate_power -value true
        }

        if {$USE_ABSTRACTS_FOR_SIGNAL_EM_ANALYSIS == "true"} {
                set_app_options -name abstract.enable_signal_em_analysis -value true
        }

        if { $PHYSICAL_HIERARCHY_LEVEL == "bottom" } {
                create_abstract -read_only
                create_frame -block_all true
                derive_hier_antenna_property -design ${DESIGN_NAME}/${ICV_IN_DESIGN_BLOCK_NAME}
                save_block ${DESIGN_NAME}/${ICV_IN_DESIGN_BLOCK_NAME}.frame
        } elseif { $PHYSICAL_HIERARCHY_LEVEL == "intermediate"} {
            if { $ABSTRACT_TYPE_FOR_MPH_BLOCKS == "nested"} {
                create_abstract -read_only
            } elseif { $ABSTRACT_TYPE_FOR_MPH_BLOCKS == "flattened"} {
                create_abstract -read_only -preserve_block_instances false
            }
            create_frame -block_all true 
            derive_hier_antenna_property -design ${DESIGN_NAME}/${ICV_IN_DESIGN_BLOCK_NAME}
	    save_block ${DESIGN_NAME}/${ICV_IN_DESIGN_BLOCK_NAME}.frame
        }
}

rm_source -file $TCL_USER_ICV_IN_DESIGN_POST_SCRIPT -optional -print "TCL_USER_ICV_IN_DESIGN_POST_SCRIPT"

if {$REPORT_QOR} {
        set REPORT_STAGE post_route
        set REPORT_ACTIVE_SCENARIOS $REPORT_ICV_IN_DESIGN_ACTIVE_SCENARIO_LIST
	if {$REPORT_PARALLEL_SUBMIT_COMMAND != ""} {
		rm_generate_variables_for_report_parallel -work_directory ${REPORTS_DIR}/${REPORT_PREFIX} -file_name rm_tcl_var.tcl

		report_parallel -work_directory ${REPORTS_DIR}/${REPORT_PREFIX} -submit_command ${REPORT_PARALLEL_SUBMIT_COMMAND} -max_cores ${REPORT_PARALLEL_MAX_CORES} -user_scripts [list "${REPORTS_DIR}/${REPORT_PREFIX}/rm_tcl_var.tcl" "[which report_qor.tcl]"]
	} else {
		rm_source -file report_qor.tcl
	}
}

redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/check_routes {check_routes}
redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/run_end.rpt {run_end}

write_qor_data -report_list "performance host_machine report_app_options" -label $REPORT_PREFIX -output $WRITE_QOR_DATA_DIR

report_msg -summary
print_message_info -ids * -summary
echo [date] > icv_in_design

exit 
