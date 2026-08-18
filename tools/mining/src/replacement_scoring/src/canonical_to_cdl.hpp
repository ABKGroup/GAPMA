#pragma once

#include <string>
#include <vector>
#include "cell_metrics.hpp"

int canonical_to_cdl_main(int argc, char** argv);

int canonical_to_cdl_write_batch(const std::vector<std::string>& batch_lines,
                                 const std::string& out_path,
                                 const std::string& pdk = "");

int canonical_to_cdl_write_batch_per_cell(const std::vector<std::string>& batch_lines,
                                          const std::string& out_dir,
                                          const std::string& pdk = "");

int compute_cell_cpp(const cell_metrics::CellData& cell,
                     double wmax_n_nm, double wmax_p_nm);
