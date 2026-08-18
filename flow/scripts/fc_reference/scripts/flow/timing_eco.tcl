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
set PREVIOUS_STEP $TIMING_ECO_FROM_BLOCK_NAME
set CURRENT_STEP  $TIMING_ECO_BLOCK_NAME
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

if {$TIMING_ECO_ACTIVE_SCENARIO_LIST != ""} {
	set_scenario_status -active false [get_scenarios -filter active]
	set_scenario_status -active true $TIMING_ECO_ACTIVE_SCENARIO_LIST
}

rm_source -file $TCL_MODE_CORNER_SCENARIO_MODEL_ADJUSTMENT_FILE -optional -print "TCL_MODE_CORNER_SCENARIO_MODEL_ADJUSTMENT_FILE"

rm_source -file $SIDEFILE_TIMING_ECO_1 -optional -print "SIDEFILE_TIMING_ECO_1" ;

rm_source -file $TCL_USER_NON_PERSISTENT_SCRIPT -optional -print "TCL_USER_NON_PERSISTENT_SCRIPT"

rm_source -file $TCL_USER_TIMING_ECO_PRE_SCRIPT -optional -print "TCL_USER_TIMING_ECO_PRE_SCRIPT"

redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_app_options.start {report_app_options -non_default *}
redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_lib_cell_purpose {report_lib_cell -objects [get_lib_cells] -column {full_name:20 valid_purposes}}

if {$ENABLE_INLINE_REPORT_QOR} {
   redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_qor.start {report_qor -scenarios [all_scenarios] -pba_mode [get_app_option_value -name time.pba_optimization_mode] -nosplit}
   redirect -append -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_qor.start {report_qor -summary -pba_mode [get_app_option_value -name time.pba_optimization_mode] -nosplit}
   redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_global_timing.start {report_global_timing -pba_mode [get_app_option_value -name time.pba_optimization_mode] -nosplit}
}

if {$ECO_OPT_ENGINE == "tweaker"} {
  if {$ECO_OPT_EXEC_PATH != ""} {
    set eco_exec_image $ECO_OPT_EXEC_PATH
  } else {
    set eco_exec_image [ exec which tweaker ]
  }
  set pt_image [ exec which pt_shell ]
} else {
  if {$ECO_OPT_EXEC_PATH != ""} {
    set eco_exec_image $ECO_OPT_EXEC_PATH
  } else {
    set eco_exec_image [ exec which pt_shell ]
  }
  set pt_image $eco_exec_image
}

if { ($ECO_OPT_ENGINE == "pt" || $ECO_OPT_ENGINE == "primeeco") && ([ file tail $eco_exec_image ] != "pt_shell") } {
	puts "RM-error: Unable to find \"[ file tail $eco_exec_image ]\".  Exiting."
	
	exit
	
} elseif {($ECO_OPT_ENGINE == "tweaker" ) && ([ file tail $eco_exec_image ] != "tweaker") && ([ file tail $pt_image ] != "pt_shell")} {
	puts "RM-error: Unable to find the binary. Please check the twekaer or PT exec path. Exiting."
	
	exit
	
}

if {$ECO_OPT_DB_PATH != ""} {lappend search_path $ECO_OPT_DB_PATH}

set set_pt_options_cmd "set_pt_options -pt_exec $pt_image -enable_user_consistency_settings_options"

if {$ECO_OPT_PT_CORES_PER_SCENARIO !=""} {
  set_host_options -name eco_opt_host_option -max_cores $ECO_OPT_PT_CORES_PER_SCENARIO localhost
  lappend set_pt_options_cmd -host_option eco_opt_host_option
}

if {[file exists [which $ECO_OPT_PRE_LINK_SCRIPT]]} {
	lappend set_pt_options_cmd -pre_link_script $ECO_OPT_PRE_LINK_SCRIPT
} elseif {$ECO_OPT_PRE_LINK_SCRIPT != ""} {
	puts "RM-error: ECO_OPT_PRE_LINK_SCRIPT($ECO_OPT_PRE_LINK_SCRIPT) is invalid. Please correct it."
}

if {[file exists [which $ECO_OPT_POST_LINK_SCRIPT]]} {
	lappend set_pt_options_cmd -post_link_script $ECO_OPT_POST_LINK_SCRIPT
} elseif {$ECO_OPT_POST_LINK_SCRIPT != ""} {
	puts "RM-error: ECO_OPT_POST_LINK_SCRIPT($ECO_OPT_POST_LINK_SCRIPT) is invalid. Please correct it."
}

