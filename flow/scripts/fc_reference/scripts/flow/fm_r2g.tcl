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
rm_source -file ./configs/design_setup.tcl

set OUTPUTS_DIR   "./outputs_fm" ;

set REPORTS_DIR      "./rpts_fm" ;

set FM_RTL_READ_SCRIPT       "" ;

set FM_ECO_RTL_READ_SCRIPT   "" ;

set FM_APP_VAR_SETTINGS "fm_app_var_settings.tcl"       ;

set FM_POST_REFERENCE_ADJUST_SCRIPT ""                  ;
                                                        ;

set FM_POST_MATCH_ADJUST_SCRIPT ""                      ;
                                                        ;

set FM_MAX_CORES 4                                      ;

set FM_ALT_REFERENCE_DATA_DIR ""                        ;

set FM_REF_NETLIST ""                                   ;

set FM_SVF_FILES ""                                     ;

set FM_SUPPLEMENTAL_SEARCH_PATH ""                      ;

set FM_REF_UPF_FILE ""                                  ;

set FM_UPF_UPDATE_SUPPLY_SET_FILE ""                    ;

set FM_REF_SUPPLEMENTAL_UPF_FILE ""                     ;

set FM_REFERENCE_POWER_MODELS ""                        ;

set FM_CONSTRAINTS_FILE ""                              ;

set FM_IMP_NETLIST ""                                   ;

set FM_IMP_NDM "$DESIGN_LIBRARY"                        ;

set FM_IMP_NDM_BLOCK ""                                 ;

set FM_REF_NDM "$DESIGN_LIBRARY"                        ;

set FM_REF_NDM_BLOCK ""                                 ;

set FM_IMP_UPF_FILE ""                                  ;

set FM_IMP_SUPPLEMENTAL_UPF_FILE ""                     ;

set FM_IMPLEMENTATION_POWER_MODELS ""                   ;

set FM_LINK_LIBRARY ""                                  ;

set FM_RETENTION_MODEL_FILE ""                          ;

set FM_GENERATE_POWER_MODEL true                        ;

set FM_SET_TOP_RTL_ARGS  ""                             ;

set FM_ECO_GUIDANCE_FROM_SYN ""                         ;

set FM_ECO_NETLIST ""                                   ;

set FM_ECO_IMPLEMENTATION_SRC ""                        ;

set FM_ECO_NETLIST_FOR_NETLIST_FLOW ""                  ;

set FM_ECO_NETLIST_FOR_RTL_FLOW ""                      ;

set FM_ECO_ORIG_REF_NETLIST ""                          ;

set FM_ECO_ORIG_IMPL_NETLIST_FOR_NETLIST_FLOW ""        ;

set FM_ECO_ORIG_IMPL_NETLIST_FOR_RTL_FLOW ""            ;

set FM_ECO_ORIG_ECO_RTL_FILE_PAIRS ""                   ;

set FM_ECO_RTL_SOURCE_FILES ""                          ;

set FM_ECO_REGION_FILE ""                               ;

lappend search_path .
lappend search_path ./scripts/tech 
lappend search_path ./scripts/tech/rm_fm_scripts
lappend search_path ./scripts/plugins 
lappend search_path ./configs 
lappend search_path ./rm_fm_scripts 
lappend search_path ./scripts/flow 
foreach path $SUPPLEMENTAL_SEARCH_PATH {
  set search_path "$path $search_path"
}

set_host_options -max_cores $FM_MAX_CORES

set sh_continue_on_error true

if {![file exists $OUTPUTS_DIR]} {file mkdir $OUTPUTS_DIR} ;
if {![file exists $REPORTS_DIR]} {file mkdir $REPORTS_DIR} ;

puts "RM-info: Hostname: [sh hostname]"; puts "RM-info: Date: [date]"; puts "RM-info: PID: [pid]"; puts "RM-info: PWD: [pwd]"

