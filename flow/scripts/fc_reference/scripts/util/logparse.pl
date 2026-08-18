#!/usr/bin/env perl
# (c) 2021 Synopsys, Inc.  All rights reserved.
#
# This script is proprietary and confidential information of
# Synopsys, Inc. and may be used and disclosed only as authorized per
# your agreement with Synopsys, Inc. controlling such use and disclosure.

use warnings;
use strict;

sub parse_error {
    my ($file) = @_ ;
    my $script_error_count = -1;
    my $tool_error_count = 0;
    my $script_start;
    my $script_stop = -1;
    open (my $fd, $file) or die "Failed to read file $file";
    while (my $line = <$fd>) {
        chomp $line;
	      if($line =~ m/^RM-info    : SCRIPT_START :/) {
		   $line =~ s/RM-info    : //;
		   $script_start = $line;
		   $script_stop = 0;
		   $script_error_count = 0;	
		} 
              if($line =~ m/^Error:|^\s+Error:/) {      
		  if ($script_error_count == 0 && $script_stop == 0) {  #Only print the SCRIPT_START line once
		   printf "\n$script_start\n";
		   $script_error_count++;
		  }
                  if ($script_error_count > 0 && $script_error_count <= 20 && $script_stop == 0) {  
			printf "\t$line issued on line $.\n";
		  } 
	          $script_error_count++;	
		  if ($script_error_count == 21 && $script_stop == 0) {
			printf "\tERROR LIMIT REACHED, see log for additional error messages\n";
		  } elsif ($script_stop != 0) { 
			$tool_error_count++; 
			if ($tool_error_count > 0 && $tool_error_count <= 20) { 
			     printf "Tool $line issued on line $.\n"; #Print detected Errors not related to rm_source 
		        } 
			if ($tool_error_count == 21) { printf "ERROR LIMIT REACHED, see log for additional error messages\n";} 
		  }
	      } else {$tool_error_count = 0;}
              if($line =~ m/^RM-info    : SCRIPT_STOP :/) {
		   $line =~ s/RM-info    : //;
		   $script_stop = -1;
		   if($script_error_count > 0) {
                      printf "$line\n\n";
		   }
		$script_error_count = -1;
              }
	}
}

sub parse_rm {
    my ($file) = @_ ;
    my $error = 0;
    my $warning = 0;
    open (my $fd, $file) or die "Failed to read file $file";
    while (my $line = <$fd>) {
        chomp $line;
              if($line =~ m/^RM-error/) {
			printf "$line issued on line $.\n";
			$error = 1; 	
                }
              if($line =~ m/^RM-warning/) {
                        printf "$line issued on line $.\n";
			$warning = 1;
                }
    }
     if($error == 0 && $warning == 0) {
		printf "No RM-error or RM-warning messages detected in log file\n";
      }
}

sub parse_summary {
    my $errors_detected = 0;
    my $SW = 90;
    my $sep0 = '=';
    my ($file) = @_ ;
    open (my $fd, $file) or die "Failed to read file $file";
    while (my $line = <$fd>) {
        chomp $line;
              if($line =~ m/^[A-Z]{2,4}-\d{3,}\s+Error/) {
			if($errors_detected ==0) {
			      &Printf2Log("Error Id          Severity         Limit    Occurrences   Suppressed\n");
		              &Printf2Log("%s\n",($sep0 x $SW));
			}
                        printf "$line\n";
			$errors_detected++;
                }
	}
    return $errors_detected; #pass status of errors_detected
}  

sub Printf2Log {
   my ($format, @values) = @_;
   printf($format,@values);
   #&Printf2Log($format,@values);
}#endsub Printf2Log

sub print_table {
}
sub report {
}

sub usage {
    my $this = (split /\//, $0)[-1];
    my $w = length($this);
    print  STDERR "Usage: $this <log file>\n";
}
###MAIN###
my @files = ();
my $SW = 90;
my $sep0 = '=';
my $sep1 = '-';
#In the future can read file to configure which errors to check
#my @error_types;

if ($ARGV[0] eq '-h' or $ARGV[0] eq '-help' or $ARGV[0] eq '--help') {
        usage;
        exit (0);
    } else {
    push @files, $ARGV[0];
    }

&Printf2Log("%s\n",($sep0 x $SW)); # separator
&Printf2Log("%s\n",($sep0 x $SW)); # separator
&Printf2Log("INFO: Processing @files for tool issued errors\n\n");

for my $log (@files) {
   #&Printf2Log("Error Id          Severity         Limit    Occurrences   Suppressed\n");
   #&Printf2Log("%s\n",($sep0 x $SW));
   my $errors_detected = parse_summary $log;
   if ($errors_detected == 0) {printf "No tool issued errors detected in log file\n";}
   if ($errors_detected > 0) {&Printf2Log("\nDetailed Error Information\n");}
   if ($errors_detected > 0) {&Printf2Log("%s\n",($sep1 x $SW));}
   if ($errors_detected > 0) {parse_error $log;}
   &Printf2Log("%s\n",($sep0 x $SW)); # separator
   &Printf2Log("INFO: Processing @files for RM issued errors and warnings\n\n");
   parse_rm $log;
}
&Printf2Log("%s\n",($sep0 x $SW)); # separator
&Printf2Log("%s\n",($sep0 x $SW)); # separator
