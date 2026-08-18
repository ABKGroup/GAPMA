#include "logic_minor/reporting.hpp"

#include "logic_minor/canonical.hpp"
#include "logic_minor/mining.hpp"
#include "logic_minor/pattern.hpp"
#include "logic_minor/util.hpp"
#include "cluster_db_writer.hpp"
#include "cell_db_writer.hpp"
#include "netlist_db_writer.hpp"
#include "canonical_utils.hpp"

#include <algorithm>
#include <atomic>
#include <climits>
#include <ctime>
#include <fstream>
#include <fnmatch.h>
#include <functional>
#include <iomanip>
#include <cmath>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace {

std::string format_count(double v) {
  if (std::fabs(v - std::round(v)) < 1e-9) {
    return std::to_string(static_cast<long long>(std::llround(v)));
  }
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(6) << v;
  auto s = oss.str();
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}

std::string candidate_pattern(const std::string& master_cell, const std::string& mode) {
  if (mode == "family_wildcard") {
    auto pos = master_cell.find('_');
    std::string head = pos == std::string::npos ? master_cell : master_cell.substr(0, pos);
    return head + "_*";
  }
  return master_cell;
}

bool is_already_enabled(const std::string& master_cell, const std::vector<std::string>& patterns) {
  for (const auto& pattern : patterns) {
    if (fnmatch(pattern.c_str(), master_cell.c_str(), 0) == 0) return true;
  }
  return false;
}

double lib_area_or_throw(const Graph& g, const std::string& master_name) {
  auto area_it = g.lib_master_area.find(master_name);
  if (area_it == g.lib_master_area.end()) {
    throw std::runtime_error("cell.cdb: library master \"" + master_name +
                             "\" has no area entry — cannot rank it for canonical->master labeling");
  }
  if (!(area_it->second > 0.0)) {
    throw std::runtime_error("cell.cdb: library master \"" + master_name + "\" has unparsed area (" +
                             std::to_string(area_it->second) +
                             ") — fix the Liberty area attribute or the cell.cdb build");
  }
  return area_it->second;
}

std::string best_master_for_signature(const Graph& g, const std::string& signature) {
  std::string best_master;
  double best_area = std::numeric_limits<double>::infinity();
  for (const auto& [master_name, master_signature] : g.lib_output_signature_for_master) {
    if (master_signature != signature) continue;
    double area = lib_area_or_throw(g, master_name);
    if (area < best_area || (area == best_area && master_name < best_master)) {
      best_master = master_name;
      best_area = area;
    }
  }
  return best_master;
}

std::string logic_label_for_signature(const Graph& g, const std::string& signature) {
  std::string best_master = best_master_for_signature(g, signature);
  if (!best_master.empty()) {
    return best_master;
  }
  return "New";
}

bool parse_binary_signature(const std::string& sig_bin, std::string& bits_out, int& n_out) {
  if (sig_bin.size() < 3) return false;
  if (sig_bin.substr(0, 2) != "0b") return false;
  std::string bits = sig_bin.substr(2);
  if (bits.empty()) return false;
  if (bits.find_first_not_of("01") != std::string::npos) return false;
  int rows_count = static_cast<int>(bits.size());
  if (rows_count <= 0 || (rows_count & (rows_count - 1)) != 0) return false;
  int n = 0;
  while ((1 << n) < rows_count) ++n;
  bits_out = std::move(bits);
  n_out = n;
  return true;
}

std::string net_label(const Net* net, const Pin* pin) {
  if (net && !net->name.empty()) return net->name;
  return pin ? pin->full_name : "";
}

std::set<std::string> cluster_input_net_names(const Graph& g, const std::vector<int>& dense_ids) {

  std::unordered_set<const Cell*> cluster_cells;
  for (int dense_id : dense_ids) {
    if (dense_id < 0 || dense_id > static_cast<int>(g.cell_list.size())) continue;
    Cell* cell = g.cell_list[dense_id];
    if (cell) cluster_cells.insert(cell);
  }

  std::set<std::string> nets;
  for (const auto* cell : cluster_cells) {
    for (const auto& [_, pin] : cell->in_pins) {
      if (!pin) continue;
      Net* net = pin->net;
      Pin* drv = net ? net->driver : nullptr;
      Cell* prev = drv ? drv->cell : nullptr;
      if (!prev || cluster_cells.count(prev) == 0) {
        auto label = net_label(net, pin);
        if (!label.empty()) nets.insert(label);
      }
    }
  }
  return nets;
}

std::set<std::string> cluster_output_net_names(const Graph& g, const std::vector<int>& dense_ids) {

  std::unordered_set<const Cell*> cluster_cells;
  for (int dense_id : dense_ids) {
    if (dense_id < 0 || dense_id > static_cast<int>(g.cell_list.size())) continue;
    Cell* cell = g.cell_list[dense_id];
    if (cell) cluster_cells.insert(cell);
  }

  std::set<std::string> nets;
  for (const auto* cell : cluster_cells) {
    for (const auto& [_, out_pin] : cell->out_pins) {
      if (!out_pin) continue;
      Net* net = out_pin->net;
      bool external = !net || net->loads.empty();
      if (!external && net) {
        for (auto* load_pin : net->loads) {
          if (!load_pin || !load_pin->cell || cluster_cells.count(load_pin->cell) == 0) {
            external = true;
            break;
          }
        }
      }
      if (!external) continue;
      auto label = net_label(net, out_pin);
      if (!label.empty()) nets.insert(label);
    }
  }
  return nets;
}

std::string join_names(const std::set<std::string>& names) {
  std::ostringstream oss;
  bool first = true;
  for (const auto& name : names) {
    if (!first) oss << " ";
    first = false;
    oss << name;
  }
  return oss.str();
}

}

