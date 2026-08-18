# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.
# Copyright (C) 2014-2023 Synopsys, Inc. All rights reserved.

set DESIGN_NAME 		"gcd" ;
				   ;
set LIBRARY_SUFFIX		"" ;
set DESIGN_LIBRARY 		"${DESIGN_NAME}${LIBRARY_SUFFIX}" ;
				   ;

set TECHLIB_DATA_DIR		"./data" ;
                                   ;

set SUPPLEMENTAL_SEARCH_PATH		"./data" ;

set INIT_DESIGN_BLOCK_NAME		"init_design"			;
set COMPILE_BLOCK_NAME                  "compile"                       ;
set PLACE_OPT_BLOCK_NAME 		"place_opt" 			;
set CLOCK_OPT_CTS_BLOCK_NAME 		"clock_opt_cts" 		;
set CLOCK_OPT_OPTO_BLOCK_NAME 		"clock_opt_opto" 		;
set ROUTE_AUTO_BLOCK_NAME 		"route_auto" 			;
set ROUTE_OPT_BLOCK_NAME 		"route_opt" 			;

set CHIP_FINISH_FROM_BLOCK_NAME		$ROUTE_OPT_BLOCK_NAME		;
set CHIP_FINISH_BLOCK_NAME 		"chip_finish" 			;
set ICV_IN_DESIGN_FROM_BLOCK_NAME	$CHIP_FINISH_BLOCK_NAME 	;
set ICV_IN_DESIGN_BLOCK_NAME		"icv_in_design" 		;

set WRITE_DATA_FROM_BLOCK_NAME 		$ICV_IN_DESIGN_BLOCK_NAME 	;
set WRITE_DATA_BLOCK_NAME 		"write_data" 			;

set ENDPOINT_OPT_FROM_BLOCK_NAME	$ROUTE_OPT_BLOCK_NAME		;
set ENDPOINT_OPT_BLOCK_NAME		"endpoint_opt"			;
set TIMING_ECO_FROM_BLOCK_NAME		$ICV_IN_DESIGN_BLOCK_NAME	;
set TIMING_ECO_BLOCK_NAME		"timing_eco" 			;
set FUNCTIONAL_ECO_FROM_BLOCK_NAME	$ICV_IN_DESIGN_BLOCK_NAME	;
set FUNCTIONAL_ECO_BLOCK_NAME		"functional_eco"		;

set DESIGN_STYLE			"flat"	;
					;
					;
					;
					;
					;

set PHYSICAL_HIERARCHY_LEVEL		"bottom" ;
					;
					;

set SUB_BLOCK_REFS                   	[list ] ;
                                                ;
                                                ;
set SUB_BLOCK_LIBRARIES			[list ] ;
						;
set USE_ABSTRACTS_FOR_BLOCKS        	[list ] ;
set CTL_FOR_ABSTRACT_BLOCKS		[list ] ;

set SKIP_ABSTRACT_GENERATION            false	;

set BLOCK_ABSTRACT_FOR_COMPILE          "$ICV_IN_DESIGN_BLOCK_NAME abstract" ;
set BLOCK_ABSTRACT_FOR_PLACE_OPT 	"$ICV_IN_DESIGN_BLOCK_NAME abstract" ;
set BLOCK_ABSTRACT_FOR_CLOCK_OPT_CTS    "$ICV_IN_DESIGN_BLOCK_NAME abstract" ;
set BLOCK_ABSTRACT_FOR_CLOCK_OPT_OPTO   "$ICV_IN_DESIGN_BLOCK_NAME abstract" ;
set BLOCK_ABSTRACT_FOR_ROUTE_AUTO       "$ICV_IN_DESIGN_BLOCK_NAME abstract" ;
set BLOCK_ABSTRACT_FOR_ROUTE_OPT        "$ICV_IN_DESIGN_BLOCK_NAME abstract" ;
set BLOCK_ABSTRACT_FOR_CHIP_FINISH      "$ICV_IN_DESIGN_BLOCK_NAME abstract" ;
set BLOCK_ABSTRACT_FOR_ICV_IN_DESIGN    "$ICV_IN_DESIGN_BLOCK_NAME abstract" ;

set USE_ABSTRACTS_FOR_POWER_ANALYSIS 	false ;
                                       	;
                                       	;

set USE_ABSTRACTS_FOR_SIGNAL_EM_ANALYSIS false ;
					;
					;

set ABSTRACT_TYPE_FOR_MPH_BLOCKS "flattened" ;
					;
					;
					;

set CHECK_HIER_TIMING_CONSTRAINTS_CONSISTENCY true ;
					;

set PROMOTE_CLOCK_BALANCE_POINTS	false ;
					;
					;
					;

set PROMOTE_ABSTRACT_CLOCK_DATA_FILE 	"promote_abstract_clock_data_script.tcl" ;