if {$ECO_OPT_SIGNOFF_SCENARIO_PAIR != ""} {
	lappend set_pt_options_cmd -scenario_constraint $ECO_OPT_SIGNOFF_SCENARIO_PAIR
}

set pre_eco_pt_options $set_pt_options_cmd

lappend set_pt_options_cmd -work_dir eco_opt_dir_1

puts "RM-info: Running $set_pt_options_cmd"
eval $set_pt_options_cmd
redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_pt_options {report_pt_options}

set extraction_mode [get_app_option_value -name extract.starrc_mode]
set_app_options -name extract.starrc_mode -value $ECO_OPT_EXTRACTION_MODE

if {$ECO_OPT_EXTRACTION_MODE == "fusion_adv" || $ECO_OPT_EXTRACTION_MODE == "in_design"} {
	if {[file exists [which $ECO_OPT_STARRC_CONFIG_FILE]]} {
		set ECO_OPT_STARRC_CONFIG_FILE [file normalize $ECO_OPT_STARRC_CONFIG_FILE]
		puts "RM-info: Running with STARRC extraction"
		set_starrc_options -config $ECO_OPT_STARRC_CONFIG_FILE ;
		redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_starrc_options.rpt {report_starrc_options} 
	} else {
		puts "RM-error: ECO_OPT_STARRC_CONFIG_FILE is invalid. Exiting..."
	
	exit
	
	}
}

redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/pre_check_legality.rpt {check_legality}
redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/pre_check_routes.rpt {check_routes}

if {$ECO_OPT_WITH_PBA} {
	set_app_options -name time.pba_optimization_mode -value path 	
}

set check_pt_qor_cmd "check_pt_qor"
if {$ECO_OPT_WITH_PBA} {lappend check_pt_qor_cmd -pba_mode [get_app_option_value -name time.pba_optimization_mode]}
redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/check_pt_qor.pre.rpt $check_pt_qor_cmd
redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/check_pt_qor.pre.summary.rpt "$check_pt_qor_cmd -type summary"

set_app_options -name route.detail.eco_route_use_soft_spacing_for_timing_optimization -value false

if {$ECO_OPT_FILLER_CELL_PREFIX==""} {
	puts "RM-warning: The variable ECO_OPT_FILLER_CELL_PREFIX is set NULL.  All filler cells will be removed."
}
set RM_prior_filler_cells [get_cells -hier -quiet xofiller!${ECO_OPT_FILLER_CELL_PREFIX}*]
if {[sizeof_collection $RM_prior_filler_cells] > 0} {
	set RM_enable_filler_insertion "1"
} else {
	set RM_enable_filler_insertion "0"
	puts "RM-info: No filler cells where detected in the source design."
}