double write_outputs(const Cfg& cfg,
                     const Graph& g,
                     const std::unordered_map<std::string, Rec>& recs,
                     const std::unordered_map<std::string, int>& cov) {

  std::vector<std::pair<std::string, Rec>> items(recs.begin(), recs.end());
  for (auto& [sig, r] : items) {
    std::unordered_set<std::string> seen;
    Rec deduped;
    for (size_t i = 0; i < r.insts.size(); ++i) {
      std::string key;
      for (size_t j = 0; j < r.insts[i].size(); ++j) {
        if (j) key += ',';
        key += std::to_string(r.insts[i][j]);
      }
      if (!seen.insert(key).second) continue;
      deduped.count += 1;
      deduped.insts.push_back(std::move(r.insts[i]));
      if (i < r.raw_truth_tables.size()) deduped.raw_truth_tables.push_back(std::move(r.raw_truth_tables[i]));
      if (i < r.input_orders.size()) deduped.input_orders.push_back(std::move(r.input_orders[i]));
    }
    r = std::move(deduped);
  }

  std::sort(items.begin(), items.end(), [](auto& a, auto& b) {
    if (a.second.count != b.second.count) return a.second.count > b.second.count;
    return a.first < b.first;
  });

  for (auto& [sig, r] : items) {
    std::vector<size_t> order(r.insts.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
      return r.insts[a] < r.insts[b];
    });
    Rec sorted_r;
    sorted_r.count = r.count;
    for (size_t idx : order) {
      sorted_r.insts.push_back(std::move(r.insts[idx]));
      if (idx < r.raw_truth_tables.size()) sorted_r.raw_truth_tables.push_back(std::move(r.raw_truth_tables[idx]));
      if (idx < r.input_orders.size()) sorted_r.input_orders.push_back(std::move(r.input_orders[idx]));
    }
    r = std::move(sorted_r);
  }

  std::unordered_map<std::string, int> dedicated_count_for_signature;
  if (cfg.text_outputs) {
    for (auto& [cn, cp] : g.cells) {
      if (!cp || !cp->master || cp->master->is_seq || is_buffer(cp.get())) continue;
      auto it = g.lib_output_signature_for_master.find(cp->master->name);
      if (it == g.lib_output_signature_for_master.end() || it->second.empty()) continue;
      dedicated_count_for_signature[it->second] += 1;
    }
  }

  struct FlatItem { const std::string* sig; const Rec* r; size_t offset; };
  std::vector<FlatItem> flat_items;
  flat_items.reserve(items.size());
  size_t total_flat = 0;
  for (const auto& [sig, r] : items) {
    flat_items.push_back({&sig, &r, total_flat});
    total_flat += r.insts.size();
  }

  struct ComputedInst {
    std::vector<int> ids;
    std::set<std::string> in_nets, out_nets;
    std::string tt;
    std::vector<std::string> io;
  };
  std::vector<ComputedInst> computed(total_flat);
  {
    int cb_workers = std::max(1, cfg.opt.num_cores);
    if (cb_workers > static_cast<int>(total_flat) && total_flat > 0)
      cb_workers = static_cast<int>(total_flat);
    std::atomic<int> cb_next{0};
    std::vector<std::thread> cb_threads;
    cb_threads.reserve(static_cast<size_t>(cb_workers));
    for (int t = 0; t < cb_workers; ++t) {
      cb_threads.emplace_back([&]() {
        while (true) {
          int fi = cb_next.fetch_add(1);
          if (fi >= static_cast<int>(total_flat)) break;

          size_t lo = 0, hi = flat_items.size();
          while (lo + 1 < hi) {
            size_t mid = (lo + hi) / 2;
            if (flat_items[mid].offset <= static_cast<size_t>(fi)) lo = mid;
            else hi = mid;
          }
          const auto& fi_item = flat_items[lo];
          size_t inst_idx = static_cast<size_t>(fi) - fi_item.offset;
          const auto& inst = fi_item.r->insts[inst_idx];
          auto& out = computed[static_cast<size_t>(fi)];
          out.ids.reserve(inst.size());
          for (int iid : inst) {
            if (iid >= 0 && iid < static_cast<int>(g.cell_list.size()) && g.cell_list[iid])
              out.ids.push_back(iid);
          }
          std::sort(out.ids.begin(), out.ids.end());
          out.in_nets = cluster_input_net_names(g, out.ids);
          out.out_nets = cluster_output_net_names(g, out.ids);
          if (inst_idx < fi_item.r->raw_truth_tables.size() &&
              !fi_item.r->raw_truth_tables[inst_idx].empty()) {
            out.tt = fi_item.r->raw_truth_tables[inst_idx];
            if (inst_idx < fi_item.r->input_orders.size())
              out.io = fi_item.r->input_orders[inst_idx];
          }
        }
      });
    }
    for (auto& th : cb_threads) th.join();
  }

  std::vector<ClusterGroupEntry> clsts;
  clsts.reserve(total_flat);
  std::ofstream fd;
  if (cfg.text_outputs) {
    fd.open(cfg.detail_file);
    if (!fd) throw std::runtime_error("failed to open: " + cfg.detail_file.string());
  }
  int cid = 1;
  size_t flat_off = 0;
  for (const auto& fi_item : flat_items) {
    const auto& signature = *fi_item.sig;
    const auto& r = *fi_item.r;
    if (cfg.text_outputs) {
      fd << "canonical_key=" << signature_to_binary(signature) << "\n";
      fd << "count=" << r.count << "\n";
    }
    for (size_t inst_idx = 0; inst_idx < r.insts.size(); ++inst_idx, ++flat_off) {
      auto& comp = computed[flat_off];
      if (cfg.text_outputs) {
        fd << "clst" << cid << ":";
        for (size_t i = 0; i < comp.ids.size(); ++i) { if (i) fd << " "; fd << comp.ids[i]; }
        fd << "\n";
        fd << "inputs: " << join_names(comp.in_nets) << "\n";
        fd << "outputs: " << join_names(comp.out_nets) << "\n";
        if (!comp.tt.empty()) {
          fd << "truth_table: " << comp.tt << "\n";
          if (!comp.io.empty()) {
            fd << "input_order:";
            for (const auto& net : comp.io) fd << " " << net;
            fd << "\n";
          }
        }
      }
      clsts.push_back({cid, signature, comp.ids, std::move(comp.in_nets),
                       std::move(comp.out_nets), std::move(comp.tt), std::move(comp.io)});
      ++cid;
    }
    if (cfg.text_outputs) fd << "\n";
  }

  std::vector<std::pair<int, Cell*>> cmap;
  cmap.reserve(g.cell_list.size());
  for (auto* cell : g.cell_list) {
    if (!cell || cell->id < 0) continue;
    cmap.push_back({cell->id, cell});
  }
  std::sort(cmap.begin(), cmap.end(), [](auto& a, auto& b) { return a.first < b.first; });
  if (cfg.text_outputs) {
    std::ofstream fm(cfg.map_file);
    if (!fm) throw std::runtime_error("failed to open: " + cfg.map_file.string());
    fm << "cell_id,cell_name,master_cell,x,y\n";
    for (auto& [id, cell] : cmap) {
      auto mn = (cell && cell->master) ? cell->master->name : "UNKNOWN";
      fm << id << "," << (cell ? cell->name : "UNKNOWN") << "," << mn << ",";
      if (cell && !std::isnan(cell->x)) fm << cell->x; else fm << "NaN";
      fm << ",";
      if (cell && !std::isnan(cell->y)) fm << cell->y; else fm << "NaN";
      fm << "\n";
    }
  }

  std::unordered_map<int, std::unordered_set<int>> adj;
  std::unordered_map<int, std::vector<int>> by_sid;
  for (auto& c : clsts) {
    adj[c.cid];
    for (int sid : c.ids) by_sid[sid].push_back(c.cid);
  }
  for (auto& [sid, ids] : by_sid) {
    for (size_t i = 0; i < ids.size(); ++i) {
      for (size_t j = i + 1; j < ids.size(); ++j) {
        adj[ids[i]].insert(ids[j]);
        adj[ids[j]].insert(ids[i]);
      }
    }
  }

  std::unordered_set<int> vis;
  std::vector<int> ids;
  for (auto& [id, _] : adj) ids.push_back(id);
  std::sort(ids.begin(), ids.end());
  std::vector<std::vector<int>> overlap_components;
  for (int s : ids) {
    if (vis.count(s)) continue;
    std::vector<int> comp;
    std::vector<int> st = {s};
    while (!st.empty()) {
      int u = st.back();
      st.pop_back();
      if (vis.count(u)) continue;
      vis.insert(u);
      comp.push_back(u);
      for (int v : adj[u])
        if (!vis.count(v)) st.push_back(v);
    }
    if (comp.size() > 1) {
      std::sort(comp.begin(), comp.end());
      overlap_components.push_back(comp);
    }
  }
  if (cfg.text_outputs) {
    std::ofstream fo(cfg.overlap_file);
    if (!fo) throw std::runtime_error("failed to open: " + cfg.overlap_file.string());
    fo << "# clsts that share at least one cell\n";
    for (const auto& comp : overlap_components) {
      for (size_t i = 0; i < comp.size(); ++i) {
        if (i) fo << " ";
        fo << "clst" << comp[i];
      }
      fo << "\n";
    }
  }

  std::vector<std::pair<int,int>> subset_pairs;
  {
    std::unordered_map<int, std::vector<size_t>> cell_to_clusters;
    for (size_t i = 0; i < clsts.size(); ++i) {
      for (int cid_s : clsts[i].ids) cell_to_clusters[cid_s].push_back(i);
    }
    int n_ss = static_cast<int>(clsts.size());
    int ss_workers = std::max(1, cfg.opt.num_cores);
    if (ss_workers > n_ss && n_ss > 0) ss_workers = n_ss;
    std::atomic<int> ss_next{0};
    struct SSTData {
      std::vector<std::pair<int,int>> local_pairs;
      std::vector<std::pair<int,int>> local_adj_edges;
    };
    std::vector<SSTData> ss_tdata(static_cast<size_t>(ss_workers));
    std::vector<std::thread> ss_threads;
    ss_threads.reserve(static_cast<size_t>(ss_workers));
    for (int t = 0; t < ss_workers; ++t) {
      ss_threads.emplace_back([&, t]() {
        auto& td = ss_tdata[static_cast<size_t>(t)];
        while (true) {
          int i = ss_next.fetch_add(1);
          if (i >= n_ss) break;
          const auto& a = clsts[static_cast<size_t>(i)];
          if (a.ids.empty()) continue;
          auto fc_it = cell_to_clusters.find(a.ids[0]);
          if (fc_it == cell_to_clusters.end()) continue;
          std::unordered_set<size_t> candidates(fc_it->second.begin(), fc_it->second.end());
          candidates.erase(static_cast<size_t>(i));
          for (size_t k = 1; k < a.ids.size() && !candidates.empty(); ++k) {
            auto nc_it = cell_to_clusters.find(a.ids[k]);
            if (nc_it == cell_to_clusters.end()) { candidates.clear(); break; }
            std::unordered_set<size_t> next_set(nc_it->second.begin(), nc_it->second.end());
            for (auto it2 = candidates.begin(); it2 != candidates.end(); ) {
              if (next_set.count(*it2)) ++it2;
              else it2 = candidates.erase(it2);
            }
          }
          for (size_t j : candidates) {
            if (clsts[j].ids.size() > a.ids.size()) {
              td.local_pairs.push_back({a.cid, clsts[j].cid});
              td.local_adj_edges.push_back({a.cid, clsts[j].cid});
              td.local_adj_edges.push_back({clsts[j].cid, a.cid});
            }
          }
        }
      });
    }
    for (auto& th : ss_threads) th.join();
    for (auto& td : ss_tdata) {
      for (auto& p : td.local_pairs) subset_pairs.push_back(p);
      for (auto& [ea, eb] : td.local_adj_edges) adj[ea].insert(eb);
    }
  }
  if (cfg.text_outputs) {
    std::ofstream fsb(cfg.subset_file);
    if (!fsb) throw std::runtime_error("failed to open: " + cfg.subset_file.string());
    fsb << "# strict subset cluster pairs: left is a strict subset of right\n";
    for (const auto& [a, b] : subset_pairs) {
      fsb << "clst" << a << " clst" << b << "\n";
    }
  }

  std::unordered_map<int, double> clst_weight;
  for (const auto& c : clsts) {
    int related = 0;
    auto it = adj.find(c.cid);
    if (it != adj.end()) related = static_cast<int>(it->second.size());
    clst_weight[c.cid] = cfg.opt.overlap_weighted_counting ? (1.0 / (1.0 + related)) : 1.0;
  }

  std::unordered_map<std::string, double> weighted_count_for_signature;
  for (const auto& c : clsts) weighted_count_for_signature[c.canonical_key] += clst_weight[c.cid];

  std::unordered_map<std::string, std::string> np_rep_key_to_master;
  std::unordered_map<std::string, double> np_rep_key_to_area;
  if (cfg.add_np_class) {
    for (const auto& [master_name, sig] : g.lib_output_signature_for_master) {
      if (sig.empty()) continue;
      double area = lib_area_or_throw(g, master_name);
      std::string p_bin = signature_to_binary(sig);
      std::string bits; int n_lib = 0;
      if (!parse_binary_signature(p_bin, bits, n_lib)) continue;
      if (n_lib < cfg.opt.min_inputs || n_lib > cfg.opt.max_inputs) continue;
      for (const auto& np_info : npn_canonical_infos(bits, n_lib)) {
        std::string np_rep_key = "0b" + np_info.bits;
        auto rit = np_rep_key_to_master.find(np_rep_key);
        if (rit == np_rep_key_to_master.end()) {
          np_rep_key_to_master[np_rep_key] = master_name;
          np_rep_key_to_area[np_rep_key] = area;
        } else {
          double prev = np_rep_key_to_area[np_rep_key];
          if (area < prev || (area == prev && master_name < rit->second)) {
            rit->second = master_name;
            np_rep_key_to_area[np_rep_key] = area;
          }
        }
      }
    }
  }

  auto np_label_for_key = [&](const std::string& np_key) -> std::string {
    auto it = np_rep_key_to_master.find(np_key);
    if (it != np_rep_key_to_master.end()) return it->second;
    return "New";
  };

  if (cfg.text_outputs) {
    struct OutRow {
      double sort_count;
      std::string line;
    };
    std::vector<OutRow> out_rows;
    out_rows.reserve(items.size() * 2);
    struct NpMapRow {
      std::string p_key;
      std::string np_key;
      double weight = 1.0;
      int erased_input_inverters = 0;
      int erased_output_inverters = 0;
      int erased_total_inverters = 0;
      bool exact = true;
    };
    std::vector<NpMapRow> np_map_rows;
    np_map_rows.reserve(items.size());

    for (auto& [signature, r] : items) {
      std::string master_label = logic_label_for_signature(g, signature);
      double count_value = cfg.opt.overlap_weighted_counting ? weighted_count_for_signature[signature]
                                                              : static_cast<double>(r.count);
      std::ostringstream oss;
      oss << "1,[P]" << master_label << "," << signature_to_binary(signature) << ","
          << format_count(count_value) << "," << exprs_from_signature(signature);
      out_rows.push_back({count_value, oss.str()});
    }

    if (cfg.add_np_class) {

      struct NpnGroup {
        double count = 0.0;
        std::string canonical_key;
        std::string master_label = "New";
      };
      std::unordered_map<std::string, NpnGroup> groups;

      for (auto& [signature, r] : items) {
        std::string p_bin = signature_to_binary(signature);
        std::string bits; int n = 0;
        double count_value = cfg.opt.overlap_weighted_counting
            ? weighted_count_for_signature[signature] : static_cast<double>(r.count);
        if (!parse_binary_signature(p_bin, bits, n)) {
          np_map_rows.push_back({p_bin, p_bin, 1.0, 0, 0, 0, false});
          continue;
        }
        std::unordered_set<std::string> counted_keys;
        for (const auto& np_info : npn_canonical_infos(bits, n)) {
          std::string npn_key = "0b" + np_info.bits;
          std::string label = np_label_for_key(npn_key);

          std::string group_key = (label != "New") ? label : npn_key;
          auto& grp = groups[group_key];
          if (counted_keys.insert(group_key).second) grp.count += count_value;
          if (grp.canonical_key.empty()) grp.canonical_key = npn_key;
          grp.master_label = label;
          np_map_rows.push_back({p_bin, npn_key, 1.0, np_info.erased_input_inverters,
                                 np_info.erased_output_inverters,
                                 np_info.erased_total_inverters, np_info.exact});
        }
      }

      for (const auto& [group_key, grp] : groups) {
        std::string expr = exprs_from_signature(grp.canonical_key);
        std::ostringstream oss;
        oss << "1,[NPN]" << grp.master_label << "," << grp.canonical_key << ","
            << format_count(grp.count) << "," << expr;
        out_rows.push_back({grp.count, oss.str()});
      }
    }

    std::sort(out_rows.begin(), out_rows.end(),
              [](const OutRow& a, const OutRow& b) { return a.sort_count > b.sort_count; });

    std::ofstream ff(cfg.freq_file);
    if (!ff) throw std::runtime_error("failed to open: " + cfg.freq_file.string());
    ff << "num_outputs,master_cell,canonical_key,count,logic_expr\n";
    for (const auto& row : out_rows) ff << row.line << "\n";

    if (cfg.add_np_class && !cfg.np_class_map_file.empty()) {
      std::ofstream fn(cfg.np_class_map_file);
      if (!fn) throw std::runtime_error("failed to open: " + cfg.np_class_map_file.string());
      fn << "p_canonical_key,np_canonical_key,weight,erased_input_inverters,"
            "erased_output_inverters,erased_total_inverters,exact\n";
      for (const auto& row : np_map_rows) {
        fn << row.p_key << "," << row.np_key << "," << format_count(row.weight) << ","
           << row.erased_input_inverters << "," << row.erased_output_inverters
           << "," << row.erased_total_inverters << "," << (row.exact ? 1 : 0) << "\n";
      }
    }

  }

  if (cfg.text_outputs) {
    struct CovRow {
      std::string logic_name;
      double total = 0;
      double dedicated = 0;
      double composite = 0;
    };
    std::vector<CovRow> cov_rows;
    cov_rows.reserve(g.lib_output_signature_for_master.size());
    std::unordered_set<std::string> seen_signatures;
    for (const auto& kv : g.lib_output_signature_for_master) {
      const std::string& signature = kv.second;
      if (signature.empty() || !seen_signatures.insert(signature).second) continue;
      std::string logic_name = logic_label_for_signature(g, signature);
      double dedicated = dedicated_count_for_signature[signature];
      double composite = cfg.opt.overlap_weighted_counting ? weighted_count_for_signature[signature]
                                                           : static_cast<double>(cov.count(signature) ? cov.at(signature) : 0);
      cov_rows.push_back({logic_name, dedicated + composite, dedicated, composite});
    }
    bool has_tie_combined = false;
    int tiehi_idx = -1;
    int tielo_idx = -1;
    for (const auto& row : cov_rows) {
      if (row.logic_name == "TIEHI.H|TIELO.L") {
        has_tie_combined = true;
        break;
      }
    }
    if (!has_tie_combined) {
      for (size_t i = 0; i < cov_rows.size(); ++i) {
        if (cov_rows[i].logic_name == "TIEHI.H") tiehi_idx = static_cast<int>(i);
        if (cov_rows[i].logic_name == "TIELO.L") tielo_idx = static_cast<int>(i);
      }
      if (tiehi_idx >= 0 && tielo_idx >= 0) {
        CovRow merged;
        merged.logic_name = "TIEHI.H|TIELO.L";
        merged.total = cov_rows[tiehi_idx].total + cov_rows[tielo_idx].total;
        merged.dedicated = cov_rows[tiehi_idx].dedicated + cov_rows[tielo_idx].dedicated;
        merged.composite = cov_rows[tiehi_idx].composite + cov_rows[tielo_idx].composite;
        if (tiehi_idx > tielo_idx) std::swap(tiehi_idx, tielo_idx);
        cov_rows.erase(cov_rows.begin() + tielo_idx);
        cov_rows.erase(cov_rows.begin() + tiehi_idx);
        cov_rows.push_back(std::move(merged));
      }
    }
    if (has_tie_combined) {
      cov_rows.erase(std::remove_if(cov_rows.begin(), cov_rows.end(), [](const CovRow& r) {
                       return r.logic_name == "TIEHI.H" || r.logic_name == "TIELO.L";
                     }),
                     cov_rows.end());
    }
    std::sort(cov_rows.begin(), cov_rows.end(), [](const CovRow& a, const CovRow& b) {
      if (a.total != b.total) return a.total > b.total;
      return a.logic_name < b.logic_name;
    });

    std::ofstream fc(cfg.cov_file);
    if (!fc) throw std::runtime_error("failed to open: " + cfg.cov_file.string());
    fc << "logic_output,total_occurrences,single_cell_matches,multi_cell_logic_clusters\n";
    for (const auto& row : cov_rows) {
      fc << row.logic_name << "," << format_count(row.total) << "," << format_count(row.dedicated) << ","
         << format_count(row.composite) << "\n";
    }
  }

  double total_occurrences = 0.0;
  for (const auto& [signature, r] : items) {
    total_occurrences += cfg.opt.overlap_weighted_counting ? weighted_count_for_signature[signature] : r.count;
  }

  if (!cfg.cdb_file.empty()) {

    std::unordered_map<int, std::vector<int>> superset_of, subset_of;
    for (const auto& [a, b] : subset_pairs) {
      superset_of[a].push_back(b);
      subset_of[b].push_back(a);
    }

    std::unordered_map<int, std::unordered_set<int>> superset_set, subset_set;
    for (const auto& [k, v] : superset_of) for (int x : v) superset_set[k].insert(x);
    for (const auto& [k, v] : subset_of) for (int x : v) subset_set[k].insert(x);

    std::unordered_map<int, std::vector<int>> overlap_of;
    for (const auto& c : clsts) {
      auto it = adj.find(c.cid);
      if (it == adj.end()) continue;
      for (int other : it->second) {
        if (superset_set[c.cid].count(other)) continue;
        if (subset_set[c.cid].count(other)) continue;
        overlap_of[c.cid].push_back(other);
      }
    }
    for (auto& [_, v] : overlap_of) std::sort(v.begin(), v.end());

    const int n_clsts = static_cast<int>(clsts.size());
    std::vector<cdb_writer::WCluster> w_clusters(static_cast<size_t>(n_clsts));
    {
      int workers = std::max(1, cfg.opt.num_cores);
      if (workers > n_clsts && n_clsts > 0) workers = n_clsts;
      std::atomic<int> next_idx{0};
      std::vector<std::thread> cdb_threads;
      cdb_threads.reserve(static_cast<size_t>(workers));
      for (int t = 0; t < workers; ++t) {
        cdb_threads.emplace_back([&]() {
          while (true) {
            int ci = next_idx.fetch_add(1);
            if (ci >= n_clsts) break;
            const auto& c = clsts[static_cast<size_t>(ci)];
            cdb_writer::WCluster& wc = w_clusters[static_cast<size_t>(ci)];
      wc.id = c.cid;
      wc.canonical = signature_to_binary(c.canonical_key);
      wc.cell_ids = c.ids;
      wc.inputs.assign(c.inputs.begin(), c.inputs.end());
      wc.outputs.assign(c.outputs.begin(), c.outputs.end());
      wc.truth_table = c.truth_table;
      wc.input_order = c.input_order;

      float total_area = 0.0f;
      int max_depth = 0;
      for (int dense_id : c.ids) {
        if (dense_id < 0 || dense_id > static_cast<int>(g.cell_list.size())) continue;
        Cell* cell = g.cell_list[dense_id];
        if (!cell) continue;
        if (cell->master) {
          auto ait = g.lib_master_area.find(cell->master->name);
          if (ait != g.lib_master_area.end()) total_area += static_cast<float>(ait->second);
        }
      }

      {
        std::unordered_set<int> cell_set(c.ids.begin(), c.ids.end());
        std::unordered_map<int, int> depth_memo;
        std::function<int(int)> compute_depth = [&](int cid_inner) -> int {
          auto it = depth_memo.find(cid_inner);
          if (it != depth_memo.end()) return it->second;
          depth_memo[cid_inner] = 1;
          if (cid_inner <= 0 || cid_inner > static_cast<int>(g.cell_list.size())) return 1;
          Cell* cell = g.cell_list[cid_inner];
          if (!cell) return 1;
          int best = 1;
          for (const auto& [_, pin] : cell->in_pins) {
            if (!pin || !pin->net || !pin->net->driver) continue;
            Cell* prev = pin->net->driver->cell;
            if (!prev || cell_set.count(prev->id) == 0) continue;
            best = std::max(best, 1 + compute_depth(prev->id));
          }
          depth_memo[cid_inner] = best;
          return best;
        };
        for (int dense_id : c.ids) max_depth = std::max(max_depth, compute_depth(dense_id));
      }
      wc.total_area = total_area;
      wc.depth = max_depth;

      if (overlap_of.count(c.cid)) wc.overlap_cluster_ids = overlap_of[c.cid];
      if (superset_of.count(c.cid)) {
        wc.superset_cluster_ids = superset_of[c.cid];
        std::sort(wc.superset_cluster_ids.begin(), wc.superset_cluster_ids.end());
      }
      if (subset_of.count(c.cid)) {
        wc.subset_cluster_ids = subset_of[c.cid];
        std::sort(wc.subset_cluster_ids.begin(), wc.subset_cluster_ids.end());
      }

      std::unordered_set<int> cell_set(c.ids.begin(), c.ids.end());
      for (int dense_id : c.ids) {
        if (dense_id < 0 || dense_id > static_cast<int>(g.cell_list.size())) continue;
        Cell* cell = g.cell_list[dense_id];
        if (!cell) continue;

        for (const auto& [term, pin] : cell->in_pins) {
          if (!pin) continue;
          Net* net = pin->net;
          Pin* drv = net ? net->driver : nullptr;
          Cell* prev = drv ? drv->cell : nullptr;
          if (!prev || cell_set.count(prev->id) == 0) {
            cdb_writer::WBoundaryPin bp;
            bp.cell_id = dense_id;
            bp.pin_name = pin->full_name;
            bp.net_name = net ? net->name : pin->full_name;
            wc.boundary_input_pins.push_back(std::move(bp));
          }
        }

        for (const auto& [term, out_pin] : cell->out_pins) {
          if (!out_pin) continue;
          Net* net = out_pin->net;
          bool external = !net || net->loads.empty();
          if (!external && net) {
            for (auto* load_pin : net->loads) {
              if (!load_pin || !load_pin->cell || cell_set.count(load_pin->cell->id) == 0) {
                external = true;
                break;
              }
            }
          }
          if (external) {
            cdb_writer::WBoundaryPin bp;
            bp.cell_id = dense_id;
            bp.pin_name = out_pin->full_name;
            bp.net_name = net ? net->name : out_pin->full_name;
            wc.boundary_output_pins.push_back(std::move(bp));
          }
        }
      }

      std::unordered_set<std::string> seen_nets;
      for (int dense_id : c.ids) {
        if (dense_id < 0 || dense_id > static_cast<int>(g.cell_list.size())) continue;
        Cell* cell = g.cell_list[dense_id];
        if (!cell) continue;
        auto check_net = [&](Net* net) {
          if (!net || net->name.empty() || !seen_nets.insert(net->name).second) return;

          if (net->driver && net->driver->cell && cell_set.count(net->driver->cell->id) == 0) return;

          for (auto* lp : net->loads) {
            if (!lp || !lp->cell || cell_set.count(lp->cell->id) == 0) return;
          }

          if (net->driver && net->driver->cell)
            wc.internal_nets.push_back(net->name);
        };
        for (const auto& [_, pin] : cell->in_pins) if (pin && pin->net) check_net(pin->net);
        for (const auto& [_, pin] : cell->out_pins) if (pin && pin->net) check_net(pin->net);
      }
      std::sort(wc.internal_nets.begin(), wc.internal_nets.end());
          }
        });
      }
      for (auto& th : cdb_threads) th.join();
    }

    std::vector<cdb_writer::WLogicFunction> w_logic_functions;
    w_logic_functions.reserve(items.size());
    for (const auto& [signature, r] : items) {
      cdb_writer::WLogicFunction wp;
      wp.master_cell = logic_label_for_signature(g, signature);
      wp.canonical = signature_to_binary(signature);
      wp.expr = exprs_from_signature(signature);
      wp.count = cfg.opt.overlap_weighted_counting
                     ? static_cast<int>(std::llround(weighted_count_for_signature[signature]))
                     : r.count;
      w_logic_functions.push_back(std::move(wp));
    }

    if (!cdb_writer::write_db(cfg.cdb_file.string(), w_clusters, w_logic_functions,
                              overlap_components, subset_pairs)) {
      throw std::runtime_error("failed to write cluster database: " + cfg.cdb_file.string());
    }

  }

  return total_occurrences;
}