set WRITE_GDS_ALLOW_INCOMPATIBLE_UNITS 	false ;

set NETLIST2GDS_FLOW		false ;
				;
set INIT_DESIGN_INPUT 		"RTL" ;
 				;
				;
				;
				;
				;
				;
				;
			      	;
				;
			      	;
				;
				;
				;
set INIT_DESIGN_INPUT_LIBRARY 	"" ;
set INIT_DESIGN_INPUT_BLOCK_NAME "" ;
				;
set RTL_SOURCE_FORMAT		sverilog ;
				;
				;
				;
				;
set UNIQUIFY_OPTIONS		"-force" ;
set EARLY_DATA_CHECK_POLICY	"none" ;
				;
				;

if {[file exists ./configs/pdk_resolved.tcl]} {
    source ./configs/pdk_resolved.tcl
    set DESIGN_LIBRARY "${DESIGN_NAME}${LIBRARY_SUFFIX}"
}

set_user_units -input  -type time        -value 1ns
set_user_units -input  -type capacitance -value 1pF
set_user_units -output -type time        -value 1ns
set_user_units -output -type capacitance -value 1pF

set ENABLE_FLOORPLAN_CHECKS      false
set TCL_LIB_CELL_DONT_USE_FILE   "./filters/dont_use.tcl"
set DFT_PORTS_FILE               ""
set DFT_SETUP_FILE               ""

if {![info exists REFERENCE_LIBRARY]} {
    set REFERENCE_LIBRARY           [list] ;
                                           ;
}

if {![info exists CLIB_REFERENCE_LIBRARY_CONFIGURATION_FLOW_FRAME_LIST]} {
    set CLIB_REFERENCE_LIBRARY_CONFIGURATION_FLOW_FRAME_LIST [list] ;
}
if {![info exists CLIB_REFERENCE_LIBRARY_CONFIGURATION_FLOW_DB_LIST]} {
    set CLIB_REFERENCE_LIBRARY_CONFIGURATION_FLOW_DB_LIST [list]    ;
}

if {![info exists FUSION_REFERENCE_LIBRARY_FRAM_LIST]} {
    set FUSION_REFERENCE_LIBRARY_FRAM_LIST  [list] ;
}
if {![info exists FUSION_REFERENCE_LIBRARY_DB_LIST]} {
    set FUSION_REFERENCE_LIBRARY_DB_LIST    [list] ;
}
set FUSION_REFERENCE_LIBRARY_DIR    "./local_fusion_library" ;

if {![info exists LINK_LIBRARY]} {
    set LINK_LIBRARY                "" ;
}

set TCL_MULTI_VT_CONSTRAINT_FILE	"multi_vth_constraint_script.tcl" ;
set TCL_LIB_CELL_PURPOSE_FILE 		"set_lib_cell_purpose.tcl" ;
					;
					;
					;
					;

set TIE_LIB_CELL_PATTERN_LIST 		"" ;
					;
set HOLD_FIX_LIB_CELL_PATTERN_LIST 	"" ;
set CTS_LIB_CELL_PATTERN_LIST 		"" ;
					;
					;
					;
				   	;
set CTS_ONLY_LIB_CELL_PATTERN_LIST 	"" ;
					;
					;
					;

if {![info exists TECH_FILE]} {
    set TECH_FILE           "" ;
}
                                                ;
                                                ;
set TECH_LIB			""	;
                        		;
set PARASITIC_TECH_LIB		""	;
                                        ;
set TECH_LIB_INCLUDES_TECH_SETUP_INFO true ;
					;
					;
set TCL_TECH_SETUP_FILE		"init_design.tech_setup.tcl" ;
					;
					;
					;
set PHYSICAL_RULES_FILE 	""	;
set DESIGN_LIBRARY_SCALE_FACTOR	""	;
					;
set TCL_USER_LIBRARY_SETUP_SCRIPT    "" ;
					;
set ENABLE_REDUNDANT_VIA_INSERTION	false ;
set ENABLE_POST_ROUTE_OPT_REDUNDANT_VIA_INSERTION	false ;
set TCL_USER_REDUNDANT_VIA_MAPPING_FILE "" ;
					;
set TCL_USER_REDUNDANT_VIA_SCRIPT	"" ;

set ENABLE_PERFORMANCE_VIA_LADDER	false ;
					;
					;
					;
					;

set TCL_ANTENNA_RULE_FILE		"" ;

set TCL_MCMM_SETUP_FILE "[file dirname [file normalize [info script]]]/mcmm_setup_simple.tcl" ;
					;
					;
					;
					;
					;
					;
					;
if {![info exists TCL_PARASITIC_SETUP_FILE]} {
    set TCL_PARASITIC_SETUP_FILE "examples/TCL_PARASITIC_SETUP_FILE.tcl"
}
                                                ;
					;