if { $HPC_CORE != "" } {
  set HPC_STAGE formal
  rm_source -file ./rm_hpc_core_scripts/sidefile_setup_hpc_core.tcl
  rm_source -file $HPC_LIBRARY_SETUP_FILE
}
if { [info exists env(RM_VARFILE)] } {
  if { [file exists $env(RM_VARFILE)] } {
    rm_source -file $env(RM_VARFILE)
  } else {
    puts "RM-error: env(RM_VARFILE) specified but not found"
  }
}

set_app_var synopsys_auto_setup true

rm_source -file $TCL_USER_LIBRARY_SETUP_SCRIPT -optional -print TCL_USER_LIBRARY_SETUP_SCRIPT

rm_source -file $FM_APP_VAR_SETTINGS -optional -print FM_APP_VAR_SETTINGS

if { $HPC_CORE != "" } {
  set search_path ". ${ADDITIONAL_SEARCH_PATH} $search_path"
  set fm_link_library "${TARGET_LIBRARY_FILES} ${ADDITIONAL_LINK_LIB_FILES}"
} else {
  set fm_link_library ${FM_LINK_LIBRARY}
}
foreach tech_lib "${fm_link_library}" {
  read_db -technology_library ${tech_lib}
}

if { $FM_SVF_FILES != "" } {
  puts "RM-info: set_svf $FM_SVF_FILES"
  set_svf $FM_SVF_FILES
}

if { $FM_REF_NDM_BLOCK=="" && $FM_REF_NETLIST=="" } {

  switch ${RTL_SOURCE_FORMAT} {
    sverilog {
      puts "RM-info: read_sverilog -r ${RTL_SOURCE_FILES} -work_library WORK"
      read_sverilog -r ${RTL_SOURCE_FILES} -work_library WORK
    }
    verilog {
      puts "RM-info: read_verilog -r ${RTL_SOURCE_FILES} -work_library WORK"
      read_verilog -r ${RTL_SOURCE_FILES} -work_library WORK
    }
    vhdl {
      puts "RM-info: read_vhdl -r ${RTL_SOURCE_FILES} -work_library WORK"
      read_vhdl -r ${RTL_SOURCE_FILES} -work_library WORK
    }
    script {
      set_app_var sh_source_uses_search_path true
      rm_source -file ${FM_RTL_READ_SCRIPT}
    }
    default {
      puts "RM-error: Unknown RTL_SOURCE_FORMAT(${RTL_SOURCE_FORMAT})"
      exit
    }
  }

  foreach power_model $FM_REFERENCE_POWER_MODELS {
    puts "RM-info: read_power_model -r $power_model"
    read_power_model -r $power_model
  }
  
  set container "r"
  rm_source -file $FM_RETENTION_MODEL_FILE -optional -print FM_RETENTION_MODEL_FILE

  puts "RM-info: CONTAINER Reference built from RTL"
  puts "RM-info: set_top r:/WORK/${DESIGN_NAME} $FM_SET_TOP_RTL_ARGS"
  eval set_top r:/WORK/${DESIGN_NAME} $FM_SET_TOP_RTL_ARGS
  
  
  if { ${UPF_MODE} != "none" } {
    set upf_files ${UPF_FILE}
    if { [ file exists [ which ${FM_UPF_UPDATE_SUPPLY_SET_FILE} ] ] } {
      lappend upf_files "${FM_UPF_UPDATE_SUPPLY_SET_FILE}"
    }
    puts "RM-info: load_upf -r \"${upf_files}\""
    load_upf -r "${upf_files}"
  }

} else {

  if { $FM_REF_NDM_BLOCK != "" } {
    puts "RM-info: read_ndm -r $FM_REF_NDM -block $DESIGN_NAME/$FM_REF_NDM_BLOCK"
    read_ndm -r $FM_REF_NDM -block $DESIGN_NAME/$FM_REF_NDM_BLOCK
  } else {
    puts "RM-info: read_verilog -r $FM_REF_NETLIST"
    read_verilog -r $FM_REF_NETLIST
  }

  foreach power_model $FM_REFERENCE_POWER_MODELS {
    puts "RM-info: read_power_model -r $power_model"
    read_power_model -r $power_model
  }

  set_top r:/WORK/${DESIGN_NAME}

  if { $FM_REF_NDM_BLOCK == "" } {
    switch ${UPF_MODE} {
      prime {
        puts "RM-info: load_upf -r $FM_REF_UPF_FILE"
        load_upf -r $FM_REF_UPF_FILE
      }
      golden {
        puts "RM-info: load_upf -r -strict_check false -target dc_pg_netlist $FM_REF_UPF_FILE -supplemental $FM_REF_SUPPLEMENTAL_UPF_FILE"
        load_upf -r -strict_check false -target dc_pg_netlist $FM_REF_UPF_FILE -supplemental $FM_REF_SUPPLEMENTAL_UPF_FILE
      }
      none {
      }
    }
  }
}

