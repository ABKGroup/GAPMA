#include "multi_output_grouping.hpp"

#include "merge_db_writer.hpp"
#include "canonical_utils.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

static int sorted_shared_count(const std::vector<int>& a, const std::vector<int>& b) {
  int c = 0;
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (a[i] < b[j]) ++i;
    else if (a[i] > b[j]) ++j;
    else { ++c; ++i; ++j; }
  }
  return c;
}

static bool sorted_intersects(const std::vector<int>& a, const std::vector<int>& b) {
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (a[i] < b[j]) ++i;
    else if (a[i] > b[j]) ++j;
    else return true;
  }
  return false;
}

static double sorted_jaccard(const std::vector<int>& a, const std::vector<int>& b) {
  int inter = 0, total_a = static_cast<int>(a.size()), total_b = static_cast<int>(b.size());
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (a[i] < b[j]) ++i;
    else if (a[i] > b[j]) ++j;
    else { ++inter; ++i; ++j; }
  }
  int uni = total_a + total_b - inter;
  return uni > 0 ? static_cast<double>(inter) / uni : 0.0;
}

struct MergedInstance {
  std::vector<int32_t> cell_ids;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::vector<int32_t> source_cluster_ids;
  std::string canonical;
};

struct MergedCandidate {
  std::string canonical_key;
  std::string expr;
  double score = 0.0;
  double sum_input_overlap = 0.0;
  int raw_count = 0;
  int output_count = 0;
  int merged_input_count = 0;
  std::string merged_canonicals;
};

struct ClusterView {
  size_t idx;
  std::vector<int> input_ids;
  std::vector<int> output_ids;
  std::vector<int> cell_ids;
  int cell_count = 0;
  std::vector<std::string> input_names;
  std::vector<std::string> output_names;
};

static std::string make_sorted_key(std::vector<std::string> v) {
  std::sort(v.begin(), v.end());
  std::string key;
  for (size_t i = 0; i < v.size(); ++i) { if (i) key += ";"; key += v[i]; }
  return key;
}

static std::string make_sorted_expr(const std::vector<std::string>& canons,
                                    const std::unordered_map<std::string, std::string>& c2e) {
  std::vector<std::pair<std::string, std::string>> pairs;
  for (const auto& c : canons) { auto it = c2e.find(c); pairs.push_back({c, it != c2e.end() ? it->second : ""}); }
  std::sort(pairs.begin(), pairs.end());
  std::string expr;
  for (size_t i = 0; i < pairs.size(); ++i) { if (i) expr += ";"; expr += pairs[i].second; }
  return expr;
}

static double max_cell_span(const std::vector<int32_t>& ids_a,
                            const std::vector<int32_t>& ids_b,
                            const std::unordered_map<int32_t, std::pair<float, float>>& coords) {
  std::vector<std::pair<float, float>> pts;
  auto add = [&](const std::vector<int32_t>& ids) {
    for (auto id : ids) {
      auto it = coords.find(id);
      if (it != coords.end() && !std::isnan(it->second.first)) pts.push_back(it->second);
    }
  };
  add(ids_a);
  add(ids_b);
  if (pts.size() < 2) return -1.0;
  float mnx = pts[0].first, mxx = mnx, mny = pts[0].second, mxy = mny;
  for (const auto& [x, y] : pts) {
    if (x < mnx) mnx = x;
    if (x > mxx) mxx = x;
    if (y < mny) mny = y;
    if (y > mxy) mxy = y;
  }
  double dx = mxx - mnx, dy = mxy - mny;
  return std::sqrt(dx * dx + dy * dy);
}

static std::string expand_truth_table(const std::string& tt,
                                      const std::vector<std::string>& orig_inputs,
                                      const std::vector<std::string>& union_inputs) {
  if (tt.empty() || orig_inputs.empty()) return "";
  int m = static_cast<int>(union_inputs.size());
  int n = static_cast<int>(orig_inputs.size());
  int rows = 1 << m;

  std::vector<int> mapping(m, -1);
  for (int ui = 0; ui < m; ++ui)
    for (int oi = 0; oi < n; ++oi)
      if (union_inputs[ui] == orig_inputs[oi]) { mapping[ui] = oi; break; }

  std::string expanded(rows, '0');
  for (int row = 0; row < rows; ++row) {
    int orig_idx = 0;
    for (int oi = 0; oi < n; ++oi)
      for (int ui = 0; ui < m; ++ui)
        if (mapping[ui] == oi) { orig_idx |= (((row >> (m-1-ui)) & 1) << (n-1-oi)); break; }
    if (orig_idx < static_cast<int>(tt.size())) expanded[row] = tt[orig_idx];
  }
  return expanded;
}