std::vector<SelectedCandidate> select_existing_library_candidates(const Cfg& cfg,
                                                                  const Graph& g,
                                                                  const std::unordered_map<std::string, Rec>& recs) {
  std::vector<std::pair<std::string, Rec>> items(recs.begin(), recs.end());
  std::sort(items.begin(), items.end(), [](auto& a, auto& b) { return a.second.count > b.second.count; });

  struct ClusterRec {
    int cid;
    std::string signature;
    std::vector<std::string> names;
  };
  std::vector<ClusterRec> clusters;
  clusters.reserve(recs.size() * 4);
  int next_cid = 1;
  for (const auto& [signature, rec] : items) {
    for (const auto& inst : rec.insts) {
      std::vector<std::string> names;
      names.reserve(inst.size());
      for (int internal_id : inst) {
        if (internal_id < 0 || internal_id >= static_cast<int>(g.cell_list.size())) continue;
        auto* cell = g.cell_list[internal_id];
        if (!cell) continue;
        names.push_back(cell->name);
      }
      std::sort(names.begin(), names.end());
      clusters.push_back({next_cid++, signature, std::move(names)});
    }
  }

  std::unordered_map<int, std::unordered_set<int>> adj;
  std::unordered_map<std::string, std::vector<int>> by_cell_name;
  for (const auto& cluster : clusters) {
    adj[cluster.cid];
    for (const auto& name : cluster.names) by_cell_name[name].push_back(cluster.cid);
  }
  for (const auto& [_, ids] : by_cell_name) {
    for (size_t i = 0; i < ids.size(); ++i) {
      for (size_t j = i + 1; j < ids.size(); ++j) {
        adj[ids[i]].insert(ids[j]);
        adj[ids[j]].insert(ids[i]);
      }
    }
  }

  std::unordered_map<int, const ClusterRec*> cluster_by_id;
  std::unordered_map<std::string, std::vector<int>> cluster_ids_by_signature;
  for (const auto& cluster : clusters) {
    cluster_by_id[cluster.cid] = &cluster;
    cluster_ids_by_signature[cluster.signature].push_back(cluster.cid);
  }

  std::string top_signature;
  std::string top_master;
  std::string top_count;
  for (const auto& [signature, rec] : items) {
    std::string master = best_master_for_signature(g, signature);
    if (master.empty()) continue;
    if (is_already_enabled(master, cfg.opt.enabled_patterns)) continue;
    top_signature = signature;
    top_master = master;
    top_count = std::to_string(rec.count);
    break;
  }
  if (top_signature.empty()) return {};

  std::unordered_set<int> top_component_cluster_ids;
  std::unordered_set<int> visited;
  for (int start_id : cluster_ids_by_signature[top_signature]) {
    if (visited.count(start_id)) continue;
    std::vector<int> stack = {start_id};
    while (!stack.empty()) {
      int u = stack.back();
      stack.pop_back();
      if (visited.count(u)) continue;
      visited.insert(u);
      top_component_cluster_ids.insert(u);
      for (int v : adj[u]) {
        if (!visited.count(v)) stack.push_back(v);
      }
    }
  }

  std::vector<SelectedCandidate> selected;
  std::unordered_set<std::string> seen_patterns;
  std::unordered_set<std::string> related_signatures;
  related_signatures.insert(top_signature);
  for (int cid : top_component_cluster_ids) {
    auto it = cluster_by_id.find(cid);
    if (it == cluster_by_id.end() || !it->second) continue;
    related_signatures.insert(it->second->signature);
  }

  int overlap_added = 0;
  for (const auto& [signature, r] : items) {
    if (!related_signatures.count(signature)) continue;
    std::string best_master = best_master_for_signature(g, signature);
    double best_area = std::numeric_limits<double>::infinity();
    std::string best_lib;
    if (!best_master.empty()) {
      auto area_it = g.lib_master_area.find(best_master);
      if (area_it != g.lib_master_area.end()) best_area = area_it->second;
      auto lib_it = g.lib_master_source_lib.find(best_master);
      best_lib = lib_it == g.lib_master_source_lib.end() ? "" : lib_it->second;
    }
    if (best_master.empty()) continue;
    if (is_already_enabled(best_master, cfg.opt.enabled_patterns)) continue;
    std::string pat = candidate_pattern(best_master, cfg.opt.candidate_pattern_mode);
    if (!seen_patterns.insert(pat).second) continue;
    bool is_anchor = (signature == top_signature);
    if (!is_anchor && overlap_added >= cfg.opt.max_overlap_candidates) continue;
    selected.push_back(
        {
            best_master,
            top_master,
            pat,
            signature,
            exprs_from_signature(signature),
            std::to_string(r.count),
            best_area,
            best_lib,
        });
    selected.back().source_master_cell = best_master;
    if (!is_anchor) overlap_added += 1;
  }
  return selected;
}

