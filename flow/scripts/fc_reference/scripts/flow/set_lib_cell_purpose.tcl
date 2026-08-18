# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.
# Copyright (C) 2014-2023 Synopsys, Inc. All rights reserved.

suppress_message ATTR-11 ;

rm_source -file $TCL_LIB_CELL_DONT_USE_FILE -optional -print "TCL_LIB_CELL_DONT_USE_FILE"

if {$TIE_LIB_CELL_PATTERN_LIST != ""} {
	set_dont_touch [get_lib_cells $TIE_LIB_CELL_PATTERN_LIST] false
	set_lib_cell_purpose -include optimization [get_lib_cells $TIE_LIB_CELL_PATTERN_LIST]
}

if {$HOLD_FIX_LIB_CELL_PATTERN_LIST != ""} {
	set_dont_touch [get_lib_cells $HOLD_FIX_LIB_CELL_PATTERN_LIST] false
	set_lib_cell_purpose -exclude hold [get_lib_cells */*]
	set_lib_cell_purpose -include hold [get_lib_cells $HOLD_FIX_LIB_CELL_PATTERN_LIST]
}

if {$CTS_LIB_CELL_PATTERN_LIST != "" || $CTS_ONLY_LIB_CELL_PATTERN_LIST != ""} {
	set_lib_cell_purpose -exclude cts [get_lib_cells */*]
}

if {$CTS_LIB_CELL_PATTERN_LIST != ""} {
	set_dont_touch [get_lib_cells $CTS_LIB_CELL_PATTERN_LIST] false ;
	set_lib_cell_purpose -include cts [get_lib_cells $CTS_LIB_CELL_PATTERN_LIST]
} 

if {$CTS_ONLY_LIB_CELL_PATTERN_LIST != ""} {
	set_dont_touch [get_lib_cells $CTS_ONLY_LIB_CELL_PATTERN_LIST] false ;
	set_lib_cell_purpose -include none [get_lib_cells $CTS_ONLY_LIB_CELL_PATTERN_LIST]
	set_lib_cell_purpose -include cts [get_lib_cells $CTS_ONLY_LIB_CELL_PATTERN_LIST]
}

