#pragma once

#include "cluster_db_format.hpp"

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace cdb_writer {

struct StringPool {
  std::vector<char> data;
  std::unordered_map<std::string, uint32_t> index;
  uint32_t add(const std::string& s) {
    auto it = index.find(s);
    if (it != index.end()) return it->second;
    uint32_t offset = static_cast<uint32_t>(data.size());
    data.insert(data.end(), s.begin(), s.end());
    data.push_back('\0');
    index[s] = offset;
    return offset;
  }
};

struct WBoundaryPin {
  int cell_id = 0;
  std::string pin_name;
  std::string net_name;
};

struct WCluster {
  int id = 0;
  std::string canonical;
  std::vector<int> cell_ids;
  std::vector<std::string> inputs, outputs;
  std::string truth_table;
  std::vector<std::string> input_order;
  float total_area = 0.0f;
  int depth = 0;
  std::vector<std::string> internal_nets;
  std::vector<WBoundaryPin> boundary_input_pins;
  std::vector<WBoundaryPin> boundary_output_pins;
  std::vector<int> overlap_cluster_ids;
  std::vector<int> superset_cluster_ids;
  std::vector<int> subset_cluster_ids;
};

struct WLogicFunction {
  std::string master_cell, canonical, expr;
  int count = 0;
};

template<typename T>
inline void write_vec(std::ofstream& f, const std::vector<T>& v) {
  if (!v.empty()) f.write(reinterpret_cast<const char*>(v.data()), sizeof(T) * v.size());
}

inline bool write_db(const std::string& out_path,
                     const std::vector<WCluster>& clusters,
                     const std::vector<WLogicFunction>& logic_functions,
                     const std::vector<std::vector<int>>& overlap_groups,
                     const std::vector<std::pair<int,int>>& subset_pairs) {
  StringPool pool;
  pool.add("");

  std::vector<ClusterRecord> cluster_recs;
  cluster_recs.reserve(clusters.size());
  std::vector<int32_t> packed_cell_ids;
  std::vector<uint32_t> packed_input_offsets, packed_output_offsets, packed_input_order_offsets;
  std::vector<uint32_t> packed_internal_net_offsets;
  std::vector<BoundaryPinRecord> packed_bip, packed_bop;
  std::vector<int32_t> packed_overlap_cids, packed_superset_cids, packed_subset_cids;

  for (const auto& c : clusters) {
    ClusterRecord r{};
    r.id = c.id;
    r.canonical_offset = pool.add(c.canonical);
    r.cell_id_start = static_cast<uint32_t>(packed_cell_ids.size());
    r.cell_id_count = static_cast<uint16_t>(c.cell_ids.size());
    for (int cid : c.cell_ids) packed_cell_ids.push_back(cid);
    r.input_start = static_cast<uint32_t>(packed_input_offsets.size());
    r.input_count = static_cast<uint16_t>(c.inputs.size());
    for (const auto& s : c.inputs) packed_input_offsets.push_back(pool.add(s));
    r.output_start = static_cast<uint32_t>(packed_output_offsets.size());
    r.output_count = static_cast<uint16_t>(c.outputs.size());
    for (const auto& s : c.outputs) packed_output_offsets.push_back(pool.add(s));
    r.truth_table_offset = pool.add(c.truth_table);
    r.input_order_start = static_cast<uint32_t>(packed_input_order_offsets.size());
    r.input_order_count = static_cast<uint16_t>(c.input_order.size());
    for (const auto& s : c.input_order) packed_input_order_offsets.push_back(pool.add(s));
    r.total_area = c.total_area;
    r.depth = static_cast<uint16_t>(c.depth);
    r.internal_net_start = static_cast<uint32_t>(packed_internal_net_offsets.size());
    r.internal_net_count = static_cast<uint16_t>(c.internal_nets.size());
    for (const auto& s : c.internal_nets) packed_internal_net_offsets.push_back(pool.add(s));
    r.boundary_input_pin_start = static_cast<uint32_t>(packed_bip.size());
    r.boundary_input_pin_count = static_cast<uint16_t>(c.boundary_input_pins.size());
    for (const auto& bp : c.boundary_input_pins) {
      BoundaryPinRecord bpr{}; bpr.cell_id = bp.cell_id;
      bpr.pin_name_offset = pool.add(bp.pin_name); bpr.net_name_offset = pool.add(bp.net_name);
      packed_bip.push_back(bpr);
    }
    r.boundary_output_pin_start = static_cast<uint32_t>(packed_bop.size());
    r.boundary_output_pin_count = static_cast<uint16_t>(c.boundary_output_pins.size());
    for (const auto& bp : c.boundary_output_pins) {
      BoundaryPinRecord bpr{}; bpr.cell_id = bp.cell_id;
      bpr.pin_name_offset = pool.add(bp.pin_name); bpr.net_name_offset = pool.add(bp.net_name);
      packed_bop.push_back(bpr);
    }
    r.overlap_start = static_cast<uint32_t>(packed_overlap_cids.size());
    r.overlap_count = static_cast<uint16_t>(c.overlap_cluster_ids.size());
    for (int cid : c.overlap_cluster_ids) packed_overlap_cids.push_back(cid);
    r.superset_start = static_cast<uint32_t>(packed_superset_cids.size());
    r.superset_count = static_cast<uint16_t>(c.superset_cluster_ids.size());
    for (int cid : c.superset_cluster_ids) packed_superset_cids.push_back(cid);
    r.subset_start = static_cast<uint32_t>(packed_subset_cids.size());
    r.subset_count = static_cast<uint16_t>(c.subset_cluster_ids.size());
    for (int cid : c.subset_cluster_ids) packed_subset_cids.push_back(cid);
    cluster_recs.push_back(r);
  }

  std::vector<LogicFunctionRecord> pattern_recs;
  for (const auto& p : logic_functions) {
    LogicFunctionRecord r{};
    r.canonical_offset = pool.add(p.canonical);
    r.count = p.count;
    r.expr_offset = pool.add(p.expr);
    r.library_match_offset = pool.add(p.master_cell == "New" ? "" : p.master_cell);
    pattern_recs.push_back(r);
  }

  std::vector<int32_t> overlap_entries;
  for (const auto& group : overlap_groups) {
    for (int id : group) overlap_entries.push_back(id);
    overlap_entries.push_back(-1);
  }

  std::vector<int32_t> subset_flat;
  for (const auto& [a, b] : subset_pairs) { subset_flat.push_back(a); subset_flat.push_back(b); }

  FileHeader hdr{};
  hdr.magic = CLUSTER_DB_MAGIC;
  hdr.version = CLUSTER_DB_VERSION;
  hdr.cluster_count = static_cast<uint32_t>(cluster_recs.size());
  hdr.logic_function_count = static_cast<uint32_t>(pattern_recs.size());
  hdr.overlap_group_count = static_cast<uint32_t>(overlap_groups.size());
  hdr.overlap_entry_count = static_cast<uint32_t>(overlap_entries.size());
  hdr.subset_pair_count = static_cast<uint32_t>(subset_pairs.size());
  hdr.total_cell_ids = static_cast<uint32_t>(packed_cell_ids.size());
  hdr.total_input_offsets = static_cast<uint32_t>(packed_input_offsets.size());
  hdr.total_output_offsets = static_cast<uint32_t>(packed_output_offsets.size());
  hdr.total_input_order_offsets = static_cast<uint32_t>(packed_input_order_offsets.size());
  hdr.total_internal_net_offsets = static_cast<uint32_t>(packed_internal_net_offsets.size());
  hdr.total_boundary_input_pins = static_cast<uint32_t>(packed_bip.size());
  hdr.total_boundary_output_pins = static_cast<uint32_t>(packed_bop.size());
  hdr.total_overlap_cluster_ids = static_cast<uint32_t>(packed_overlap_cids.size());
  hdr.total_superset_cluster_ids = static_cast<uint32_t>(packed_superset_cids.size());
  hdr.total_subset_cluster_ids = static_cast<uint32_t>(packed_subset_cids.size());
  hdr.string_pool_bytes = static_cast<uint32_t>(pool.data.size());

  std::ofstream f(out_path, std::ios::binary);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  write_vec(f, cluster_recs);
  write_vec(f, pattern_recs);
  write_vec(f, overlap_entries);
  if (!subset_flat.empty())
    f.write(reinterpret_cast<const char*>(subset_flat.data()), sizeof(int32_t) * subset_flat.size());
  write_vec(f, packed_cell_ids);
  write_vec(f, packed_input_offsets);
  write_vec(f, packed_output_offsets);
  write_vec(f, packed_input_order_offsets);
  write_vec(f, packed_internal_net_offsets);
  write_vec(f, packed_bip);
  write_vec(f, packed_bop);
  write_vec(f, packed_overlap_cids);
  write_vec(f, packed_superset_cids);
  write_vec(f, packed_subset_cids);
  f.write(pool.data.data(), pool.data.size());
  return true;
}

}
