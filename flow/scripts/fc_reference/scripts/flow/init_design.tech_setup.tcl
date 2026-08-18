# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.
# Copyright (C) 2014-2023 Synopsys, Inc. All rights reserved.
if {$ROUTING_LAYER_DIRECTION_OFFSET_LIST != ""} {
	foreach direction_offset_pair $ROUTING_LAYER_DIRECTION_OFFSET_LIST {
		set layer [lindex $direction_offset_pair 0]
		set direction [lindex $direction_offset_pair 1]
		set offset [lindex $direction_offset_pair 2]
		set_attribute [get_layers $layer] routing_direction $direction
		if {$offset != ""} {
			set_attribute [get_layers $layer] track_offset $offset
		}
	}
} else {
	puts "RM-warning : ROUTING_LAYER_DIRECTION_OFFSET_LIST is not specified. You must manually set routing layer directions and offsets!"
}

if {$SITE_DEFAULT != ""} {
	set_attribute [get_site_defs] is_default false
	set_attribute [get_site_defs $SITE_DEFAULT] is_default true
}

if {$SITE_SYMMETRY_LIST != ""} {
	foreach sym_pair $SITE_SYMMETRY_LIST {
		set site_name [lindex $sym_pair 0]
		set site_sym [lindex $sym_pair 1]
		set_attribute [get_site_defs $site_name] symmetry $site_sym
	}   	
}