set TCL_MODE_CORNER_SCENARIO_MODEL_ADJUSTMENT_FILE "" ;
					;

set TCL_POCV_SETUP_FILE			"" ;
					;
set TCL_AOCV_SETUP_FILE			"" ;
					;
set TCL_PVT_CONFIGURATION_FILE		"" ;
					;

set PREROUTE_CTS_PRIMARY_CORNER		"" ;
					;
					;
					;
set COMPILE_ACTIVE_SCENARIO_LIST	"" ;
					;
					;
set CLOCK_OPT_CTS_ACTIVE_SCENARIO_LIST  "" ;
					;
set ROUTE_OPT_ACTIVE_SCENARIO_LIST 	"" ;
					;
set CLOCK_OPT_OPTO_ACTIVE_SCENARIO_LIST "$ROUTE_OPT_ACTIVE_SCENARIO_LIST" ;
					;
					;
set ROUTE_AUTO_ACTIVE_SCENARIO_LIST 	"$ROUTE_OPT_ACTIVE_SCENARIO_LIST" ;
					;
					;
set CHIP_FINISH_ACTIVE_SCENARIO_LIST 	"" ;
					;
set ICV_IN_DESIGN_ACTIVE_SCENARIO_LIST 	"" ;
					;
set ENDPOINT_OPT_ACTIVE_SCENARIO_LIST 	"" ;
					;
set TIMING_ECO_ACTIVE_SCENARIO_LIST 	"" ;
					;

set ROUTE_FOCUSED_SCENARIO		"" ;
					;
					;

if {![info exists env(RTL_ROOT)]} {
    error "design_setup.tcl: RTL_ROOT env var is not set (path to the vendored rtl/ dir)."
}
set RTL_ROOT $env(RTL_ROOT)

