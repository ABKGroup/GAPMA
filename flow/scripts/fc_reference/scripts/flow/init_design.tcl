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
if {[file exists header_from_dprm.tcl]} {rm_source -file header_from_dprm.tcl}
if {$HPC_CORE != ""} {
	if {$DESIGN_STYLE == "hier"} {rm_source -file ./dp_override.tcl}
	rm_source -file ./rm_hpc_core_scripts/sidefile_setup_hpc_core.tcl
}
set CURRENT_STEP  $INIT_DESIGN_BLOCK_NAME
if { [info exists env(RM_VARFILE)] } {
	if { [file exists $env(RM_VARFILE)] } {
		rm_source -file $env(RM_VARFILE)
	} else {
		puts "RM-error: env(RM_VARFILE) specified but not found"
	}
}

set REPORT_PREFIX $CURRENT_STEP
file mkdir ${REPORTS_DIR}/${REPORT_PREFIX}
puts "RM-info: CURRENT_STEP  = $CURRENT_STEP"
puts "RM-info: REPORT_PREFIX = $REPORT_PREFIX"

redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/run_start.rpt {run_start}

rm_source -file $TCL_USER_LIBRARY_SETUP_SCRIPT -optional -print "TCL_USER_LIBRARY_SETUP_SCRIPT"

rm_source -file $TCL_USER_NON_PERSISTENT_SCRIPT -optional -print "TCL_USER_NON_PERSISTENT_SCRIPT"

rm_source -file $TCL_USER_INIT_DESIGN_PRE_SCRIPT -optional -print "TCL_USER_INIT_DESIGN_PRE_SCRIPT"

if {$INIT_DESIGN_INPUT == "NDM"} {
	if {[file exists $INIT_DESIGN_INPUT_LIBRARY] && $INIT_DESIGN_INPUT_BLOCK_NAME != ""} {
        	if {[file exists $DESIGN_LIBRARY]} {
			file delete -force $DESIGN_LIBRARY
		}
		open_lib -read ${INIT_DESIGN_INPUT_LIBRARY}
		copy_lib -from_lib ${INIT_DESIGN_INPUT_LIBRARY} -to_lib ${DESIGN_LIBRARY} -no_design
		copy_block -from ${INIT_DESIGN_INPUT_LIBRARY}:${INIT_DESIGN_INPUT_BLOCK_NAME} -to ${DESIGN_LIBRARY}:${DESIGN_NAME}/${INIT_DESIGN_BLOCK_NAME}
		close_lib ${INIT_DESIGN_INPUT_LIBRARY}
		current_lib ${DESIGN_LIBRARY}
		current_block ${DESIGN_NAME}/${INIT_DESIGN_BLOCK_NAME}
		
		if {$SET_QOR_STRATEGY_MODE == "early_design"} {
			set_early_data_check_policy -policy lenient -if_not_exist
		} elseif {$EARLY_DATA_CHECK_POLICY != "none"} {
			set_early_data_check_policy -policy $EARLY_DATA_CHECK_POLICY -if_not_exist
		}
		
		if {$DESIGN_STYLE == "hier" && $PHYSICAL_HIERARCHY_LEVEL != "bottom"} {
			if {$USE_ABSTRACTS_FOR_BLOCKS != ""} {
				if {$NETLIST2GDS_FLOW} {
					set label_name $BLOCK_ABSTRACT_FOR_PLACE_OPT
				} else {
					set label_name $BLOCK_ABSTRACT_FOR_COMPILE
				}
				set top_block [current_block]
				foreach BLOCK $SUB_BLOCK_REFS {
					if {[lsearch $SUB_BLOCK_LIBRARIES *${BLOCK}${LIBRARY_SUFFIX}] >= 0} {
						set library [lindex $SUB_BLOCK_LIBRARIES [lsearch $SUB_BLOCK_LIBRARIES *${BLOCK}${LIBRARY_SUFFIX}]]
						puts "RM-info: Swap abstract for $BLOCK to PNR block library and block label [lindex $label_name 0]."
						open_lib -read $library
						current_block $top_block
						change_abstract -lib [get_libs -explicit ${BLOCK}${LIBRARY_SUFFIX}] -references ${BLOCK} -label [lindex $label_name 0] -view [lindex $label_name 1] -update_ref_libs
						close_lib $library
						current_block $top_block
					} else {
						puts "RM-error: Library does not exist for ${BLOCK}${LIBRARY_SUFFIX}. Exiting"
						exit
					}
				}
				report_abstracts
			}

       			set_editability -blocks [get_blocks -hierarchical] -value false
        		report_editability -blocks [get_blocks -hierarchical]

			if {$USE_ABSTRACTS_FOR_BLOCKS != ""} {
              			set_timing_paths_disabled_blocks -all_sub_blocks
			}		
		}
	} else {
		puts "RM-error: INIT_DESIGN_INPUT is set to NDM but either INIT_DESIGN_INPUT_LIBRARY or INIT_DESIGN_INPUT_BLOCK_NAME is invalid. Please fix it before you continue."
		exit
	}
	if {$RESET_CHECK_STAGE_SETTINGS == "all"} {
	        reset_app_options compile*
	        reset_app_options place_opt*
		reset_app_options place.coarse*
	        reset_app_options refine*
	        reset_app_options clock_opt*
	        reset_app_options cts*
	        reset_app_options multibit*
	        reset_app_options extract*
	        reset_app_options time*
	        reset_app_options power*
	        reset_app_options opt*
	        reset_app_options route*
	        reset_app_options ccd*
	}
if {$TECHNOLOGY_NODE != "" && [get_attribute [current_block] technology_node -quiet] == ""} {
	redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/set_technology {set_technology -node $TECHNOLOGY_NODE -report_only}
	set_technology -node $TECHNOLOGY_NODE
}
} ;

