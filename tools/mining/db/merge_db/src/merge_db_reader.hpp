#pragma once

#include "merge_db_format.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

struct MergeEntry {
  std::string canonical;
  std::vector<int32_t> cluster_ids;
  std::vector<std::string> shared_inputs;
  std::vector<std::string> outputs;
};

struct MergeDb {
  std::vector<MergeEntry> merges;
};

inline MergeDb read_merge_db(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open merge_db: " + path);

  MergeDbHeader hdr{};
  f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
  if (hdr.magic != MERGE_DB_MAGIC) throw std::runtime_error("bad magic in merge_db: " + path);
  if (hdr.version != MERGE_DB_VERSION)
    throw std::runtime_error("unsupported merge_db version " + std::to_string(hdr.version));

  std::vector<MergeRecord> recs(hdr.merge_count);
  f.read(reinterpret_cast<char*>(recs.data()), sizeof(MergeRecord) * hdr.merge_count);

  std::vector<int32_t> packed_cids(hdr.total_cluster_ids);
  f.read(reinterpret_cast<char*>(packed_cids.data()), sizeof(int32_t) * hdr.total_cluster_ids);

  std::vector<uint32_t> packed_inputs(hdr.total_shared_input_offsets);
  f.read(reinterpret_cast<char*>(packed_inputs.data()), sizeof(uint32_t) * hdr.total_shared_input_offsets);

  std::vector<uint32_t> packed_outputs(hdr.total_output_offsets);
  f.read(reinterpret_cast<char*>(packed_outputs.data()), sizeof(uint32_t) * hdr.total_output_offsets);

  std::vector<char> pool(hdr.string_pool_bytes);
  f.read(pool.data(), hdr.string_pool_bytes);

  auto str_at = [&](uint32_t offset) -> std::string {
    if (offset >= hdr.string_pool_bytes) return "";
    return std::string(&pool[offset]);
  };

  MergeDb db;
  db.merges.resize(hdr.merge_count);
  for (uint32_t i = 0; i < hdr.merge_count; ++i) {
    auto& r = recs[i];
    auto& me = db.merges[i];
    me.canonical = str_at(r.canonical_offset);
    me.cluster_ids.assign(packed_cids.begin() + r.cluster_id_start,
                          packed_cids.begin() + r.cluster_id_start + r.cluster_id_count);
    me.shared_inputs.resize(r.shared_input_count);
    for (uint16_t j = 0; j < r.shared_input_count; ++j)
      me.shared_inputs[j] = str_at(packed_inputs[r.shared_input_start + j]);
    me.outputs.resize(r.output_count);
    for (uint16_t j = 0; j < r.output_count; ++j)
      me.outputs[j] = str_at(packed_outputs[r.output_start + j]);
  }
  return db;
}
