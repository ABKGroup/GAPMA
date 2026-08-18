#!/usr/bin/env python3
# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.

import os
import sys
import argparse
import re
import glob

runType = ""
runStyle = "new"
reportPath = glob.glob('*/rpts/*/report_qor')
if reportPath :
  if re.search("rpts",reportPath[0]) is not None:
      if reportPath[0].count("/") == 3:
          runType = "fpm"
          runStyle = "new"
reportPath = glob.glob('*/*/report_qor')
if reportPath :
  runStyle = "new"
  if re.search("rpts_fc",reportPath[0]) is not None:
      runType = "fc"
  if re.search("rpts_icc2",reportPath[0]) is not None:
      runType = "icc2"

reportPath = glob.glob('*/*report_qor')
if reportPath:
  runStyle = "old"
  if re.search("rpts_fc",reportPath[0]) is not None:
      runType = "fc"
  if re.search("rpts_icc2",reportPath[0]) is not None:
      runType = "icc2"

sList = []
if runType == "fpm":
   sortDirList = glob.glob('*/*/*/report_qor')
   for f in sortDirList:
       f_split = f.split("/")[2]
       sList.append(f_split) if f_split not in sList else sList
if runType == "fc" or runType == "icc2":
    if runStyle == "old": 
     sortDirList = glob.glob('*/*report_qor')
     for f in sortDirList:
         f_split = f.split("/")[1]
         f_split = f_split.split(".")[0]
         sList.append(f_split) if f_split not in sList else sList
    else :
     sortDirList = glob.glob('*/*/report_qor')
     for f in sortDirList:
         f_split = f.split("/")[1]
         sList.append(f_split) if f_split not in sList else sList

report_list = ["report_qor","report_global_timing","report_utilization","report_power","report_congestion","check_routes","report_clock_qor.summary","run_end.rpt"]
qor_dict={}
printResults=1
rpt_power=0
power_list = []

print ("{:<15} {:>10} {:>15} {:>6} {:>10} {:>15} {:>8} {:>8} {:>10} {:>8} {:>8} {:>8} {:>6} {:>10} {:>8} {:>4} {:>8} {:>6} {:>10} {:>10} {:>10} {:>10} {:>10} {:>15} {:>10} {:>10} {:>15} {:>8} {:>6} {:>6} {:>6} {:>8}".format(
'Step','Wns','Tns','Nvp','R2RWns','R2RTns','R2RNvp','HWns','HTns','HNvp',
'Tran','Cap','Util',
'Area  ','ClkArea','InstCnt','SeqCellCnt','BitsPF','Buff/Inv','ICG',
'Leakage','Switching','Internal','Dynamic',
'Total','Nets','Wirelength','Gr%','DRC',
'Shorts','Mem(GB)','Wall(hr)'))

