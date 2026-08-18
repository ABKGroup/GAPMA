# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.

set parasitic1 "wst"
set tluplus_file($parasitic1)   "$env(PDK_DIR)/SO3/tlup/PROBE.tlup"
set layer_map_file($parasitic1) "[file dirname [file dirname [info script]]]/layermap/SO3.map"

foreach p [array names tluplus_file] {
    if {$tluplus_file($p) eq "" || $layer_map_file($p) eq ""} { continue }
    puts "RM-info: read_parasitic_tech -tlup $tluplus_file($p) -layermap $layer_map_file($p) -name $p"
    read_parasitic_tech -tlup $tluplus_file($p) -layermap $layer_map_file($p) -name $p
}
