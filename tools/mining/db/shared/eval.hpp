#pragma once

#include "types.hpp"

#include <string>
#include <unordered_set>
#include <vector>

struct PatternEvalSummary {
  std::string vec_key;
  std::string signature;
  std::string canonical_key;
  bool has_output_vector = false;
  std::string raw_truth_table;
  std::vector<std::string> input_net_order;
};

bool has_single_root_output(const Pattern& p);
PatternEvalSummary evaluate_pattern_outputs(const Pattern& p);
