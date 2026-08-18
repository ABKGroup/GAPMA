# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.
# Copyright (C) 2014-2023 Synopsys, Inc. All rights reserved.

set TECHNOLOGY_NODE                                     "" ;
set SET_TECHNOLOGY_AFTER_FLOORPLAN                      false ;
set SIDEFILE_CREATE_FLOORPLAN_FLAT_FLOORPLANNING        "" ;
set SIDEFILE_CREATE_FLOORPLAN_FLAT_BOUNDARY_CELLS       "" ;
set SIDEFILE_CREATE_FLOORPLAN_FLAT_TAP_CELLS            "" ;
set SIDEFILE_CREATE_FLOORPLAN_FLAT_NODE_SUPPLEMENT      "" ;
set SIDEFILE_INIT_DP_FLOORPLANNING                      "" ;
set SIDEFILE_INIT_DP_TECH_SETTINGS                      "" ;
set SIDEFILE_CREATE_FLOORPLAN_FLOORPLANNING             "" ;
set SIDEFILE_PLACE_PINS					"" ;

set SIDEFILE_INIT_DESIGN                                "" ;
set SIDEFILE_CLOCK_OPT_CTS                              "" ;
set SIDEFILE_ROUTE_AUTO                                 "" ;
set SIDEFILE_ROUTE_OPT                                  "" ;
set SIDEFILE_CHIP_FINISH_1                              "chip_finish.tcl.default" ;
set SIDEFILE_CHIP_FINISH_2                              "" ;
set SIDEFILE_TIMING_ECO_1                               "" ;
set SIDEFILE_TIMING_ECO_2                               "chip_finish.tcl.default" ;
set SIDEFILE_TIMING_ECO_3                               "" ;
set SIDEFILE_ICV_IN_DESIGN_1                            "" ;
set SIDEFILE_ICV_IN_DESIGN_2                            "icv_in_design.tcl.default" ;
set SIDEFILE_ICV_IN_DESIGN_CUSTOM_METAL_FILL            "" ;
set SIDEFILE_WRITE_DATA                                 "" ;
set SIDEFILE_WRITE_FULL_CHIP_DATA                       "" ;
set SIDEFILE_FUNCTIONAL_ECO                             "" ;

set ADVANCED_NODE_LIBRARY_TYPE                          "" ;

set SYNOPSYS_LOGIC_LIBRARY_MODE                         "" ;

set HASH_VIA_FLOW                                       false ;

set MIXED_FLOW                                          false ;

set ROUTING_LAYER_MASK_NUMBER_LIST                      "" ;
                                                        ;
                                                        ;

if {![info exists ROUTING_LAYER_DIRECTION_OFFSET_LIST]} {
    set ROUTING_LAYER_DIRECTION_OFFSET_LIST "{M0 horizontal 0.012} {M1 vertical 0.030} {M2 horizontal 0.012} {M3 vertical 0.0} {M4 horizontal 0.0} {M5 vertical 0.0} {M6 horizontal 0.0} {M7 vertical 0.0} {M8 horizontal 0.0} {M9 vertical 0.0} {M10 horizontal 0.0} {M11 vertical 0.0} {M12 horizontal 0.0} {M13 vertical 0.0}"
}
                                                        ;
                                                        ;

set SITE_DEFAULT                                        "" ;
                                                        ;
                                                        ;

if {![info exists MIN_ROUTING_LAYER]} {
    set MIN_ROUTING_LAYER "M0" ;
}

if {![info exists MAX_ROUTING_LAYER]} {
    set MAX_ROUTING_LAYER "" ;
}

set TCL_TRACK_CREATION_FILE                             "" ;

set TCL_PLACEMENT_CONSTRAINT_FILE_LIST                  "" ;
                                                        ;
                                                        ;

set TCL_USER_CONNECT_PG_NET_SCRIPT                      "" ;
                                                        ;

set TCL_COMPILE_PG_FILE                                 "" ;

set PG_STAPLING_VIA                                     false ;

set TCL_LIB_CELL_DONT_USE_FILE                          "./filters/dont_use.tcl" ;
                                                        ;

set TCL_CTS_NDR_RULE_FILE                               "" ;
                                                        ;
                                                        ;

set TCL_VIA_LADDER_DEFINITION_FILE                      "" ;

set TCL_SET_VIA_LADDER_CANDIDATE_FILE                   "" ;

set CHIP_FINISH_METAL_FILLER_LIB_CELL_LIST              "" ;
                                                        ;
set CHIP_FINISH_NON_METAL_FILLER_LIB_CELL_LIST          "" ;
                                                        ;

set CHIP_FINISH_CREATE_CUT_METALS                       false ;

set ICV_IN_DESIGN_CREATE_CUT_METALS                     false ;
                                                        ;

set ICV_IN_DESIGN_PATTERN_MATCHING                      false ;

set WRITE_GDS_DPT_LAYERS                                "" ;

set WRITE_GDS_LAYER_MAP_FILE                            "" ;

set WRITE_GDS_RENAME_CELL_FILE                          "" ;

set WRITE_GDS_MASK_SHIFTED_SUFFIX                       "" ;

set WRITE_GDS_PROPAGATE_PIN_MASK_TO_VIA_METAL           false ;
                                                        ;
                                                        ;
                                                        ;

set WRITE_OASIS_LAYER_MAP_FILE                          "" ;

set WRITE_OASIS_DPT_LAYERS                              "" ;

set WRITE_OASIS_RENAME_CELL_FILE                        "" ;

set WRITE_OASIS_MASK_SHIFTED_SUFFIX                     "" ;

set WRITE_OASIS_PROPAGATE_PIN_MASK_TO_VIA_METAL         false ;
                                                        ;
                                                        ;
                                                        ;

