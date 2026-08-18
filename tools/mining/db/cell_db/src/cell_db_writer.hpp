#pragma once

#include "cell_db_format.hpp"

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace cell_db_writer {

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

struct WPin {
  std::string name;
  uint8_t direction = 0;
};

struct WFunction {
  std::string pin_name;
  std::string expr;
  std::string canonical;

  int16_t stage_count          = -1;
  int16_t max_pu_stack_depth   = -1;
  int16_t max_pd_stack_depth   = -1;
  float   resistance_proxy_max  = 0.0f;
  float   capacitance_proxy_max = 0.0f;
};

struct WMaster {
  std::string name;
  std::string family;
  float area = 0.0f;
  bool is_seq = false;
  int output_count = 0;
  std::vector<WPin> pins;
  std::vector<WFunction> functions;

  float width = 0.0f;
  float height = 0.0f;
  std::string site;
  uint8_t symmetry = 0;

  uint16_t tr_nmos = 0;
  uint16_t tr_pmos = 0;

  uint8_t shared_diffusion = 0;
};

template<typename T>
inline void write_vec(std::ofstream& f, const std::vector<T>& v) {
  if (!v.empty()) f.write(reinterpret_cast<const char*>(v.data()), sizeof(T) * v.size());
}

inline bool write_db(const std::string& out_path,
                     const std::vector<WMaster>& masters,
                     uint32_t source_flags = CELL_DB_SRC_LIBERTY) {
  StringPool pool;
  pool.add("");

  std::vector<MasterRecord> master_recs;
  master_recs.reserve(masters.size());
  std::vector<MasterPinRecord> pin_recs;
  std::vector<MasterFunctionRecord> func_recs;

  for (const auto& m : masters) {
    MasterRecord r{};
    r.name_offset   = pool.add(m.name);
    r.family_offset = pool.add(m.family);
    r.area          = m.area;
    r.is_seq        = m.is_seq ? 1 : 0;
    r.output_count  = static_cast<uint16_t>(m.output_count);
    r.pin_start     = static_cast<uint32_t>(pin_recs.size());
    r.pin_count     = static_cast<uint16_t>(m.pins.size());
    for (const auto& p : m.pins) {
      MasterPinRecord pr{};
      pr.name_offset = pool.add(p.name);
      pr.direction   = p.direction;
      pin_recs.push_back(pr);
    }
    r.func_start = static_cast<uint32_t>(func_recs.size());
    r.func_count = static_cast<uint16_t>(m.functions.size());
    for (const auto& fn : m.functions) {
      MasterFunctionRecord fr{};
      fr.pin_name_offset    = pool.add(fn.pin_name);
      fr.expr_offset        = pool.add(fn.expr);
      fr.canonical_offset   = pool.add(fn.canonical);
      fr.stage_count        = fn.stage_count;
      fr.max_pu_stack_depth = fn.max_pu_stack_depth;
      fr.max_pd_stack_depth = fn.max_pd_stack_depth;
      fr.resistance_proxy_max  = fn.resistance_proxy_max;
      fr.capacitance_proxy_max = fn.capacitance_proxy_max;
      func_recs.push_back(fr);
    }

    r.width       = m.width;
    r.height      = m.height;
    r.site_offset = pool.add(m.site);
    r.symmetry    = m.symmetry;

    r.tr_nmos = m.tr_nmos;
    r.tr_pmos = m.tr_pmos;
    r.shared_diffusion = m.shared_diffusion;
    master_recs.push_back(r);
  }

  CellDbHeader hdr{};
  hdr.magic             = CELL_DB_MAGIC;
  hdr.version           = CELL_DB_VERSION;
  hdr.master_count      = static_cast<uint32_t>(master_recs.size());
  hdr.total_pins        = static_cast<uint32_t>(pin_recs.size());
  hdr.total_functions   = static_cast<uint32_t>(func_recs.size());
  hdr.string_pool_bytes = static_cast<uint32_t>(pool.data.size());
  hdr.source_flags      = source_flags;

  std::ofstream f(out_path, std::ios::binary);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  write_vec(f, master_recs);
  write_vec(f, pin_recs);
  write_vec(f, func_recs);
  f.write(pool.data.data(), pool.data.size());
  return true;
}

}
