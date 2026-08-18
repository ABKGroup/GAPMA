# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.
set_app_options -list {route.common.single_connection_to_pins standard_cell_pins}
set_app_options -list {route.common.global_min_layer_mode hard}
set_app_options -list {route.common.net_min_layer_mode allow_pin_connection}
set_app_options -list {route.common.number_of_vias_under_global_min_layer 1}
set_app_options -list {route.common.number_of_vias_under_net_min_layer 0}
set_app_options -list {route.common.net_max_layer_mode hard}

set_app_options -list {route.detail.var_spacing_to_same_net true}
set_app_options -list {route.detail.check_pin_min_area_min_length true }
set_app_options -list {route.detail.check_port_min_area_min_length true}

set die_size [get_attr [get_designs *] boundary_bbox]

set _routing_guide_layers [list]
foreach _triple $ROUTING_LAYER_DIRECTION_OFFSET_LIST {
    lappend _routing_guide_layers [lindex $_triple 0]
}
create_routing_guide -boundary $die_size -layers $_routing_guide_layers -preferred_direction_only
unset _triple _routing_guide_layers
