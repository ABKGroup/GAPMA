# This script was written and developed by ABKGroup students at UCSD.
# However, the underlying commands and reports are copyrighted by Cadence.
# We thank Cadence for granting permission to share our research to help
# promote and foster the next generation of innovators.

set SRC_DIR              $env(SCRIPT_DIR)
set RUN_DIR              $env(RESULT_DIR)
source ${RUN_DIR}/libchar/cells.tcl
set LIB                  $libname
set PROCESS              [lindex $argv 0]
set VDD                  [lindex $argv 1]
set TEMP                 [lindex $argv 2]
set SETTINGS_FILE        ${SRC_DIR}/settings_liberate.tcl
set TEMPLATE_FILE        ${SRC_DIR}/template.tcl
set CELLS_FILE           ${RUN_DIR}/libchar/cells.tcl
set NETLIST_DIR          ${RUN_DIR}/pex
set USERDATA             ${RUN_DIR}/libchar/${libname}_template.lib

puts "INFO: Process command line input:"
foreach arg $argv {
    if { [string match *=* $arg] } {
        lassign [split $arg =] a b
        set $a $b
        puts "INFO:   Setting $a = $b"
    }
}

set PVT                  ${PROCESS}_${VDD}_${TEMP}
set LIBNAME              ${LIB}_${PVT}
set MODEL_INCLUDE_FILE   $env(INPUT_DIR)/PROBE.pm

set THREAD    1
set CLIENTS   0

puts "INFO:"
puts "    SRC_DIR              = ${SRC_DIR}"
puts "    RUN_DIR              = ${RUN_DIR}"
puts "    LIBNAME              = ${LIBNAME}"
puts "    PVT                  = ${PVT}"
puts "    SETTINGS_FILE        = ${SETTINGS_FILE}"
puts "    TEMPLATE_FILE        = ${TEMPLATE_FILE}"
puts "    MODEL_INCLUDE_FILE   = ${MODEL_INCLUDE_FILE}"
puts "    NETLIST_DIR          = ${NETLIST_DIR}"
puts "    USERDATA             = ${USERDATA}"
if { [info exists CELLS_FILE] } { puts "    CELLS_FILE           = ${CELLS_FILE}" }
puts ""
puts "    THREAD               = ${THREAD}"
if { [info exists CLIENTS] } { 
    puts "    CLIENTS              = ${CLIENTS}"
    if { [info exists RSH_CMD] } { puts "    RSH_CMD              = ${RSH_CMD}" }
}
puts ""

puts "INFO: Set Operating Condition"
set_operating_condition -name ${PVT} -voltage ${VDD} -temp ${TEMP}

puts "INFO: Read settings file ${SETTINGS_FILE}"
source ${SETTINGS_FILE}

puts "INFO: Read template file ${TEMPLATE_FILE}"

set MIN_TRAN		[lindex $argv 3]
set MAX_TRAN		[lindex $argv 4]
set MIN_OUT_CAP		[lindex $argv 5]
set INV_X1_PIN_CAP	[lindex $argv 6]

source ${TEMPLATE_FILE}

if {[info exists CELLS_FILE]} {
    if {[file exists ${CELLS_FILE}]} {
	puts "INFO: Read cell list file"
	source ${CELLS_FILE}
    } else {
	puts "WARNING: Specified CELLS_FILE (${CELLS_FILE}) does not exist."
    }
}

puts "INFO: Define device models (spectre, define_leafcell)."
set_var extsim_model_include ${MODEL_INCLUDE_FILE}
define_leafcell -type nmos -pin_position {0 1 2 3} { nmos_rvt }
define_leafcell -type pmos -pin_position {0 1 2 3} { pmos_rvt }

puts "INFO: Read cell netlist "
set packet_cells [packet_slave_cells]
if {[llength $packet_cells]>0} { set cells $packet_cells }
set spicefiles {}
foreach cell $cells { lappend spicefiles ${NETLIST_DIR}/${cell}_${LIB}.sp }
read_spice -format spectre "$MODEL_INCLUDE_FILE $spicefiles"

puts $spicefiles

if { [info exists CLIENTS] && ($CLIENTS > 0) } {
    set_var packet_mode      arc
    set_var rsh_cmd          $RSH_CMD
    if { ($RSH_CMD eq "local") } {
	set_var packet_clients 1
    } else {
	set_var packet_clients $CLIENTS
	if {${THREAD}<1} {
	    puts "WARNING: When using DRM, THREAD=0 can cause client machine to overload; resetting THREAD=1."
	    set THREAD 1
	}
    }
}

puts "INFO: Run Characterization"
char_library -extsim spectre -cells $cells -thread $THREAD

puts "INFO: Write ldb"
file mkdir ${RUN_DIR}/ldb
write_ldb -overwrite ${RUN_DIR}/ldb/${LIBNAME}.ldb

puts "INFO: Write Liberty"
file mkdir ${RUN_DIR}/lib 
write_library -driver_waveform -bus_syntax {[]} -user_data ${USERDATA} -overwrite -filename ${RUN_DIR}/lib/${LIBNAME}_nldm.lib ${LIBNAME}
