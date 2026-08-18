#pragma once

#include "logic_minor/types.hpp"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct MappedPatternsTracker {
  std::mutex mu;
  std::atomic<int> rep_sig_count{0};

  std::unordered_map<Cell*, std::unordered_set<std::string>> mapped;

  std::unordered_set<Cell*> needs_supplementary;

  void record(Cell* member, const std::string& sig, bool lower_fanout) {
    std::lock_guard<std::mutex> lk(mu);
    mapped[member].insert(sig);
    if (lower_fanout) needs_supplementary.insert(member);
  }
};

bool visit_pattern(const Pattern& p,
                   const Opt& opt,
                   std::unordered_map<std::string, Rec>& recs,
                   std::unordered_map<std::string, int>& cov,
                   int multiplicity = 1,
                   const std::vector<Cell*>& group_members = {},
                   MappedPatternsTracker* tracker = nullptr,
                   const std::unordered_set<std::string>* skip_sigs = nullptr);
std::vector<Pattern> expand_pattern_children(Pattern p);

void merge_records(std::unordered_map<std::string, Rec>& dst,
                   const std::unordered_map<std::string, Rec>& src);
void merge_counts(std::unordered_map<std::string, int>& dst,
                  const std::unordered_map<std::string, int>& src);

void init_runtime_log(const std::filesystem::path& runtime_log_file);
void set_verification_logs(bool enable,
                           const std::filesystem::path& included_clusters_file,
                           const std::filesystem::path& excluded_clusters_file,
                           const std::filesystem::path& branch_log_file);
void close_verification_logs();
void log_line(const std::string& msg);
