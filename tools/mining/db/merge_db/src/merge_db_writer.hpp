#pragma once

#include "merge_db_format.hpp"

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace merge_db_writer {

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

struct WMerge {
  std::string canonical;
  std::vector<int32_t> cluster_ids;
  std::vector<std::string> shared_inputs;
  std::vector<std::string> outputs;
};

template<typename T>
inline void write_vec(std::ofstream& f, const std::vector<T>& v) {
  if (!v.empty()) f.write(reinterpret_cast<const char*>(v.data()), sizeof(T) * v.size());
}

inline bool write_db(const std::string& out_path,
                     const std::vector<WMerge>& merges) {
  StringPool pool;
  pool.add("");

  std::vector<MergeRecord> recs;
  recs.reserve(merges.size());
  std::vector<int32_t> packed_cids;
  std::vector<uint32_t> packed_inputs, packed_outputs;

  for (const auto& m : merges) {
    MergeRecord r{};
    r.canonical_offset = pool.add(m.canonical);
    r.cluster_id_start = static_cast<uint32_t>(packed_cids.size());
    r.cluster_id_count = static_cast<uint16_t>(m.cluster_ids.size());
    for (int32_t cid : m.cluster_ids) packed_cids.push_back(cid);
    r.shared_input_start = static_cast<uint32_t>(packed_inputs.size());
    r.shared_input_count = static_cast<uint16_t>(m.shared_inputs.size());
    for (const auto& s : m.shared_inputs) packed_inputs.push_back(pool.add(s));
    r.output_start = static_cast<uint32_t>(packed_outputs.size());
    r.output_count = static_cast<uint16_t>(m.outputs.size());
    for (const auto& s : m.outputs) packed_outputs.push_back(pool.add(s));
    recs.push_back(r);
  }

  MergeDbHeader hdr{};
  hdr.magic = MERGE_DB_MAGIC;
  hdr.version = MERGE_DB_VERSION;
  hdr.merge_count = static_cast<uint32_t>(recs.size());
  hdr.total_cluster_ids = static_cast<uint32_t>(packed_cids.size());
  hdr.total_shared_input_offsets = static_cast<uint32_t>(packed_inputs.size());
  hdr.total_output_offsets = static_cast<uint32_t>(packed_outputs.size());
  hdr.string_pool_bytes = static_cast<uint32_t>(pool.data.size());

  std::ofstream f(out_path, std::ios::binary);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  write_vec(f, recs);
  write_vec(f, packed_cids);
  write_vec(f, packed_inputs);
  write_vec(f, packed_outputs);
  f.write(pool.data.data(), pool.data.size());
  return true;
}

}
