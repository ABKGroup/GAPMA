#pragma once

#include "cluster_db_reader.hpp"
#include "netlist_db_reader.hpp"

#include <string>
#include <vector>

struct MultiOutputParams {
  int max_outputs = 2;
  int max_inputs = 6;
  int num_cores = 8;
  double max_distance = -1.0;
  std::string input_sharing_mode = "shared";
  int min_shared_inputs = 1;
  int min_shared_cells = 0;
};

struct MultiOutputRow {
  int num_inputs = 0;
  int num_outputs = 0;
  std::string canonical;
  int count = 0;
  double avg_input_overlap = 0.0;
  double avg_cell_overlap = 0.0;
};

struct MultiOutputResult {
  std::vector<MultiOutputRow> rows;
  int total_instances = 0;
};

MultiOutputResult run_multi_output_grouping(
    const ClusterDB& db,
    const NetlistDb& ldb,
    const MultiOutputParams& params,
    const std::string& merge_db_path,
    const std::string& instance_out_prefix);