rm_source -file $FM_POST_REFERENCE_ADJUST_SCRIPT -optional -print FM_POST_REFERENCE_ADJUST_SCRIPT

if { $FM_IMP_NDM_BLOCK != "" } {
  puts "RM-info: read_ndm -i $FM_IMP_NDM -block $DESIGN_NAME/$FM_IMP_NDM_BLOCK"
  read_ndm -i $FM_IMP_NDM -block $DESIGN_NAME/$FM_IMP_NDM_BLOCK
} else {
  puts "RM-info: read_verilog -i $FM_IMP_NETLIST"
  read_verilog -i $FM_IMP_NETLIST
}

foreach power_model $FM_IMPLEMENTATION_POWER_MODELS {
  puts "RM-info: read_power_model -i $power_model"
  read_power_model -i $power_model
}

if { $FM_REF_NDM_BLOCK=="" && $FM_REF_NETLIST=="" } {
  puts "RM-info: Loading Verilog retention models"
  set container "i"
  rm_source -file $FM_RETENTION_MODEL_FILE -optional -print FM_RETENTION_MODEL_FILE
}

set_top i:/WORK/${DESIGN_NAME}

if { $FM_IMP_NDM_BLOCK == "" && ${UPF_MODE} != "none" } {
  switch ${UPF_MODE} {
    prime {
      puts "RM-info: load_upf -i $FM_IMP_UPF_FILE -target dc_netlist"
      load_upf -i $FM_IMP_UPF_FILE -target dc_netlist
    }
    golden {
      puts "RM-info: load_upf -i -strict_check false -target dc_netlist $FM_IMP_UPF_FILE -supplemental $FM_IMP_SUPPLEMENTAL_UPF_FILE"
      load_upf -i -strict_check false -target dc_netlist $FM_IMP_UPF_FILE -supplemental $FM_IMP_SUPPLEMENTAL_UPF_FILE
    }
    none {
    }
  }
}

rm_source -file $FM_CONSTRAINTS_FILE -optional -print "FM_CONSTRAINTS_FILE"

set_app_var verification_force_upf_supplies_on false

match

report_unmatched_points > $REPORTS_DIR/${DESIGN_NAME}.fmv_unmatched_points.rpt

rm_source -file $FM_POST_MATCH_ADJUST_SCRIPT -optional -print FM_POST_MATCH_ADJUST_SCRIPT

if { ![ verify ] } {
  save_session -replace $REPORTS_DIR/${DESIGN_NAME}
  report_failing_points > $REPORTS_DIR/${DESIGN_NAME}.fmv_failing_points.rpt
  report_aborted > $REPORTS_DIR/${DESIGN_NAME}.fmv_aborted_points.rpt
  analyze_points -all > $REPORTS_DIR/${DESIGN_NAME}.fmv_analyze_points.rpt
  set fm_passed FALSE
} else {
  set fm_passed TRUE
}

report_status > $REPORTS_DIR/${DESIGN_NAME}.report_status.rpt

if { $FM_GENERATE_POWER_MODEL } {
  write_power_model -r $OUTPUTS_DIR/$DESIGN_NAME.r -replace
  write_power_model -i $OUTPUTS_DIR/$DESIGN_NAME.i -replace
}

exit

