#include "logic_minor/verification.hpp"

#include "logic_minor/mining.hpp"
#include "logic_minor/util.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct LogEntry {
  std::string reason;
  std::string root;
  std::string state_key;
  int num_cell = 0;
  int inputs = 0;
  int depth = 0;
  double dist = 0.0;
  double root_dx = 0.0;
  double root_dy = 0.0;
  bool constraints_ok = false;
  bool has_output_vector = false;
  bool target_match = false;
  std::string vec_key;
  std::string canonical_key;
  std::string cells;
  std::string frontier;
};

struct BranchEntry {
  std::string action;
  std::string reason;
  std::string parent_state_key;
  std::string child_state_key;
};

std::unordered_map<std::string, std::string> parse_kv_line(const std::string& line) {
  std::unordered_map<std::string, std::string> kv;
  std::stringstream ss(line);
  std::string part;
  while (std::getline(ss, part, '|')) {
    auto t = trim(part);
    if (t.empty()) continue;
    auto pos = t.find('=');
    if (pos == std::string::npos) continue;
    kv.emplace(trim(t.substr(0, pos)), trim(t.substr(pos + 1)));
  }
  return kv;
}

bool parse_bool01(const std::unordered_map<std::string, std::string>& kv, const std::string& key) {
  auto it = kv.find(key);
  if (it == kv.end()) throw std::runtime_error("verification: missing key '" + key + "'");
  if (it->second == "1") return true;
  if (it->second == "0") return false;
  throw std::runtime_error("verification: invalid bool field '" + key + "' value='" + it->second + "'");
}

int parse_int_field(const std::unordered_map<std::string, std::string>& kv, const std::string& key) {
  auto it = kv.find(key);
  if (it == kv.end()) throw std::runtime_error("verification: missing key '" + key + "'");
  return std::stoi(it->second);
}

double parse_double_field(const std::unordered_map<std::string, std::string>& kv, const std::string& key) {
  auto it = kv.find(key);
  if (it == kv.end()) throw std::runtime_error("verification: missing key '" + key + "'");
  return std::stod(it->second);
}

std::string get_field(const std::unordered_map<std::string, std::string>& kv, const std::string& key) {
  auto it = kv.find(key);
  if (it == kv.end()) throw std::runtime_error("verification: missing key '" + key + "'");
  return it->second;
}

std::vector<LogEntry> read_log_entries(const fs::path& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("verification: failed to open log " + path.string());
  std::vector<LogEntry> out;
  std::string line;
  while (std::getline(f, line)) {
    auto t = trim(line);
    if (t.empty()) continue;
    auto kv = parse_kv_line(t);
    LogEntry e;
    e.reason = get_field(kv, "reason");
    e.root = get_field(kv, "root");
    e.state_key = get_field(kv, "state_key");
    e.num_cell = parse_int_field(kv, "num_cell");
    e.inputs = parse_int_field(kv, "inputs");
    e.depth = parse_int_field(kv, "depth");
    e.dist = parse_double_field(kv, "dist");
    e.root_dx = parse_double_field(kv, "root_dx");
    e.root_dy = parse_double_field(kv, "root_dy");
    e.constraints_ok = parse_bool01(kv, "constraints_ok");
    e.has_output_vector = parse_bool01(kv, "has_output_vector");
    e.target_match = parse_bool01(kv, "target_match");
    e.vec_key = get_field(kv, "vec_key");
    e.canonical_key = get_field(kv, "canonical_key");
    e.cells = get_field(kv, "cells");
    e.frontier = get_field(kv, "frontier");
    out.push_back(std::move(e));
  }
  return out;
}

std::vector<BranchEntry> read_branch_entries(const fs::path& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("verification: failed to open branch log " + path.string());
  std::vector<BranchEntry> out;
  std::string line;
  while (std::getline(f, line)) {
    auto t = trim(line);
    if (t.empty()) continue;
    auto kv = parse_kv_line(t);
    BranchEntry e;
    e.action = get_field(kv, "action");
    e.reason = get_field(kv, "reason");
    e.parent_state_key = get_field(kv, "parent_state_key");
    auto it = kv.find("child_state_key");
    if (it != kv.end()) e.child_state_key = it->second;
    out.push_back(std::move(e));
  }
  return out;
}

std::string join_sorted(const std::unordered_set<std::string>& vals) {
  std::set<std::string> ordered(vals.begin(), vals.end());
  std::ostringstream oss;
  bool first = true;
  for (const auto& v : ordered) {
    if (!first) oss << ";";
    first = false;
    oss << v;
  }
  return oss.str();
}

void append_error(std::vector<std::string>& errors, const std::string& msg) { errors.push_back(msg); }

bool violates_constraints(const LogEntry& e, const Opt& opt) {
  if (e.num_cell < opt.min_cells || e.num_cell > opt.max_cells) return true;
  if (e.inputs < opt.min_inputs || e.inputs > opt.max_inputs) return true;
  if (e.depth < 1 || e.depth > opt.max_depth) return true;

  if (!e.constraints_ok) return true;
  return false;
}

}

