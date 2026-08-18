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
	if {$DESIGN_STYLE == "hier"} {rm_source -file ./block_override.tcl}
	rm_source -file ./rm_hpc_core_scripts/sidefile_setup_hpc_core.tcl
}
set PREVIOUS_STEP $CHIP_FINISH_FROM_BLOCK_NAME
set CURRENT_STEP  $CHIP_FINISH_BLOCK_NAME
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
copy_block -from ${DESIGN_NAME}/${PREVIOUS_STEP} -to ${DESIGN_NAME}/${CURRENT_STEP}
current_block ${DESIGN_NAME}/${CURRENT_STEP}
link_block

if {$DESIGN_STYLE == "hier"} {
   if {$USE_ABSTRACTS_FOR_BLOCKS != "" && ($BLOCK_ABSTRACT_FOR_CHIP_FINISH != $BLOCK_ABSTRACT_FOR_ROUTE_OPT)} {
      puts "RM-info: Swapping from [lindex $BLOCK_ABSTRACT_FOR_ROUTE_OPT 0] to [lindex $BLOCK_ABSTRACT_FOR_CHIP_FINISH 0] abstracts for all blocks."
      change_abstract -references $USE_ABSTRACTS_FOR_BLOCKS -label [lindex $BLOCK_ABSTRACT_FOR_CHIP_FINISH 0] -view [lindex $BLOCK_ABSTRACT_FOR_CHIP_FINISH 1]
      report_abstracts
   }
}

if {$CHIP_FINISH_ACTIVE_SCENARIO_LIST != ""} {
	set_scenario_status -active false [get_scenarios -filter active]
	set_scenario_status -active true $CHIP_FINISH_ACTIVE_SCENARIO_LIST
}

rm_source -file $TCL_MODE_CORNER_SCENARIO_MODEL_ADJUSTMENT_FILE -optional -print "TCL_MODE_CORNER_SCENARIO_MODEL_ADJUSTMENT_FILE"

rm_source -file $TCL_USER_NON_PERSISTENT_SCRIPT -optional -print "TCL_USER_NON_PERSISTENT_SCRIPT"

set_app_options -name route.detail.eco_route_use_soft_spacing_for_timing_optimization -value false

rm_source -file $TCL_USER_CHIP_FINISH_PRE_SCRIPT -optional -print "TCL_USER_CHIP_FINISH_PRE_SCRIPT"

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

if { [info exists "rm_dp_flow"] && [regexp {h$} [get_attribute -quiet [current_design] rm_lib_type]] } {
	rm_legalize_placement
}

rm_source -file $SIDEFILE_CHIP_FINISH_1 -optional -print "SIDEFILE_CHIP_FINISH_1"

	if {[file exists [which $CHIP_FINISH_SIGNAL_EM_CONSTRAINT_FILE]]} {
		set read_signal_em_constraints_cmd "read_signal_em_constraints $CHIP_FINISH_SIGNAL_EM_CONSTRAINT_FILE"
		switch -regexp $CHIP_FINISH_SIGNAL_EM_CONSTRAINT_FORMAT {
			"ITF"      {lappend read_signal_em_constraints_cmd -format ITF}
			"ALF"      {lappend read_signal_em_constraints_cmd -format ALF}
		}
		puts "RM-info: $read_signal_em_constraints_cmd"
		eval $read_signal_em_constraints_cmd
  
		if {[file exists [which $CHIP_FINISH_SIGNAL_EM_SAIF]]} {
			read_saif $CHIP_FINISH_SIGNAL_EM_SAIF
		} elseif {$CHIP_FINISH_SIGNAL_EM_SAIF != ""} {
			puts "RM-error: CHIP_FINISH_SIGNAL_EM_SAIF($CHIP_FINISH_SIGNAL_EM_SAIF) is invalid. Please correct it."
		}

		if {$CHIP_FINISH_SIGNAL_EM_SCENARIO != ""} {
			if {[regexp $CHIP_FINISH_SIGNAL_EM_SCENARIO [get_object_name [get_scenarios -filter "setup&&hold&&active"]]]} {
				if {![get_app_option_value -name time.si_enable_analysis]} {
					set RM_current_value_enable_si false
					set_app_options -name time.si_enable_analysis -value true
				}

				set RM_current_value_scenario [current_scenario]
				current_scenario $CHIP_FINISH_SIGNAL_EM_SCENARIO
				redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_signal_em {report_signal_em -violated}

				if {$CHIP_FINISH_SIGNAL_EM_FIXING} {
					fix_signal_em
					redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_signal_em.post {report_signal_em -violated}
				}
				current_scenario ${RM_current_value_scenario}

				if {[info exists RM_current_value_enable_si] && !${RM_current_value_enable_si}} {
					set_app_options -name time.si_enable_analysis -value false
				}
			} else {
				puts "RM-error: CHIP_FINISH_SIGNAL_EM_SCENARIO must be active and enabled for setup and hold. Signal EM analysis and fixing are skipped."
			}
		} else {
			puts "RM-error: CHIP_FINISH_SIGNAL_EM_SCENARIO is not specified. Signal EM analysis and fixing are skipped."
		}
	} elseif {$CHIP_FINISH_SIGNAL_EM_CONSTRAINT_FILE != ""} {
		puts "RM-error: CHIP_FINISH_SIGNAL_EM_CONSTRAINT_FILE($CHIP_FINISH_SIGNAL_EM_CONSTRAINT_FILE) is invalid. Please correct it."
	}

