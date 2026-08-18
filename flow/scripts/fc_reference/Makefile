# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.
##########################################################################################
# Tool: Fusion Compiler
# Script: Makefile
# Version: U-2022.12-SP4
# Copyright (C) 2014-2023 Synopsys, Inc. All rights reserved.
##########################################################################################

FC_EXEC = fc_shell
FM_EXEC = fm_shell
OPTIONS =

## LOGS_DIR value comes from configs/design_setup.tcl; default is logs_fc
LOGS_DIR := $(subst $\",,$(shell expand configs/design_setup.tcl | grep "^set LOGS_DIR" | awk '{print $$3}'))

## STAGE and REPORT_PARALLEL_SUBMIT_COMMAND values are used by rm_summary target 
STAGE := $(filter-out rm_summary,$(MAKECMDGOALS))
REPORT_PARALLEL_SUBMIT_COMMAND := $(subst $\",,$(shell expand configs/design_setup.tcl | grep "^set REPORT_PARALLEL_SUBMIT_COMMAND" | awk '{print $$3}'))

## Optional: Specify design library if you want backup step and clean step to use it
## CAUTION: if added, the clean step will delete it
DESIGN_LIB =

console:
	$(FC_EXEC)

setup:
	test -d $(LOGS_DIR) || mkdir $(LOGS_DIR)
	date > setup

create_fusion_reference_library: setup
	$(FC_EXEC) $(OPTIONS) -f ./scripts/flow/create_fusion_reference_library.tcl | tee -i $(LOGS_DIR)/create_fusion_reference_library.log

init_design: setup
	$(FC_EXEC) $(OPTIONS) -f ./scripts/flow/init_design.tcl | tee -i $(LOGS_DIR)/init_design.log
	test -f ./init_design || { echo "RM-error : init_design touch file was not found. Pls check init_design.log for RM-error message. Exiting ..."; exit 1; }

compile: setup init_design 
	$(FC_EXEC) $(OPTIONS) -f ./scripts/flow/compile.tcl |tee -i $(LOGS_DIR)/compile.log

################################################################################################################
## Optional compile_fusion for DFT flow 
################################################################################################################
compile_pre_dft: setup init_design 
	rm -f compile_pre_dft.varfile
	echo > compile_pre_dft.varfile
	echo 'set PREVIOUS_STEP $$INIT_DESIGN_BLOCK_NAME' >> compile_pre_dft.varfile
	echo 'set CURRENT_STEP $${COMPILE_BLOCK_NAME}_logic_opto' >> compile_pre_dft.varfile
	echo 'set FC_STEP_LIST "pre_initial_map initial_map logic_opto"' >> compile_pre_dft.varfile
	export RM_VARFILE=compile_pre_dft.varfile; $(FC_EXEC) $(OPTIONS) -f ./scripts/flow/compile.tcl |tee -i $(LOGS_DIR)/compile_pre_dft.log

compile_dft: setup compile_pre_dft
	rm -f compile_dft.varfile
	echo > compile_dft.varfile
	echo 'set PREVIOUS_STEP $${COMPILE_BLOCK_NAME}_logic_opto' >> compile_dft.varfile
	echo 'set CURRENT_STEP $${COMPILE_BLOCK_NAME}_insert_dft' >> compile_dft.varfile
	echo 'set FC_STEP_LIST "insert_dft"' >> compile_dft.varfile
	export RM_VARFILE=compile_dft.varfile; $(FC_EXEC) $(OPTIONS) -f ./scripts/flow/compile.tcl |tee -i $(LOGS_DIR)/compile_dft.log

compile_post_dft: setup compile_dft
	rm -f compile_post_dft.varfile
	echo > compile_post_dft.varfile
	echo 'set PREVIOUS_STEP $${COMPILE_BLOCK_NAME}_insert_dft' >> compile_post_dft.varfile
	echo 'set CURRENT_STEP $$COMPILE_BLOCK_NAME' >> compile_post_dft.varfile
	echo 'set FC_STEP_LIST "initial_place initial_drc initial_opto final_place final_opto"' >> compile_post_dft.varfile
	export RM_VARFILE=compile_post_dft.varfile; $(FC_EXEC) $(OPTIONS) -f ./scripts/flow/compile.tcl |tee -i $(LOGS_DIR)/compile_post_dft.log
################################################################################################################

clock_opt_cts: setup compile
	$(FC_EXEC) $(OPTIONS) -f ./scripts/flow/clock_opt_cts.tcl | tee -i $(LOGS_DIR)/clock_opt_cts.log

clock_opt_opto: setup clock_opt_cts
	$(FC_EXEC) $(OPTIONS) -f ./scripts/flow/clock_opt_opto.tcl | tee -i $(LOGS_DIR)/clock_opt_opto.log

route_auto: setup clock_opt_opto
	$(FC_EXEC) $(OPTIONS) -f ./scripts/flow/route_auto.tcl | tee -i $(LOGS_DIR)/route_auto.log

route_opt: setup route_auto
	$(FC_EXEC) $(OPTIONS) -f ./scripts/flow/route_opt.tcl | tee -i $(LOGS_DIR)/route_opt.log

chip_finish: setup route_opt
	$(FC_EXEC) $(OPTIONS) -f ./scripts/flow/chip_finish.tcl | tee -i $(LOGS_DIR)/chip_finish.log

icv_in_design: setup chip_finish
	$(FC_EXEC) $(OPTIONS) -f ./scripts/flow/icv_in_design.tcl | tee -i $(LOGS_DIR)/icv_in_design.log

all: icv_in_design
	date > all

# Remember to also set WRITE_DATA_FROM_BLOCK_NAME to your desired step as input; default is icv_in_design
write_data: setup 
	$(FC_EXEC) $(OPTIONS) -f ./scripts/flow/write_data.tcl | tee -i $(LOGS_DIR)/write_data.log

## rm_summary target will run the rm_summary script that generates and refreshes a qor summary table 
## To use, submit the rm_summary target in parallel with the appropriate implementation Makefile target 
## make -j -f Makefile route_opt rm_summary
## Note: the rm_summary target should only be submitted in parallel with an implementation makefile target.
## Valid targets: init_design, compile or any substep of compile, clock_opt_cts, clock_opt_opto, route_auto, route_opt, endpoint_opt, timing_eco, icv_in_design, chip_finish 
ifeq ($(MAKECMDGOALS),rm_summary)
rm_summary:
	scripts/util/rm_summary.py  > summary_rm.rpt
else ifneq ($(REPORT_PARALLEL_SUBMIT_COMMAND),)
rm_summary: 
	@until [ -f rpts_fc/$(STAGE)/report_done ] ; do \
                sleep 60 ; \
                scripts/util/rm_summary.py  > summary_rm.rpt ; \
        done 
else
rm_summary: 
	@until [ -f $(STAGE) ] ; do \
                sleep 60 ; \
                scripts/util/rm_summary.py  > summary_rm.rpt ; \
        done
endif

################################################################################################################
## Optional for Flat Synthesis and P&R 
################################################################################################################
# route_opt PBA-CCD targeted optimization
endpoint_opt: route_opt
	$(FC_EXEC) $(OPTIONS) -f ./scripts/flow/endpoint_opt.tcl | tee -i $(LOGS_DIR)/endpoint_opt.log

# Timing Closure ECO flow
# The timing closure ECO target supports both eco_opt and a user provided change file.  This is controlled via
# the PT_ECO_CHANGE_FILE variable in fc_setup.tcl.  If filler cells or metal fill exists in the source design,
# they are automatically reinserted after the ECO.
timing_eco: setup
	$(FC_EXEC) $(OPTIONS) -f ./scripts/flow/timing_eco.tcl | tee -i $(LOGS_DIR)/timing_eco.log

# Functional ECO flow
# The functional ECO target supports both freeze_silicon and MPI.  If filler cells or metal fill exists in the 
# source design, they are automatically reinserted after the ECO.  It is recommended that functional ECOs are 
# followed by a timing closure ECO.
functional_eco: setup
	$(FC_EXEC) $(OPTIONS) -f ./scripts/flow/functional_eco.tcl | tee -i $(LOGS_DIR)/functional_eco.log

# Formality
fm_fc_compile.varfile : setup
	echo '' > fm_fc_compile.varfile
	echo 'set FM_LINK_LIBRARY $$LINK_LIBRARY' >> fm_fc_compile.varfile
	echo 'set FM_SVF_FILES "outputs_fc/init_design.svf ./outputs_fc/compile_logic_opto.svf ./outputs_fc/compile_insert_dft.svf ./outputs_fc/compile.svf"' >> fm_fc_compile.varfile
	echo 'set FM_IMP_NDM_BLOCK compile' >> fm_fc_compile.varfile

fm_fc_compile: fm_fc_compile.varfile
	export RM_VARFILE=fm_fc_compile.varfile ; $(FM_EXEC) $(OPTIONS) -f ./scripts/flow/fm_r2g.tcl | tee -i $(LOGS_DIR)/fm_fc_compile.log
	date > fm_fc_compile


fm_fc_synpnr.varfile : setup
	echo '' > fm_fc_synpnr.varfile
	echo 'set FM_LINK_LIBRARY $$LINK_LIBRARY' >> fm_fc_synpnr.varfile
	echo 'set FM_SVF_FILES "outputs_fc/init_design.svf ./outputs_fc/compile_logic_opto.svf ./outputs_fc/compile_insert_dft.svf ./outputs_fc/compile.svf"' >> fm_fc_synpnr.varfile
	echo 'set FM_IMP_NDM_BLOCK write_data' >> fm_fc_synpnr.varfile

fm_fc_synpnr: fm_fc_synpnr.varfile
	export RM_VARFILE=fm_fc_synpnr.varfile ; $(FM_EXEC) $(OPTIONS) -f ./scripts/flow/fm_r2g.tcl | tee -i $(LOGS_DIR)/fm_fc_synpnr.log
	date > fm_fc_synpnr

# Alternate FM targets for the flow
fm_fc_clock_opt_cts.varfile : setup
	echo '' > fm_fc_clock_opt_cts.varfile
	echo 'set FM_LINK_LIBRARY $$LINK_LIBRARY' >> fm_fc_clock_opt_cts.varfile
	echo 'set FM_SVF_FILES "outputs_fc/init_design.svf ./outputs_fc/compile_logic_opto.svf ./outputs_fc/compile_insert_dft.svf ./outputs_fc/compile.svf"' >> fm_fc_clock_opt_cts.varfile
	echo 'set FM_REF_NDM_BLOCK compile' >> fm_fc_clock_opt_cts.varfile
	echo 'set FM_IMP_NDM_BLOCK clock_opt_cts' >> fm_fc_clock_opt_cts.varfile

fm_fc_clock_opt_cts: fm_fc_clock_opt_cts.varfile
	export RM_VARFILE=fm_fc_clock_opt_cts.varfile ; $(FM_EXEC) $(OPTIONS) -f ./scripts/flow/fm_r2g.tcl | tee -i $(LOGS_DIR)/fm_fc_clock_opt_cts.log
	date > fm_fc_clock_opt_cts

################################################################################################################
# Utilities
################################################################################################################
# Generates a summary report and csv file after any stage of the flow. 
# Running it multiple times will overwrite previous reports with the new report.
summary: setup
	$(FC_EXEC) $(OPTIONS) -f ./scripts/flow/summary.tcl | tee -i $(LOGS_DIR)/summary.log

##Backup
BACKUP  = BACKUP.`date "+%m_%d_%H_%M"`
backup:
	rm -rf $(BACKUP)
	mkdir -p $(BACKUP)
	cp -rf $(DESIGN_LIB) $(LOGS_DIR) $(BACKUP)

##Clean
clean:
	rm -f setup create_fusion_reference_library init_design compile write_data_for_init_design incremental_init_design compile_post_dft clock_opt_cts clock_opt_opto route_auto route_opt chip_finish icv_in_design write_data all endpoint_opt timing_eco functional_eco fm_fc_compile fm_fc_synpnr fm_fc_clock_opt_cts

clean_all: clean
	rm -rf $(LOGS_DIR) $(DESIGN_LIB)

## View a saved block in an interactive session
## view target start
## Required input : VIEW_BLOCK_NAME
## 1. Specify VIEW_BLOCK_NAME with a block name, either in the <design name>/<label> format, such as "RISC_CORE/init_design", or without using a label, such as "test1"
##    Note that RM's saved block format is always $DESIGN_NAME/<stage name>. So if the DESIGN_NAME is RISC_CORE and the stage is init_design, and you want to open the final saved block from this task,
##    you should specify RISC_CORE/init_design as the VIEW_BLOCK_NAME 
## 2. It's recommended to specify the value of VIEW_BLOCK_NAME when running make so there's no edit needed in the makefile. Here is the usage example: 
##    make -f Makefile view VIEW_BLOCK_NAME=RISC_CORE/init_design
##    where view is the makefile target, RISC_CORE/init_design is the name of the saved block which you want to open, 
##    and VIEW_BLOCK_NAME=RISC_CORE/init_design is the syntax to specify a value for VIEW_BLOCK_NAME when running make
## view target end
VIEW_BLOCK_NAME =
VIEW_TIMESTAMP = $(shell date +%m_%d_%H_%M_%S)

ifneq ($(VIEW_BLOCK_NAME),)
view: 
	rm -f view.varfile; echo > view.varfile
	echo 'set VIEW_BLOCK_NAME "${VIEW_BLOCK_NAME}"' >> view.varfile
	echo 'set VIEW_TIMESTAMP "${VIEW_TIMESTAMP}"' >> view.varfile
	export RM_VARFILE=view.varfile; $(FC_EXEC) $(OPTIONS) -f ./scripts/flow/view.tcl | tee -i $(LOGS_DIR)/view_${VIEW_TIMESTAMP}.log
else
view:
	@echo 'RM-error : VIEW_BLOCK_NAME is null. To use view, please specify a valid value for VIEW_BLOCK_NAME.'
	@echo 'RM-error : For example, make -f Makefile view VIEW_BLOCK_NAME=RISC_CORE/route_opt ,where view is the makefile target, RISC_CORE/route_opt is the name of the saved block which you want to open, and VIEW_BLOCK_NAME=RISC_CORE/route_opt is the syntax to specify a value for a makefile variable when running make'
endif
## view target end