void write_selected_candidates_csv(const fs::path& path, const std::vector<SelectedCandidate>& candidates) {
  std::ofstream f(path);
  if (!f) throw std::runtime_error("failed to open: " + path.string());
  f << "master_cell,source_master_cell,candidate_pattern,canonical_key,logic_expr,count,selected_area,selected_lib\n";
  for (const auto& cand : candidates) {
    f << cand.master_cell << ","
      << cand.source_master_cell << ","
      << cand.candidate_pattern << ","
      << cand.canonical_key << ","
      << cand.logic_expr << ","
      << cand.count << ","
      << format_count(cand.selected_area) << ","
      << cand.selected_lib << "\n";
  }
}

void write_graph(const Graph& g, const fs::path& p) {
  std::ofstream f(p);
  if (!f) throw std::runtime_error("failed to open graph file: " + p.string());
  for (const auto& [_, cup] : g.cells) {
    auto* c = cup.get();
    if (!c) continue;
    f << "cell=" << c->name << " master=" << (c->master ? c->master->name : "UNKNOWN")
      << " fanins=" << c->fanins.size() << " fanouts=" << c->fanouts.size() << "\n";
  }
}

void write_config_log(const Cfg& cfg, int unique_logic_functions, double total_occurrences) {
  std::ofstream f(cfg.config_log_file);
  if (!f) throw std::runtime_error("failed to open config log: " + cfg.config_log_file.string());
  std::time_t now = std::time(nullptr);
  f << "[" << std::put_time(std::gmtime(&now), "%FT%TZ") << "]\n";
  f << "out_dir=" << cfg.out_dir << "\n";
  f << "pattern_freq=" << cfg.freq_file << "\n";
  f << "pattern_detail=" << cfg.detail_file << "\n";
  f << "overlap_clusters=" << cfg.overlap_file << "\n";
  f << "subset_clusters=" << cfg.subset_file << "\n";
  f << "cell_map=" << cfg.map_file << "\n";
  f << "logic_coverage=" << cfg.cov_file << "\n";
  if (cfg.add_np_class && cfg.text_outputs) f << "p_to_np_class=" << cfg.np_class_map_file << "\n";
  f << "cluster_db=" << cfg.cdb_file << "\n";
  f << "text_outputs=" << (cfg.text_outputs ? "true" : "false") << "\n";
  f << "unique_logic_functions=" << unique_logic_functions << "\n";
  f << "total_occurrences=" << format_count(total_occurrences) << "\n";
  f << "min_cells=" << cfg.opt.min_cells << "\n";
  f << "max_cells=" << cfg.opt.max_cells << "\n";
  f << "min_inputs=" << cfg.opt.min_inputs << "\n";
  f << "max_inputs=" << cfg.opt.max_inputs << "\n";
  f << "max_depth=" << cfg.opt.max_depth << "\n";
  f << "max_distance=" << (cfg.opt.max_distance ? std::to_string(*cfg.opt.max_distance) : "None") << "\n";
  f << "max_root_dx=" << (cfg.opt.max_root_dx ? std::to_string(*cfg.opt.max_root_dx) : "None") << "\n";
  f << "max_root_dy=" << (cfg.opt.max_root_dy ? std::to_string(*cfg.opt.max_root_dy) : "None") << "\n";
  f << "min_cell_overlap=" << cfg.opt.min_cell_overlap << "\n";
  f << "allow_internal_fanout=" << (cfg.opt.allow_internal_fanout ? "true" : "false") << "\n";
  f << "ignore_nonroot_outputs=" << (cfg.opt.ignore_nonroot_outputs ? "true" : "false") << "\n\n";
  f << "overlap_weighted_counting=" << (cfg.opt.overlap_weighted_counting ? "true" : "false") << "\n";
  f << "target_canonical=" << cfg.opt.target_canonical << "\n";
  {
    std::vector<std::string> target_set(cfg.opt.target_canonical_set.begin(), cfg.opt.target_canonical_set.end());
    std::sort(target_set.begin(), target_set.end());
    std::ostringstream oss;
    for (size_t i = 0; i < target_set.size(); ++i) {
      if (i) oss << ";";
      oss << target_set[i];
    }
    f << "target_canonical_set=" << oss.str() << "\n";
  }
  f << "verification_mode=" << (cfg.verification_mode ? "true" : "false") << "\n";
  if (cfg.verification_mode) {
    f << "included_clusters_log=" << cfg.included_clusters_file << "\n";
    f << "excluded_clusters_log=" << cfg.excluded_clusters_file << "\n";
    f << "branch_trace_log=" << cfg.branch_log_file << "\n";
    f << "verification_report=" << cfg.verification_report_file << "\n";
  }
}

long read_peak_rss_kb() {
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind("VmHWM:", 0) == 0) {
      std::stringstream ss(line.substr(6));
      long kb = 0;
      ss >> kb;
      return kb;
    }
  }
  return -1;
}
