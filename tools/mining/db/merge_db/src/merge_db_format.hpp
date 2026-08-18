

#pragma once

#include <cstdint>

constexpr uint32_t MERGE_DB_MAGIC   = 0x4D524730;
constexpr uint32_t MERGE_DB_VERSION = 2;

struct MergeDbHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t merge_count;
  uint32_t total_cluster_ids;
  uint32_t total_shared_input_offsets;
  uint32_t total_output_offsets;
  uint32_t string_pool_bytes;
};

struct MergeRecord {
  uint32_t canonical_offset;
  uint32_t cluster_id_start;
  uint16_t cluster_id_count;
  uint16_t _pad0;
  uint32_t shared_input_start;
  uint16_t shared_input_count;
  uint16_t _pad1;
  uint32_t output_start;
  uint16_t output_count;
  uint16_t _pad2;
};

static_assert(sizeof(MergeDbHeader) == 28, "MergeDbHeader size mismatch");
static_assert(sizeof(MergeRecord)   == 28, "MergeRecord size mismatch");
