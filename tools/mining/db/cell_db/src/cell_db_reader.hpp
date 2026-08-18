#pragma once

#include "cell_db_format.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

struct MasterPinEntry {
  std::string name;
  uint8_t direction;
};

struct MasterFunctionEntry {
  std::string pin_name;
  std::string expr;
  std::string canonical;

  int16_t stage_count          = -1;
  int16_t max_pu_stack_depth   = -1;
  int16_t max_pd_stack_depth   = -1;
  float   resistance_proxy_max  = 0.0f;
  float   capacitance_proxy_max = 0.0f;
};

struct MasterEntry {
  std::string name;
  std::string family;
  float area;
  bool is_seq;
  int output_count;
  std::vector<MasterPinEntry> pins;
  std::vector<MasterFunctionEntry> functions;

  float width = 0.0f;
  float height = 0.0f;
  std::string site;
  uint8_t symmetry = 0;

  uint16_t tr_nmos = 0;
  uint16_t tr_pmos = 0;

  uint8_t shared_diffusion = 0;
};

struct CellDb {
  std::vector<MasterEntry> masters;
  std::unordered_map<std::string, const MasterEntry*> master_by_name;
  uint32_t source_flags = 0;
  bool has_liberty() const { return source_flags & CELL_DB_SRC_LIBERTY; }
  bool has_lef()     const { return source_flags & CELL_DB_SRC_LEF; }
  bool has_cdl()     const { return source_flags & CELL_DB_SRC_CDL; }
};

