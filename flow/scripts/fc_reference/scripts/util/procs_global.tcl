# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.

# Copyright (C) 2014-2023 Synopsys, Inc. All rights reserved.

proc rm_source { args } {

  global env SEV synopsys_program_name search_path

  set options(-file) ""
  set options(-quiet) 0
  set options(-optional) 0
  set options(-print) ""
  set options(-before_file) ""
  set options(-after_file) ""
  parse_proc_arguments -args $args options

  set error_count 0

  switch $synopsys_program_name {
    tcl -
    spyglass {
      set cmd_verbosity ""
    }
    default {
      if { $options(-quiet) } {
        set cmd_verbosity ""
      } else {
        set cmd_verbosity "-e"
      }
    }
  }

  switch -glob $synopsys_program_name {
    fc*_shell -
    icc*_shell -
    d*_shell -
    p*_shell -
    fm_shell -
    vcst -
    tmax_tcl {
      set cmd_continue "-continue_on_error"
    }
    default {
      set cmd_continue ""
    }
  }

  set file_list [list]

  if { $options(-before_file) != "" } {
    lappend file_spec [list $options(-before_file) 0 "-before_file"]
  }

  lappend file_spec [list $options(-file) $options(-optional) $options(-print)]

  if { $options(-after_file) != "" } {
    lappend file_spec [list $options(-after_file) 0 "-after_file"]
  }

  foreach spec $file_spec {
    set file_root     [lindex $spec 0]
    set optional      [lindex $spec 1]
    set print_string  [lindex $spec 2]
    set script_file_which ""

    if { [llength $file_root] > 0 } {
      set script_file_which [which $file_root]
      if { [llength $script_file_which] > 0 } {
        set script_file [file normalize $script_file_which]
        if { $script_file != [file normalize $file_root] } {
          puts "RM-info     : Found $file_root using search_path"
        }
      } else {
        set script_file $file_root
      }
 
      if { [file exists $script_file] } {

        puts ""

        set date [clock format [clock seconds] -format {%a %b %e %H:%M:%S %Y}]
        puts "RM-info    : SCRIPT_START : [file normalize $script_file] : $date"

        set cmd "uplevel 1 source $cmd_verbosity $cmd_continue $script_file"
        eval $cmd

        puts ""

        set date [clock format [clock seconds] -format {%a %b %e %H:%M:%S %Y}]
        puts "RM-info    : SCRIPT_STOP : [file normalize $script_file] : $date"

      } else {
        if { $print_string != "" } {
          puts "RM-error   : rm_source: The specified file does not exist; '$file_root' : '$print_string'"
        } else {
          puts "RM-error   : rm_source: The specified file does not exist; '$file_root'"
        }
        incr error_count

      }
    } else {

      if {$optional} {
        if { $print_string != "" } {
          puts "RM-info     : rm_source : Optional file corresponds to an empty variable. '$print_string'"
        } else {
          puts "RM-info     : rm_source : Optional file corresponds to an empty variable."
        }
      } else {
        if { $print_string != "" } {
          puts "RM-error    : rm_source : An empty file specification was provided; '$print_string'"
        } else {
          puts "RM-error    : rm_source : An empty file specification was provided"
        }
      }
      incr error_count
    }
  }
  if { $error_count > 0 } {
    return 0
  } else {
    return 1
  }
}

define_proc_attributes rm_source \
  -info "Provides a standard way to source files. Returns a 0 if no file was sourced." \
  -define_args {
  {-file     "Used to specify the file to source." AString string optional}
  {-before_file  "Special option for sourcing a required file before the main file" AString string optional}
  {-after_file  "Special option for sourcing a required file after the main file" AString string optional}
  {-optional "Allows an <empty-string> -file argument to output a message and not an error" "" boolean optional}
  {-print    "string to enhance default messages when file is <empty-string>." AString string optional}
  {-quiet "Echo minimal file content." "" boolean optional}
}

proc rm_get_ews_info { } {

  set rc [list] ;
  set model_name "Unknown" ;
  set cache_size "Unknown" ;
  set cpu_cores  "Unknown" ;
  set freq_mhz   "Unknown" ;

  set cpuinfo_file [open "/proc/cpuinfo" r] ;

  while { [gets $cpuinfo_file line] >= 0 } {
    set token0 [lindex $line 0] ;
    set token1 [lindex $line 1] ;

    if { [string match "model" $token0] && [string match "name" $token1] } {
      set line_length [llength $line] ;
      set model_name [lrange $line 3 [expr $line_length -1]] ;
    } 
    if { [string match "cache" $token0] && [string match "size" $token1] } {
      set line_length [llength $line] ;
      set cache_size [lrange $line 3 [expr $line_length -1]] ;
    }
    if { [string match "cpu" $token0] && [string match "cores" $token1] } {
      set line_length [llength $line] ;
      set cpu_cores [lrange $line 3 [expr $line_length -1]] ;
    }
    if { [string match "cpu" $token0] && [string match "MHz" $token1] } {
      set line_length [llength $line] ;
      set freq_mhz [lrange $line 3 [expr $line_length -1]] ;
    }

  }
  lappend rc $model_name ;
  lappend rc $freq_mhz ;
  lappend rc $cache_size ;
  lappend rc $cpu_cores ;

  close $cpuinfo_file ;

  return $rc ;
}

