#include "logic_minor/mining.hpp"
#include "logic_minor/canonical.hpp"
#include "logic_minor/eval.hpp"
#include "logic_minor/pattern.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace {

std::mutex g_log_mu;
std::unique_ptr<std::ofstream> g_runtime_log;
std::unique_ptr<std::ofstream> g_included_clusters_log;
std::unique_ptr<std::ofstream> g_excluded_clusters_log;
std::unique_ptr<std::ofstream> g_branch_log;

std::string cluster_cells_csv(const Pattern& p) {
  std::vector<std::string> names;
  names.reserve(p.cells.size());
  for (auto* c : p.cells) names.push_back(c ? c->name : "NULL");
  std::sort(names.begin(), names.end());
  std::ostringstream oss;
  for (size_t i = 0; i < names.size(); ++i) { if (i) oss << ","; oss << names[i]; }
  return oss.str();
}

std::string frontier_cells_csv(const Pattern& p) {
  std::vector<std::string> names;
  names.reserve(p.frontier.size());
  for (auto* c : p.frontier) names.push_back(c ? c->name : "NULL");
  std::sort(names.begin(), names.end());
  std::ostringstream oss;
  for (size_t i = 0; i < names.size(); ++i) { if (i) oss << ","; oss << names[i]; }
  return oss.str();
}

std::string pattern_state_key(const Pattern& p) {
  std::ostringstream oss;
  oss << "root:" << (p.root ? p.root->name : "NULL")
      << ";cells:" << cluster_cells_csv(p)
      << ";frontier:" << frontier_cells_csv(p);
  return oss.str();
}

bool constraints_ok_for_pattern(const Pattern& p, const Opt& opt) {
  int ic = input_count(p);
  int oc = boundary_output_count(p, opt.ignore_nonroot_outputs);
  bool depth_ok = (p.max_depth >= 2);
  bool size_ok = (p.num_cells >= opt.min_cells && p.num_cells <= opt.max_cells) &&
                 (ic >= opt.min_inputs && ic <= opt.max_inputs) &&
                 (oc >= opt.min_outputs && oc <= opt.max_outputs) && depth_ok;
  if (!size_ok) return false;

  if (!opt.allow_internal_fanout && !has_no_external_output(p)) return false;
  return true;
}

void log_included_cluster(const Pattern& p, const std::string& reason,
                          const std::string& vec_key, const std::string& canonical_key,
                          bool constraints_ok, bool has_output_vector, bool target_match) {
  if (!g_included_clusters_log) return;
  std::lock_guard<std::mutex> lk(g_log_mu);
  (*g_included_clusters_log)
      << "reason=" << reason << " | root=" << (p.root ? p.root->name : "NULL")
      << " | state_key=" << pattern_state_key(p) << " | num_cell=" << p.num_cells
      << " | inputs=" << input_count(p) << " | depth=" << p.max_depth
      << " | dist=" << p.max_dist << " | root_dx=" << p.max_root_dx << " | root_dy=" << p.max_root_dy
      << " | constraints_ok=" << (constraints_ok ? 1 : 0) << " | has_output_vector=" << (has_output_vector ? 1 : 0)
      << " | target_match=" << (target_match ? 1 : 0)
      << " | vec_key=" << (vec_key.empty() ? "NA" : vec_key)
      << " | canonical_key=" << (canonical_key.empty() ? "NA" : canonical_key)
      << " | cells=" << cluster_cells_csv(p) << " | frontier=" << frontier_cells_csv(p) << "\n";
}

void log_excluded_cluster(const Pattern& p, const std::string& reason,
                          const std::string& vec_key, const std::string& canonical_key,
                          bool constraints_ok, bool has_output_vector, bool target_match) {
  if (!g_excluded_clusters_log) return;
  std::lock_guard<std::mutex> lk(g_log_mu);
  (*g_excluded_clusters_log)
      << "reason=" << reason << " | root=" << (p.root ? p.root->name : "NULL")
      << " | state_key=" << pattern_state_key(p) << " | num_cell=" << p.num_cells
      << " | inputs=" << input_count(p) << " | depth=" << p.max_depth
      << " | dist=" << p.max_dist << " | root_dx=" << p.max_root_dx << " | root_dy=" << p.max_root_dy
      << " | constraints_ok=" << (constraints_ok ? 1 : 0) << " | has_output_vector=" << (has_output_vector ? 1 : 0)
      << " | target_match=" << (target_match ? 1 : 0)
      << " | vec_key=" << (vec_key.empty() ? "NA" : vec_key)
      << " | canonical_key=" << (canonical_key.empty() ? "NA" : canonical_key)
      << " | cells=" << cluster_cells_csv(p) << " | frontier=" << frontier_cells_csv(p) << "\n";
}