inline CellDb read_cell_db(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open cell_db: " + path);

  uint32_t peek[2]{};
  f.read(reinterpret_cast<char*>(peek), 8);
  if (peek[0] != CELL_DB_MAGIC)
    throw std::runtime_error("bad magic in cell_db: " + path);
  uint32_t file_version = peek[1];
  if (file_version != 2 && file_version != 3 && file_version != CELL_DB_VERSION)
    throw std::runtime_error("unsupported cell_db version " +
                             std::to_string(file_version) + " in " + path);
  f.seekg(0);
  CellDbHeader hdr{};
  if (file_version <= 3) {

    f.read(reinterpret_cast<char*>(&hdr), 24);
    hdr.source_flags = 0;
  } else {
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
  }

  struct MRaw {
    uint32_t name_offset, family_offset;
    float    area;
    uint16_t is_seq, output_count;
    uint32_t pin_start;
    uint16_t pin_count;
    uint32_t func_start;
    uint16_t func_count;
    float    width, height;
    uint32_t site_offset;
    uint8_t  symmetry;
    uint16_t tr_nmos = 0, tr_pmos = 0;
    uint8_t  shared_diffusion = 0;
  };

  std::vector<MRaw> mraw(hdr.master_count);

  if (hdr.version == 2) {
    std::vector<MasterRecordV2> recs(hdr.master_count);
    f.read(reinterpret_cast<char*>(recs.data()),
           sizeof(MasterRecordV2) * hdr.master_count);
    for (uint32_t i = 0; i < hdr.master_count; ++i) {
      const auto& r = recs[i];
      mraw[i] = {r.name_offset, r.family_offset, r.area, r.is_seq, r.output_count,
                 r.pin_start, r.pin_count, r.func_start, r.func_count,
                 r.width, r.height, r.site_offset, r.symmetry, 0, 0};
    }
  } else {
    std::vector<MasterRecord> recs(hdr.master_count);
    f.read(reinterpret_cast<char*>(recs.data()),
           sizeof(MasterRecord) * hdr.master_count);
    for (uint32_t i = 0; i < hdr.master_count; ++i) {
      const auto& r = recs[i];
      mraw[i] = {r.name_offset, r.family_offset, r.area, r.is_seq, r.output_count,
                 r.pin_start, r.pin_count, r.func_start, r.func_count,
                 r.width, r.height, r.site_offset, r.symmetry, r.tr_nmos, r.tr_pmos,
                 r.shared_diffusion};
    }
  }

  std::vector<MasterPinRecord> pin_recs(hdr.total_pins);
  f.read(reinterpret_cast<char*>(pin_recs.data()),
         sizeof(MasterPinRecord) * hdr.total_pins);

  struct FRaw {
    uint32_t pin_name_offset, expr_offset, canonical_offset;
    int16_t  stage_count          = -1;
    int16_t  max_pu_stack_depth   = -1;
    int16_t  max_pd_stack_depth   = -1;
    float    resistance_proxy_max  = 0.0f;
    float    capacitance_proxy_max = 0.0f;
  };

  std::vector<FRaw> fraw(hdr.total_functions);

  if (hdr.version == 2) {
    std::vector<MasterFunctionRecordV2> recs(hdr.total_functions);
    f.read(reinterpret_cast<char*>(recs.data()),
           sizeof(MasterFunctionRecordV2) * hdr.total_functions);
    for (uint32_t i = 0; i < hdr.total_functions; ++i) {
      fraw[i] = {recs[i].pin_name_offset, recs[i].expr_offset,
                 recs[i].canonical_offset, -1, -1, -1, 0.0f, 0.0f};
    }
  } else {
    std::vector<MasterFunctionRecord> recs(hdr.total_functions);
    f.read(reinterpret_cast<char*>(recs.data()),
           sizeof(MasterFunctionRecord) * hdr.total_functions);
    for (uint32_t i = 0; i < hdr.total_functions; ++i) {
      const auto& r = recs[i];
      fraw[i] = {r.pin_name_offset, r.expr_offset, r.canonical_offset,
                 r.stage_count, r.max_pu_stack_depth, r.max_pd_stack_depth,
                 r.resistance_proxy_max, r.capacitance_proxy_max};
    }
  }

  std::vector<char> pool(hdr.string_pool_bytes);
  f.read(pool.data(), hdr.string_pool_bytes);

  auto str_at = [&](uint32_t offset) -> std::string {
    if (offset >= hdr.string_pool_bytes) return "";
    return std::string(&pool[offset]);
  };

  CellDb db;
  db.masters.resize(hdr.master_count);
  for (uint32_t i = 0; i < hdr.master_count; ++i) {
    const auto& r = mraw[i];
    auto& me = db.masters[i];
    me.name         = str_at(r.name_offset);
    me.family       = str_at(r.family_offset);
    me.area         = r.area;
    me.is_seq       = r.is_seq != 0;
    me.output_count = r.output_count;
    me.width        = r.width;
    me.height       = r.height;
    me.site         = str_at(r.site_offset);
    me.symmetry     = r.symmetry;
    me.tr_nmos      = r.tr_nmos;
    me.tr_pmos      = r.tr_pmos;
    me.shared_diffusion = r.shared_diffusion;

    me.pins.resize(r.pin_count);
    for (uint16_t j = 0; j < r.pin_count; ++j) {
      const auto& pr = pin_recs[r.pin_start + j];
      me.pins[j] = {str_at(pr.name_offset), pr.direction};
    }

    me.functions.resize(r.func_count);
    for (uint16_t j = 0; j < r.func_count; ++j) {
      const auto& fr = fraw[r.func_start + j];
      auto& fe = me.functions[j];
      fe.pin_name              = str_at(fr.pin_name_offset);
      fe.expr                  = str_at(fr.expr_offset);
      fe.canonical             = str_at(fr.canonical_offset);
      fe.stage_count           = fr.stage_count;
      fe.max_pu_stack_depth    = fr.max_pu_stack_depth;
      fe.max_pd_stack_depth    = fr.max_pd_stack_depth;
      fe.resistance_proxy_max  = fr.resistance_proxy_max;
      fe.capacitance_proxy_max = fr.capacitance_proxy_max;
    }
  }

  db.source_flags = hdr.source_flags;
  for (auto& me : db.masters) db.master_by_name[me.name] = &me;
  return db;
}
