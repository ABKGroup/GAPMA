#include "logic_minor/canonical.hpp"
#include "logic_minor/frontend.hpp"
#include "logic_minor/graph_prep.hpp"
#include "logic_minor/mining.hpp"
#include "logic_minor/pattern.hpp"
#include "logic_minor/reporting.hpp"
#include "logic_minor/verification.hpp"
#include "logic_minor/util.hpp"

#include "multi_output_grouping.hpp"
#include "cluster_db_reader.hpp"
#include "netlist_db_reader.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

constexpr size_t kLocalShardFlushThreshold = 512;

static void collect_cone(Cell* cell, int depth, std::unordered_set<Cell*>& cone) {
  if (!cell || cone.count(cell)) return;
  cone.insert(cell);
  if (depth <= 0) return;
  for (const auto& [term, net] : cell->fanins) {
    if (!net || !net->driver) continue;
    Cell* drv = net->driver->cell;
    if (!drv || (drv->master && drv->master->is_seq)) continue;
    collect_cone(drv, depth - 1, cone);
  }
}

static size_t cell_structural_hash(Cell* cell, int depth,
                                   const std::unordered_set<Cell*>& cone,
                                   std::unordered_map<Cell*, size_t>& memo) {
  if (!cell) return 0;
  auto it = memo.find(cell);
  if (it != memo.end()) return it->second;
  memo[cell] = 0;

  size_t h = std::hash<std::string>{}(cell->master ? cell->master->name : "__null__");

  {
    int ext = 0;
    for (const auto& [pin_name, pin] : cell->out_pins) {
      if (!pin || !pin->net) continue;
      for (auto* load : pin->net->loads) {
        if (!load || !load->cell) { ext++; continue; }
        if (!cone.count(load->cell)) ext++;
      }
    }
    h ^= std::hash<int>{}(ext) + 0x517cc1b7ULL + (h << 6) + (h >> 2);
  }
  if (depth > 0) {
    std::vector<std::pair<std::string, size_t>> fanin_pairs;
    for (const auto& [term, net] : cell->fanins) {
      if (!net || !net->driver) {
        fanin_pairs.push_back({term, 0xDEADBEEFULL});
        continue;
      }
      Cell* drv = net->driver->cell;
      if (!drv || (drv->master && drv->master->is_seq)) {
        fanin_pairs.push_back({term, 0xBEEFCAFEULL});
      } else {
        fanin_pairs.push_back({term, cell_structural_hash(drv, depth - 1, cone, memo)});
      }
    }
    std::sort(fanin_pairs.begin(), fanin_pairs.end());
    for (const auto& [t, fh] : fanin_pairs) {
      h ^= std::hash<std::string>{}(t)  + 0x9e3779b9ULL + (h << 6) + (h >> 2);
      h ^= fh                           + 0x9e3779b9ULL + (h << 6) + (h >> 2);
    }
  }
  memo[cell] = h;
  return h;
}

struct PatternDedup {
  std::unordered_set<size_t> seen;

  static size_t hash_cells(const std::unordered_set<Cell*>& cells) {

    size_t h = cells.size();
    for (auto* c : cells) {
      size_t id = static_cast<size_t>(c ? c->id : 0);
      h ^= id * 0x9e3779b97f4a7c15ULL + 0x9e3779b9ULL + (h << 6) + (h >> 2);
    }
    return h;
  }

  bool insert(const Pattern& p) {
    return seen.insert(hash_cells(p.cells)).second;
  }
};

struct AggregationShard {
  std::mutex mu;
  std::unordered_map<std::string, Rec> recs;
  std::unordered_map<std::string, int> cov;
};

void merge_record_data(Rec& dst, const Rec& src) {
  dst.count += src.count;
  dst.insts.insert(dst.insts.end(), src.insts.begin(), src.insts.end());
  dst.raw_truth_tables.insert(dst.raw_truth_tables.end(), src.raw_truth_tables.begin(), src.raw_truth_tables.end());
  dst.input_orders.insert(dst.input_orders.end(), src.input_orders.begin(), src.input_orders.end());
}