void log_branch(const Pattern& parent, Cell* pred, const std::string& action,
                const std::string& reason, const Pattern* child) {
  if (!g_branch_log) return;
  std::lock_guard<std::mutex> lk(g_log_mu);
  (*g_branch_log)
      << "action=" << action << " | reason=" << reason
      << " | parent_root=" << (parent.root ? parent.root->name : "NULL")
      << " | parent_state_key=" << pattern_state_key(parent)
      << " | parent_cells=" << cluster_cells_csv(parent)
      << " | pred=" << (pred ? pred->name : "NULL");
  if (child) {
    (*g_branch_log)
        << " | child_root=" << (child->root ? child->root->name : "NULL")
        << " | child_state_key=" << pattern_state_key(*child)
        << " | child_cells=" << cluster_cells_csv(*child);
  }
  (*g_branch_log) << "\n";
}

bool same_logic_function(const Master* a, const Master* b) {
  if (!a || !b) return false;
  if (a == b) return true;
  if (a->functions.size() != b->functions.size()) return false;
  return a->functions == b->functions;
}

int cell_output_fanout(Cell* c) {
  int total = 0;
  if (!c) return 0;
  for (const auto& [pin_name, pin] : c->out_pins) {
    if (pin && pin->net) total += static_cast<int>(pin->net->loads.size());
  }
  return total;
}

void collect_equivalent_ids(Cell* rep_cell, Cell* member_cell,
                            const std::unordered_set<Cell*>& rep_cells,
                            std::vector<int>& out_ids,
                            std::unordered_set<Cell*>& visited,
                            bool& any_lower_fanout) {
  if (!member_cell || visited.count(member_cell)) return;
  if (!same_logic_function(rep_cell->master, member_cell->master)) return;
  visited.insert(member_cell);
  if (member_cell->id >= 0) out_ids.push_back(member_cell->id);

  if (cell_output_fanout(member_cell) < cell_output_fanout(rep_cell))
    any_lower_fanout = true;
  for (const auto& [term, rep_net] : rep_cell->fanins) {
    if (!rep_net || !rep_net->driver) continue;
    Cell* rep_pred = rep_net->driver->cell;
    if (!rep_pred || !rep_cells.count(rep_pred)) continue;
    auto mit = member_cell->fanins.find(term);
    if (mit == member_cell->fanins.end() || !mit->second || !mit->second->driver) continue;
    Cell* member_pred = mit->second->driver->cell;
    if (!member_pred) continue;
    if (!same_logic_function(rep_pred->master, member_pred->master)) continue;
    collect_equivalent_ids(rep_pred, member_pred, rep_cells, out_ids, visited, any_lower_fanout);
  }
}

bool verify_fanin_connectivity(Cell* root, const std::unordered_set<Cell*>& cells) {
  if (cells.size() <= 1) return true;
  std::unordered_set<Cell*> reachable;
  std::vector<Cell*> queue = {root};
  reachable.insert(root);
  while (!queue.empty()) {
    Cell* c = queue.back(); queue.pop_back();
    for (const auto& [term, net] : c->fanins) {
      if (!net || !net->driver) continue;
      Cell* pred = net->driver->cell;
      if (pred && cells.count(pred) && !reachable.count(pred)) {
        reachable.insert(pred);
        queue.push_back(pred);
      }
    }
  }
  return reachable.size() == cells.size();
}

