#pragma once

#include "netlist_db_format.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

struct InstanceEntry {
  int32_t id;
  std::string name;
  std::string master;
  float x = std::numeric_limits<float>::quiet_NaN();
  float y = std::numeric_limits<float>::quiet_NaN();
};

struct PinConnectionEntry {
  int32_t instance_id;
  std::string pin_name;
  bool is_driver;
};

struct NetlistNetEntry {
  std::string name;
  std::vector<PinConnectionEntry> pins;
};

struct DieAreaEntry {
  float llx = 0, lly = 0, urx = 0, ury = 0;
  float dbu_per_um = 0;
  bool valid() const { return urx > llx && ury > lly; }
};

struct NetlistDb {
  std::string top_name;
  DieAreaEntry die;
  std::vector<InstanceEntry> instances;
  std::vector<NetlistNetEntry> nets;
  std::unordered_map<int32_t, const InstanceEntry*> instance_by_id;
  std::unordered_map<std::string, const InstanceEntry*> instance_by_name;
  std::unordered_map<std::string, const NetlistNetEntry*> net_by_name;
  uint32_t source_flags = 0;
  bool has_netlist() const { return source_flags & NETLIST_DB_SRC_NETLIST; }
  bool has_def()     const { return source_flags & NETLIST_DB_SRC_DEF; }
};

inline NetlistDb read_netlist_db(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open netlist_db: " + path);

  uint32_t peek[2]{};
  f.read(reinterpret_cast<char*>(peek), 8);
  if (peek[0] != NETLIST_DB_MAGIC) throw std::runtime_error("bad magic in netlist_db: " + path);
  uint32_t file_version = peek[1];
  if (file_version != 2 && file_version != NETLIST_DB_VERSION)
    throw std::runtime_error("unsupported netlist_db version " + std::to_string(file_version));
  f.seekg(0);
  NetlistDbHeader hdr{};
  if (file_version == 2) {
    f.read(reinterpret_cast<char*>(&hdr), 48);
    hdr.source_flags = 0;
  } else {
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
  }

  std::vector<InstanceRecord> inst_recs(hdr.instance_count);
  f.read(reinterpret_cast<char*>(inst_recs.data()), sizeof(InstanceRecord) * hdr.instance_count);

  std::vector<NetlistNetRecord> net_recs(hdr.net_count);
  f.read(reinterpret_cast<char*>(net_recs.data()), sizeof(NetlistNetRecord) * hdr.net_count);

  std::vector<PinConnectionRecord> pin_recs(hdr.total_pin_connections);
  f.read(reinterpret_cast<char*>(pin_recs.data()), sizeof(PinConnectionRecord) * hdr.total_pin_connections);

  std::vector<char> pool(hdr.string_pool_bytes);
  f.read(pool.data(), hdr.string_pool_bytes);

  auto str_at = [&](uint32_t offset) -> std::string {
    if (offset >= hdr.string_pool_bytes) return "";
    return std::string(&pool[offset]);
  };

  NetlistDb db;
  db.top_name = str_at(hdr.top_name_offset);
  if (hdr.version >= 2) {
    db.die = {hdr.die_llx, hdr.die_lly, hdr.die_urx, hdr.die_ury, hdr.dbu_per_um};
  }

  db.instances.resize(hdr.instance_count);
  for (uint32_t i = 0; i < hdr.instance_count; ++i) {
    auto& ir = inst_recs[i];
    db.instances[i] = {ir.id, str_at(ir.name_offset), str_at(ir.master_offset), ir.x, ir.y};
  }

  db.nets.resize(hdr.net_count);
  for (uint32_t i = 0; i < hdr.net_count; ++i) {
    auto& nr = net_recs[i];
    auto& ne = db.nets[i];
    ne.name = str_at(nr.name_offset);
    ne.pins.resize(nr.pin_count);
    for (uint16_t j = 0; j < nr.pin_count; ++j) {
      auto& pr = pin_recs[nr.pin_start + j];
      ne.pins[j] = {pr.instance_id, str_at(pr.pin_name_offset), pr.is_driver != 0};
    }
  }

  db.source_flags = hdr.source_flags;

  for (auto& ie : db.instances) {
    db.instance_by_id[ie.id] = &ie;
    db.instance_by_name[ie.name] = &ie;
  }
  for (auto& ne : db.nets) db.net_by_name[ne.name] = &ne;

  return db;
}
