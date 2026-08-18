# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.

puts "\[extract_features_core\] Starting per-cell feature extraction ..."
puts "\[extract_features_core\] Out: $env(FEAT_OUT_CSV)"

puts "\[extract_features_core\] Phase 1: collecting per-instance fanout, cap, slew ..."

array set type_count  {}
array set type_fanout {}
array set type_outcap {}
array set type_islew  {}
array set type_oslew  {}

foreach_in_collection cell [get_cells -hierarchical *] {
    if {[get_attribute $cell is_hierarchical] eq "true"} continue

    set ref [get_attribute $cell ref_name]
    if {[string match "ctmi_*" $ref]} continue

    if {![info exists type_count($ref)]} {
        set type_count($ref)  0
        set type_fanout($ref) {}
        set type_outcap($ref) {}
        set type_islew($ref)  {}
        set type_oslew($ref)  {}
    }
    incr type_count($ref)

    set o_pins [get_pins -of_objects $cell -filter "direction==out"]
    foreach_in_collection opin $o_pins {
        set onet [get_nets -of_objects $opin]
        if {[sizeof_collection $onet] == 0} continue

        set fanout [sizeof_collection [get_pins -of_objects $onet -filter "direction==in"]]
        lappend type_fanout($ref) $fanout

        set cap [get_attribute -class net -name wire_capacitance_max $onet]
        if {$cap eq "" || ![string is double $cap] || $cap <= 0.0} {
            set cap [get_attribute -class net -name total_capacitance $onet]
        }
        if {$cap ne "" && [string is double $cap] && $cap > 0.0} {
            lappend type_outcap($ref) $cap
        }

        set tr_r ""; set tr_f ""
        catch {set tr_r [get_attribute $opin max_rise_transition]}
        catch {set tr_f [get_attribute $opin max_fall_transition]}
        set oslew 0.0
        if {$tr_r ne "" && [string is double $tr_r] && $tr_r > $oslew} { set oslew $tr_r }
        if {$tr_f ne "" && [string is double $tr_f] && $tr_f > $oslew} { set oslew $tr_f }
        if {$oslew > 0.0} { lappend type_oslew($ref) $oslew }
    }

    set i_pins [get_pins -of_objects $cell -filter "direction==in"]
    foreach_in_collection ipin $i_pins {
        set tr_r ""; set tr_f ""
        catch {set tr_r [get_attribute $ipin max_rise_transition]}
        catch {set tr_f [get_attribute $ipin max_fall_transition]}
        set islew 0.0
        if {$tr_r ne "" && [string is double $tr_r] && $tr_r > $islew} { set islew $tr_r }
        if {$tr_f ne "" && [string is double $tr_f] && $tr_f > $islew} { set islew $tr_f }
        if {$islew > 0.0} { lappend type_islew($ref) $islew }
    }
}
puts "\[extract_features_core\] Phase 1 done: [array size type_count] unique cell types."

puts "\[extract_features_core\] Phase 2a: collecting timing paths (max 5000) ..."
set all_paths [get_timing_paths -max_paths 5000 -delay_type max -nworst 1]
set n_paths [sizeof_collection $all_paths]
puts "\[extract_features_core\] Found $n_paths timing paths."

set wns_val 1.0e9
foreach_in_collection path $all_paths {
    set sl [get_attribute $path slack]
    if {$sl ne "" && [string is double $sl] && $sl < $wns_val} {
        set wns_val $sl
    }
}
if {$wns_val > 1.0e8} { set wns_val 0.0 }
puts "\[extract_features_core\] WNS: $wns_val"

set type_list [array names type_count]

array set type_n_through   {}
array set type_n_fail      {}
array set type_worst_slack {}
foreach ref $type_list {
    set type_n_through($ref)   0
    set type_n_fail($ref)      0
    set type_worst_slack($ref) 1.0e9
}

puts "\[extract_features_core\] Phase 2b: per-type path counts + worst slack from global sample ..."
foreach_in_collection path $all_paths {
    set sl [get_attribute -class timing_path -name slack $path]
    if {$sl eq "" || ![string is double $sl]} continue
    set is_fail [expr {$sl < 0 ? 1 : 0}]

    set tp_coll [get_attribute -class timing_path -name points $path]
    set seen {}
    foreach_in_collection tp $tp_coll {
        set obj [get_attribute -class timing_point -name object $tp]
        if {[sizeof_collection $obj] == 0} continue
        if {[get_attribute [index_collection $obj 0] object_class] ne "pin"} continue
        set cell [get_cells -of_objects $obj]
        if {[sizeof_collection $cell] == 0} continue
        set ref [get_attribute $cell ref_name]
        if {[info exists type_n_through($ref)] && [lsearch -exact $seen $ref] < 0} {
            incr type_n_through($ref)
            if {$is_fail} { incr type_n_fail($ref) }
            if {$sl < $type_worst_slack($ref)} { set type_worst_slack($ref) $sl }
            lappend seen $ref
        }
    }
}
foreach ref $type_list {
    if {$type_worst_slack($ref) > 1.0e8} { set type_worst_slack($ref) 0.0 }
}
puts "\[extract_features_core\] Phase 2b done."

