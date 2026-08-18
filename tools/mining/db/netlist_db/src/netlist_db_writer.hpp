#pragma once

#include "netlist_db_format.hpp"

#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace netlist_db_writer {

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

struct WInstance {
  int id = 0;
  std::string name, master;
  float x = std::numeric_limits<float>::quiet_NaN();
  float y = std::numeric_limits<float>::quiet_NaN();
};

struct WPinConnection {
  int instance_id = 0;
  std::string pin_name;
  bool is_driver = false;
};

struct WNet {
  std::string name;
  std::vector<WPinConnection> pins;
};

template<typename T>
inline void write_vec(std::ofstream& f, const std::vector<T>& v) {
  if (!v.empty()) f.write(reinterpret_cast<const char*>(v.data()), sizeof(T) * v.size());
}

struct WDieArea {
  float llx = 0, lly = 0, urx = 0, ury = 0;
  float dbu_per_um = 0;
};

inline bool write_db(const std::string& out_path,
                     const std::string& top_name,
                     const std::vector<WInstance>& instances,
                     const std::vector<WNet>& nets,
                     const WDieArea& die = {},
                     uint32_t source_flags = NETLIST_DB_SRC_NETLIST) {
  StringPool pool;
  pool.add("");

  uint32_t top_off = pool.add(top_name);

  std::vector<InstanceRecord> inst_recs;
  inst_recs.reserve(instances.size());
  for (const auto& inst : instances) {
    InstanceRecord r{};
    r.id = inst.id;
    r.name_offset = pool.add(inst.name);
    r.master_offset = pool.add(inst.master);
    r.x = inst.x;
    r.y = inst.y;
    inst_recs.push_back(r);
  }

  std::vector<NetlistNetRecord> net_recs;
  net_recs.reserve(nets.size());
  std::vector<PinConnectionRecord> pin_recs;
  for (const auto& net : nets) {
    NetlistNetRecord r{};
    r.name_offset = pool.add(net.name);
    r.pin_start = static_cast<uint32_t>(pin_recs.size());

    if (net.pins.size() > static_cast<size_t>(std::numeric_limits<uint16_t>::max())) {
      throw std::runtime_error("net '" + net.name + "' has " + std::to_string(net.pins.size()) +
                               " pin connections, exceeding the netlist_db pin_count limit of " +
                               std::to_string(std::numeric_limits<uint16_t>::max()));
    }
    r.pin_count = static_cast<uint16_t>(net.pins.size());
    for (const auto& pc : net.pins) {
      PinConnectionRecord pr{};
      pr.instance_id = pc.instance_id;
      pr.pin_name_offset = pool.add(pc.pin_name);
      pr.is_driver = pc.is_driver ? 1 : 0;
      pin_recs.push_back(pr);
    }
    net_recs.push_back(r);
  }

  NetlistDbHeader hdr{};
  hdr.magic = NETLIST_DB_MAGIC;
  hdr.version = NETLIST_DB_VERSION;
  hdr.instance_count = static_cast<uint32_t>(inst_recs.size());
  hdr.net_count = static_cast<uint32_t>(net_recs.size());
  hdr.total_pin_connections = static_cast<uint32_t>(pin_recs.size());
  hdr.string_pool_bytes = static_cast<uint32_t>(pool.data.size());
  hdr.top_name_offset = top_off;
  hdr.die_llx = die.llx;
  hdr.die_lly = die.lly;
  hdr.die_urx = die.urx;
  hdr.die_ury = die.ury;
  hdr.dbu_per_um = die.dbu_per_um;
  hdr.source_flags = source_flags;

  std::ofstream f(out_path, std::ios::binary);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  write_vec(f, inst_recs);
  write_vec(f, net_recs);
  write_vec(f, pin_recs);
  f.write(pool.data.data(), pool.data.size());
  return true;
}

}