void store_rec(std::unordered_map<std::string, Rec>& out, const std::string& vec,
               const Pattern& p, int count,
               const std::string& raw_tt, const std::vector<std::string>& input_order,
               const std::vector<Cell*>& group_members,
               const Opt* opt,
               MappedPatternsTracker* tracker) {
  std::vector<int> rep_ids;
  rep_ids.reserve(p.cells.size());
  for (auto* c : p.cells) { if (c && c->id >= 0) rep_ids.push_back(c->id); }
  std::sort(rep_ids.begin(), rep_ids.end());

  auto& r = out[vec];
  r.count += count;
  r.insts.push_back(rep_ids);
  r.raw_truth_tables.push_back(raw_tt);
  r.input_orders.push_back(input_order);

  for (Cell* member : group_members) {
    if (member == p.root) continue;
    std::vector<int> member_ids;
    member_ids.reserve(p.cells.size());
    std::unordered_set<Cell*> visited;
    bool lower_fanout = false;
    collect_equivalent_ids(p.root, member, p.cells, member_ids, visited, lower_fanout);
    std::sort(member_ids.begin(), member_ids.end());
    if (static_cast<int>(member_ids.size()) != static_cast<int>(p.cells.size())) continue;
    if (!verify_fanin_connectivity(member, visited)) continue;

    Pattern mp;
    mp.root = member;
    mp.num_cells = 0;
    mp.max_depth = p.max_depth;
    mp.max_dist = 0.0;
    mp.max_root_dx = 0.0;
    mp.max_root_dy = 0.0;
    for (auto* vc : visited) {
      if (!vc) continue;
      mp.cells.insert(vc);
      mp.num_cells++;
    }
    if (opt && !constraints_ok_for_pattern(mp, *opt)) continue;
    auto member_eval = evaluate_pattern_outputs(mp, opt ? opt->ignore_nonroot_outputs : false, opt ? opt->reduce_arity : true);
    if (member_eval.signature.empty() || member_eval.signature != vec) continue;

    r.count += 1;
    r.insts.push_back(std::move(member_ids));
    r.raw_truth_tables.push_back(member_eval.raw_truth_table);
    r.input_orders.push_back(member_eval.input_net_order);
    if (tracker) tracker->record(member, vec, lower_fanout);
  }
}

std::vector<std::string> split_sig(const std::string& signature) {
  std::vector<std::string> parts;
  std::stringstream ss(signature);
  std::string tok;
  while (std::getline(ss, tok, ';')) {
    size_t b = tok.find_first_not_of(" \t");
    if (b != std::string::npos) parts.push_back(tok.substr(b, tok.find_last_not_of(" \t") - b + 1));
  }
  return parts;
}

}

void init_runtime_log(const std::filesystem::path& runtime_log_file) {
  g_runtime_log = std::make_unique<std::ofstream>(runtime_log_file);
  if (!g_runtime_log->is_open()) throw std::runtime_error("failed to open runtime log: " + runtime_log_file.string());
  g_runtime_log->setf(std::ios::unitbuf);
}

void set_verification_logs(bool enable, const std::filesystem::path& included_clusters_file,
                           const std::filesystem::path& excluded_clusters_file,
                           const std::filesystem::path& branch_log_file) {
  if (enable) {
    g_included_clusters_log = std::make_unique<std::ofstream>(included_clusters_file);
    g_excluded_clusters_log = std::make_unique<std::ofstream>(excluded_clusters_file);
    g_branch_log = std::make_unique<std::ofstream>(branch_log_file);
  } else {
    g_included_clusters_log.reset();
    g_excluded_clusters_log.reset();
    g_branch_log.reset();
  }
}

void close_verification_logs() {
  g_included_clusters_log.reset();
  g_excluded_clusters_log.reset();
  g_branch_log.reset();
}

void log_line(const std::string& msg) {
  std::lock_guard<std::mutex> lk(g_log_mu);
  std::cout << msg << "\n" << std::flush;
  if (g_runtime_log && g_runtime_log->is_open()) (*g_runtime_log) << msg << "\n" << std::flush;
}