VerificationSummary verify_mining_logs(const Cfg& cfg) {
  VerificationSummary summary;
  std::vector<std::string> errors;

  auto included = read_log_entries(cfg.included_clusters_file);
  auto excluded = read_log_entries(cfg.excluded_clusters_file);
  auto branches = read_branch_entries(cfg.branch_log_file);
  summary.included_entries = static_cast<int>(included.size());
  summary.excluded_entries = static_cast<int>(excluded.size());
  summary.branch_entries = static_cast<int>(branches.size());

  std::unordered_set<std::string> visited_states;
  for (const auto& e : included) visited_states.insert(e.state_key);
  for (const auto& e : excluded) visited_states.insert(e.state_key);

  for (const auto& e : included) {
    if (e.reason != "include:constraints_ok") {
      append_error(errors, "included entry has unexpected reason: " + e.reason + " state=" + e.state_key);
    }
    if (!e.constraints_ok || !e.has_output_vector || !e.target_match) {
      append_error(errors, "included entry flags inconsistent for state=" + e.state_key);
    }
    if (violates_constraints(e, cfg.opt)) {
      append_error(errors, "included entry violates configured constraints for state=" + e.state_key);
    }
    if (cfg.opt.max_distance && e.dist > *cfg.opt.max_distance) {
      append_error(errors, "included entry violates max_distance for state=" + e.state_key);
    }
    if (cfg.opt.max_root_dx && e.root_dx > *cfg.opt.max_root_dx) {
      append_error(errors, "included entry violates max_root_dx for state=" + e.state_key);
    }
    if (cfg.opt.max_root_dy && e.root_dy > *cfg.opt.max_root_dy) {
      append_error(errors, "included entry violates max_root_dy for state=" + e.state_key);
    }
    if (e.vec_key == "NA" || e.canonical_key == "NA") {
      append_error(errors, "included entry missing output-vector fields for state=" + e.state_key);
    }
  }

  for (const auto& e : excluded) {
    if (e.reason == "prune:max_distance") {
      if (!cfg.opt.max_distance || e.dist <= *cfg.opt.max_distance) {
        append_error(errors, "prune:max_distance without actual distance violation for state=" + e.state_key);
      }
    } else if (e.reason == "prune:max_root_dx") {
      if (!cfg.opt.max_root_dx || e.root_dx <= *cfg.opt.max_root_dx) {
        append_error(errors, "prune:max_root_dx without actual violation for state=" + e.state_key);
      }
    } else if (e.reason == "prune:max_root_dy") {
      if (!cfg.opt.max_root_dy || e.root_dy <= *cfg.opt.max_root_dy) {
        append_error(errors, "prune:max_root_dy without actual violation for state=" + e.state_key);
      }
    } else if (e.reason == "prune:max_cells") {
      if (e.num_cell <= cfg.opt.max_cells) {
        append_error(errors, "prune:max_cells without actual cell-count violation for state=" + e.state_key);
      }
    } else if (e.reason == "prune:max_depth") {
      if (e.depth <= cfg.opt.max_depth) {
        append_error(errors, "prune:max_depth without actual depth violation for state=" + e.state_key);
      }
    } else if (e.reason == "skip:constraints") {
      if (e.constraints_ok || !violates_constraints(e, cfg.opt)) {
        append_error(errors, "skip:constraints without actual constraint failure for state=" + e.state_key);
      }
    } else if (e.reason == "skip:target_canonical") {
      if (!e.constraints_ok || !e.has_output_vector || e.target_match) {
        append_error(errors, "skip:target_canonical flags inconsistent for state=" + e.state_key);
      }
    } else if (e.reason == "skip:empty_output_vector") {
      if (!e.constraints_ok || e.has_output_vector) {
        append_error(errors, "skip:empty_output_vector flags inconsistent for state=" + e.state_key);
      }
    } else if (e.reason == "prune:single_output_only") {

    } else if (e.reason == "prune:external_output_load_budget") {

    } else if (e.reason == "skip:already_mapped") {

    } else {
      append_error(errors, "unexpected excluded reason: " + e.reason + " state=" + e.state_key);
    }
  }

  for (const auto& b : branches) {
    if (b.action == "expand") {
      if (b.child_state_key.empty()) {
        append_error(errors, "expand branch missing child_state_key for parent=" + b.parent_state_key);
        continue;
      }
      if (!visited_states.count(b.child_state_key)) {
        append_error(errors, "expand branch child state never classified: " + b.child_state_key);
      }
    }
  }

  std::ofstream report(cfg.verification_report_file);
  if (!report) {
    throw std::runtime_error("verification: failed to open report file " + cfg.verification_report_file.string());
  }
  report << "included_entries=" << summary.included_entries << "\n";
  report << "excluded_entries=" << summary.excluded_entries << "\n";
  report << "branch_entries=" << summary.branch_entries << "\n";
  report << "visited_state_count=" << visited_states.size() << "\n";
  report << "target_canonical=" << cfg.opt.target_canonical << "\n";
  report << "target_canonical_set=" << join_sorted(cfg.opt.target_canonical_set) << "\n";
  report << "errors=" << errors.size() << "\n";
  for (const auto& err : errors) report << "ERROR: " << err << "\n";

  summary.errors = static_cast<int>(errors.size());
  if (summary.errors > 0) {
    throw std::runtime_error("verification failed; see " + cfg.verification_report_file.string());
  }
  return summary;
}
