

#pragma once

#include "cluster_db_format.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

struct BoundaryPinEntry {
  int32_t cell_id;
  std::string pin_name;
  std::string net_name;
};

struct ClusterEntry {
  int32_t id;
  std::string canonical;
  std::vector<int32_t> cell_ids;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::string truth_table;
  std::vector<std::string> input_order;
  float total_area = 0.0f;
  int depth = 0;
  std::vector<std::string> internal_nets;
  std::vector<BoundaryPinEntry> boundary_input_pins;
  std::vector<BoundaryPinEntry> boundary_output_pins;
  std::vector<int32_t> overlap_cluster_ids;
  std::vector<int32_t> superset_cluster_ids;
  std::vector<int32_t> subset_cluster_ids;
};

struct LogicFunctionEntry {
  std::string canonical;
  int32_t count;
  std::string expr;
  std::string library_match;
};

struct ClusterDB {
  std::vector<ClusterEntry> clusters;
  std::vector<LogicFunctionEntry> logic_functions;
  std::vector<std::vector<int32_t>> overlap_groups;
  std::vector<std::pair<int32_t, int32_t>> subset_pairs;
};

inline ClusterDB read_cluster_db(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open cluster_db: " + path);

  FileHeader hdr{};
  f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
  if (hdr.magic != CLUSTER_DB_MAGIC) throw std::runtime_error("bad magic in cluster_db: " + path);
  if (hdr.version != CLUSTER_DB_VERSION)
    throw std::runtime_error("unsupported cluster_db version " + std::to_string(hdr.version) + " (expected " + std::to_string(CLUSTER_DB_VERSION) + ")");

  std::vector<ClusterRecord> cluster_recs(hdr.cluster_count);
  f.read(reinterpret_cast<char*>(cluster_recs.data()), sizeof(ClusterRecord) * hdr.cluster_count);

  std::vector<LogicFunctionRecord> lf_recs(hdr.logic_function_count);
  f.read(reinterpret_cast<char*>(lf_recs.data()), sizeof(LogicFunctionRecord) * hdr.logic_function_count);

  std::vector<int32_t> overlap_entries(hdr.overlap_entry_count);
  f.read(reinterpret_cast<char*>(overlap_entries.data()), sizeof(int32_t) * hdr.overlap_entry_count);

  std::vector<std::pair<int32_t, int32_t>> subset_pairs(hdr.subset_pair_count);
  f.read(reinterpret_cast<char*>(subset_pairs.data()), sizeof(int32_t) * 2 * hdr.subset_pair_count);

  std::vector<int32_t> packed_cell_ids(hdr.total_cell_ids);
  f.read(reinterpret_cast<char*>(packed_cell_ids.data()), sizeof(int32_t) * hdr.total_cell_ids);

  std::vector<uint32_t> packed_input_offsets(hdr.total_input_offsets);
  f.read(reinterpret_cast<char*>(packed_input_offsets.data()), sizeof(uint32_t) * hdr.total_input_offsets);

  std::vector<uint32_t> packed_output_offsets(hdr.total_output_offsets);
  f.read(reinterpret_cast<char*>(packed_output_offsets.data()), sizeof(uint32_t) * hdr.total_output_offsets);

  std::vector<uint32_t> packed_input_order_offsets(hdr.total_input_order_offsets);
  f.read(reinterpret_cast<char*>(packed_input_order_offsets.data()), sizeof(uint32_t) * hdr.total_input_order_offsets);

  std::vector<uint32_t> packed_internal_net_offsets(hdr.total_internal_net_offsets);
  f.read(reinterpret_cast<char*>(packed_internal_net_offsets.data()), sizeof(uint32_t) * hdr.total_internal_net_offsets);

  std::vector<BoundaryPinRecord> packed_bip(hdr.total_boundary_input_pins);
  f.read(reinterpret_cast<char*>(packed_bip.data()), sizeof(BoundaryPinRecord) * hdr.total_boundary_input_pins);

  std::vector<BoundaryPinRecord> packed_bop(hdr.total_boundary_output_pins);
  f.read(reinterpret_cast<char*>(packed_bop.data()), sizeof(BoundaryPinRecord) * hdr.total_boundary_output_pins);

  std::vector<int32_t> packed_ov(hdr.total_overlap_cluster_ids);
  f.read(reinterpret_cast<char*>(packed_ov.data()), sizeof(int32_t) * hdr.total_overlap_cluster_ids);

  std::vector<int32_t> packed_sup(hdr.total_superset_cluster_ids);
  f.read(reinterpret_cast<char*>(packed_sup.data()), sizeof(int32_t) * hdr.total_superset_cluster_ids);

  std::vector<int32_t> packed_sub(hdr.total_subset_cluster_ids);
  f.read(reinterpret_cast<char*>(packed_sub.data()), sizeof(int32_t) * hdr.total_subset_cluster_ids);

  std::vector<char> pool(hdr.string_pool_bytes);
  f.read(pool.data(), hdr.string_pool_bytes);

  auto str_at = [&](uint32_t offset) -> std::string {
    if (offset >= hdr.string_pool_bytes) return "";
    return std::string(&pool[offset]);
  };

  ClusterDB db;
  db.clusters.resize(hdr.cluster_count);
  for (uint32_t i = 0; i < hdr.cluster_count; ++i) {
    auto& cr = cluster_recs[i];
    auto& ce = db.clusters[i];
    ce.id = cr.id;
    ce.canonical = str_at(cr.canonical_offset);
    ce.cell_ids.assign(packed_cell_ids.begin() + cr.cell_id_start, packed_cell_ids.begin() + cr.cell_id_start + cr.cell_id_count);
    ce.inputs.resize(cr.input_count);
    for (uint16_t j = 0; j < cr.input_count; ++j) ce.inputs[j] = str_at(packed_input_offsets[cr.input_start + j]);
    ce.outputs.resize(cr.output_count);
    for (uint16_t j = 0; j < cr.output_count; ++j) ce.outputs[j] = str_at(packed_output_offsets[cr.output_start + j]);
    ce.truth_table = str_at(cr.truth_table_offset);
    ce.input_order.resize(cr.input_order_count);
    for (uint16_t j = 0; j < cr.input_order_count; ++j) ce.input_order[j] = str_at(packed_input_order_offsets[cr.input_order_start + j]);
    ce.total_area = cr.total_area;
    ce.depth = cr.depth;
    ce.internal_nets.resize(cr.internal_net_count);
    for (uint16_t j = 0; j < cr.internal_net_count; ++j) ce.internal_nets[j] = str_at(packed_internal_net_offsets[cr.internal_net_start + j]);
    ce.boundary_input_pins.resize(cr.boundary_input_pin_count);
    for (uint16_t j = 0; j < cr.boundary_input_pin_count; ++j) {
      auto& bp = packed_bip[cr.boundary_input_pin_start + j];
      ce.boundary_input_pins[j] = {bp.cell_id, str_at(bp.pin_name_offset), str_at(bp.net_name_offset)};
    }
    ce.boundary_output_pins.resize(cr.boundary_output_pin_count);
    for (uint16_t j = 0; j < cr.boundary_output_pin_count; ++j) {
      auto& bp = packed_bop[cr.boundary_output_pin_start + j];
      ce.boundary_output_pins[j] = {bp.cell_id, str_at(bp.pin_name_offset), str_at(bp.net_name_offset)};
    }
    ce.overlap_cluster_ids.assign(packed_ov.begin() + cr.overlap_start, packed_ov.begin() + cr.overlap_start + cr.overlap_count);
    ce.superset_cluster_ids.assign(packed_sup.begin() + cr.superset_start, packed_sup.begin() + cr.superset_start + cr.superset_count);
    ce.subset_cluster_ids.assign(packed_sub.begin() + cr.subset_start, packed_sub.begin() + cr.subset_start + cr.subset_count);
  }

  db.logic_functions.resize(hdr.logic_function_count);
  for (uint32_t i = 0; i < hdr.logic_function_count; ++i) {
    db.logic_functions[i] = {str_at(lf_recs[i].canonical_offset), lf_recs[i].count, str_at(lf_recs[i].expr_offset), str_at(lf_recs[i].library_match_offset)};
  }

  db.overlap_groups.reserve(hdr.overlap_group_count);
  { std::vector<int32_t> group;
    for (uint32_t i = 0; i < hdr.overlap_entry_count; ++i) {
      if (overlap_entries[i] == -1) { if (!group.empty()) db.overlap_groups.push_back(std::move(group)); group.clear(); }
      else group.push_back(overlap_entries[i]);
    }
    if (!group.empty()) db.overlap_groups.push_back(std::move(group));
  }

  db.subset_pairs = std::move(subset_pairs);
  return db;
}