bool visit_pattern(const Pattern& p, const Opt& opt,
                   std::unordered_map<std::string, Rec>& recs,
                   std::unordered_map<std::string, int>& cov,
                   int multiplicity,
                   const std::vector<Cell*>& group_members,
                   MappedPatternsTracker* tracker,
                   const std::unordered_set<std::string>* skip_sigs) {
  if (!has_single_root_output(p)) {
    log_excluded_cluster(p, "prune:single_output_only", "", "", false, false, false);
    return false;
  }
  if (boundary_output_count(p, opt.ignore_nonroot_outputs) > opt.max_outputs) {
    log_excluded_cluster(p, "prune:max_outputs", "", "", false, false, false);
    return false;
  }
  if (has_external_output_exceeding_budget(p, opt)) {
    log_excluded_cluster(p, "prune:external_output_load_budget", "", "", false, false, false);
    return false;
  }
  if (opt.max_distance && p.max_dist > *opt.max_distance) {
    log_excluded_cluster(p, "prune:max_distance", "", "", false, false, false);
    return false;
  }
  if (opt.max_root_dx && p.max_root_dx > *opt.max_root_dx) {
    log_excluded_cluster(p, "prune:max_root_dx", "", "", false, false, false);
    return false;
  }
  if (opt.max_root_dy && p.max_root_dy > *opt.max_root_dy) {
    log_excluded_cluster(p, "prune:max_root_dy", "", "", false, false, false);
    return false;
  }
  if (p.num_cells > opt.max_cells) {
    log_excluded_cluster(p, "prune:max_cells", "", "", false, false, false);
    return false;
  }
  if (p.max_depth > opt.max_depth) {
    log_excluded_cluster(p, "prune:max_depth", "", "", false, false, false);
    return false;
  }

  bool ok = constraints_ok_for_pattern(p, opt);
  if (ok) {
    auto eval = evaluate_pattern_outputs(p, opt.ignore_nonroot_outputs, opt.reduce_arity);
    auto cand_set = split_sig(eval.signature);
    std::unordered_set<std::string> cand_lookup(cand_set.begin(), cand_set.end());
    auto sig = eval.signature;
    bool has_output_vector = eval.has_output_vector && !sig.empty();
    if (!sig.empty()) {
      auto ck = eval.canonical_key;
      bool target_match = opt.target_canonical.empty() || (cand_lookup.find(opt.target_canonical) != cand_lookup.end());
      if (!opt.target_canonical_set.empty()) {
        target_match = false;
        for (const auto& t : opt.target_canonical_set) {
          if (cand_lookup.find(t) != cand_lookup.end()) { target_match = true; break; }
        }
      }
      if (target_match) {

        bool already_mapped = skip_sigs && skip_sigs->count(sig);
        if (!already_mapped) {
          cov[sig] += 1;
          store_rec(recs, sig, p, 1, eval.raw_truth_table, eval.input_net_order,
                    group_members, &opt, tracker);
          if (tracker) tracker->rep_sig_count.fetch_add(1);
          log_included_cluster(p, "include:constraints_ok", sig, ck, true, has_output_vector, true);
        } else {
          log_excluded_cluster(p, "skip:already_mapped", sig, ck, true, has_output_vector, true);
        }
      } else {
        log_excluded_cluster(p, "skip:target_canonical", sig, ck, true, has_output_vector, false);
      }
    } else {
      log_excluded_cluster(p, "skip:empty_output_vector", "", "", true, false, false);
    }
  } else {
    log_excluded_cluster(p, "skip:constraints", "", "", false, false, false);
  }
  return true;
}

std::vector<Pattern> expand_pattern_children(Pattern p) {
  std::vector<Cell*> fr(p.frontier.begin(), p.frontier.end());

  std::vector<Cell*> valid;
  valid.reserve(fr.size());
  for (auto* pred : fr) {
    if (!pred) { log_branch(p, pred, "skip", "null_pred", nullptr); continue; }
    if (pred->master && pred->master->is_seq) { log_branch(p, pred, "skip", "sequential_pred", nullptr); continue; }
    if (pred->master && pred->master->is_hierarchy_boundary) { log_branch(p, pred, "skip", "hierarchy_boundary_pred", nullptr); continue; }
    valid.push_back(pred);
  }

  std::vector<Pattern> children;
  children.reserve(valid.size());
  for (size_t i = 0; i < valid.size(); ++i) {
    Pattern child = add_pred(p, valid[i]);
    log_branch(p, valid[i], "expand", "dfs_recurse", &child);
    children.push_back(std::move(child));
  }
  return children;
}

void merge_records(std::unordered_map<std::string, Rec>& dst,
                   const std::unordered_map<std::string, Rec>& src) {
  for (const auto& [k, r] : src) {
    auto& d = dst[k];
    d.count += r.count;
    d.insts.insert(d.insts.end(), r.insts.begin(), r.insts.end());
    d.raw_truth_tables.insert(d.raw_truth_tables.end(), r.raw_truth_tables.begin(), r.raw_truth_tables.end());
    d.input_orders.insert(d.input_orders.end(), r.input_orders.begin(), r.input_orders.end());
  }
}

void merge_counts(std::unordered_map<std::string, int>& dst,
                  const std::unordered_map<std::string, int>& src) {
  for (const auto& [k, c] : src) dst[k] += c;
}