if {$PT_ECO_CHANGE_FILE==""} {
	if {$RM_enable_filler_insertion} {
		remove_cells $RM_prior_filler_cells
	}

	
        set eco_count 1
	
	foreach eco_opt_type $ECO_OPT_RECIPE_INFO { 
          if {$ECO_OPT_ENGINE == "pt"} {
            
            puts "RM-info: Running eco_opt with PT"
	    set eco_opt_cmd "eco_opt"
	    if {$ECO_OPT_PHYSICAL_MODE != ""} {lappend eco_opt_cmd -physical_mode $ECO_OPT_PHYSICAL_MODE}
	    if {$ECO_OPT_WITH_PBA} {lappend eco_opt_cmd -pba_mode [get_app_option_value -name time.pba_optimization_mode]}
	    if {$ECO_OPT_CUSTOM_OPTIONS != ""} {lappend eco_opt_cmd $ECO_OPT_CUSTOM_OPTIONS}

	    set eco_opt_args "$eco_opt_cmd -types [list $eco_opt_type]"
	    puts "RM-info: Starting run $eco_count: $eco_opt_args"
	    eval $eco_opt_args
          
          } elseif {$ECO_OPT_ENGINE == "primeeco" || $ECO_OPT_ENGINE == "tweaker"} {
            
            puts "RM-info: Fixing \"$eco_opt_type\""
           
            rm_generate_variables_for_eco -work_directory ${REPORTS_DIR}/${REPORT_PREFIX} -file_name eco_opt_rm_tcl_var.tcl

            set_pt_options -reset
	    
            if {$ECO_OPT_ENGINE == "primeeco"} { 
              puts "RM-info: Running eco_opt with PrimeECO"
              set_pt_options -primeeco_exec $eco_exec_image -work_dir eco_opt_dir_${eco_count}/primeeco_work
            } else {
              puts "RM-info: Running eco_opt with Tweaker"
              set_pt_options -tweaker_exec $eco_exec_image -work_dir eco_opt_dir_${eco_count}/tweaker_work
            }
            eco_opt -eco_script $ECO_OPT_ENGINE_SCRIPT
          
          } else {
            puts "RM-error: Invalid ECO_OPT_ENGINE.  Exiting."
			
			exit
			
          }
	  

          set_pt_options -reset
          eval $pre_eco_pt_options -work_dir post_eco_pt_${eco_count}
	  redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/run_$eco_count.check_pt_qor.post.rpt $check_pt_qor_cmd
	  redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/run_$eco_count.check_pt_qor.post.summary.rpt "$check_pt_qor_cmd -type summary"
	 
          incr eco_count
	  
          set_pt_options -reset
	  eval $pre_eco_pt_options -work_dir eco_opt_dir_${eco_count}
        }

} elseif {[file exists [which $PT_ECO_CHANGE_FILE]]} {
	remove_attribute [get_cell -quiet -hier -filter "defined(eco_change_status)"] eco_change_status

	if {$PT_ECO_MODE == "freeze_silicon"} {
		puts "RM-info: Running freeze silicon PT-ECO flow"
	
	
		set_app_options -name design.eco_freeze_silicon_mode -value true

		rm_source -file $PT_ECO_CHANGE_FILE
		set_app_options -name design.eco_freeze_silicon_mode -value false
	
		redirect -file ${REPORTS_DIR}/${REPORT_PREFIX}/check_freeze_silicon {check_freeze_silicon}

		place_freeze_silicon

	} else {

		puts "RM-info: Running MPI PT-ECO flow"
		rm_source -file $PT_ECO_CHANGE_FILE

		redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_eco_physical_changes.pre_eco_place.rpt {report_eco_physical_changes -type all}
	
		set place_eco_cells_cmd "place_eco_cells -eco_changed_cells -legalize_only -legalize_mode minimum_physical_impact -displacement_threshold $PT_ECO_DISPLACEMENT_THRESHOLD"
		if {$CHIP_FINISH_METAL_FILLER_LIB_CELL_LIST != "" || $CHIP_FINISH_NON_METAL_FILLER_LIB_CELL_LIST != ""} {
			lappend place_eco_cells_cmd -remove_filler_references "$CHIP_FINISH_METAL_FILLER_LIB_CELL_LIST $CHIP_FINISH_NON_METAL_FILLER_LIB_CELL_LIST"
		}
		puts "RM-info: $place_eco_cells_cmd"
		eval ${place_eco_cells_cmd}

		redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_eco_physical_changes.post_eco_place.rpt {report_eco_physical_changes -type all}

	}
	connect_pg_net

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
} else {
	puts "RM-error: PT_ECO_CHANGE_FILE($PT_ECO_CHANGE_FILE) is invalid. Please correct it."
}
if { $ENABLE_INCR_ROUTE_POST_ECO } {
  if {$PT_ECO_MODE != "freeze_silicon"} {
    check_routes > ./check_routes_report
    set fid [ open ./check_routes_report r ]
    set string_file [ read $fid ]
    close $fid
    set lines [ split $string_file \n ]
    foreach line $lines {
      if { [ regexp {^Total number of DRCs =\s([\d]+)} $line match data ] } {
        puts "RM-info: DRCs post eco_opt: $data"
        if { $data > "0" } {
          puts "RM-info: Number of DRCs reported post route_eco : $data"
          puts "RM-info: Running  a cycle of incremental detail route to resolve residual DRCs" 
          route_detail -incremental true -initial_drc_from_input true
        } else {
          puts "RM-info: DRCs are already clean. No need to run incremental detail routing."
        }
      }
    }
  }
}
if {$RM_enable_filler_insertion} {
	if {($PT_ECO_CHANGE_FILE!="") && ($PT_ECO_MODE=="freeze_silicon")} {
 		puts "RM-info: Skipping filler cell reinsertion for freeze_silicon mode."
	} else {
		puts "RM-info: Filler cells were detected in the source design.  Performing reinsertion..."
		rm_source -file $SIDEFILE_TIMING_ECO_2
	}
} else {
	puts "RM-info: Skipping filler cell reinsertion as none detected in source design."
}

