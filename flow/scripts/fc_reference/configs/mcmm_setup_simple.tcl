# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.

if {![info exists DESIGN_NAME] || $DESIGN_NAME eq ""} {
    error "mcmm_setup_simple.tcl: DESIGN_NAME is not set — source pdk_resolved.tcl first"
}

create_mode    func
create_corner  tt
create_scenario -name func.tt -mode func -corner tt
set_parasitic_parameters -corners {tt} -early_spec wst -late_spec wst

current_scenario func.tt
set_scenario_status func.tt -active true -setup true -hold true

set _sdc_candidates [list \
    ./sdc/${DESIGN_NAME}_fc.sdc \
    ./data/${DESIGN_NAME}_fc.sdc \
    $env(RTL_ROOT)/${DESIGN_NAME}/sdc/${DESIGN_NAME}_fc.sdc \
]
set _sdc_path ""
foreach _c $_sdc_candidates {
    if {[file exists $_c]} { set _sdc_path $_c; break }
}
if {$_sdc_path eq ""} {
    error "mcmm_setup_simple.tcl: SDC not found in any of: $_sdc_candidates"
}
puts "RM-info: mcmm_setup_simple.tcl sourcing SDC $_sdc_path"
source $_sdc_path
unset _sdc_path _sdc_candidates _c
