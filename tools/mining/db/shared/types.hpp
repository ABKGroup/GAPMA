#pragma once

#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

struct SelectedCandidate {
  std::string master_cell;
  std::string source_master_cell;
  std::string candidate_pattern;
  std::string canonical_key;
  std::string logic_expr;
  std::string count;
  double selected_area = 0.0;
  std::string selected_lib;
};

struct Pin;
struct Cell;

struct Net {
  std::string name;
  Pin* driver = nullptr;
  std::vector<Pin*> loads;
};

struct Pin {
  std::string full_name;
  std::string term;
  std::string dir;
  Cell* cell = nullptr;
  Net* net = nullptr;
};

struct Master {
  std::string name;
  std::string family;
  bool is_seq = false;
  bool is_hierarchy_boundary = false;
  std::map<std::string, std::string> functions;
  std::map<std::string, std::string> pin_directions;
  std::set<std::string> input_terms;
};

struct Cell {
  int id = -1;
  std::string name;
  Master* master = nullptr;
  std::map<std::string, Pin*> in_pins;
  std::map<std::string, Pin*> out_pins;
  std::map<std::string, Net*> fanins;
  std::map<std::string, std::vector<Net*>> fanouts;
  double x = std::numeric_limits<double>::quiet_NaN();
  double y = std::numeric_limits<double>::quiet_NaN();
};

struct Graph {
  std::unordered_map<std::string, std::unique_ptr<Master>> masters;
  std::unordered_map<std::string, std::unique_ptr<Cell>> cells;
  std::unordered_map<std::string, std::unique_ptr<Net>> nets;
  std::unordered_map<std::string, std::unique_ptr<Pin>> pins;
  std::vector<Cell*> cell_list;
  std::unordered_map<std::string, std::string> lib_master_family;
  std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> lib_master_funcs;
  std::unordered_map<std::string, std::vector<std::string>> lib_master_inputs;
  std::unordered_map<std::string, std::unordered_set<std::string>> lib_outputs_for_canon;
  std::unordered_map<std::string, std::unordered_set<std::string>> lib_canonicals_for_master;
  std::unordered_map<std::string, std::string> lib_output_signature_for_master;
  std::unordered_map<std::string, double> lib_master_area;
  std::unordered_map<std::string, int> lib_master_output_count;
  std::unordered_map<std::string, std::string> lib_master_source_lib;
};

struct Opt {
  int min_cells = 2;
  int max_cells = 10;
  int min_inputs = 2;
  int max_inputs = 4;
  int max_depth = 3;
  std::optional<double> max_distance;
  std::optional<double> max_root_dx;
  std::optional<double> max_root_dy;
  bool overlap_weighted_counting = false;
  std::string target_canonical;
  std::unordered_set<std::string> target_canonical_set;
  int num_cores = 1;
  int candidate_limit = 0;
  int max_overlap_candidates = 2;
  std::string candidate_pattern_mode = "exact";
  std::vector<std::string> enabled_patterns;
  int min_outputs = 1;
  int max_outputs = 1;

  bool allow_internal_fanout = false;

  bool allow_multi_root_output = true;

  int min_cell_overlap = 1;
};

struct Pattern {
  Cell* root = nullptr;
  std::unordered_set<Cell*> cells;
  std::unordered_set<Cell*> frontier;
  std::unordered_map<Cell*, double> dist;
  std::unordered_map<Cell*, int> depth;
  int num_cells = 0;
  double max_dist = 0.0;
  double max_root_dx = 0.0;
  double max_root_dy = 0.0;
  int max_depth = 1;
};

struct Rec {
  int count = 0;
  std::vector<std::vector<int>> insts;
  std::vector<std::string> raw_truth_tables;
  std::vector<std::vector<std::string>> input_orders;
};

struct EvalCtx {
  const Pattern* p = nullptr;
  std::unordered_map<std::string, bool> assign;
  std::unordered_map<const Pin*, bool> memo;
  std::unordered_set<const Cell*> active;
};