proc rm_get_mem_info { } {

  set rc [list] ;
  set mem_total "Unknown" ;
  set mem_free  "Unknown" ;
  set mem_available "Unknown" ;
  set swap_total "Unknown" ;
  set swap_free  "Unknown" ;

  set meminfo_file [open "/proc/meminfo" r] ;

  while { [gets $meminfo_file line] >= 0 } {
    set token0 [lindex $line 0] ;

    if { [string match "MemTotal:" $token0] } {
      set mem_total [lindex $line 1] ;
      set unit [lindex $line 2] ;
      if {[string match -nocase "kb" $unit]} {
        set mem_total [expr [lindex $line 1] / 1000000] ;
      } elseif {[string match -nocase "mb" $unit]} {
        set mem_total [expr [lindex $line 1] / 1000] ;
      } elseif {[string match -nocase "mb" $unit]} {
        set mem_total [lindex $line 1] ;
      }
    }
    if { [string match "MemFree:" $token0] } {
      set unit [lindex $line 2] ;
      if {[string match -nocase "kb" $unit]} {
        set mem_free [expr [lindex $line 1] / 1000000] ;
      } elseif {[string match -nocase "mb" $unit]} {
        set mem_free [expr [lindex $line 1] / 1000] ;
      } elseif {[string match -nocase "mb" $unit]} {
        set mem_free [lindex $line 1] ;
      }
    }
    if { [string match "MemAvailable:" $token0] } {
      if {[string match -nocase "kb" $unit]} {
        set mem_available [expr [lindex $line 1] / 1000000] ;
      } elseif {[string match -nocase "mb" $unit]} {
        set mem_available [expr [lindex $line 1] / 1000] ;
      } elseif {[string match -nocase "mb" $unit]} {
        set mem_available [lindex $line 1] ;
      }
    }
    if { [string match "SwapTotal:" $token0] } {
      if {[string match -nocase "kb" $unit]} {
        set swap_total [expr [lindex $line 1] / 1000000] ;
      } elseif {[string match -nocase "mb" $unit]} {
        set swap_total [expr [lindex $line 1] / 1000] ;
      } elseif {[string match -nocase "mb" $unit]} {
        set swap_total [lindex $line 1] ;
      }
    }
    if { [string match "SwapFree:" $token0] } {
      if {[string match -nocase "kb" $unit]} {
        set swap_free [expr [lindex $line 1] / 1000000] ;
      } elseif {[string match -nocase "mb" $unit]} {
        set swap_free [expr [lindex $line 1] / 1000] ;
      } elseif {[string match -nocase "mb" $unit]} {
        set swap_free [lindex $line 1] ;
      }
    }
  }
  lappend rc $mem_total ;
  lappend rc $mem_free ;
  lappend rc $mem_available ;
  lappend rc $swap_total ;
  lappend rc $swap_free ;

  close $meminfo_file ;

  return $rc ;
}

proc rm_report_ews_info {args} {

  set use_snps_sge 0 ;
  parse_proc_arguments -args $args results ;

  set ews_info [rm_get_ews_info] ;
  set mem_info [rm_get_mem_info] ;

  puts [format "#-----------------------------------------------------------------------"] ;
  puts [format "#    EWS Information @ %s" [exec date]] ;
  puts [format "#-----------------------------------------------------------------------"] ;
  puts [format "# Host Name       | %s"       [exec hostname]] ;
  puts [format "# Model Name      | %s"       [lindex $ews_info 0]] ;
  puts [format "# Frequency       | %.2f MHz" [lindex $ews_info 1]] ;
  puts [format "# Cache Memory    | %s"       [lindex $ews_info 2]] ;
  puts [format "# Number of Cores | %s"       [lindex $ews_info 3]] ;
  puts [format "#-----------------+-----------------------------------------------------"] ;
  puts [format "# Total Memory    | %4d GB" [lindex $mem_info 0]] ;
  puts [format "# Free Memory     | %4d GB" [lindex $mem_info 1]] ;
  puts [format "# Available Memory| %4d GB" [lindex $mem_info 2]] ;
  puts [format "#-----------------+-----------------------------------------------------"] ;
  puts [format "# Total Swap      | %4d GB" [lindex $mem_info 3]] ;
  puts [format "# Free Swap       | %4d GB" [lindex $mem_info 4]] ;
  puts [format "#-----------------------------------------------------------------------"] ;
  puts [format ""] ;
}

define_proc_attributes rm_report_ews_info \
  -info "Reports EWS spec information." \
  -define_args {
  }