if {[file exists $DESIGN_LIBRARY]} {
	if {$INIT_DESIGN_INPUT == "ASCII" && !$NETLIST2GDS_FLOW && [info exists INCREMENTAL_INIT_DESIGN] && [file exists $DESIGN_LIBRARY]} {
		open_lib ${DESIGN_LIBRARY}
	} elseif {$INIT_DESIGN_INPUT == "ASCII" || $INIT_DESIGN_INPUT == "DC_ASCII" || $INIT_DESIGN_INPUT == "RTL"} {
		file delete -force $DESIGN_LIBRARY
	}
}

if { $INIT_DESIGN_INPUT == "DC_ASCII" || \
    ($INIT_DESIGN_INPUT == "RTL" && $RTL_SOURCE_FORMAT != "elaborated_ndm") || \
    ($INIT_DESIGN_INPUT == "ASCII" && $NETLIST2GDS_FLOW) || \
    ($INIT_DESIGN_INPUT == "ASCII" && !$NETLIST2GDS_FLOW && [info exists INCREMENTAL_INIT_DESIGN] && ![file exists $DESIGN_LIBRARY]) } {
	set create_lib_cmd "create_lib $DESIGN_LIBRARY"
	if {[file exists [which $TECH_FILE]]} {
		lappend create_lib_cmd -tech $TECH_FILE ;
	} elseif {$TECH_LIB != ""} {
		lappend create_lib_cmd -use_technology_lib $TECH_LIB ;
	}
	if {$DESIGN_LIBRARY_SCALE_FACTOR != ""} {lappend create_lib_cmd -scale_factor $DESIGN_LIBRARY_SCALE_FACTOR}

        if {$PARASITIC_TECH_LIB != "" } {
		lappend create_lib_cmd -use_parasitic_tech_lib $PARASITIC_TECH_LIB ;
        }

	set rm_fusion_reference_library_list ""
	if {[file exists $FUSION_REFERENCE_LIBRARY_DIR]} {
		foreach lib [glob -type d $FUSION_REFERENCE_LIBRARY_DIR/*] {
			puts "RM-info: adding $lib to the reference library list"
			lappend rm_fusion_reference_library_list $lib	
		}
	} elseif {$FUSION_REFERENCE_LIBRARY_DIR != "" && [file exists create_fusion_reference_library]} {
		puts "RM-error: $FUSION_REFERENCE_LIBRARY_DIR is specified but not found, please correct it!"
	}

	if {$CLIB_REFERENCE_LIBRARY_CONFIGURATION_FLOW_FRAME_LIST != "" && $CLIB_REFERENCE_LIBRARY_CONFIGURATION_FLOW_DB_LIST != ""} {
		set link_library $CLIB_REFERENCE_LIBRARY_CONFIGURATION_FLOW_DB_LIST
	}

	lappend create_lib_cmd -ref_libs "\
	$rm_fusion_reference_library_list \
	$REFERENCE_LIBRARY \
	$CLIB_REFERENCE_LIBRARY_CONFIGURATION_FLOW_FRAME_LIST \
	$SUB_BLOCK_LIBRARIES \
	$PARASITIC_TECH_LIB"

	puts "RM-info: $create_lib_cmd"
	eval ${create_lib_cmd}
	redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_ref_libs {report_ref_libs}
}

if {$INIT_DESIGN_INPUT == "DC_ASCII"} {
	if {[file exists ${DCRM_RESULTS_DIR}/${DCRM_FINAL_DESIGN_ICC2}/${DESIGN_NAME}.icc2_script.tcl]} {
		puts "RM-info: Sourcing [which ${DCRM_RESULTS_DIR}/${DCRM_FINAL_DESIGN_ICC2}/${DESIGN_NAME}.icc2_script.tcl]"
		rm_source -file ${DCRM_RESULTS_DIR}/${DCRM_FINAL_DESIGN_ICC2}/${DESIGN_NAME}.icc2_script.tcl

		if {$SET_QOR_STRATEGY_MODE == "early_design"} {
			set_early_data_check_policy -policy lenient -if_not_exist
		} elseif {$EARLY_DATA_CHECK_POLICY != "none"} {
			set_early_data_check_policy -policy $EARLY_DATA_CHECK_POLICY -if_not_exist
		}

		puts "RM-info: Running commit_upf"
		commit_upf
	} else {
		puts "RM-error : ${DCRM_RESULTS_DIR}/${DCRM_FINAL_DESIGN_ICC2}/${DESIGN_NAME}.icc2_script.tcl is not found." 
		puts "RM-warning : ${DCRM_RESULTS_DIR}/${DCRM_FINAL_DESIGN_ICC2}/${DESIGN_NAME}.icc2_script.tcl is required for DC_ASCII flow." 
	}
} ;

if {$INIT_DESIGN_INPUT == "ASCII"} {
	if {$DESIGN_STYLE == "hier" && $PHYSICAL_HIERARCHY_LEVEL != "bottom"} {
		set_app_options -name file.verilog.default_user_label -value $INIT_DESIGN_BLOCK_NAME
	
		read_verilog -top ${DESIGN_NAME} $VERILOG_NETLIST_FILES
		current_block ${DESIGN_NAME}/${INIT_DESIGN_BLOCK_NAME}
		if {$SET_QOR_STRATEGY_MODE == "early_design"} {
			set_early_data_check_policy -policy lenient -if_not_exist
		} elseif {$EARLY_DATA_CHECK_POLICY != "none"} {
			set_early_data_check_policy -policy $EARLY_DATA_CHECK_POLICY -if_not_exist
		}
		link_block
		save_lib
	
		if {$USE_ABSTRACTS_FOR_BLOCKS != ""} {
	 		puts "RM-info: Swap abstracts to [lindex $BLOCK_ABSTRACT_FOR_PLACE_OPT 0] abstracts for all blocks."
	 		change_abstract -view [lindex $BLOCK_ABSTRACT_FOR_PLACE_OPT 1] -references $USE_ABSTRACTS_FOR_BLOCKS -label [lindex $BLOCK_ABSTRACT_FOR_PLACE_OPT 0]
	 		report_abstracts
		}
	} else {
                read_verilog -top $DESIGN_NAME $VERILOG_NETLIST_FILES
                current_block $DESIGN_NAME
		if {$SET_QOR_STRATEGY_MODE == "early_design"} {
			set_early_data_check_policy -policy lenient -if_not_exist
		} elseif {$EARLY_DATA_CHECK_POLICY != "none"} {
			set_early_data_check_policy -policy $EARLY_DATA_CHECK_POLICY -if_not_exist
		}
                link_block
                save_lib
	}

	if {[file exists [which $UPF_SUPPLEMENTAL_FILE]]} {set_app_options -name mv.upf.enable_golden_upf -value true}
	if {[file exists [which $UPF_FILE]]} {
		load_upf $UPF_FILE

		if {[file exists [which $UPF_SUPPLEMENTAL_FILE]]} { 
			load_upf -supplemental $UPF_SUPPLEMENTAL_FILE
		} elseif {$UPF_SUPPLEMENTAL_FILE != ""} {
			puts "RM-error: UPF_SUPPLEMENTAL_FILE($UPF_SUPPLEMENTAL_FILE) is invalid. Please correct it."
		}

		if {[file exists [which $UPF_UPDATE_SUPPLY_SET_FILE]]} {
			load_upf $UPF_UPDATE_SUPPLY_SET_FILE
		} elseif {$UPF_UPDATE_SUPPLY_SET_FILE != ""} {
			puts "RM-error: UPF_UPDATE_SUPPLY_SET_FILE($UPF_UPDATE_SUPPLY_SET_FILE) is invalid. Please correct it."
		}

		puts "RM-info: Running commit_upf"
		commit_upf
	} elseif {$UPF_FILE != ""} {
		puts "RM-error: UPF file($UPF_FILE) is invalid. Please correct it."
	}
} ;

if {$INIT_DESIGN_INPUT == "RTL"} {

	set_svf ${OUTPUTS_DIR}/${INIT_DESIGN_BLOCK_NAME}.svf

	set_app_options -name hdlin.naming.upf_compatible -value true
		
	rm_source -file $TCL_USER_READ_RTL_PRE_SCRIPT -optional -print "TCL_USER_READ_RTL_PRE_SCRIPT"
	
	switch ${RTL_SOURCE_FORMAT} {
	        sverilog {
	                analyze -format sverilog ${RTL_SOURCE_FILES}
	                elaborate ${DESIGN_NAME}

                	if {$DESIGN_STYLE == "hier" && $PHYSICAL_HIERARCHY_LEVEL != "bottom" && $BLOCK_ABSTRACT_FOR_COMPILE != ""} {
				set_label_switch_list  [lindex $BLOCK_ABSTRACT_FOR_COMPILE 0]
                	}
                	set_top_module ${DESIGN_NAME}
        	}
        	verilog {
        	        analyze -format verilog ${RTL_SOURCE_FILES}
        	        elaborate ${DESIGN_NAME}

                	if {$DESIGN_STYLE == "hier" && $PHYSICAL_HIERARCHY_LEVEL != "bottom" && $BLOCK_ABSTRACT_FOR_COMPILE != ""} {
				set_label_switch_list  [lindex $BLOCK_ABSTRACT_FOR_COMPILE 0]
                	}
                	set_top_module ${DESIGN_NAME}
        	}
        	vhdl {
                	analyze -format vhdl ${RTL_SOURCE_FILES}
                	elaborate ${DESIGN_NAME}

                	if {$DESIGN_STYLE == "hier" && $PHYSICAL_HIERARCHY_LEVEL != "bottom" && $BLOCK_ABSTRACT_FOR_COMPILE != ""} {
				set_label_switch_list  [lindex $BLOCK_ABSTRACT_FOR_COMPILE 0]
                	}
                	set_top_module ${DESIGN_NAME}
        	}
        	script {
			if {![rm_source -file $FC_RTL_READ_SCRIPT]} {
				exit
			}
        	}
        	elaborated_ndm {
			if {[file exists $INIT_DESIGN_INPUT_LIBRARY] && $INIT_DESIGN_INPUT_BLOCK_NAME != ""} {
				open_lib -read ${INIT_DESIGN_INPUT_LIBRARY}
				copy_lib -from_lib ${INIT_DESIGN_INPUT_LIBRARY} -to_lib ${DESIGN_LIBRARY} -no_design
				copy_block -from ${INIT_DESIGN_INPUT_LIBRARY}:${INIT_DESIGN_INPUT_BLOCK_NAME} -to ${DESIGN_LIBRARY}:${DESIGN_NAME}/${INIT_DESIGN_BLOCK_NAME}
				close_lib ${INIT_DESIGN_INPUT_LIBRARY}
				current_lib ${DESIGN_LIBRARY}
				current_block ${DESIGN_NAME}/${INIT_DESIGN_BLOCK_NAME}
			} else {
				puts "RM-error: RTL_SOURCE_FORMAT is set to elaborated_ndm but either INIT_DESIGN_INPUT_LIBRARY or INIT_DESIGN_INPUT_BLOCK_NAME is invalid. Please fix it before you continue."
				exit
			}
        	}
        	default {
        	        puts "RM-error: Unknown RTL_SOURCE_FORMAT(${RTL_SOURCE_FORMAT})"
        	        exit 
        	}
	} ;

	if {$SET_QOR_STRATEGY_MODE == "early_design"} {
		set_early_data_check_policy -policy lenient -if_not_exist
	} elseif {$EARLY_DATA_CHECK_POLICY != "none"} {
		set_early_data_check_policy -policy $EARLY_DATA_CHECK_POLICY -if_not_exist
	}

	save_block -as ${DESIGN_NAME}/${INIT_DESIGN_BLOCK_NAME}_elaborated

	rm_source -file $TCL_USER_READ_RTL_POST_SCRIPT -optional -print "TCL_USER_READ_RTL_POST_SCRIPT"

	set_app_option -name design.uniquify_naming_style -value ${DESIGN_NAME}_%s_%d
	set uniquify_cmd "uniquify $UNIQUIFY_OPTIONS"
	puts "RM-info: Uniquify the Design: $uniquify_cmd"
	eval ${uniquify_cmd}

	redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/check_design.design_mismatch {check_design -ems_database check_design.design_mismatch.ems -checks design_mismatch}
	redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_design_mismatch {report_design_mismatch -verbose}
	redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_unbound {report_unbound}

	saif_map -start
	
	rm_source -file $DFT_PORTS_FILE -optional -print "DFT_PORTS_FILE"
        if {$HPC_CORE != ""} {rm_source -file $HPC_EMA_RAM_PINS_SCRIPT -optional -print "HPC_EMA_RAM_PINS_SCRIPT"}
	rm_source -file $TCL_USER_CREATE_DFT_PORTS_POST_SCRIPT -optional -print "TCL_USER_CREATE_DFT_PORTS_POST_SCRIPT"
	
	if {$UPF_MODE == "golden"} {set_app_options -name mv.upf.enable_golden_upf -value true}
	if {$UPF_MODE != "none"} {
		if {[file exists [which $UPF_FILE]]} {
	      		load_upf $UPF_FILE
			if {[file exists [which $UPF_UPDATE_SUPPLY_SET_FILE]]} {
			      load_upf $UPF_UPDATE_SUPPLY_SET_FILE
			} elseif {$UPF_UPDATE_SUPPLY_SET_FILE != ""} {
			      puts "RM-error: UPF_UPDATE_SUPPLY_SET_FILE($UPF_UPDATE_SUPPLY_SET_FILE) is invalid. Please correct it."
			}
			puts "RM-info: Running commit_upf"
	      		commit_upf
		} elseif {$UPF_FILE != ""} {
	      		puts "RM-error: UPF file($UPF_FILE) is invalid. Please correct it."
		}
	}
} ;

if {$INIT_DESIGN_INPUT == "ASCII" || $INIT_DESIGN_INPUT == "RTL"} {

        if {$TECHNOLOGY_NODE != "" && !$SET_TECHNOLOGY_AFTER_FLOORPLAN} {
		redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/set_technology {set_technology -node $TECHNOLOGY_NODE -report_only}
                set_technology -node $TECHNOLOGY_NODE
        }

	if {$TCL_FLOORPLAN_FILE != ""} {
	        rm_source -file $TCL_FLOORPLAN_FILE
	} elseif {$DEF_FLOORPLAN_FILES != ""} {
	        set RM_DEF_FLOORPLAN_FILE_is_not_found FALSE
	        foreach def_file $DEF_FLOORPLAN_FILES {
	                if {![file exists [which $def_file]]} {
	                        puts "RM-error: DEF floorplan file ($def_file) is invalid."
	                        set RM_DEF_FLOORPLAN_FILE_is_not_found TRUE
	                }       
	        }       
	        if {!$RM_DEF_FLOORPLAN_FILE_is_not_found} {
	                set read_def_cmd "read_def $DEF_READ_OPTIONS [list $DEF_FLOORPLAN_FILES]"
	                puts "RM-info: Creating floorplan from DEF file DEF_FLOORPLAN_FILES ($DEF_FLOORPLAN_FILES)"
	                puts "RM-info: $read_def_cmd"
                  eval ${read_def_cmd}
	
			if {$DEF_RESOLVE_PG_NETS} {
				redirect -var x {catch {resolve_pg_nets}} ;
				puts $x
				if {[regexp ".*NDMUI-096.*" $x]} {
					puts "RM-error: UPF may have an issue. Please review and correct it."
				}
			}
	        } else {
	                puts "RM-error: At least one of the DEF_FLOORPLAN_FILES specified is invalid. Please correct it."
	                puts "RM-info: Skipped reading of DEF_FLOORPLAN_FILES"
	        }
	}

	if {[rm_source -file $SWITCH_CONNECTIVITY_FILE -optional -print "SWITCH_CONNECTIVITY_FILE"]} {
	        associate_mv_cell -power_switches
	}

	if {$INIT_DESIGN_INPUT != "RTL"} {
		if {[file exists [which $DEF_SCAN_FILE]]} {
		        read_def $DEF_SCAN_FILE
		} elseif {$DEF_SCAN_FILE != ""} {
		        puts "RM-error: DEF_SCAN_FILE($DEF_SCAN_FILE) is invalid. Please correct it."
		}
	}
} ;

rm_source -file $TCL_ADDITIONAL_FLOORPLAN_FILE -optional -print "TCL_ADDITIONAL_FLOORPLAN_FILE"

if {$TECHNOLOGY_NODE != "" && ($SET_TECHNOLOGY_AFTER_FLOORPLAN || [get_attribute [current_block] technology_node -quiet] == "")} {
	redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/set_technology {set_technology -node $TECHNOLOGY_NODE -report_only}
	set_technology -node $TECHNOLOGY_NODE
}
rm_source -file $SIDEFILE_INIT_DESIGN -optional -print "SIDEFILE_INIT_DESIGN"

if {$ENABLE_FUSA} {
	rm_source -file fusa_setup.tcl 
}

if {$TECH_FILE != "" || ($TECH_LIB != "" && !$TECH_LIB_INCLUDES_TECH_SETUP_INFO)} {
	rm_source -file $TCL_TECH_SETUP_FILE -optional -print "TCL_TECH_SETUP_FILE"
}

if {[file exists [which $PHYSICAL_RULES_FILE]]} {
	read_physical_rules $PHYSICAL_RULES_FILE
}

if {![rm_source -file $TCL_USER_CONNECT_PG_NET_SCRIPT -optional -print "TCL_USER_CONNECT_PG_NET_SCRIPT"]} {
	connect_pg_net
}

rm_source -file $TCL_VIA_LADDER_DEFINITION_FILE -optional -print "TCL_VIA_LADDER_DEFINITION_FILE"

rm_source -file $TCL_SET_VIA_LADDER_CANDIDATE_FILE -optional -print "TCL_SET_VIA_LADDER_CANDIDATE_FILE"

set RM_FAILURE 0
if {[info exists rm_fp_pnr_flow]} {
	puts "RM-info: Skipping checks as the floorplan is created later in the flow and checks are performed there.  See Makefile_fp_pnr for details."
} elseif {$ENABLE_FLOORPLAN_CHECKS} {
	set RM_FAILURE [rm_check_design -step init_design] 

	rm_source -file $TCL_FLOORPLAN_RULE_SCRIPT -optional -print "TCL_FLOORPLAN_RULE_SCRIPT"
	redirect -var x {catch {report_floorplan_rules}}
	if {![regexp "^.*No floorplan rules exist" $x]} {
		redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/check_floorplan_rules.rpt {check_floorplan_rules}
	}
}

if {$PARASITIC_TECH_LIB == "" } {
	rm_source -file $TCL_PARASITIC_SETUP_FILE -optional -print "TCL_PARASITIC_SETUP_FILE"
}

rm_source -file $TCL_MCMM_SETUP_FILE -optional -print "TCL_MCMM_SETUP_FILE"

rm_source -file $TCL_CONSTRAINTS_SETUP_FILE -optional -print "TCL_CONSTRAINTS_SETUP_FILE"

if {[rm_source -file $TCL_POCV_SETUP_FILE -optional -print "TCL_POCV_SETUP_FILE"]} {
	set_app_options -name time.pocvm_enable_analysis -value true ;
	reset_app_options time.aocvm_enable_analysis ;
}

if {![get_app_option_value -name time.pocvm_enable_analysis] && $TCL_POCV_SETUP_FILE == ""} {
	rm_source -file $TCL_AOCV_SETUP_FILE -optional -print "TCL_AOCV_SETUP_FILE"
}

if {$TCL_PLACEMENT_CONSTRAINT_FILE_LIST != ""} {
	foreach file $TCL_PLACEMENT_CONSTRAINT_FILE_LIST {
		rm_source -file $file
	}
}

if {$INIT_DESIGN_INPUT != "RTL"} {
	set cur_mode [current_mode]
	foreach_in_collection mode [all_modes] {
		current_mode $mode
	        remove_propagated_clocks [all_clocks]
		remove_propagated_clocks [get_ports]
		remove_propagated_clocks [get_pins -hierarchical]
	}
	current_mode $cur_mode
}

rm_source -file $TCL_CTS_NDR_RULE_FILE -optional -print "TCL_CTS_NDR_RULE_FILE"
redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_routing_rules {report_routing_rules -verbose}
redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_clock_routing_rules {report_clock_routing_rules}
redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_clock_settings {report_clock_settings}

rm_source -file $TCL_LIB_CELL_PURPOSE_FILE -optional -print "TCL_LIB_CELL_PURPOSE_FILE"

if {$SAIF_FILE_LIST != ""} {
	if {$SAIF_FILE_POWER_SCENARIO != ""} {
		set read_saif_cmd "read_saif \"$SAIF_FILE_LIST\" -scenarios \"$SAIF_FILE_POWER_SCENARIO\""
	} else {
		set read_saif_cmd "read_saif \"$SAIF_FILE_LIST\""
	}
	if {$SAIF_FILE_SOURCE_INSTANCE != ""} {lappend read_saif_cmd -strip_path $SAIF_FILE_SOURCE_INSTANCE}
	if {$SAIF_FILE_TARGET_INSTANCE != ""} {lappend read_saif_cmd -path $SAIF_FILE_TARGET_INSTANCE}
	puts "RM-info: Running $read_saif_cmd"
    	eval ${read_saif_cmd}
	if {$INIT_DESIGN_INPUT == "RTL"} {
		if {$SAIF_FILE_POWER_SCENARIO != ""} {
			reset_switching_activity -non_essential -scenarios $SAIF_FILE_POWER_SCENARIO
		} else {
			reset_switching_activity -non_essential
		}
	}
	if {$SAIF_FILE_POWER_SCENARIO != ""} {
		redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_activity {report_activity -driver -scenarios $SAIF_FILE_POWER_SCENARIO}
	} else {
		redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_activity {report_activity -driver}
	}
}

if {$SET_QOR_STRATEGY_METRIC == "total_power"} {
	foreach sce [get_object_name [get_scenarios -filter "dynamic_power"]] {
		puts "RM-info: Checking for simulated activities in the design for scenario $sce."
		report_activity -driver -scenario $sce > report_activity.tmp.rpt
		set line [sh grep ^simulated report_activity.tmp.rpt] 
		lappend table [list {*}[string map {( { } ) { } % { }} $line]]
		set total_simulated_perc [lindex [lindex $table 0] end]
		if {[string trim $total_simulated_perc] == 0} {
			puts "RM-info: There are no simulated activity in the design. Running infer_switching_activity"
			infer_switching_activity -apply -sci_based all -scenario $sce
		} else {
			puts "RM-info: Simulated activities found in the design. Will not run infer_switching_activity."
		}
		sh rm -rf report_activity.tmp.rpt
	}
}

rm_source -file $TCL_USER_INIT_DESIGN_POST_SCRIPT -optional -print "TCL_USER_INIT_DESIGN_POST_SCRIPT"

if {$UPF_MODE == "golden"} {
	save_upf ${OUTPUTS_DIR}/${INIT_DESIGN_BLOCK_NAME}.supplemental.upf
} else {
	save_upf ${OUTPUTS_DIR}/${INIT_DESIGN_BLOCK_NAME}.save_upf
}

set duplicate_shapes [check_duplicates -return_as_collection]
if {[sizeof_collection $duplicate_shapes] > 0} {
   remove_shapes -force $duplicate_shapes
}

save_block
save_block -as ${DESIGN_NAME}/${INIT_DESIGN_BLOCK_NAME}

if {$REPORT_QOR} {
	set REPORT_STAGE init_design
	set REPORT_ACTIVE_SCENARIOS $REPORT_INIT_DESIGN_ACTIVE_SCENARIO_LIST
	if {$REPORT_PARALLEL_SUBMIT_COMMAND != ""} {
		rm_generate_variables_for_report_parallel -work_directory ${REPORTS_DIR}/${REPORT_PREFIX} -file_name rm_tcl_var.tcl

		report_parallel -work_directory ${REPORTS_DIR}/${REPORT_PREFIX} -submit_command ${REPORT_PARALLEL_SUBMIT_COMMAND} -max_cores ${REPORT_PARALLEL_MAX_CORES} -user_scripts [list "${REPORTS_DIR}/${REPORT_PREFIX}/rm_tcl_var.tcl" "[which report_qor.tcl]"]
	} else {
		rm_source -file report_qor.tcl
	}
	write_tech_file ${REPORTS_DIR}/${REPORT_PREFIX}/tech_file.dump
}

redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/run_end.rpt {run_end}

write_qor_data -report_list "performance host_machine report_app_options" -label $REPORT_PREFIX -output $WRITE_QOR_DATA_DIR 

report_msg -summary
print_message_info -ids * -summary
if {[info exists INCREMENTAL_INIT_DESIGN]} {
       rm_logparse $LOGS_DIR/incremental_init_design.log
} else {
       rm_logparse $LOGS_DIR/init_design.log
}

if {[info exists INCREMENTAL_INIT_DESIGN] && !$RM_FAILURE} {
	echo [date] > incremental_init_design
} elseif {![info exists INCREMENTAL_INIT_DESIGN] && !$RM_FAILURE} {
	echo [date] > init_design
} else {
	puts "RM-info: init_design touch file was not created due to potential issues found in \"Basic floorplan and design checks\" section. Please check RM-error messages in the log."
}
exit
