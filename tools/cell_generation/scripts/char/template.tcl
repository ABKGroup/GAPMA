# This script was written and developed by ABKGroup students at UCSD.
# However, the underlying commands and reports are copyrighted by Cadence.
# We thank Cadence for granting permission to share our research to help
# promote and foster the next generation of innovators.

set_var slew_lower_rise 0.2
set_var slew_lower_fall 0.2
set_var slew_upper_rise 0.5
set_var slew_upper_fall 0.5

set_var measure_slew_lower_rise 0.2
set_var measure_slew_lower_fall 0.2
set_var measure_slew_upper_rise 0.5
set_var measure_slew_upper_fall 0.5

set_var delay_inp_rise 0.35
set_var delay_inp_fall 0.35
set_var delay_out_rise 0.35
set_var delay_out_fall 0.35

set_var def_arc_msg_level 0
set_var process_match_pins_to_ports 1
set_var min_transition $MIN_TRAN
set_var max_transition $MAX_TRAN
set_var min_output_cap $MIN_OUT_CAP

set inv_x1_pin_cap $INV_X1_PIN_CAP
set cap_idx_coef_list [list 2.0 4.0 8.0 16.0 24.0 32.0 45.0]
set cap_idx_list [list]

foreach c $cap_idx_coef_list {
  lappend cap_idx_list [format %.9f [expr $inv_x1_pin_cap*$c]]
}

set max_tran_list [list 0.005 0.010 0.020 0.040 0.080 0.160 0.320]

define_template -type delay -index_2 $cap_idx_list -index_1 $max_tran_list delay_template
define_template -type power -index_2 $cap_idx_list -index_1 $max_tran_list power_template
define_template -type constraint -index_2 $cap_idx_list -index_1 $max_tran_list const_template

source ./results/libchar/cells.tcl

set_pin_vdd -supply_name VDD $cells {*}
set_pin_gnd -supply_name VSS $cells {*}

foreach cell $cells {
  if {[string match "INV_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { I } \
       -output { ZN } \
       -pinlist { I ZN } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "BUF_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { I } \
       -output { Z } \
       -pinlist { I Z } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "AND2_*" $cell] || [string match "OR2_*" $cell] || [string match "XOR2_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { A1 A2 } \
       -output { Z } \
       -pinlist { A1 A2 Z } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "AND3_*" $cell] || [string match "OR3_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { A1 A2 A3 } \
       -output { Z } \
       -pinlist { A1 A2 A3 Z } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "NAND2_*" $cell] || [string match "NOR2_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { A1 A2 } \
       -output { ZN } \
       -pinlist { A1 A2 ZN } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "NAND3_*" $cell] || [string match "NOR3_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { A1 A2 A3 } \
       -output { ZN } \
       -pinlist { A1 A2 A3 ZN } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "NAND4_*" $cell] || [string match "NOR4_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { A1 A2 A3 A4 } \
       -output { ZN } \
       -pinlist { A1 A2 A3 A4 ZN } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "AOI21_*" $cell] || [string match "OAI21_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { A1 A2 B } \
       -output { ZN } \
       -pinlist { A1 A2 B ZN } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "AOI22_*" $cell] || [string match "OAI22_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { A1 A2 B1 B2 } \
       -output { ZN } \
       -pinlist { A1 A2 B1 B2 ZN } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "DFFHQN_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -clock { CLK } \
       -input { D } \
       -output { QN } \
       -pinlist { CLK D QN } \
       -delay delay_template \
       -power power_template \
       -constraint const_template \
       $cell
    }
  }

  if {[string match "DFFRNQ_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -async { RN } \
       -clock { CK } \
       -input { D } \
       -output { Q } \
       -pinlist { CK D RN Q } \
       -delay delay_template \
       -power power_template \
       -constraint const_template \
       $cell
    }
  }

  if {[string match "LHQ_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -clock { E } \
       -input { D } \
       -output { Q } \
       -pinlist { E D Q } \
       -delay delay_template \
       -power power_template \
       -constraint const_template \
       $cell
    }
  }

  if {[string match "MUX2_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { I0 I1 S } \
       -output { Z } \
       -pinlist { I0 I1 S Z } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "2BDFFHQN_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -clock { CLK } \
       -input { D0 D1 } \
       -output { QN0 QN1 } \
       -pinlist { CLK D0 D1 QN0 QN1 } \
       -delay delay_template \
       -power power_template \
       -constraint const_template \
       $cell
    }
    define_bundle_pins $cell D {D0 D1}
    define_bundle_pins $cell QN {QN0 QN1}
  }

  if {[string match "FA_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { A B CI } \
       -output { CON SN } \
       -pinlist { A B CI CON SN } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "HA_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { A B } \
       -output { CON SN } \
       -pinlist { A B CON SN } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "XNOR2_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { A B } \
       -output { Y } \
       -pinlist { A B Y } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "AOI222_*" $cell] || [string match "OAI222_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { A1 A2 B1 B2 C1 C2 } \
       -output { Y } \
       -pinlist { A1 A2 B1 B2 C1 C2 Y } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "AOI211_*" $cell] || [string match "OAI211_*" $cell]} {
    set info_file "${RESULTS_DIR}/cell_info/${cell}.info"
    if {[file exists $info_file]} {
      set fh [open $info_file r]
      set lines [split [read $fh] \n]
      close $fh
      set raw_inputs  [split [lindex $lines 1] " "]
      set raw_outputs [split [lindex $lines 2] " "]
      set input_pins  [lsearch -all -inline -not $raw_inputs  ""]
      set output_pins [lsearch -all -inline -not $raw_outputs ""]
      set pinlist [concat $input_pins $output_pins]
      if {[llength $input_pins] > 0 && [llength $output_pins] > 0 && [ALAPI_active_cell $cell]} {
        define_cell \
         -input  $input_pins  \
         -output $output_pins \
         -pinlist $pinlist    \
         -delay delay_template \
         -power power_template \
         $cell
      }
    }
  }

  if {[string match "AOI221_*" $cell] || [string match "OAI221_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { A1 A2 B1 B2 C } \
       -output { Y } \
       -pinlist { A1 A2 B1 B2 C Y } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "NAND2XOR2_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { A1 A2 B1 } \
       -output { ZN } \
       -pinlist { A1 A2 B1 ZN } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "NAND2XNOR2_*" $cell]} {
    if {[ALAPI_active_cell $cell]} {
      define_cell \
       -input { A1 A2 B1 } \
       -output { ZN } \
       -pinlist { A1 A2 B1 ZN } \
       -delay delay_template \
       -power power_template \
       $cell
    }
  }

  if {[string match "*" $cell] || [string match "*input*" $cell]} {
    set info_file "${RESULTS_DIR}/cell_info/${cell}.info"
    if {[file exists $info_file]} {
      set fh [open $info_file r]
      set lines [split [read $fh] \n]
      close $fh
      set raw_inputs  [split [lindex $lines 1] " "]
      set raw_outputs [split [lindex $lines 2] " "]
      set input_pins  [lsearch -all -inline -not $raw_inputs  ""]
      set output_pins [lsearch -all -inline -not $raw_outputs ""]
      set pinlist [concat $input_pins $output_pins]
      if {[llength $input_pins] > 0 && [llength $output_pins] > 0 && [ALAPI_active_cell $cell]} {
        define_cell \
         -input  $input_pins  \
         -output $output_pins \
         -pinlist $pinlist    \
         -delay delay_template \
         -power power_template \
         $cell
        set_three_state -off [list $cell]
      }
    }
  }

}