if {$DESIGN_NAME eq "gcd"} {
    set RTL_SOURCE_FILES [list ${RTL_ROOT}/gcd/rtl/gcd_simple.v]
} elseif {$DESIGN_NAME eq "aes_cipher_top"} {
    set RTL_SOURCE_FILES [list \
        ${RTL_ROOT}/aes_cipher_top/rtl/aes_cipher_top.v \
        ${RTL_ROOT}/aes_cipher_top/rtl/aes_key_expand_128.v \
        ${RTL_ROOT}/aes_cipher_top/rtl/aes_sbox.v \
        ${RTL_ROOT}/aes_cipher_top/rtl/aes_rcon.v \
        ${RTL_ROOT}/aes_cipher_top/rtl/timescale.v \
    ]
} elseif {$DESIGN_NAME eq "jpeg_encoder"} {
    set RTL_SOURCE_FILES [glob ${RTL_ROOT}/jpeg_encoder/rtl/*.v]
} elseif {$DESIGN_NAME eq "picorv32"} {
    set RTL_SOURCE_FILES [list ${RTL_ROOT}/picorv32/rtl/picorv32.v]
} elseif {$DESIGN_NAME eq "ibex_core"} {
    set RTL_SOURCE_FORMAT "sverilog"
    set _ibex_all_sv [glob ${RTL_ROOT}/ibex/rtl/*.sv]
    set _ibex_no_pkg [lsearch -all -inline -not $_ibex_all_sv *ibex_pkg.sv]
    set RTL_SOURCE_FILES  [concat \
        [list ${RTL_ROOT}/ibex/rtl/ibex_pkg.sv] \
        $_ibex_no_pkg \
        [list ${RTL_ROOT}/ibex/rtl/syn/rtl/prim_clock_gating.v]]
    set SUPPLEMENTAL_SEARCH_PATH "$SUPPLEMENTAL_SEARCH_PATH ${RTL_ROOT}/ibex/rtl/vendor/lowrisc_ip/prim/rtl"
} elseif {$DESIGN_NAME eq "sha256_core"} {
    set RTL_SOURCE_FORMAT "verilog"
    set RTL_SOURCE_FILES [glob ${RTL_ROOT}/sha256_secworks/rtl/*.v]
} elseif {$DESIGN_NAME eq "poly1305_core"} {
    set RTL_SOURCE_FORMAT "verilog"
    set RTL_SOURCE_FILES [glob ${RTL_ROOT}/poly1305_secworks/rtl/*.v]
} elseif {$DESIGN_NAME eq "blake2s_core"} {
    set RTL_SOURCE_FORMAT "verilog"
    set RTL_SOURCE_FILES [glob ${RTL_ROOT}/blake2s_secworks/rtl/*.v]
} elseif {$DESIGN_NAME eq "ethmac"} {
    set RTL_SOURCE_FORMAT "verilog"
    set RTL_SOURCE_FILES [glob ${RTL_ROOT}/ethmac/rtl/*.v]
} elseif {$DESIGN_NAME eq "eth_top"} {
    set RTL_SOURCE_FORMAT "verilog"
    set RTL_SOURCE_FILES [glob ${RTL_ROOT}/ethmac/rtl/*.v]
} elseif {$DESIGN_NAME eq "sha3"} {
    set RTL_SOURCE_FORMAT "verilog"
    set RTL_SOURCE_FILES [glob ${RTL_ROOT}/keccak_secworks/rtl/*.v]
} else {
    error "design_setup.tcl: RTL_SOURCE_FILES not defined for DESIGN_NAME=$DESIGN_NAME"
}
;
set FC_RTL_READ_SCRIPT		${DESIGN_NAME}.FC.read_design.tcl ;
set FM_RTL_READ_SCRIPT		${DESIGN_NAME}.FM.read_design.tcl ;
set UPF_MODE      		"prime" ;
                          		;

set VERILOG_NETLIST_FILES	""	;
					;
					;

set UPF_FILE 			""	;
					;
					;
                                        ;
                                        ;
                			;
set UPF_SUPPLEMENTAL_FILE	""      ;
					;
					;
					;
set UPF_UPDATE_SUPPLY_SET_FILE	""	;

set SAIF_FILE_LIST			"" ;
					;
					;
					;
set SAIF_FILE_POWER_SCENARIO		"" ;
set SAIF_FILE_SOURCE_INSTANCE		"" ;
set SAIF_FILE_TARGET_INSTANCE		"" ;

if {![info exists TCL_FLOORPLAN_FILE]} {
    set TCL_FLOORPLAN_FILE          "./scripts/floorplan.tcl"
}
                                                ;
                                                ;

set DEF_FLOORPLAN_FILES			"" ;
					;
					;
					;
set DEF_READ_OPTIONS			"-add_def_only_objects all" ;
					;
set DEF_RESOLVE_PG_NETS			true ;
set TCL_ADDITIONAL_FLOORPLAN_FILE 	"" ;
					;

set SITE_SYMMETRY_LIST			"" ;
					;
					;

set DEF_SCAN_FILE			"" ;
					;

set TCL_FLOORPLAN_RULE_SCRIPT		"" ;
					;

set TCL_USER_SPARE_CELL_PRE_SCRIPT	"" ;
					   ;
set TCL_USER_SPARE_CELL_POST_SCRIPT	"" ;
					   ;

set SWITCH_CONNECTIVITY_FILE    	"" ;

if {![info exists ENABLE_FLOORPLAN_CHECKS]} {
    set ENABLE_FLOORPLAN_CHECKS true ;
}

set DFT_INSERT_ENABLE           false   ;
set TCL_DFT_PRE_IN_COMPILE_SETUP_FILE   "" ;
set TCL_CONSTRAINTS_SETUP_FILE          "" ;

set ENDPOINT_OPT_MAX_PATHS 		"10000" ;
set ENDPOINT_OPT_SLACK_THRESHOLD	"-0.001" ;
					;
					;
set ENDPOINT_OPT_TARGET_SCENARIOS	"*" ;
					;
set ENDPOINT_OPT_LOOP			1 ;
set ENDPOINT_OPT_PATH_GROUP_FILTER 	"" ;
					;

set CHIP_FINISH_METAL_FILLER_PREFIX 	"RM_filler" ;
set CHIP_FINISH_NON_METAL_FILLER_PREFIX $CHIP_FINISH_METAL_FILLER_PREFIX ;

set CHIP_FINISH_SIGNAL_EM_CONSTRAINT_FORMAT "ITF" ;
set CHIP_FINISH_SIGNAL_EM_CONSTRAINT_FILE "" ;
					   ;
					   ;
					   ;
set CHIP_FINISH_SIGNAL_EM_SAIF 		"" ;
set CHIP_FINISH_SIGNAL_EM_SCENARIO 	"" ;
					   ;
set CHIP_FINISH_SIGNAL_EM_FIXING 	false ;

set ICV_IN_DESIGN_DRC_CHECK_RUNSET 		"" ;
set ICV_IN_DESIGN_DRC_CHECK_RUNDIR 		"z_check_drc" ;
					   	;

set ICV_IN_DESIGN_DRC_USER_DEFINED_OPTIONS 	"" ;
set ICV_IN_DESIGN_DRC_FILL_VIEW_DATA 		"read" ;
set ICV_IN_DESIGN_DRC_CELL_VIEWS 		"frame" ;
						;
set ICV_IN_DESIGN_DRC_EXCLUDED_CELL_TYPES 	"" ;
						;

set ICV_IN_DESIGN_DRC_IGNORE_CHILD_CELL_ERRORS 	false ;
						;
set ICV_IN_DESIGN_DRC_SELECT_RULES 		"" ;
set ICV_IN_DESIGN_DRC_UNSELECT_RULES 		"" ;
set STREAM_FILES_FOR_MERGE 			"" ;

set ICV_IN_DESIGN_DRC				false ;

set ICV_IN_DESIGN_METAL_FILL 			false ;
set ICV_IN_DESIGN_METAL_FILL_RUNSET		"" ;
					   	;
set ICV_IN_DESIGN_METAL_FILL_RUNDIR		"z_icvFill" ;

set ICV_IN_DESIGN_METAL_FILL_USER_DEFINED_OPTIONS "" ;
set ICV_IN_DESIGN_METAL_FILL_FIX_DENSITY_ERRORS "false" ;
						;
set ICV_IN_DESIGN_METAL_FILL_SELECT_LAYERS 	"" ;

set ICV_IN_DESIGN_METAL_FILL_COORDINATES 	"" ;
						   ;
set ICV_IN_DESIGN_METAL_FILL_TIMING_DRIVEN_THRESHOLD "" ;
					   	;
					   	;
set ICV_IN_DESIGN_METAL_FILL_TRACK_BASED 	"off" ;
					   	;
					   	;
set ICV_IN_DESIGN_METAL_FILL_ECO_THRESHOLD 	"" ;
set ICV_IN_DESIGN_POST_METAL_FILL_RUNDIR 	"z_MFILL_after" ;
					   	;
set ICV_IN_DESIGN_METAL_FILL_TRACK_BASED_PARAMETER_FILE "auto" ;
					   	;
					   	;
set ICV_IN_DESIGN_BASE_FILL false               ;

set ICV_IN_DESIGN_BASE_FILL_RUNSET ""           ;

set ICV_IN_DESIGN_BASE_FILL_RUNDIR "z_icvFill"  ;

set ICV_IN_DESIGN_BASE_FILL_FOUNDRY_NODE ""          ;

set ECO_OPT_ENGINE                      "pt" ;
set ECO_OPT_EXEC_PATH                   "" ;
                                        ;
set ECO_OPT_DB_PATH			"" ;
					;
set ECO_OPT_RECIPE_INFO			"" ;
                                        ;
                                        ;
                                        ;
                                        ;
                                        ;
set ECO_OPT_ENGINE_SCRIPT		"" ;
                                        ;
					;
set ECO_OPT_PHYSICAL_MODE		"" ;
set ECO_OPT_WITH_PBA 			false ;
set ECO_OPT_EXTRACTION_MODE		"fusion_adv" ;
					;
					;
set ECO_OPT_STARRC_CONFIG_FILE 		"" ;
set ECO_OPT_WORK_DIR			"eco_opt_dir" ;
					;
set ECO_OPT_PRE_LINK_SCRIPT		"" ;
					;
set ECO_OPT_POST_LINK_SCRIPT		"" ;
					;
set ECO_OPT_PT_CORES_PER_SCENARIO	"4" ;
set ECO_OPT_SIGNOFF_SCENARIO_PAIR	"" ;
set ECO_OPT_FILLER_CELL_PREFIX 		"$CHIP_FINISH_METAL_FILLER_PREFIX" ;
					;
set ECO_OPT_CUSTOM_OPTIONS 		""

set PT_ECO_CHANGE_FILE 			"" ;
set PT_ECO_MODE				"default" ;
					;
					;
set PT_ECO_DISPLACEMENT_THRESHOLD 	"10" ;
					;

set FUNCTIONAL_ECO_ACTIVE_SCENARIO_LIST	"" ;
					   ;
set TCL_USER_FUNCTIONAL_ECO_PRE_SCRIPT	"" ;
set TCL_USER_FUNCTIONAL_ECO_POST_SCRIPT	"" ;
set FUNCTIONAL_ECO_DISPLACEMENT_THRESHOLD "10" ;
					   ;
set FUNCTIONAL_ECO_VERILOG_FILE		"" ;
set FUNCTIONAL_ECO_MODE			"default" ;
					   ;
					   ;
set TCL_USER_PSC_AUTO_DERIVE_MAPPING_RULE_FILE "" ;
					   ;
					   ;

set TCL_USER_NON_PERSISTENT_SCRIPT 	"non_persistent_script.tcl" ;
set TCL_USER_INIT_DESIGN_PRE_SCRIPT 	"init_design_pre_script.tcl" ;
set TCL_USER_INIT_DESIGN_POST_SCRIPT 	"init_design_post_script.tcl" ;
set TCL_USER_READ_RTL_PRE_SCRIPT 	"read_rtl_pre_script.tcl" ;
set TCL_USER_READ_RTL_POST_SCRIPT 	"read_rtl_post_script.tcl" ;
set TCL_USER_COMPILE_PRE_SCRIPT 	"compile_pre_script.tcl" ;
set TCL_USER_COMPILE_INITIAL_MAP_POST_SCRIPT "compile_initial_map_post_script.tcl" ;
set TCL_USER_COMPILE_LOGIC_OPTO_PRE_SCRIPT "compile_logic_opto_pre_script.tcl" ;
set TCL_USER_COMPILE_LOGIC_OPTO_POST_SCRIPT "compile_logic_opto_post_script.tcl" ;
set TCL_USER_DFT_SETUP_PRE_SCRIPT	"dft_setup_pre_script.tcl" ;
set TCL_USER_COMPILE_INITIAL_PLACE_PRE_SCRIPT "compile_initial_place_pre_script.tcl" ;
set TCL_USER_COMPILE_INITIAL_DRC_PRE_SCRIPT "compile_initial_drc_pre_script.tcl" ;
set TCL_USER_COMPILE_INITIAL_OPTO_PRE_SCRIPT "compile_initial_opto_pre_script.tcl" ;
set TCL_USER_COMPILE_INCREMENTAL_INITIAL_OPTO_PRE_SCRIPT "compile_incremental_initial_opto_pre_script.tcl" ;
set TCL_USER_COMPILE_FINAL_PLACE_PRE_SCRIPT "compile_final_place_pre_script.tcl" ;
set TCL_USER_COMPILE_FINAL_OPTO_PRE_SCRIPT "compile_final_opto_pre_script.tcl" ;
set TCL_USER_COMPILE_POST_SCRIPT 	"compile_post_script.tcl" ;
set TCL_USER_CREATE_DFT_PORTS_POST_SCRIPT "create_dft_ports_post_script.tcl" ;

set TCL_USER_PLACE_OPT_PRE_SCRIPT 	"place_opt_pre_script.tcl" ;
set TCL_USER_PLACE_OPT_SCRIPT 		"" ;
set TCL_USER_PLACE_OPT_POST_SCRIPT 	"place_opt_post_script.tcl" ;
set TCL_USER_PLACE_OPT_INCREMENTAL_PLACEMENT_POST_SCRIPT "place_opt_incremental_placement_post_script.tcl" ;
					;
set TCL_USER_CLOCK_OPT_CTS_PRE_SCRIPT 	"clock_opt_cts_pre_script.tcl" ;
set TCL_USER_CLOCK_OPT_CTS_SCRIPT 	"" ;
set TCL_USER_CLOCK_OPT_CTS_POST_SCRIPT 	"clock_opt_cts_post_script.tcl" ;

set TCL_USER_CLOCK_OPT_OPTO_PRE_SCRIPT 	"clock_opt_opto_pre_script.tcl" ;
set TCL_USER_CLOCK_OPT_OPTO_SCRIPT 	"" ;
set TCL_USER_CLOCK_OPT_OPTO_POST_SCRIPT "clock_opt_opto_post_script.tcl" ;

set TCL_USER_ROUTE_AUTO_PRE_SCRIPT 	"route_auto_pre_script.tcl" ;
set TCL_USER_ROUTE_AUTO_SCRIPT 		"" ;
set TCL_USER_ROUTE_AUTO_POST_SCRIPT 	"route_auto_post_script.tcl" ;

set TCL_USER_ROUTE_OPT_PRE_SCRIPT 	"route_opt_pre_script.tcl" ;
set TCL_USER_ROUTE_OPT_SCRIPT 		"" ;
set TCL_USER_ROUTE_OPT_1_POST_SCRIPT    "route_opt_1_post_script.tcl" ;
					;
set TCL_USER_ROUTE_OPT_2_POST_SCRIPT    "route_opt_2_post_script.tcl" ;
					;
set TCL_USER_ROUTE_OPT_POST_SCRIPT 	"route_opt_post_script.tcl" ;

set TCL_USER_ENDPOINT_OPT_PRE_SCRIPT 	"endpoint_opt_pre_script.tcl" ;
set TCL_USER_ENDPOINT_OPT_SCRIPT 	"" ;
set TCL_USER_ENDPOINT_OPT_POST_SCRIPT 	"endpoint_opt_post_script.tcl" ;

set TCL_USER_TIMING_ECO_PRE_SCRIPT 	"timing_eco_pre_script.tcl" ;
set TCL_USER_TIMING_ECO_POST_SCRIPT 	"timing_eco_post_script.tcl" ;
set ENABLE_INCR_ROUTE_POST_ECO          "true" ;

set TCL_USER_CHIP_FINISH_PRE_SCRIPT 	"chip_finish_pre_script.tcl" ;
set TCL_USER_CHIP_FINISH_POST_SCRIPT 	"chip_finish_post_script.tcl" ;

set TCL_USER_ICV_IN_DESIGN_PRE_SCRIPT 	"icv_in_design_pre_script.tcl" ;
set TCL_USER_ICV_IN_DESIGN_POST_SCRIPT 	"icv_in_design_post_script.tcl" ;

set TCL_USER_WRITE_DATA_PRE_SCRIPT 	"" ;
set TCL_USER_WRITE_DATA_POST_SCRIPT	"" ;

set TCL_USER_FLOORPLAN_PRE_SCRIPT 	"" ;
set TCL_USER_FLOORPLAN_POST_SCRIPT 	"" ;

set DEFINE_NAME_RULES_OPTIONS		"" ;
set OUTPUTS_DIR				"./outputs_fc" ;
set REPORTS_DIR				"./rpts_fc" ;
set LOGS_DIR				"./logs_fc" ;

set ENABLE_INLINE_REPORT_QOR		true ;
set REPORT_QOR				true ;
					;
set REPORT_DEBUG			false ;
set REPORT_VERBOSE			true ;
set REPORT_DISABLE_GUI			false ;
set REPORT_QOR_REPORT_CONGESTION	true ;
					;

set REPORT_QOR_REPORT_POWER		true ;
					;
set REPORT_CLOCK_POWER			false ;
set REPORT_POWER_SAIF_FILE		"" ;
set REPORT_POWER_SCALING_RATIO		"" ;
					;
set REPORT_INIT_DESIGN_ACTIVE_SCENARIO_LIST    "" ;
set REPORT_COMPILE_ACTIVE_SCENARIO_LIST        "" ;
set REPORT_PLACE_OPT_ACTIVE_SCENARIO_LIST      "" ;
set REPORT_CLOCK_OPT_CTS_ACTIVE_SCENARIO_LIST  "" ;
set REPORT_CLOCK_OPT_OPTO_ACTIVE_SCENARIO_LIST "" ;
set REPORT_ROUTE_AUTO_ACTIVE_SCENARIO_LIST     "" ;
set REPORT_ROUTE_OPT_ACTIVE_SCENARIO_LIST      "" ;
set REPORT_CHIP_FINISH_ACTIVE_SCENARIO_LIST    "" ;
set REPORT_ICV_IN_DESIGN_ACTIVE_SCENARIO_LIST  "" ;
set REPORT_ENDPOINT_OPT_ACTIVE_SCENARIO_LIST   "" ;
set REPORT_TIMING_ECO_ACTIVE_SCENARIO_LIST     "" ;
set REPORT_FUNCTIONAL_ECO_ACTIVE_SCENARIO_LIST     "" ;

set REPORT_POWER_SAIF_MAP		"${OUTPUTS_DIR}/${COMPILE_BLOCK_NAME}.saif.fc.map" ;

set WRITE_QOR_DATA			true ;
set WRITE_QOR_DATA_DIR			"./qor_data" ;
set COMPARE_QOR_DATA_DIR		"./compare_qor_data" ;
set REPORT_PARALLEL_MAX_CORES 		4 ;
set REPORT_PARALLEL_SUBMIT_COMMAND 	"" ;
					;
					;
set SET_HOST_OPTIONS_MAX_CORES		[expr {[info exists ::env(FC_MAX_CORES)] ? $::env(FC_MAX_CORES) : [exec nproc]}] ;
set TCL_USER_SUPPLEMENTAL_REPORTS_SCRIPT "" ;
set FUSION_REFERENCE_LIBRARY_LOG_DIR    "${LOGS_DIR}/lcsh" ;

set ENABLE_FUSA                         false ;

set HPC_CORE                    "" ;

set SET_QOR_STRATEGY_METRIC		"timing" ;
					;
set SET_QOR_STRATEGY_MODE		"balanced" ;
					;
set SET_QOR_STRATEGY_CONGESTION_EFFORT	"" ;
					;
set ENABLE_REDUCED_EFFORT		false ;
					;
set ENABLE_HIGH_EFFORT_TIMING           true ;

set RESET_CHECK_STAGE_SETTINGS		false ;
					;
					;
set NON_DEFAULT_CHECK_STAGE_SETTINGS    false ;
                                        ;

set ENABLE_SPG 				false ;
					;
					;

set ENABLE_HIGH_UTILIZATION_FLOW	false ;
					;
					;

set ENABLE_MULTIBIT                     true ;
                                        ;
                                        ;

set ENABLE_DPS				false ;
					;
					;
					;
					;
set ENABLE_IRDP				false ;
set ENABLE_IRDCCD			false ;

set TCL_PRIMEPOWER_CONFIG_FILE		"" ;
					;
                                        ;
					;

set CTS_STYLE                           "standard" ;
set TCL_USER_MSCTS_MESH_ROUTING_SCRIPT  "" ;

set INCR_ROUTE_DETAIL_MODE              "auto" ;
                                        ;
                                        ;
                                        ;
                                        ;
                                        ;
                                        ;
set INCR_ROUTE_DETAIL_DRC_INCREASE_THRESHOLD_MIN "0.1" ;
                                        ;
                                        ;
                                        ;
set INCR_ROUTE_DETAIL_DRC_THRESHOLD_MAX "10000" ;
                                        ;
set INCR_ROUTE_DETAIL_DRC_THRESHOLD_MIN "50" ;
                                        ;
set INCR_ROUTE_DETAIL_MAX_ITERATIONS    "" ;
                                        ;

set ENABLE_ROUTE_OPT_PBA		true ;

set ENABLE_CREATE_SHIELDS		false ;
					;
					;

set ROUTE_OPT_STARRC_CONFIG_FILE 	"" ;
					;
set SET_STARRC_IN_DESIGN_OPTIONS	"" ;
					;
set VMF_PARAMETER_FILE			"" ;

set DP_FLOW                 "flat"  ;
set DISTRIBUTED             "true"  ;
set BLOCK_DIST_JOB_COMMAND  ""      ;
set DP_BLOCK_REFS           [list]  ;
set WORK_DIR                ./work  ;

if {[info exists INCREMENTAL_INIT_DESIGN]} {
    set INIT_DESIGN_INPUT           "ASCII" ;
                                    ;
                                    ;
                                    ;
                                    ;
    
    
    set VERILOG_NETLIST_FILES       ""      ;
                                            ;
    
    set UPF_FILE                    ""      ;
                                            ;
    set UPF_SUPPLEMENTAL_FILE       ""      ;
                                            ;
                                            ;
    set UPF_UPDATE_SUPPLY_SET_FILE  ""      ;
    
    set TCL_MCMM_SETUP_FILE         ""      ;
                                            ;
                                            ;
                                            ;
                                            ;
                                            ;
                                            ;
    set TCL_PARASITIC_SETUP_FILE    ""      ;
                                            ;
    
    set TCL_USER_NON_PERSISTENT_SCRIPT      "non_persistent_script.tcl" ;
    set TCL_USER_INIT_DESIGN_PRE_SCRIPT     "init_design_pre_script.tcl" ;
    set TCL_USER_INIT_DESIGN_POST_SCRIPT    "init_design_post_script.tcl" ;
    
    set TCL_FLOORPLAN_FILE                  "" ;
                                            ;
                                            ;
                                            ;
                                            ;
    
    set DEF_FLOORPLAN_FILES                 "" ;
                                            ;
                                            ;
                                            ;
    set DEF_READ_OPTIONS                    "-add_def_only_objects all" ;
                                            ;
    set TCL_ADDITIONAL_FLOORPLAN_FILE       "" ;
                                            ;
    
    set DEF_SCAN_FILE                       "" ;
                                               ;
    
    set TCL_FLOORPLAN_RULE_SCRIPT           "" ;
                                            ;
    
    set TCL_USER_SPARE_CELL_PRE_SCRIPT      "" ;
                                            ;
    set TCL_USER_SPARE_CELL_POST_SCRIPT     "" ;
                                            ;
}

if {[get_app_var synopsys_program_name] == "fc_shell" && [get_app_var synopsys_shell_mode] == "frontend"}  {
   puts "RM-warning: Using the Fusion Compiler Frontend Shell. Design Planning commands will require a special license in this shell mode. Please use the Unified Fusion Compiler shell."
}

set search_path "$search_path ./scripts/plugins ./scripts/tech ./scripts/flow ./configs ./examples"
if {$SUPPLEMENTAL_SEARCH_PATH != ""} {
   set search_path "$search_path $SUPPLEMENTAL_SEARCH_PATH"
}

if {$synopsys_program_name == "icc2_shell" || $synopsys_program_name == "fc_shell"} {
   set_host_options -max_cores $SET_HOST_OPTIONS_MAX_CORES

   set_app_options -name shell.common.report_default_significant_digits -value 3 ;

}

set sh_continue_on_error true

if {![file exists $OUTPUTS_DIR]} {file mkdir $OUTPUTS_DIR} ;
if {![file exists $REPORTS_DIR]} {file mkdir $REPORTS_DIR} ;
if {$WRITE_QOR_DATA && ![file exists $WRITE_QOR_DATA_DIR]} {file mkdir $WRITE_QOR_DATA_DIR} ;
if {$WRITE_QOR_DATA && ![file exists $COMPARE_QOR_DATA_DIR]} {file mkdir $COMPARE_QOR_DATA_DIR} ;

if {[get_app_var synopsys_program_name] == "fc_shell" || [get_app_var synopsys_program_name] == "icc2_shell"} {
	suppress_message ATTR-11 ;
	set_message_info -id PVT-012 -limit 1
	set_message_info -id PVT-013 -limit 1
}
puts "RM-info: Hostname: [sh hostname]"; puts "RM-info: Date: [date]"; puts "RM-info: PID: [pid]"; puts "RM-info: PWD: [pwd]"