if {!([compare_collections [get_shapes -hier] [get_shapes -hier -include_fill]]=="0")} {
	puts "RM-info: Metal fill was detected in the source design.  Performing refill..."

	rm_source -file $SIDEFILE_TIMING_ECO_3 -optional -print "SIDEFILE_TIMING_ECO_3" ;

	save_block

	redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/report_app_options.signoff.create_metal_fill.rpt {report_app_options signoff.create_metal_fill*}
	
	if {$ICV_IN_DESIGN_METAL_FILL_ECO_THRESHOLD!=""} {
        	 set_app_options   -name  signoff.create_metal_fill.auto_eco_threshold_value -value $ICV_IN_DESIGN_METAL_FILL_ECO_THRESHOLD
	}

        set_app_options -name signoff.create_metal_fill.full_run_on_large_eco -value true

	set create_metal_fill_cmd "signoff_create_metal_fill"
	
	if {$ICV_IN_DESIGN_METAL_FILL_TRACK_BASED != "off"} {
	
		if {$ICV_IN_DESIGN_METAL_FILL_TRACK_BASED != "generic"} {
			lappend create_metal_fill_cmd -track_fill $ICV_IN_DESIGN_METAL_FILL_TRACK_BASED -fill_all_tracks true
		} else {
			lappend create_metal_fill_cmd -track_fill $ICV_IN_DESIGN_METAL_FILL_TRACK_BASED
		}
	
		if {$ICV_IN_DESIGN_METAL_FILL_TRACK_BASED_PARAMETER_FILE != "auto" && [file exists [which $ICV_IN_DESIGN_METAL_FILL_TRACK_BASED_PARAMETER_FILE]]} {
			lappend create_metal_fill_cmd -track_fill_parameter_file $ICV_IN_DESIGN_METAL_FILL_TRACK_BASED_PARAMETER_FILE
		}
	}

	if {$ICV_IN_DESIGN_METAL_FILL_SELECT_LAYERS != ""} {
		lappend create_metal_fill_cmd -select_layers $ICV_IN_DESIGN_METAL_FILL_SELECT_LAYERS
	}

	puts "RM-info: Running $create_metal_fill_cmd"
	eval $create_metal_fill_cmd -auto_eco true

} else {
	puts "RM-info: Skipping metal fill reinsertion as none detected in source design."
}

redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/post_check_legality.rpt {check_legality}
redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/post_check_routes.rpt {check_routes}

set_pt_options -reset
eval $pre_eco_pt_options -work_dir post_eco_pt
redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/check_pt_qor.post.rpt $check_pt_qor_cmd
redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/check_pt_qor.post.summary.rpt "$check_pt_qor_cmd -type summary"

set_app_options -name extract.starrc_mode -value $extraction_mode

rm_source -file $TCL_USER_TIMING_ECO_POST_SCRIPT -optional -print "TCL_USER_TIMING_ECO_POST_SCRIPT"

if {![rm_source -file $TCL_USER_CONNECT_PG_NET_SCRIPT -optional -print "TCL_USER_CONNECT_PG_NET_SCRIPT"]} {
	connect_pg_net
}

save_block
save_lib

if {$REPORT_QOR} {
        set REPORT_STAGE post_route
        set REPORT_ACTIVE_SCENARIOS $REPORT_TIMING_ECO_ACTIVE_SCENARIO_LIST
	if {$REPORT_PARALLEL_SUBMIT_COMMAND != ""} {
		rm_generate_variables_for_report_parallel -work_directory ${REPORTS_DIR}/${REPORT_PREFIX} -file_name rm_tcl_var.tcl

		report_parallel -work_directory ${REPORTS_DIR}/${REPORT_PREFIX} -submit_command ${REPORT_PARALLEL_SUBMIT_COMMAND} -max_cores ${REPORT_PARALLEL_MAX_CORES} -user_scripts [list "${REPORTS_DIR}/${REPORT_PREFIX}/rm_tcl_var.tcl" "[which report_qor.tcl]"]
	} else {
		rm_source -file report_qor.tcl
	}
}
redirect -tee -file ${REPORTS_DIR}/${REPORT_PREFIX}/run_end.rpt {run_end}

report_msg -summary
print_message_info -ids * -summary
rm_logparse $LOGS_DIR/timing_eco.log
echo [date] > timing_eco

exit