void flush_local_into_shards(std::vector<AggregationShard>& shards,
                             std::unordered_map<std::string, Rec>& local_recs,
                             std::unordered_map<std::string, int>& local_cov) {
  if (shards.empty()) return;
  std::hash<std::string> hasher;

  for (auto& [key, rec] : local_recs) {
    auto& shard = shards[hasher(key) % shards.size()];
    std::lock_guard<std::mutex> lk(shard.mu);
    merge_record_data(shard.recs[key], rec);
  }
  local_recs.clear();

  for (auto& [key, count] : local_cov) {
    auto& shard = shards[hasher(key) % shards.size()];
    std::lock_guard<std::mutex> lk(shard.mu);
    shard.cov[key] += count;
  }
  local_cov.clear();
}

void mine_graph_once(const Cfg& cfg,
                     Graph& g,
                     std::unordered_map<std::string, Rec>& recs,
                     std::unordered_map<std::string, int>& cov) {
  std::vector<Cell*> roots;
  for (auto* c : g.cell_list) {
    if (!c || !c->master || c->master->is_seq || c->master->is_hierarchy_boundary || is_buffer(c)) continue;
    int root_outputs = 0;
    for (const auto& kv : c->out_pins) {
      if (kv.second) root_outputs += 1;
    }
    if (root_outputs != 1) continue;
    roots.push_back(c);
  }

  std::unordered_map<size_t, int> hash_to_rep_idx;
  std::vector<int> root_mult(roots.size(), 0);
  std::unordered_map<size_t, std::vector<Cell*>> hash_groups;
  std::vector<size_t> root_hashes(roots.size());

  for (int i = 0; i < static_cast<int>(roots.size()); ++i) {
    size_t h;
    if (cfg.no_structural_hash) {
      h = static_cast<size_t>(i);
    } else {
      std::unordered_set<Cell*> cone;
      int hash_depth = cfg.opt.max_depth - 1;
      collect_cone(roots[i], hash_depth, cone);
      std::unordered_map<Cell*, size_t> memo;
      h = cell_structural_hash(roots[i], hash_depth, cone, memo);
    }
    root_hashes[static_cast<size_t>(i)] = h;
    hash_groups[h].push_back(roots[i]);
    auto [it, inserted] = hash_to_rep_idx.try_emplace(h, i);
    if (inserted) {
      root_mult[i] = 1;
    } else {
      root_mult[it->second]++;
    }
  }

  struct UniqueRoot { Cell* cell; size_t hash; std::vector<Cell*> group_members; };
  std::vector<UniqueRoot> unique_roots;
  unique_roots.reserve(roots.size());
  for (int i = 0; i < static_cast<int>(roots.size()); ++i) {
    if (root_mult[i] > 0) {
      unique_roots.push_back({roots[i], root_hashes[static_cast<size_t>(i)],
                              hash_groups[root_hashes[static_cast<size_t>(i)]]});
    }
  }

  const int orig_total = static_cast<int>(roots.size());
  int total = static_cast<int>(unique_roots.size());
  if (total < orig_total) {
    log_line("[info] structural_hash: " + std::to_string(total) +
             " unique roots / " + std::to_string(orig_total) + " total");
  }

  int workers = std::max(1, cfg.opt.num_cores);
  if (workers > total && total > 0) workers = total;
  if (total <= 0) return;

  std::sort(unique_roots.begin(), unique_roots.end(), [](const UniqueRoot& a, const UniqueRoot& b) {
    return a.cell->in_pins.size() > b.cell->in_pins.size();
  });

  std::vector<std::unique_ptr<MappedPatternsTracker>> trackers(static_cast<size_t>(total));
  for (int i = 0; i < total; ++i) {
    if (unique_roots[static_cast<size_t>(i)].group_members.size() > 1)
      trackers[static_cast<size_t>(i)] = std::make_unique<MappedPatternsTracker>();
  }

  std::atomic<int> next_root{0};
  std::atomic<int> done{0};
  std::vector<AggregationShard> shards(static_cast<size_t>(std::max(16, workers * 4)));
  std::vector<std::thread> threads;
  threads.reserve(workers);
  for (int t = 0; t < workers; ++t) {
    threads.emplace_back([&]() {
      std::unordered_map<std::string, Rec> local_recs;
      std::unordered_map<std::string, int> local_cov;
      while (true) {
        int i = next_root.fetch_add(1);
        if (i >= total) break;
        const auto& ur = unique_roots[static_cast<size_t>(i)];
        MappedPatternsTracker* tracker = trackers[static_cast<size_t>(i)].get();

        PatternDedup dedup;
        std::vector<Pattern> stack;
        stack.push_back(init_pattern(ur.cell));
        while (!stack.empty()) {
          Pattern current = std::move(stack.back());
          stack.pop_back();
          if (!dedup.insert(current)) continue;
          if (!visit_pattern(current, cfg.opt, local_recs, local_cov,
                             1, ur.group_members, tracker, nullptr)) continue;
          auto children = expand_pattern_children(std::move(current));
          for (auto it = children.rbegin(); it != children.rend(); ++it)
            stack.push_back(std::move(*it));
        }

        if (local_recs.size() + local_cov.size() >= kLocalShardFlushThreshold)
          flush_local_into_shards(shards, local_recs, local_cov);

        int d = done.fetch_add(1) + 1;
        if (d % 100 == 0 || d == total)
          log_line("[progress] Pattern mining " + std::to_string(d) + "/" + std::to_string(total));
      }
      flush_local_into_shards(shards, local_recs, local_cov);
    });
  }
  for (auto& th : threads) th.join();

  for (auto& shard : shards) {
    std::lock_guard<std::mutex> lk(shard.mu);
    merge_records(recs, shard.recs);
    merge_counts(cov, shard.cov);
  }

  if (!cfg.no_structural_hash) {
    struct SuppRoot { Cell* cell; const std::unordered_set<std::string>* skip_sigs; };
    std::vector<SuppRoot> supp_roots;
    int fully_mapped = 0;
    for (int i = 0; i < total; ++i) {
      auto* trk = trackers[static_cast<size_t>(i)].get();
      if (!trk) continue;
      const auto& ur = unique_roots[static_cast<size_t>(i)];
      int rep_sigs = trk->rep_sig_count.load();
      for (Cell* member : ur.group_members) {
        if (member == ur.cell) continue;
        auto mit = trk->mapped.find(member);
        int mapped_count = (mit != trk->mapped.end()) ? static_cast<int>(mit->second.size()) : 0;
        bool needs_supp = trk->needs_supplementary.count(member)
                          || mapped_count < rep_sigs;
        if (needs_supp) {
          const std::unordered_set<std::string>* skip =
              (mit != trk->mapped.end() && !mit->second.empty()) ? &mit->second : nullptr;
          supp_roots.push_back({member, skip});
        } else {
          ++fully_mapped;
        }
      }
    }
    if (!supp_roots.empty()) {
      log_line("[info] supplementary_dfs: " + std::to_string(supp_roots.size()) +
               " members need DFS, " + std::to_string(fully_mapped) + " fully mapped (skipped)");
      int supp_total = static_cast<int>(supp_roots.size());
      int supp_workers = std::max(1, cfg.opt.num_cores);
      if (supp_workers > supp_total) supp_workers = supp_total;

      std::sort(supp_roots.begin(), supp_roots.end(), [](const SuppRoot& a, const SuppRoot& b) {
        return a.cell->in_pins.size() > b.cell->in_pins.size();
      });

      std::atomic<int> supp_next{0};
      std::atomic<int> supp_done{0};
      std::vector<AggregationShard> supp_shards(static_cast<size_t>(std::max(16, supp_workers * 4)));
      std::vector<std::thread> supp_threads;
      supp_threads.reserve(supp_workers);

      for (int t = 0; t < supp_workers; ++t) {
        supp_threads.emplace_back([&]() {
          std::unordered_map<std::string, Rec> local_recs;
          std::unordered_map<std::string, int> local_cov;
          while (true) {
            int i = supp_next.fetch_add(1);
            if (i >= supp_total) break;
            const auto& sr = supp_roots[static_cast<size_t>(i)];

            PatternDedup dedup;
            std::vector<Pattern> stack;
            stack.push_back(init_pattern(sr.cell));
            while (!stack.empty()) {
              Pattern current = std::move(stack.back());
              stack.pop_back();
              if (!dedup.insert(current)) continue;
              if (!visit_pattern(current, cfg.opt, local_recs, local_cov,
                                 1, {}, nullptr, sr.skip_sigs)) continue;
              auto children = expand_pattern_children(std::move(current));
              for (auto it = children.rbegin(); it != children.rend(); ++it)
                stack.push_back(std::move(*it));
            }

            if (local_recs.size() + local_cov.size() >= kLocalShardFlushThreshold)
              flush_local_into_shards(supp_shards, local_recs, local_cov);

            int d = supp_done.fetch_add(1) + 1;
            if (d % 100 == 0 || d == supp_total)
              log_line("[progress] Supplementary DFS " + std::to_string(d) + "/" + std::to_string(supp_total));
          }
          flush_local_into_shards(supp_shards, local_recs, local_cov);
        });
      }
      for (auto& th : supp_threads) th.join();
      for (auto& shard : supp_shards) {
        std::lock_guard<std::mutex> lk(shard.mu);
        merge_records(recs, shard.recs);
        merge_counts(cov, shard.cov);
      }
      log_line("[info] supplementary_dfs complete");
    } else if (fully_mapped > 0) {
      log_line("[info] structural_hash: all " + std::to_string(fully_mapped) +
               " non-representative members fully mapped, no supplementary DFS needed");
    }
  }
}

}