puts "\[extract_features_core\] Phase 3: dynamic power (toggle_rate=0.1) ..."
set_app_option -name power.default_toggle_rate -value 0.1
set all_scen [get_scenarios]
if {[sizeof_collection $all_scen] > 0} {
    set_scenario_status -dynamic_power true $all_scen
}
report_power -nosplit

array set type_dynpower {}
foreach ref $type_list {
    set type_dynpower($ref) {}
}
foreach_in_collection cell [get_cells -hierarchical *] {
    if {[get_attribute $cell is_hierarchical] eq "true"} continue
    set ref [get_attribute $cell ref_name]
    if {![info exists type_dynpower($ref)]} continue
    set sw [get_attribute $cell switching_power]
    set ip [get_attribute $cell internal_power]
    set dp 0.0
    if {$sw ne "" && [string is double $sw] && $sw > 0.0} { set dp [expr {$dp + $sw}] }
    if {$ip ne "" && [string is double $ip] && $ip > 0.0} { set dp [expr {$dp + $ip}] }
    if {$dp > 0.0} { lappend type_dynpower($ref) $dp }
}
puts "\[extract_features_core\] Phase 3 done."

set slack_pairs {}
foreach ref $type_list {
    lappend slack_pairs [list $ref $type_worst_slack($ref)]
}
set sorted_slack [lsort -real -index 1 $slack_pairs]
set n_types [llength $sorted_slack]

array set slack_rank {}
for {set i 0} {$i < $n_types} {incr i} {
    set ref [lindex [lindex $sorted_slack $i] 0]
    set slack_rank($ref) [expr {$n_types > 1 ? double($i) / ($n_types - 1) : 0.5}]
}

proc stat3 {vals} {
    if {[llength $vals] == 0} { return {0.0 0.0 0.0} }
    set mn [lindex $vals 0]; set mx $mn; set sm 0.0
    foreach v $vals {
        if {$v < $mn} { set mn $v }
        if {$v > $mx} { set mx $v }
        set sm [expr {$sm + $v}]
    }
    return [list [expr {$sm / [llength $vals]}] $mn $mx]
}
proc fmt6 {v} { return [format "%.6g" $v] }

puts "\[extract_features_core\] Writing $env(FEAT_OUT_CSV) ..."
set fp [open $env(FEAT_OUT_CSV) w]
puts $fp "cell_type,n_instances,avg_fanout,min_fanout,max_fanout,avg_output_cap,min_output_cap,max_output_cap,avg_input_slew,min_input_slew,max_input_slew,avg_output_slew,min_output_slew,max_output_slew,violation_ratio,path_coverage,worst_slack_rank,avg_dynamic_power,min_dynamic_power,max_dynamic_power"

foreach ref $type_list {
    set n $type_count($ref)
    lassign [stat3 $type_fanout($ref)]   af   minf  maxf
    lassign [stat3 $type_outcap($ref)]   ac   minc  maxc
    lassign [stat3 $type_islew($ref)]    ais  mini  maxi
    lassign [stat3 $type_oslew($ref)]    aos  mino  maxo
    lassign [stat3 $type_dynpower($ref)] adp  mindp maxdp

    set vr [expr {$type_n_through($ref) > 0 ? \
        double($type_n_fail($ref)) / $type_n_through($ref) : 0.0}]
    set pc [expr {$n_paths > 0 ? \
        double($type_n_through($ref)) / $n_paths : 0.0}]
    set sr [expr {[info exists slack_rank($ref)] ? $slack_rank($ref) : 0.5}]

    puts $fp "$ref,$n,[fmt6 $af],[fmt6 $minf],[fmt6 $maxf],[fmt6 $ac],[fmt6 $minc],[fmt6 $maxc],[fmt6 $ais],[fmt6 $mini],[fmt6 $maxi],[fmt6 $aos],[fmt6 $mino],[fmt6 $maxo],[fmt6 $vr],[fmt6 $pc],[fmt6 $sr],[fmt6 $adp],[fmt6 $mindp],[fmt6 $maxdp]"
}
close $fp
puts "\[extract_features_core\] Done. Wrote [llength $type_list] cell types to $env(FEAT_OUT_CSV)."
