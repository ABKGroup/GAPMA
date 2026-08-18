# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.
# Copyright (C) 2014-2023 Synopsys, Inc. All rights reserved.
if { [ info exists block_libfilename ] } { 
   source ./scripts/util/procs_global.tcl 
   source ./scripts/util/procs_fc.tcl 
   rm_source -file ./configs/design_setup.tcl
   rm_source -file sidefile_setup.tcl -after_file technology_override.tcl
   set full_lib_name $block_libfilename
   set block_name $block_refname_no_label
   set full_block_name "${block_libfilename}:${block_refname}.design"
} else {
   set lib_name [get_attribute $block lib_name]
   set full_lib_name [get_attribute [get_lib $lib_name] extended_name]
   set label_name [get_attribute $block label_name]
   set block_name [get_attribute $block block_name]
   set full_block_name "${lib_name}:${block_name}/${label_name}.design"
}

rm_source -file $TCL_USER_LIBRARY_SETUP_SCRIPT -optional -print "TCL_USER_LIBRARY_SETUP_SCRIPT"

open_block -read $full_block_name
set BLOCK_OUTPUT_DIR "${OUTPUTS_DIR}/$block_name"
file delete -force ${BLOCK_OUTPUT_DIR}
file mkdir ${BLOCK_OUTPUT_DIR}

set write_verilog_logic_only_cmd "write_verilog -compress gzip -exclude {scalar_wire_declarations leaf_module_declarations pg_objects end_cap_cells well_tap_cells filler_cells pad_spacer_cells physical_only_cells cover_cells} -hierarchy all ${BLOCK_OUTPUT_DIR}/${block_name}.v"

set write_verilog_dc_cmd "write_verilog -compress gzip -exclude {scalar_wire_declarations leaf_module_declarations pg_objects end_cap_cells well_tap_cells filler_cells pad_spacer_cells physical_only_cells cover_cells diode_cells} -hierarchy all ${BLOCK_OUTPUT_DIR}/${block_name}.dc.v"

set write_verilog_pt_cmd "write_verilog -compress gzip -exclude {scalar_wire_declarations leaf_module_declarations pg_objects end_cap_cells well_tap_cells filler_cells pad_spacer_cells physical_only_cells cover_cells flip_chip_pad_cells} -hierarchy all ${BLOCK_OUTPUT_DIR}/${block_name}.pt.v"
if {$CHIP_FINISH_METAL_FILLER_LIB_CELL_LIST != ""} {
	lappend write_verilog_pt_cmd -force_reference $CHIP_FINISH_METAL_FILLER_LIB_CELL_LIST
}

set write_verilog_fm_cmd "write_verilog -compress gzip -exclude {scalar_wire_declarations leaf_module_declarations end_cap_cells well_tap_cells filler_cells pad_spacer_cells physical_only_cells cover_cells supply_statements} -hierarchy all ${BLOCK_OUTPUT_DIR}/${block_name}.fm.v"

set write_verilog_vclp_cmd "write_verilog -compress gzip -exclude {scalar_wire_declarations leaf_module_declarations end_cap_cells well_tap_cells filler_cells pad_spacer_cells physical_only_cells cover_cells diode_cells supply_statements} -hierarchy all ${BLOCK_OUTPUT_DIR}/${block_name}.vc_lp.v"

puts "RM-info: running $write_verilog_logic_only_cmd"
puts "RM-info: running $write_verilog_dc_cmd"
puts "RM-info: running $write_verilog_pt_cmd"
puts "RM-info: running $write_verilog_fm_cmd"
puts "RM-info: running $write_verilog_vclp_cmd"
parallel_execute -commands_only [list
eval ${write_verilog_logic_only_cmd}
eval ${write_verilog_dc_cmd}
eval ${write_verilog_pt_cmd}
eval ${write_verilog_fm_cmd}
eval ${write_verilog_vclp_cmd}
]

set write_verilog_lvs_cmd "write_verilog -compress gzip -exclude {scalar_wire_declarations leaf_module_declarations empty_modules} -hierarchy all ${BLOCK_OUTPUT_DIR}/${block_name}.lvs.v"

set write_gds_cmd "write_gds -compress -hierarchy all -long_names -keep_data_type ${BLOCK_OUTPUT_DIR}/${block_name}.gds"
if {[file exists $WRITE_GDS_LAYER_MAP_FILE]} {lappend write_gds_cmd -layer_map $WRITE_GDS_LAYER_MAP_FILE}
if {$STREAM_FILES_FOR_MERGE != ""} {
	lappend write_gds_cmd -merge_gds_top_cell ${block_name}
	lappend write_gds_cmd -merge_files $STREAM_FILES_FOR_MERGE
}

set write_oasis_cmd "write_oasis -compress 6 -hierarchy all -keep_data_type ${BLOCK_OUTPUT_DIR}/${block_name}.oasis"
if {[file exists $WRITE_OASIS_LAYER_MAP_FILE]} {lappend write_oasis_cmd -layer_map $WRITE_OASIS_LAYER_MAP_FILE}
if {$STREAM_FILES_FOR_MERGE != ""} {
	lappend write_oasis_cmd -merge_oasis_top_cell ${block_name}
	lappend write_oasis_cmd -merge_files $STREAM_FILES_FOR_MERGE
}

rm_source -file $SIDEFILE_WRITE_DATA -optional -print "SIDEFILE_WRITE_DATA"

puts "RM-info: running $write_verilog_lvs_cmd"
puts "RM-info: running $write_gds_cmd"
puts "RM-info: running $write_oasis_cmd"
parallel_execute -commands_only [list
eval ${write_verilog_lvs_cmd}
eval ${write_gds_cmd}
eval ${write_oasis_cmd}
]

if {$UPF_SUPPLEMENTAL_FILE != ""} {

	save_upf ${BLOCK_OUTPUT_DIR}/${block_name}.supplemental.pg.upf
	set_app_options -name mv.upf.save_upf_include_supply_exceptions -value false
	save_upf ${BLOCK_OUTPUT_DIR}/${block_name}.supplemental.upf

} else {

	save_upf ${BLOCK_OUTPUT_DIR}/${block_name}.upf

}

write_script -force -compress gzip -output ${BLOCK_OUTPUT_DIR}/${block_name}_wscript
write_script -force -compress gzip -format pt -output ${BLOCK_OUTPUT_DIR}/${block_name}_wscript_for_pt

write_routing_constraints ${BLOCK_OUTPUT_DIR}/${block_name}_write_routing_constraints

update_timing
write_parasitics -compress -output ${BLOCK_OUTPUT_DIR}/${block_name}

write_floorplan \
  -format icc2 \
  -def_version 5.8 \
  -force \
  -output ${BLOCK_OUTPUT_DIR}/${block_name}_write_floorplan \
  -read_def_options {-add_def_only_objects {all} -skip_pg_net_connections} \
  -exclude {scan_chains fills pg_metal_fills routing_rules} \
  -net_types {power ground} \
  -include_physical_status {fixed locked}

set write_def_cmd "write_def -compress gzip -version 5.8 -include_tech_via_definitions ${BLOCK_OUTPUT_DIR}/${block_name}.def"
puts "RM-info: running $write_def_cmd"
eval ${write_def_cmd}

close_blocks -force
