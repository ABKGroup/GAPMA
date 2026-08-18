

#pragma once

#include <cstdint>

constexpr uint32_t CLUSTER_DB_MAGIC   = 0x43444230;
constexpr uint32_t CLUSTER_DB_VERSION = 5;

struct FileHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t cluster_count;
  uint32_t logic_function_count;
  uint32_t overlap_group_count;
  uint32_t overlap_entry_count;
  uint32_t subset_pair_count;
  uint32_t total_cell_ids;
  uint32_t total_input_offsets;
  uint32_t total_output_offsets;
  uint32_t total_input_order_offsets;
  uint32_t total_internal_net_offsets;
  uint32_t total_boundary_input_pins;
  uint32_t total_boundary_output_pins;
  uint32_t total_overlap_cluster_ids;
  uint32_t total_superset_cluster_ids;
  uint32_t total_subset_cluster_ids;
  uint32_t string_pool_bytes;
};

struct ClusterRecord {
  int32_t  id;
  uint32_t canonical_offset;
  uint32_t cell_id_start;
  uint16_t cell_id_count;
  uint16_t _pad0;
  uint32_t input_start;
  uint16_t input_count;
  uint16_t _pad1;
  uint32_t output_start;
  uint16_t output_count;
  uint16_t _pad2;
  uint32_t truth_table_offset;
  uint32_t input_order_start;
  uint16_t input_order_count;
  uint16_t _pad3;

  float    total_area;
  uint16_t depth;
  uint16_t _pad4;

  uint32_t internal_net_start;
  uint16_t internal_net_count;
  uint16_t _pad5;
  uint32_t boundary_input_pin_start;
  uint16_t boundary_input_pin_count;
  uint16_t _pad6;
  uint32_t boundary_output_pin_start;
  uint16_t boundary_output_pin_count;
  uint16_t _pad7;

  uint32_t overlap_start;
  uint16_t overlap_count;
  uint16_t _pad8;
  uint32_t superset_start;
  uint16_t superset_count;
  uint16_t _pad9;
  uint32_t subset_start;
  uint16_t subset_count;
  uint16_t _pad10;
};

struct LogicFunctionRecord {
  uint32_t canonical_offset;
  int32_t  count;
  uint32_t expr_offset;
  uint32_t library_match_offset;
};

struct BoundaryPinRecord {
  int32_t  cell_id;
  uint32_t pin_name_offset;
  uint32_t net_name_offset;
};

static_assert(sizeof(FileHeader)         == 72, "FileHeader size mismatch");
static_assert(sizeof(ClusterRecord)      == 100, "ClusterRecord size mismatch");
static_assert(sizeof(LogicFunctionRecord)== 16, "LogicFunctionRecord size mismatch");
static_assert(sizeof(BoundaryPinRecord)  == 12, "BoundaryPinRecord size mismatch");
