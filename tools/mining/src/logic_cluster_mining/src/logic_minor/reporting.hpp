#pragma once

#include "logic_minor/types.hpp"

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

struct ClusterGroupEntry {
  int cid = 0;
  std::string canonical_key;
  std::vector<int> ids;
  std::set<std::string> inputs;
  std::set<std::string> outputs;
  std::string truth_table;
  std::vector<std::string> input_order;
};

double write_outputs(const Cfg& cfg,
                     const Graph& g,
                     const std::unordered_map<std::string, Rec>& recs,
                     const std::unordered_map<std::string, int>& cov);
std::vector<SelectedCandidate> select_existing_library_candidates(const Cfg& cfg,
                                                                  const Graph& g,
                                                                  const std::unordered_map<std::string, Rec>& recs);
void write_selected_candidates_csv(const fs::path& path, const std::vector<SelectedCandidate>& candidates);
void write_graph(const Graph& g, const fs::path& p);
void write_config_log(const Cfg& cfg, int unique_logic_functions, double total_occurrences);
long read_peak_rss_kb();
