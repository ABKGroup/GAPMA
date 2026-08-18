# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.
# Copyright (C) 2014-2023 Synopsys, Inc. All rights reserved.

if { [info exists env(RM_VARFILE)] } {
 
	if { [file exists $env(RM_VARFILE)] } {

		source ./scripts/util/procs_global.tcl 
		source ./scripts/util/procs_fc.tcl 
		rm_source -file ./configs/design_setup.tcl
		rm_source -file sidefile_setup.tcl -after_file technology_override.tcl
		if {$HPC_CORE != ""} {
			if {$DESIGN_STYLE == "hier"} {rm_source -file ./flow_override.tcl}
			rm_source -file ./rm_hpc_core_scripts/sidefile_setup_hpc_core.tcl
		}

		rm_source -file $env(RM_VARFILE)

		if { [info exists VIEW_BLOCK_NAME] && [info exists VIEW_TIMESTAMP] } {
			set PREVIOUS_STEP ${VIEW_BLOCK_NAME}
			set CURRENT_STEP view_${VIEW_TIMESTAMP}

			open_lib $DESIGN_LIBRARY
			copy_block -from ${PREVIOUS_STEP} -to ${CURRENT_STEP}
			current_block ${CURRENT_STEP}

			rm_source -file $TCL_USER_NON_PERSISTENT_SCRIPT -optional -print "TCL_USER_NON_PERSISTENT_SCRIPT"
			
			puts "RM-info : opened a copy of ${VIEW_BLOCK_NAME} as view_${VIEW_TIMESTAMP}"
			puts "RM-info : RM's proc files, setup files, and TCL_USER_NON_PERSISTENT_SCRIPT were sourced"

			return
		} else {
			puts "RM-error.view-1: RM variables VIEW_BLOCK_NAME and VIEW_TIMESTAMP are not found in $env(RM_VARFILE). They are defined only if you run RM Makefile's view target."
		}

	} else {

		puts "RM-error.view-2: env(RM_VARFILE) is defined but file is not found. env(RM_VARFILE) is defined and created only if you run RM Makefile's view target."
		exit

	}
} else {

	puts "RM-error.view-3: env(RM_VARFILE) is not defined. env(RM_VARFILE) is defined only if you run RM Makefile's view target."
	exit

}
