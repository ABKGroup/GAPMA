# This script was written and developed by ABKGroup students at UCSD.
# However, the underlying commands and reports are copyrighted by Cadence.
# We thank Cadence for granting permission to share our research to help
# promote and foster the next generation of innovators.

lassign [split [ALAPI_version] .] x1 x2 version(minor) version(sub)
set version(major) "${x1}.${x2}"

set_var extsim_cmd_option      "+aps +spice -mt +liberate +rcopt=2"
set_var extsim_deck_header     "simulator lang=spectre\nOpt1 options reltol=1e-4 \nsimulator lang=spice"
set_var extsim_option          "redefinedparams=ignore hier_ambiguity=lower limit=delta "
set_var extsim_leakage_option  "redefinedparams=ignore hier_ambiguity=lower limit=delta "

set_var ski_enable               1
set_var ski_clean_mode           1  ;
set_var ski_compatibility_mode   1
set_var power_tend_match_tran    1  ;

set_var parse_auto_define_leafcell   0 ;
set_var tmpdir /dev/shm          ;
set_var extsim_deck_dir [file normalize "decks"]   ;
set_var set_var_failure_action   error

if { [info exists libname] && $libname eq "4input0x00A880_X1" } {
    set_var extsim_exclusive      1    ;
    set_var sim_estimate_duration 0    ;
    set_var sim_duration          1e-4 ;
}

set_var predriver_waveform       2 ;

set_var min_capacitance_for_outputs            1        ;

set_var force_condition              4

set_var constraint_info                  2
set_var nochange_mode                    1        ;
if {($version(major)>=17.1) && ($version(minor)>=2)} {
    set_var constraint_vector_mode           4        ;
}

set_var conditional_mpw            0       ;

set_var max_leakage_vector                 [expr 2**10]
set_var leakage_float_internal_supply      0            ;
set_var reset_negative_leakage_power       1            ;

set_var voltage_map                         1	;
set_var pin_based_power                     0	;
set_var power_combinational_include_output  0   ;

set_var force_default_group                 1
set_default_group -criteria                 {power avg}  ;

set_var power_subtract_leakage              4
set_var subtract_hidden_power               2   ;
set_var subtract_hidden_power_use_default   2   ;
set_var power_multi_output_binning_mode     1   ;
set_var power_minimize_switching            1
set_var max_hidden_vector                   [expr 2**10]

set_var ccsn_include_passgate_attr 1  ;
set_var ccsn_model_related_node_attr 1 ;

set_var ccsp_min_pts              15   ;
set_var ccsp_rel_tol              0.01 ;
set_var ccsp_table_reduction      0    ;
set_var ccsp_tail_tol             0.02 ;
set_var ccsp_related_pin_mode     2    ;

if { [info exists CHAR_EM_TECH_FILE] && ($CHAR_EM_TECH_FILE ne "") } { set_var em_tech_file [file normalize $CHAR_EM_TECH_FILE] }

set_var write_library_is_unbuffered            1
set_var cell_use_both_ff_latch_groups          2 ;
set_var user_data_override { power_down_function pg_pin input_signal_level output_signal_level }
set_var sdf_cond_style                         1
set_var parenthesize_not                       0 ;
set_var driver_type_model_pad_check            1 ;
set_var ccsn_print_is_needed_if_false_attr_value 1
set_default_group -criteria {constraint off}    ;
set_var write_library_allow_switching_and_hidden_power 1  ;

if { $::LIBERATE_program == "LIBERATE_LV" } {
    set validate_cells_per_bundle 10000
}

if { $::LIBERATE_program == "VARIETY" } {
    set_var variation_mean_nominal_mode       4 ;
    set_var lvf_constraint_early_late_mode   1 ;

    set_var extsim_monte_option  "sampling=lds" ;
}