rm_source -file $SIDEFILE_CHIP_FINISH_2 -optional -print "SIDEFILE_CHIP_FINISH_2"

rm_source -file $TCL_USER_CHIP_FINISH_POST_SCRIPT -optional -print "TCL_USER_CHIP_FINISH_POST_SCRIPT"

if {![rm_source -file $TCL_USER_CONNECT_PG_NET_SCRIPT -optional -print "TCL_USER_CONNECT_PG_NET_SCRIPT"]} {
	connect_pg_net
}

redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/check_routes {check_routes}

save_block

if {$HPC_CORE != ""} {set_scenario_status [get_scenarios $ALL_SCENARIOS] -active true}
if {$DESIGN_STYLE == "hier" && $PHYSICAL_HIERARCHY_LEVEL != "top" && !$SKIP_ABSTRACT_GENERATION} {
        if {$USE_ABSTRACTS_FOR_POWER_ANALYSIS == "true"} {
                if {$HPC_CORE != ""} {rm_source -file $HPC_REPORT_QOR_POWER -optional -print "HPC_REPORT_QOR_POWER"}
                set_app_options -name abstract.annotate_power -value true
        }
        if {$USE_ABSTRACTS_FOR_SIGNAL_EM_ANALYSIS == "true"} {
                set_app_options -name abstract.enable_signal_em_analysis -value true
        }
        if { $PHYSICAL_HIERARCHY_LEVEL == "bottom" } {
                create_abstract -read_only
                create_frame -block_all true
                derive_hier_antenna_property -design ${DESIGN_NAME}/${CHIP_FINISH_BLOCK_NAME}
                save_block ${DESIGN_NAME}/${CHIP_FINISH_BLOCK_NAME}.frame

        } elseif { $PHYSICAL_HIERARCHY_LEVEL == "intermediate"} {
            if { $ABSTRACT_TYPE_FOR_MPH_BLOCKS == "nested"} {
                create_abstract -read_only
            } elseif { $ABSTRACT_TYPE_FOR_MPH_BLOCKS == "flattened"} {
                create_abstract -read_only -preserve_block_instances false
            }
            create_frame -block_all true
            derive_hier_antenna_property -design ${DESIGN_NAME}/${CHIP_FINISH_BLOCK_NAME}
	    save_block ${DESIGN_NAME}/${CHIP_FINISH_BLOCK_NAME}.frame
        }
}

if {$REPORT_QOR} {
	set REPORT_STAGE post_route
        set REPORT_ACTIVE_SCENARIOS $REPORT_CHIP_FINISH_ACTIVE_SCENARIO_LIST
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
rm_logparse $LOGS_DIR/chip_finish.log
echo [date] > chip_finish

exit 