for step in sList:

    pwrArry = []
    tPower = []
    qor_dict[step+'_design_setup_wns'] = "NA"
    qor_dict[step+'_design_setup_tns'] = "NA"
    qor_dict[step+'_design_setup_nvp'] = "NA"
    qor_dict[step+'_design_setup_r2rwns'] = "NA"
    qor_dict[step+'_design_setup_r2rtns'] = "NA"
    qor_dict[step+'_design_setup_r2rnvp'] = "NA"
    qor_dict[step+'_design_hold_wns'] = "NA"
    qor_dict[step+'_design_hold_tns'] = " NA"
    qor_dict[step+'_design_hold_nvp'] = "NA"
    qor_dict[step+'_max_tran_cnt'] = "NA"
    qor_dict[step+'_max_cap_cnt'] = "NA"
    qor_dict[step+'_utilization'] = "NA"
    qor_dict[step+'_gr_percent'] = "NA"
    qor_dict[step+'_cell_area'] = "NA"
    qor_dict[step+'_clk_area'] = "NA"
    qor_dict[step+'_instance_cnt'] = "NA"
    qor_dict[step+'_buff_inv_cnt'] ="NA"
    qor_dict[step+'_icg_cnt'] = "NA"
    qor_dict[step+'_leakage_pwr'] = "NA"
    qor_dict[step+'_switch_pwr'] = "NA"
    qor_dict[step+'_internal_pwr'] = "NA"
    qor_dict[step+'_dynamic_pwr'] = "NA"
    qor_dict[step+'_total_pwr'] = "NA"
    qor_dict[step+'_net_cnt'] = "NA"
    qor_dict[step+'_wirelength'] = "NA"
    qor_dict[step+'_wirelength_other'] = "NA"
    qor_dict[step+'_drc_cnt'] = "NA"
    qor_dict[step+'_short_cnt'] = "NA"
    qor_dict[step+'_mem'] = "NA"
    qor_dict[step+'_runtime'] = "NA"
    for rpt in report_list:
        if runStyle != "":
            filename = ""
            fName = ""
            if runType == "fpm":
               filename = glob.glob('*/*/'+step+'/'+rpt)
               if filename:
                 fName = filename[0]
            if runType == "fc" or runType == "icc2":
                if runStyle == "new": 
                 filename = glob.glob('*/'+step+'/'+rpt)
                else :  
                 filename = glob.glob('*/'+step+'.'+rpt)
                if filename:
                 fName = filename[0]

            if os.path.exists(fName):
              rpt_file = open(fName,'r')
              wnsType = "setup" 
              for line in rpt_file:
                  if rpt == "report_global_timing":
                    if re.search("^Hold violations",line) is not None:
                       wnsType = "hold" 
                    if wnsType == "setup":   
                      if re.search("^WNS",line) is not None:
                          tmp_line = line.split()
                          qor_dict[step+'_design_setup_wns'] = str(round(float(tmp_line[1]),2))
                          qor_dict[step+'_design_setup_r2rwns'] = tmp_line[2]
                      if re.search("^TNS",line) is not None:
                          tmp_line = line.split()
                          qor_dict[step+'_design_setup_tns'] = str(round(float(tmp_line[1]),2))
                          qor_dict[step+'_design_setup_r2rtns'] = str(round(float(tmp_line[2]),2))
                      if re.search("^NUM",line) is not None:
                          tmp_line = line.split()
                          qor_dict[step+'_design_setup_nvp'] = tmp_line[1]
                          qor_dict[step+'_design_setup_r2rnvp'] = tmp_line[2]
                    else:
                      if re.search("^WNS",line) is not None:
                          tmp_line = line.split()
                          qor_dict[step+'_design_hold_wns'] = str(round(float(tmp_line[1]),2))
                      if re.search("^TNS",line) is not None:
                          tmp_line = line.split()
                          qor_dict[step+'_design_hold_tns'] = str(round(float(tmp_line[1]),2))
                      if re.search("^NUM",line) is not None:
                          tmp_line = line.split()
                          qor_dict[step+'_design_hold_nvp'] = tmp_line[1]

                  if re.search("Utilization Ratio:",line) is not None:
                      utilVal = line.split()[2]
                      if utilVal !="NA" :
                        qor_dict[step+'_utilization'] = float("{:.2f}".format(float(utilVal)))
                  if re.search("^Leaf Cell Count:",line) is not None:
                      qor_dict[step+'_instance_cnt'] = line.split()[3]
                  if re.search("^Max Trans Violations:",line) is not None:
                      qor_dict[step+'_max_tran_cnt'] = line.split()[3]
                  if re.search("^Max Cap Violations:",line) is not None:
                      qor_dict[step+'_max_cap_cnt'] = line.split()[3]
                  if re.search("^Total Number of Nets:",line) is not None:
                      qor_dict[step+'_net_cnt'] = line.split()[4]
                  if re.search("BitsPerflop:",line) is not None:
                      qor_dict[step+'_bitspf'] = line.split()[1]
                  if re.search("^Sequential Cell Count:",line) is not None:
                      qor_dict[step+'_seq_cell_cnt'] = line.split()[3]
                  if rpt == "report_congestion":
                     if re.search("^phase3. Total Wire Length",line) is not None:
                      qor_dict[step+'_wirelength'] = line.split()[5]
                  if re.search("^Net Length:",line) is not None:
                      qor_dict[step+'_wirelength_other'] = str(round(float(line.split()[2]),2))
                  if re.search("Total   ",line) is not None:
                      if rpt == "report_power":
                         rpt_power = 1; 
                         tPower.append(line.split()[-2]) 
                         pwrArry.append(line.split()) 
                  if re.search("^Both Dirs",line) is not None:
                      qor_dict[step+'_gr_percent'] = line.split()[9]
                  if re.search("^Total number of DRCs =",line) is not None:
                      qor_dict[step+'_drc_cnt'] = line.split()[5]
                  if re.search("Short :",line) is not None:
                      qor_dict[step+'_short_cnt'] = line.split()[2]
                  if re.search("Runtime :",line) is not None:
                      qor_dict[step+'_runtime'] = line.split()[4]
                  if re.search("Memory :",line) is not None:
                      qor_dict[step+'_mem'] = line.split()[4]
                  if re.search("Integrated Clock-Gating Cell Count:",line) is not None:
                      qor_dict[step+'_icg_cnt'] = line.split()[4]
                  if re.search("^Buf/Inv Cell Count:",line) is not None:
                      qor_dict[step+'_buff_inv_cnt'] = line.split()[3]
                  if re.search("Cell Area",line) is not None:
                     if re.search("physical",line) is None:
                        qor_dict[step+'_cell_area'] = str(round(float(line.split()[3]),2))
                  if re.search("All Clocks",line) is not None:
                        qor_dict[step+'_clk_area'] = line.split()[6]
                        qor_dict[step+'_clk_area'] = str(round(float(line.split()[6]),2))
                  if re.search("SNPS_INFO : Runtime",line) is not None:
                     if rpt == "run_end.rpt":
                        minutes = line.split()[4]
                        hr = float(minutes)/60
                        hr = str(round(hr, 2))
                        qor_dict[step+'_runtime'] = hr 
                  if re.search("SNPS_INFO : Memory",line) is not None:
                     if rpt == "run_end.rpt":
                        qor_dict[step+'_mem'] = line.split()[4]

              rpt_file.close()
              if rpt_power:
                 maxIndex = tPower.index(max(tPower))
                 rpt_power = 0
                 if "N/A" in pwrArry[maxIndex]:
                    qor_dict[step+'_internal_pwr'] = pwrArry[maxIndex][1]
                    qor_dict[step+'_switch_pwr'] = pwrArry[maxIndex][2]
                    qor_dict[step+'_leakage_pwr'] = pwrArry[maxIndex][3]
                    qor_dict[step+'_total_pwr'] = pwrArry[maxIndex][-2]
                    qor_dict[step+'_dynamic_pwr'] = "N/A"
                 else:
                    qor_dict[step+'_internal_pwr'] = pwrArry[maxIndex][1]
                    qor_dict[step+'_switch_pwr'] = pwrArry[maxIndex][3]
                    qor_dict[step+'_leakage_pwr'] = pwrArry[maxIndex][5]
                    qor_dict[step+'_total_pwr'] = pwrArry[maxIndex][-2]
                    qor_dict[step+'_dynamic_pwr'] = '%.3E'%(float(qor_dict[step+'_internal_pwr']) + float(qor_dict[step+'_switch_pwr']))
    if qor_dict[step+'_wirelength'] == "NA":
       qor_dict[step+'_wirelength'] = qor_dict[step+'_wirelength_other']   
    if printResults:
        print ("{:<15} {:>10} {:>15} {:>6} {:>10} {:>15} {:>8} {:>8} {:>10} {:>8} {:>8} {:>8} {:>6} {:>10} {:>8} {:>4} {:>10} {:>6} {:>10} {:>10} {:>10} {:>10} {:>10} {:>15} {:>10} {:>10} {:>15} {:>8} {:>6} {:>6} {:>6} {:>8}".format(step,
        qor_dict[step+'_design_setup_wns'],
        qor_dict[step+'_design_setup_tns'],
        qor_dict[step+'_design_setup_nvp'],
        qor_dict[step+'_design_setup_r2rwns'],
        qor_dict[step+'_design_setup_r2rtns'],
        qor_dict[step+'_design_setup_r2rnvp'],
        qor_dict[step+'_design_hold_wns'],
        qor_dict[step+'_design_hold_tns'],
        qor_dict[step+'_design_hold_nvp'],
        qor_dict[step+'_max_tran_cnt'],
        qor_dict[step+'_max_cap_cnt'],
        qor_dict[step+'_utilization'],
        qor_dict[step+'_cell_area'],
        qor_dict[step+'_clk_area'],
        qor_dict[step+'_instance_cnt'],
        qor_dict[step+'_seq_cell_cnt'],
        qor_dict[step+'_bitspf'],
        qor_dict[step+'_buff_inv_cnt'],
        qor_dict[step+'_icg_cnt'],
        qor_dict[step+'_leakage_pwr'],
        qor_dict[step+'_switch_pwr'],
        qor_dict[step+'_internal_pwr'],
        qor_dict[step+'_dynamic_pwr'],
        qor_dict[step+'_total_pwr'],
        qor_dict[step+'_net_cnt'],
        qor_dict[step+'_wirelength'],
        qor_dict[step+'_gr_percent'].split('%')[0],
        qor_dict[step+'_drc_cnt'],
        qor_dict[step+'_short_cnt'],
        qor_dict[step+'_mem'],
        qor_dict[step+'_runtime']))

    qor_dict = {}