int main(int argc, char** argv) {
  try {
    auto t_start = std::chrono::steady_clock::now();
    std::string target_expr;
    std::string target_canonical;
    std::optional<int> cli_num_cores;
    bool cli_overlap_weighted_counting = false;
    std::optional<int> cli_min_cells, cli_max_cells, cli_min_inputs, cli_max_inputs, cli_max_depth;
    std::optional<double> cli_max_distance, cli_max_root_dx, cli_max_root_dy;
    std::optional<fs::path> cli_selected_candidates;
    std::optional<int> cli_candidate_limit, cli_max_overlap_candidates;
    std::optional<std::string> cli_candidate_pattern_mode;
    bool cli_cdb_only = false;
    bool cli_allow_internal_fanout = false;
    bool cli_no_structural_hash = false;
    bool cli_verification_mode = false;
    std::optional<int> cli_min_outputs, cli_max_outputs;
    std::optional<int> cli_min_cell_overlap;
    std::optional<fs::path> cli_cell_db, cli_netlist_db;
    std::optional<fs::path> cli_out_dir;
    std::optional<std::string> cli_input_sharing_mode;
    std::optional<int> cli_min_shared_inputs;
    bool cli_add_np_class = true;
    bool cli_reduce_arity = true;
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      auto need = [&](const std::string& flag) {
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + flag);
        return std::string(argv[++i]);
      };
      if (a == "--target_expr")
        target_expr = need(a);
      else if (a == "--target_canonical")
        target_canonical = need(a);
      else if (a == "--num_cores")
        cli_num_cores = std::stoi(need(a));
      else if (a == "--overlap_weighted_counting")
        cli_overlap_weighted_counting = true;
      else if (a == "--min_cells")
        cli_min_cells = std::stoi(need(a));
      else if (a == "--max_cells")
        cli_max_cells = std::stoi(need(a));
      else if (a == "--min_inputs")
        cli_min_inputs = std::stoi(need(a));
      else if (a == "--max_inputs")
        cli_max_inputs = std::stoi(need(a));
      else if (a == "--max_depth")
        cli_max_depth = std::stoi(need(a));
      else if (a == "--max_distance")
        cli_max_distance = std::stod(need(a));
      else if (a == "--max_root_dx")
        cli_max_root_dx = std::stod(need(a));
      else if (a == "--max_root_dy")
        cli_max_root_dy = std::stod(need(a));
      else if (a == "--selected_candidates")
        cli_selected_candidates = need(a);
      else if (a == "--candidate_limit")
        cli_candidate_limit = std::stoi(need(a));
      else if (a == "--max_overlap_candidates")
        cli_max_overlap_candidates = std::stoi(need(a));
      else if (a == "--candidate_pattern_mode")
        cli_candidate_pattern_mode = need(a);
      else if (a == "--cdb-only")
        cli_cdb_only = true;
      else if (a == "--cell-db")
        cli_cell_db = need(a);
      else if (a == "--netlist-db")
        cli_netlist_db = need(a);
      else if (a == "--allow_internal_fanout")
        cli_allow_internal_fanout = true;
      else if (a == "--min_outputs")
        cli_min_outputs = std::stoi(need(a));
      else if (a == "--max_outputs")
        cli_max_outputs = std::stoi(need(a));
      else if (a == "--min_cell_overlap")
        cli_min_cell_overlap = std::stoi(need(a));
      else if (a == "--no_structural_hash")
        cli_no_structural_hash = true;
      else if (a == "--input_sharing_mode") {
        cli_input_sharing_mode = need(a);
        if (*cli_input_sharing_mode != "shared" && *cli_input_sharing_mode != "exact") {
          throw std::runtime_error("--input_sharing_mode must be 'shared' or 'exact'");
        }
      }
      else if (a == "--min_shared_inputs") {
        cli_min_shared_inputs = std::stoi(need(a));
        if (*cli_min_shared_inputs < 1) {
          throw std::runtime_error("--min_shared_inputs must be >= 1");
        }
      }
      else if (a == "--verification_mode")
        cli_verification_mode = true;
      else if (a == "--add_np_class" || a == "--add-np-class")
        cli_add_np_class = true;
      else if (a == "--no_add_np_class" || a == "--no-add-np-class")
        cli_add_np_class = false;
      else if (a == "--no-reduce-arity")
        cli_reduce_arity = false;
      else if (a == "--out-dir" || a == "--out_dir")
        cli_out_dir = need(a);
      else if (a == "--help" || a == "-h") {
        std::cout <<
          "usage: logic_func_minor_cpp --cell-db <cell.cdb> --netlist-db <netlist.cdb>\n"
          "                            [--out-dir DIR]\n"
          "\n"
          "Required:\n"
          "  --cell-db <path>        Input cell.cdb\n"
          "  --netlist-db <path>     Input netlist.cdb\n"
          "\n"
          "Output:\n"
          "  --out-dir DIR           Output directory (default: ./logic_func_minor_results)\n"
          "  --cdb-only              Write only clusters.cdb; suppress CSV/text reports (default: text enabled)\n"
          "\n"
          "Mining constraints:\n"
          "  --min_cells N           Min cells per cluster (default: 2)\n"
          "  --max_cells N           Max cells per cluster (default: 8)\n"
          "  --min_inputs N          Min boundary inputs (default: 2)\n"
          "  --max_inputs N          Max boundary inputs (default: 4)\n"
          "  --min_outputs N         Min boundary outputs (default: 1)\n"
          "  --max_outputs N         Max boundary outputs (default: 1)\n"
          "  --max_depth N           Max fanin depth from root (default: 3)\n"
          "  --max_distance F        Max Euclidean distance (requires DEF in netlist.cdb)\n"
          "  --max_root_dx F         Max X-distance from root\n"
          "  --max_root_dy F         Max Y-distance from root\n"
          "  --min_cell_overlap N    Min shared cells for multi-output pair (default: 1)\n"
          "  --allow_internal_fanout Allow non-root cells to have external fanout;\n"
          "                          also excludes those fanouts from output count (enables NAND3/NOR3 mining)\n"
          "  --no_structural_hash    Disable structural hashing dedup\n"
          "  --overlap_weighted_counting  Weight occurrence counts by cell overlap\n"
          "  --no-reduce-arity       Keep the declared input count as-is; do not drop\n"
          "                          variables the function does not depend on before\n"
          "                          canonicalization (default: reduction is ON)\n"
          "\n"
          "Multi-output grouping (runs automatically when --max_outputs >= 2):\n"
          "  --input_sharing_mode M  shared | exact (default: shared)\n"
          "  --min_shared_inputs N   min shared inputs in 'shared' mode (default: 1)\n"
          "\n"
          "Target a specific function:\n"
          "  --target_expr EXPR              Mine only clusters matching Boolean expr\n"
          "  --target_canonical KEY[;KEY2;...] Mine only clusters matching canonical key(s); semicolon-delimited for multiple\n"
          "\n"
          "Candidate selection:\n"
          "  --selected_candidates PATH      Write library candidate CSV\n"
          "  --candidate_limit N             Max candidates to select\n"
          "  --max_overlap_candidates N      Max secondary overlap candidates\n"
          "  --candidate_pattern_mode M      exact | family_wildcard\n"
          "\n"
          "Other:\n"
          "  --num_cores N           Parallel threads (default: 8)\n"
          "  --verification_mode     Enable detailed per-cluster decision logs\n"
          "  --add_np_class          Append NPN representative aggregate rows ([NPN] prefix\n"
          "                          in master_cell) to pattern_frequency.csv (default: on)\n"
          "  --no_add_np_class       Disable NPN aggregate rows\n";
        return 0;
      }
      else {
        throw std::runtime_error("unknown argument: " + a);
      }
    }

    if (!cli_cell_db.has_value() || !cli_netlist_db.has_value()) {
      throw std::runtime_error("--cell-db and --netlist-db are required. See --help.");
    }

    Cfg cfg;
    cfg.cell_db_input    = fs::absolute(*cli_cell_db);
    cfg.netlist_db_input = fs::absolute(*cli_netlist_db);

    if (cli_out_dir) {
      cfg.out_dir = fs::absolute(*cli_out_dir);
    } else {

      std::time_t now = std::time(nullptr);
      std::tm tm{};
      localtime_r(&now, &tm);
      char buf[32];
      std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
      cfg.out_dir = fs::absolute("logic_func_minor_results") / buf;
    }
    cfg.run_dir  = cfg.out_dir;
    cfg.logs_dir = cfg.out_dir;

    cfg.cdb_file                 = cfg.out_dir / "clusters.cdb";
    cfg.freq_file                = cfg.out_dir / "pattern_frequency.csv";
    cfg.detail_file              = cfg.out_dir / "pattern_instances.out";
    cfg.overlap_file             = cfg.out_dir / "overlap_clusters.out";
    cfg.subset_file              = cfg.out_dir / "subset_clusters.out";
    cfg.map_file                 = cfg.out_dir / "cell_id_map.csv";
    cfg.cov_file                 = cfg.out_dir / "logic_coverage.csv";
    cfg.graph_file               = cfg.out_dir / "graph_snapshot.out";
    cfg.config_log_file          = cfg.out_dir / "config.log";
    cfg.runtime_log_file         = cfg.out_dir / "runtime.log";
    cfg.included_clusters_file   = cfg.out_dir / "included_clusters.log";
    cfg.excluded_clusters_file   = cfg.out_dir / "excluded_clusters.log";
    cfg.branch_log_file          = cfg.out_dir / "branch_trace.log";
    cfg.verification_report_file = cfg.out_dir / "verification_report.log";
    cfg.np_class_map_file        = cfg.out_dir / "p_to_np_class.csv";

    if (cli_min_cells)  cfg.opt.min_cells  = *cli_min_cells;
    if (cli_max_cells)  cfg.opt.max_cells  = *cli_max_cells;
    if (cli_min_inputs) cfg.opt.min_inputs = *cli_min_inputs;
    if (cli_max_inputs) cfg.opt.max_inputs = *cli_max_inputs;
    if (cli_max_depth)  cfg.opt.max_depth  = *cli_max_depth;
    if (cli_max_distance) cfg.opt.max_distance = *cli_max_distance;
    if (cli_max_root_dx)  cfg.opt.max_root_dx  = *cli_max_root_dx;
    if (cli_max_root_dy)  cfg.opt.max_root_dy  = *cli_max_root_dy;
    if (cli_overlap_weighted_counting) cfg.opt.overlap_weighted_counting = true;
    cfg.opt.num_cores = (cli_num_cores && *cli_num_cores > 0) ? *cli_num_cores : 8;
    cfg.text_outputs = !cli_cdb_only;
    cfg.verification_mode = cli_verification_mode;
    cfg.add_np_class = cli_add_np_class;
    if (!target_expr.empty() && !target_canonical.empty()) {
      throw std::runtime_error("Use only one of --target_expr or --target_canonical.");
    }
    if (!target_expr.empty()) {
      auto ins = detect_expr_inputs(target_expr);
      if (ins.empty()) throw std::runtime_error("--target_expr has no detectable input symbols.");
      cfg.opt.target_canonical = normalize_target_canonical(canonical_from_expr(target_expr, ins));
      cfg.opt.min_inputs = (int)ins.size();
      cfg.opt.max_inputs = (int)ins.size();
    } else if (!target_canonical.empty()) {

      std::vector<std::string> items;
      {
        std::stringstream ss(target_canonical);
        std::string tok;
        while (std::getline(ss, tok, ';')) {
          auto t = trim(tok);
          if (!t.empty()) items.push_back(t);
        }
      }
      if (items.empty()) {
        throw std::runtime_error("--target_canonical is empty after parsing.");
      }
      if (items.size() == 1) {
        cfg.opt.target_canonical = normalize_target_canonical(items[0]);
        auto ni = input_count_from_canonical(cfg.opt.target_canonical);
        if (ni) {
          cfg.opt.min_inputs = *ni;
          cfg.opt.max_inputs = *ni;
        }
      } else {
        std::optional<int> inferred_inputs;
        for (const auto& item : items) {
          auto norm = normalize_target_canonical(item);
          cfg.opt.target_canonical_set.insert(norm);
          auto ni = input_count_from_canonical(norm);
          if (ni) {
            if (!inferred_inputs) {
              inferred_inputs = *ni;
            } else if (*inferred_inputs != *ni) {
              throw std::runtime_error("all canonical keys in --target_canonical must imply same input count.");
            }
          }
        }
        if (inferred_inputs) {
          cfg.opt.min_inputs = *inferred_inputs;
          cfg.opt.max_inputs = *inferred_inputs;
        }
      }
    }

    if (cli_selected_candidates) cfg.selected_candidates_file = cfg.out_dir / cli_selected_candidates->filename();
    if (cli_candidate_limit && *cli_candidate_limit > 0) cfg.opt.candidate_limit = *cli_candidate_limit;
    if (cli_max_overlap_candidates && *cli_max_overlap_candidates >= 0) {
      cfg.opt.max_overlap_candidates = *cli_max_overlap_candidates;
    }
    if (cli_candidate_pattern_mode) cfg.opt.candidate_pattern_mode = *cli_candidate_pattern_mode;
    if (cli_allow_internal_fanout) {
      cfg.opt.allow_internal_fanout = true;
      cfg.opt.ignore_nonroot_outputs = true;
    }
    if (cli_min_outputs && *cli_min_outputs >= 1) cfg.opt.min_outputs = *cli_min_outputs;
    if (cli_max_outputs && *cli_max_outputs >= 1) cfg.opt.max_outputs = *cli_max_outputs;
    if (cli_min_cell_overlap && *cli_min_cell_overlap >= 0) cfg.opt.min_cell_overlap = *cli_min_cell_overlap;
    if (cli_no_structural_hash) cfg.no_structural_hash = true;
    if (!cli_reduce_arity) cfg.opt.reduce_arity = false;
    fs::create_directories(cfg.out_dir);
    fs::create_directories(cfg.logs_dir);
    init_runtime_log(cfg.runtime_log_file);
    set_verification_logs(cfg.verification_mode,
                          cfg.included_clusters_file,
                          cfg.excluded_clusters_file,
                          cfg.branch_log_file);

    log_line("[config] out_dir=" + cfg.out_dir.string());
    log_line("[config] num_cores=" + std::to_string(cfg.opt.num_cores));
    log_line(std::string("[config] verification_mode=") + (cfg.verification_mode ? "true" : "false"));
    log_line(std::string("[config] reduce_arity=") + (cfg.opt.reduce_arity ? "true" : "false"));

    std::unordered_map<std::string, Rec> recs;
    std::unordered_map<std::string, int> cov;
    Graph g;
    if (!cfg.cell_db_input.empty() && !cfg.netlist_db_input.empty()) {
      log_line("[stage] Building graph from CDB inputs...");
      load_graph_from_cdbs(cfg, cfg.cell_db_input.string(), cfg.netlist_db_input.string(), g);
      remove_sequential_cells_from_graph(g);
      remove_buffer_cells_from_graph(g);
      split_multi_output_cells(g);
      if (cfg.verification_mode) write_graph(g, cfg.graph_file);
      log_line("[stage] Graph build complete (from CDB).");
      mine_graph_once(cfg, g, recs, cov);
    } else {
      throw std::runtime_error("--cell-db and --netlist-db are required");
    }

    double total_occurrences = write_outputs(cfg, g, recs, cov);

    if (cfg.opt.max_outputs >= 2 && !cfg.cdb_file.empty()) {
      log_line("[stage] Multi-output grouping (--max_outputs=" + std::to_string(cfg.opt.max_outputs) + ")");
      MultiOutputParams mparams;
      mparams.max_outputs = cfg.opt.max_outputs;
      mparams.max_inputs = cfg.opt.max_inputs;
      mparams.num_cores = cfg.opt.num_cores;
      mparams.max_distance = cfg.opt.max_distance ? *cfg.opt.max_distance : -1.0;
      mparams.input_sharing_mode = cli_input_sharing_mode ? *cli_input_sharing_mode : "shared";
      mparams.min_shared_inputs = cli_min_shared_inputs ? *cli_min_shared_inputs : 1;
      mparams.min_shared_cells = cfg.opt.min_cell_overlap;

      ClusterDB mdb = read_cluster_db(cfg.cdb_file.string());
      NetlistDb mldb = read_netlist_db(cfg.netlist_db_input.string());
      std::string merge_db_path = (cfg.out_dir / "merge.cdb").string();
      std::string merge_inst_prefix = cfg.text_outputs ? (cfg.out_dir / "merge").string() : std::string();
      auto mres = run_multi_output_grouping(mdb, mldb, mparams, merge_db_path, merge_inst_prefix);

      if (cfg.text_outputs && !cfg.freq_file.empty() && !mres.rows.empty()) {
        std::ofstream ff(cfg.freq_file, std::ios::app);
        if (ff) {
          std::sort(mres.rows.begin(), mres.rows.end(),
                    [](const MultiOutputRow& a, const MultiOutputRow& b) { return a.count > b.count; });
          for (const auto& r : mres.rows) {
            ff << r.num_outputs << ",Multi," << r.canonical << "," << r.count << ",\n";
          }
        }
      }
      log_line("[stage] Multi-output grouping complete: " +
               std::to_string(mres.rows.size()) + " distinct joint canonicals, " +
               std::to_string(mres.total_instances) + " instances -> merge.cdb");
    }

    if (!cfg.selected_candidates_file.empty()) {
      auto selected = select_existing_library_candidates(cfg, g, recs);
      write_selected_candidates_csv(cfg.selected_candidates_file, selected);
    }
    int unique_logic_functions = (int)recs.size();
    write_config_log(cfg, unique_logic_functions, total_occurrences);
    close_verification_logs();
    if (cfg.verification_mode) {
      auto verify = verify_mining_logs(cfg);
      log_line("[done] verification_report=" + cfg.verification_report_file.string());
      log_line("[done] verification_included=" + std::to_string(verify.included_entries));
      log_line("[done] verification_excluded=" + std::to_string(verify.excluded_entries));
      log_line("[done] verification_branch=" + std::to_string(verify.branch_entries));
    }

    auto t_end = std::chrono::steady_clock::now();
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(t_end - t_start).count();
    long peak_kb = read_peak_rss_kb();
    if (peak_kb > 0) {
      std::ostringstream oss;
      oss << "[resource] peak_rss=" << std::fixed << std::setprecision(2) << (peak_kb / 1024.0) << " MB";
      log_line(oss.str());
    }
    log_line("[stage] Total runtime: " + std::to_string(sec) + "s");
    log_line("[done] run_dir=" + cfg.run_dir.string());
    if (cfg.text_outputs) {
      log_line("[done] pattern_frequency=" + cfg.freq_file.string());
      log_line("[done] pattern_detail=" + cfg.detail_file.string());
      log_line("[done] overlap_clusters=" + cfg.overlap_file.string());
      log_line("[done] subset_clusters=" + cfg.subset_file.string());
      log_line("[done] cell_map=" + cfg.map_file.string());
      log_line("[done] logic_coverage=" + cfg.cov_file.string());
    }
    if (!cfg.cdb_file.empty()) {
      log_line("[done] cluster_db=" + cfg.cdb_file.string());
    }
    if (cfg.opt.max_outputs >= 2 && !cfg.cdb_file.empty()) {
      log_line("[done] merge_db=" + (cfg.out_dir / "merge.cdb").string());
    }
    if (!cfg.selected_candidates_file.empty()) {
      log_line("[done] selected_candidates=" + cfg.selected_candidates_file.string());
    }
    if (cfg.text_outputs && cfg.add_np_class) {
      log_line("[done] p_to_np_class=" + cfg.np_class_map_file.string());
    }
    if (cfg.verification_mode) {
      log_line("[done] included_clusters_log=" + cfg.included_clusters_file.string());
      log_line("[done] excluded_clusters_log=" + cfg.excluded_clusters_file.string());
      log_line("[done] branch_trace_log=" + cfg.branch_log_file.string());
    }
    log_line("[done] config_log=" + cfg.config_log_file.string());
    log_line("[done] runtime_log=" + cfg.runtime_log_file.string());
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "[error] " << e.what() << "\n";
    return 1;
  }
}