static int stream_instance(std::ofstream& f, int& cid, const MergedInstance& mi) {
  if (!f) return 0;
  f << "clst" << cid << ":";
  for (size_t i = 0; i < mi.cell_ids.size(); ++i) {
    if (i) f << " ";
    f << mi.cell_ids[i];
  }
  f << "\ninputs:";
  for (const auto& n : mi.inputs) f << " " << n;
  f << "\noutputs:";
  for (const auto& n : mi.outputs) f << " " << n;
  f << "\n";
  ++cid;
  return 1;
}

}

MultiOutputResult run_multi_output_grouping(
    const ClusterDB& db,
    const NetlistDb& ldb,
    const MultiOutputParams& params,
    const std::string& merge_db_path,
    const std::string& instance_out_prefix) {
  MultiOutputResult result;

  const int max_outputs = params.max_outputs;
  const int max_inputs = params.max_inputs;
  const int num_cores = std::max(1, params.num_cores);
  const double max_distance = params.max_distance;
  const std::string& input_sharing_mode = params.input_sharing_mode;
  const int min_shared_inputs = std::max(1, params.min_shared_inputs);
  const int min_shared_cells = std::max(0, params.min_shared_cells);
  const bool write_instances = !instance_out_prefix.empty();

  std::map<std::string, MergedCandidate> all_cands;

  std::unordered_map<int32_t, std::pair<float, float>> cell_coords;
  for (const auto& inst : ldb.instances) {
    cell_coords[inst.id] = {inst.x, inst.y};
  }

  std::unordered_map<std::string, std::vector<int32_t>> net_to_cells;
  for (const auto& net : ldb.nets) {
    std::vector<int32_t> cids;
    for (const auto& pc : net.pins) {
      if (pc.instance_id > 0) cids.push_back(pc.instance_id);
    }
    if (!cids.empty()) net_to_cells[net.name] = std::move(cids);
  }

  std::unordered_map<std::string, std::string> canon_to_expr;
  for (const auto& p : db.logic_functions)
    if (canon_to_expr.find(p.canonical) == canon_to_expr.end())
      canon_to_expr[p.canonical] = p.expr;

  std::unordered_map<std::string, int> input_bit, output_bit, cell_bit;
  for (const auto& c : db.clusters) {
    for (const auto& s : c.inputs) if (input_bit.find(s) == input_bit.end()) { int id = input_bit.size(); input_bit[s] = id; }
    for (const auto& s : c.outputs) if (output_bit.find(s) == output_bit.end()) { int id = output_bit.size(); output_bit[s] = id; }
    for (auto id : c.cell_ids) { auto key = std::to_string(id); if (cell_bit.find(key) == cell_bit.end()) { int bid = cell_bit.size(); cell_bit[key] = bid; } }
  }

  int n_inputs = static_cast<int>(input_bit.size());
  int n_cells = static_cast<int>(cell_bit.size());

  std::vector<ClusterView> views(db.clusters.size());
  for (size_t i = 0; i < db.clusters.size(); ++i) {
    views[i].idx = i;
    for (const auto& s : db.clusters[i].inputs) views[i].input_ids.push_back(input_bit[s]);
    for (const auto& s : db.clusters[i].outputs) views[i].output_ids.push_back(output_bit[s]);
    for (auto id : db.clusters[i].cell_ids) views[i].cell_ids.push_back(cell_bit[std::to_string(id)]);
    std::sort(views[i].input_ids.begin(), views[i].input_ids.end());
    std::sort(views[i].output_ids.begin(), views[i].output_ids.end());
    std::sort(views[i].cell_ids.begin(), views[i].cell_ids.end());
    views[i].cell_count = static_cast<int>(db.clusters[i].cell_ids.size());
    views[i].input_names = db.clusters[i].inputs;
    views[i].output_names = db.clusters[i].outputs;
  }

  std::unordered_map<int, std::vector<size_t>> input_to_clusters;
  for (size_t i = 0; i < db.clusters.size(); ++i)
    for (const auto& s : db.clusters[i].inputs)
      input_to_clusters[input_bit[s]].push_back(i);

  std::unordered_map<std::string, std::vector<size_t>> by_canonical;
  for (size_t i = 0; i < db.clusters.size(); ++i)
    by_canonical[db.clusters[i].canonical].push_back(i);

  std::vector<std::string> canon_list;
  for (const auto& [k, _] : by_canonical) canon_list.push_back(k);
  std::sort(canon_list.begin(), canon_list.end());

  std::cerr << "[info] " << canon_list.size() << " distinct canonicals, "
            << n_inputs << " unique input nets, " << n_cells << " unique cells\n";

  auto input_ok_pair = [&](size_t ai, size_t bi) -> bool {
    if (input_sharing_mode == "exact")
      return views[ai].input_ids == views[bi].input_ids;
    return sorted_shared_count(views[ai].input_ids, views[bi].input_ids) >= min_shared_inputs;
  };

  struct InstanceTuple {
    std::vector<size_t> cluster_indices;
    std::set<int> union_input_ids;
    std::set<int> union_output_ids;
  };

  std::vector<InstanceTuple> prev_tuples;

  auto compute_joint_canonical = [&](const std::vector<size_t>& indices) -> std::pair<std::string, int> {
    std::vector<std::string> union_inputs;
    std::set<std::string> seen;
    for (size_t idx : indices)
      for (const auto& n : db.clusters[idx].input_order)
        if (seen.insert(n).second) union_inputs.push_back(n);
    std::sort(union_inputs.begin(), union_inputs.end());
    int mic = static_cast<int>(union_inputs.size());
    if (mic > max_inputs) return {"", mic};
    for (size_t idx : indices)
      if (db.clusters[idx].truth_table.empty()) return {"", mic};
    std::vector<std::string> expanded;
    for (size_t idx : indices)
      expanded.push_back(expand_truth_table(db.clusters[idx].truth_table,
                                            db.clusters[idx].input_order, union_inputs));
    auto joint = canonical_bits_joint(expanded, mic);
    std::string jk;
    for (size_t ji = 0; ji < joint.size(); ++ji) {
      if (ji) jk += ";";
      jk += "0b" + joint[ji];
    }
    return {jk, mic};
  };

  auto check_boundary_outputs = [&](const std::vector<size_t>& indices, int required_outputs) -> bool {
    std::unordered_set<int32_t> ucells;
    for (size_t idx : indices)
      for (auto id : db.clusters[idx].cell_ids)
        ucells.insert(id);
    std::unordered_set<std::string> uouts;
    for (size_t idx : indices) {
      for (const auto& net : db.clusters[idx].outputs) {
        auto nit = net_to_cells.find(net);
        if (nit == net_to_cells.end()) { uouts.insert(net); continue; }
        for (int32_t cid : nit->second)
          if (ucells.find(cid) == ucells.end()) { uouts.insert(net); break; }
      }
    }
    return static_cast<int>(uouts.size()) >= required_outputs;
  };

  auto compute_pairwise_jaccard_avg = [&](const std::vector<size_t>& indices) -> double {
    int k = static_cast<int>(indices.size());
    if (k < 2) return 0.0;
    double sum = 0.0;
    int pairs = 0;
    for (int i = 0; i < k; ++i)
      for (int j = i + 1; j < k; ++j) {
        sum += sorted_jaccard(views[indices[i]].cell_ids, views[indices[j]].cell_ids);
        pairs++;
      }
    return pairs > 0 ? sum / pairs : 0.0;
  };

  std::vector<MergedInstance> all_instances_for_db;

  if (max_outputs >= 2) {
    int n_canon = static_cast<int>(canon_list.size());
    int workers = std::min(num_cores, n_canon);
    if (workers < 1) workers = 1;

    std::vector<std::vector<int>> canon_input_union(n_canon);
    for (int _ci = 0; _ci < n_canon; ++_ci) {
      std::unordered_set<int> u;
      for (size_t idx : by_canonical[canon_list[_ci]])
        for (int id : views[idx].input_ids)
          u.insert(id);
      canon_input_union[_ci].assign(u.begin(), u.end());
      std::sort(canon_input_union[_ci].begin(), canon_input_union[_ci].end());
    }

    struct SubGroup { double score = 0.0, sum_input_ov = 0.0; int raw = 0, mic = 0; };
    struct ThreadResult {
      std::map<std::string, MergedCandidate> cands;
      std::vector<InstanceTuple> tuples;
      std::ofstream inst_file;
      int inst_cid = 1, total_inst = 0;
      std::vector<bool> gb_membership;
      std::vector<MergedInstance> instances_for_db;
    };
    std::vector<ThreadResult> thread_results(workers);
    for (int t = 0; t < workers; ++t) {
      thread_results[t].gb_membership.assign(db.clusters.size(), false);
      if (write_instances)
        thread_results[t].inst_file.open(instance_out_prefix + "_instances_2out.part" + std::to_string(t));
    }

    struct PairTask { int ci, cj; };
    std::vector<PairTask> pair_tasks;
    pair_tasks.reserve(static_cast<size_t>(n_canon) * (n_canon - 1) / 2);
    for (int ci = 0; ci < n_canon; ++ci)
      for (int cj = ci + 1; cj < n_canon; ++cj)
        if (sorted_intersects(canon_input_union[ci], canon_input_union[cj]))
          pair_tasks.push_back({ci, cj});
    int total_pairs = static_cast<int>(pair_tasks.size());

    std::atomic<int> next_pair{0};
    std::vector<std::atomic<int>> ci_done(n_canon);
    for (int i = 0; i < n_canon; ++i) ci_done[i].store(0);
    std::vector<int> ci_total(n_canon, 0);
    for (auto& pt : pair_tasks) ci_total[pt.ci]++;
    std::atomic<int> progress_ci{0};

    auto worker_fn = [&](int tid) {
      auto& tr = thread_results[tid];
      while (true) {
        int pi = next_pair.fetch_add(1);
        if (pi >= total_pairs) break;
        int ci = pair_tasks[pi].ci;
        int cj = pair_tasks[pi].cj;
        const auto& canon_a = canon_list[ci];
        const auto& canon_b = canon_list[cj];
        const auto& ga = by_canonical[canon_a];
        const auto& gb = by_canonical[canon_b];

        std::map<std::string, SubGroup> sub_groups;
        auto& gb_membership = tr.gb_membership;
        for (size_t idx : gb) gb_membership[idx] = true;

        for (size_t ai : ga) {
          std::unordered_set<size_t> candidates;
          for (const auto& inp : db.clusters[ai].inputs) {
            int bit = input_bit[inp];
            auto iit = input_to_clusters.find(bit);
            if (iit == input_to_clusters.end()) continue;
            for (size_t idx : iit->second)
              if (gb_membership[idx]) candidates.insert(idx);
          }
          for (size_t bi : candidates) {
            if (!input_ok_pair(ai, bi)) continue;
            if (min_shared_cells > 0 &&
                sorted_shared_count(views[ai].cell_ids, views[bi].cell_ids) < min_shared_cells) continue;
            if (sorted_intersects(views[ai].output_ids, views[bi].output_ids)) continue;
            if (max_distance > 0) {
              double span = max_cell_span(db.clusters[ai].cell_ids, db.clusters[bi].cell_ids, cell_coords);
              if (span > max_distance) continue;
            }
            std::vector<size_t> tuple = {ai, bi};
            auto [jk, mic] = compute_joint_canonical(tuple);
            if (jk.empty()) continue;
            if (!check_boundary_outputs(tuple, 2)) continue;

            MergedInstance mi;
            std::set<int32_t> ucells(db.clusters[ai].cell_ids.begin(), db.clusters[ai].cell_ids.end());
            ucells.insert(db.clusters[bi].cell_ids.begin(), db.clusters[bi].cell_ids.end());
            mi.cell_ids.assign(ucells.begin(), ucells.end());
            std::set<std::string> uins(db.clusters[ai].inputs.begin(), db.clusters[ai].inputs.end());
            uins.insert(db.clusters[bi].inputs.begin(), db.clusters[bi].inputs.end());
            mi.inputs.assign(uins.begin(), uins.end());
            std::set<std::string> uouts;
            auto is_boundary = [&](const std::string& net) -> bool {
              auto nit = net_to_cells.find(net);
              if (nit == net_to_cells.end()) return true;
              for (int32_t cid : nit->second)
                if (ucells.find(cid) == ucells.end()) return true;
              return false;
            };
            for (const auto& o : db.clusters[ai].outputs) if (is_boundary(o)) uouts.insert(o);
            for (const auto& o : db.clusters[bi].outputs) if (is_boundary(o)) uouts.insert(o);
            mi.outputs.assign(uouts.begin(), uouts.end());
            mi.source_cluster_ids = {db.clusters[ai].id, db.clusters[bi].id};
            mi.canonical = jk;
            if (write_instances) {
              stream_instance(tr.inst_file, tr.inst_cid, mi);
            }
            tr.total_inst++;
            if (!merge_db_path.empty()) {
              tr.instances_for_db.push_back(mi);
            }

            int shared_in = sorted_shared_count(views[ai].input_ids, views[bi].input_ids);
            int union_in = static_cast<int>(views[ai].input_ids.size() + views[bi].input_ids.size()) - shared_in;
            auto& sg = sub_groups[jk];
            sg.mic = mic;
            sg.sum_input_ov += union_in > 0 ? static_cast<double>(shared_in) / union_in : 0.0;
            sg.score += sorted_jaccard(views[ai].cell_ids, views[bi].cell_ids);
            sg.raw++;

            if (max_outputs > 2) {
              InstanceTuple it;
              it.cluster_indices = {std::min(ai, bi), std::max(ai, bi)};
              it.union_input_ids.insert(views[ai].input_ids.begin(), views[ai].input_ids.end());
              it.union_input_ids.insert(views[bi].input_ids.begin(), views[bi].input_ids.end());
              it.union_output_ids.insert(views[ai].output_ids.begin(), views[ai].output_ids.end());
              it.union_output_ids.insert(views[bi].output_ids.begin(), views[bi].output_ids.end());
              tr.tuples.push_back(std::move(it));
            }
          }
        }
        for (size_t idx : gb) gb_membership[idx] = false;

        for (auto& [jc, sg] : sub_groups) {
          if (sg.raw == 0) continue;
          MergedCandidate mc;
          mc.canonical_key = make_sorted_key({canon_a, canon_b});
          mc.expr = make_sorted_expr({canon_a, canon_b}, canon_to_expr);
          mc.score = sg.score;
          mc.sum_input_overlap = sg.sum_input_ov;
          mc.raw_count = sg.raw;
          mc.output_count = 2;
          mc.merged_input_count = sg.mic;
          mc.merged_canonicals = jc;
          tr.cands[jc] = mc;
        }

        int cd = ci_done[ci].fetch_add(1) + 1;
        if (cd == ci_total[ci]) {
          int pc = progress_ci.fetch_add(1) + 1;
          if (pc % 10 == 0 || pc == n_canon)
            std::cerr << "[progress] 2-output scan " << pc << "/" << n_canon << "\r";
        }
      }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < workers; ++t) threads.emplace_back(worker_fn, t);
    for (auto& th : threads) th.join();

    int total_inst_2 = 0;
    for (auto& tr : thread_results) {
      for (auto& [k, v] : tr.cands) all_cands[k] = std::move(v);
      prev_tuples.insert(prev_tuples.end(),
                         std::make_move_iterator(tr.tuples.begin()),
                         std::make_move_iterator(tr.tuples.end()));
      total_inst_2 += tr.total_inst;
      tr.inst_file.close();
      for (auto& mi : tr.instances_for_db) all_instances_for_db.push_back(std::move(mi));
    }
    result.total_instances += total_inst_2;
    std::cerr << "\n[info] found " << all_cands.size() << " 2-output candidates, "
              << prev_tuples.size() << " instance tuples\n";
    if (write_instances) {
      std::string inst_path = instance_out_prefix + "_instances_2out.out";
      std::ofstream merged_inst(inst_path);
      for (int t = 0; t < workers; ++t) {
        std::string part = instance_out_prefix + "_instances_2out.part" + std::to_string(t);
        std::ifstream pf(part);
        if (pf) merged_inst << pf.rdbuf();
        pf.close();
        std::remove(part.c_str());
      }
      std::cerr << "[done] " << inst_path << " (" << total_inst_2 << " instances)\n";
    }
  }

  for (int k = 3; k <= max_outputs; ++k) {
    if (prev_tuples.empty()) break;

    std::map<std::string, MergedCandidate> level_cands;
    std::vector<InstanceTuple> next_tuples;
    int level_instances = 0;

    for (const auto& pt : prev_tuples) {
      std::set<size_t> existing(pt.cluster_indices.begin(), pt.cluster_indices.end());

      std::unordered_set<size_t> candidates;
      for (int inp_id : pt.union_input_ids) {
        auto iit = input_to_clusters.find(inp_id);
        if (iit == input_to_clusters.end()) continue;
        for (size_t idx : iit->second) {
          if (!existing.count(idx)) candidates.insert(idx);
        }
      }

      for (size_t ci : candidates) {
        bool output_conflict = false;
        for (int oid : views[ci].output_ids) {
          if (pt.union_output_ids.count(oid)) { output_conflict = true; break; }
        }
        if (output_conflict) continue;

        int shared = 0;
        for (int iid : views[ci].input_ids)
          if (pt.union_input_ids.count(iid)) shared++;
        if (shared < min_shared_inputs) continue;

        std::vector<size_t> new_indices = pt.cluster_indices;
        new_indices.push_back(ci);
        std::sort(new_indices.begin(), new_indices.end());

        if (max_distance > 0) {
          std::vector<int32_t> all_ids;
          for (size_t idx : new_indices)
            all_ids.insert(all_ids.end(), db.clusters[idx].cell_ids.begin(), db.clusters[idx].cell_ids.end());
          double span = max_cell_span(all_ids, {}, cell_coords);
          if (span > max_distance) continue;
        }

        auto [jk, mic] = compute_joint_canonical(new_indices);
        if (jk.empty()) continue;

        if (!check_boundary_outputs(new_indices, k)) continue;

        auto& mc = level_cands[jk];
        if (mc.raw_count == 0) {
          std::vector<std::string> src_canons;
          for (size_t idx : new_indices) src_canons.push_back(db.clusters[idx].canonical);
          mc.canonical_key = make_sorted_key(src_canons);
          mc.expr = make_sorted_expr(src_canons, canon_to_expr);
          mc.output_count = k;
          mc.merged_input_count = mic;
          mc.merged_canonicals = jk;
        }
        mc.score += compute_pairwise_jaccard_avg(new_indices);
        mc.raw_count++;
        level_instances++;
        result.total_instances++;

        if (!merge_db_path.empty()) {
          MergedInstance mi;
          std::set<int32_t> ucells;
          std::set<std::string> uins, uouts;
          for (size_t idx : new_indices) {
            ucells.insert(db.clusters[idx].cell_ids.begin(), db.clusters[idx].cell_ids.end());
            uins.insert(db.clusters[idx].inputs.begin(), db.clusters[idx].inputs.end());
            uouts.insert(db.clusters[idx].outputs.begin(), db.clusters[idx].outputs.end());
            mi.source_cluster_ids.push_back(db.clusters[idx].id);
          }
          mi.cell_ids.assign(ucells.begin(), ucells.end());
          mi.inputs.assign(uins.begin(), uins.end());
          mi.outputs.assign(uouts.begin(), uouts.end());
          mi.canonical = jk;
          all_instances_for_db.push_back(std::move(mi));
        }

        if (k < max_outputs) {
          InstanceTuple nt;
          nt.cluster_indices = new_indices;
          nt.union_input_ids = pt.union_input_ids;
          nt.union_input_ids.insert(views[ci].input_ids.begin(), views[ci].input_ids.end());
          nt.union_output_ids = pt.union_output_ids;
          nt.union_output_ids.insert(views[ci].output_ids.begin(), views[ci].output_ids.end());
          next_tuples.push_back(std::move(nt));
        }
      }
    }

    std::cerr << "[info] found " << level_cands.size() << " " << k << "-output candidates ("
              << level_instances << " instances)\n";

    for (auto& [jc, mc] : level_cands) {
      mc.score = mc.raw_count > 0 ? mc.score / mc.raw_count : 0.0;
      all_cands[jc] = std::move(mc);
    }

    prev_tuples = std::move(next_tuples);
  }

  for (const auto& [_, mc] : all_cands) {
    MultiOutputRow r;
    r.num_inputs = mc.merged_input_count;
    r.num_outputs = mc.output_count;
    r.canonical = mc.merged_canonicals.empty() ? mc.canonical_key : mc.merged_canonicals;
    r.count = mc.raw_count;
    r.avg_input_overlap = mc.raw_count > 0 ? mc.sum_input_overlap / mc.raw_count : 0.0;
    r.avg_cell_overlap = mc.raw_count > 0 ? mc.score / mc.raw_count : 0.0;
    result.rows.push_back(std::move(r));
  }

  if (!merge_db_path.empty()) {

    std::vector<merge_db_writer::WMerge> all_merges;
    all_merges.reserve(all_instances_for_db.size());
    for (const auto& mi : all_instances_for_db) {
      merge_db_writer::WMerge wm;
      wm.canonical = mi.canonical;
      wm.cluster_ids = mi.source_cluster_ids;
      wm.shared_inputs = {};
      wm.outputs = mi.outputs;
      all_merges.push_back(std::move(wm));
    }
    if (!merge_db_writer::write_db(merge_db_path, all_merges)) {
      throw std::runtime_error("failed to write merge.cdb: " + merge_db_path);
    }
    std::cerr << "[done] merge_db=" << merge_db_path << " entries=" << all_merges.size() << "\n";
  }

  return result;
}
