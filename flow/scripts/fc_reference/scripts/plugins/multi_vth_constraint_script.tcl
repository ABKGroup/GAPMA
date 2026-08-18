# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.
## Copyright (C) 2014-2023 Synopsys, Inc. All rights reserved.

set VT_CLASS_HVT_LIB_regexp             [list ] ;
set VT_CLASS_SVT_LIB_regexp             [list ] ;
set VT_CLASS_LVT_LIB_regexp             [list ] ;

foreach lib $VT_CLASS_HVT_LIB_regexp  { set_attribute [get_lib_cells -quiet $lib ] threshold_voltage_group hvt -quiet}
foreach lib $VT_CLASS_SVT_LIB_regexp  { set_attribute [get_lib_cells -quiet $lib ] threshold_voltage_group svt -quiet}
foreach lib $VT_CLASS_LVT_LIB_regexp  { set_attribute [get_lib_cells -quiet $lib ] threshold_voltage_group lvt -quiet}

if {[sizeof [get_lib_cells -filter "threshold_voltage_group == hvt"]]} {
        set_threshold_voltage_group_type -type high_vt   {hvt}
}

if {[sizeof [get_lib_cells -filter "threshold_voltage_group == svt"]]} {
        set_threshold_voltage_group_type -type normal_vt {svt}
}

if {[sizeof [get_lib_cells -filter "threshold_voltage_group == lvt"]]} {
        set_threshold_voltage_group_type -type low_vt    {lvt}
}

set ENABLE_AUTO_MULTI_VT_CONSTRAINT	false ;
set LVT_percentage                      ""  ;

if {$ENABLE_AUTO_MULTI_VT_CONSTRAINT} {
   if {$SET_QOR_STRATEGY_METRIC != "timing"} {
      set auto_multi_vth_cmd "auto_multi_vth_constraint -apply"
      if {$LVT_percentage != "" } {lappend auto_multi_vth_cmd -percentage ${LVT_percentage} }
      puts "RM-info: Running ${auto_multi_vth_cmd}"
      eval ${auto_multi_vth_cmd}
   }
}
