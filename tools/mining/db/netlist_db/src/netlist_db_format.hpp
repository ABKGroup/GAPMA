

#pragma once

#include <cstdint>

constexpr uint32_t NETLIST_DB_MAGIC   = 0x4E455430;
constexpr uint32_t NETLIST_DB_VERSION = 3;

constexpr uint32_t NETLIST_DB_SRC_NETLIST = 1 << 0;
constexpr uint32_t NETLIST_DB_SRC_DEF     = 1 << 1;

struct NetlistDbHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t instance_count;
  uint32_t net_count;
  uint32_t total_pin_connections;
  uint32_t string_pool_bytes;
  uint32_t top_name_offset;

  float    die_llx;
  float    die_lly;
  float    die_urx;
  float    die_ury;
  float    dbu_per_um;

  uint32_t source_flags;
};

struct InstanceRecord {
  int32_t  id;
  uint32_t name_offset;
  uint32_t master_offset;
  float    x;
  float    y;
};

struct NetlistNetRecord {
  uint32_t name_offset;
  uint32_t pin_start;
  uint16_t pin_count;
  uint16_t _pad;
};

struct PinConnectionRecord {
  int32_t  instance_id;
  uint32_t pin_name_offset;
  uint8_t  is_driver;
  uint8_t  _pad[3];
};

static_assert(sizeof(NetlistDbHeader)      == 52, "NetlistDbHeader size mismatch");
static_assert(sizeof(InstanceRecord)      == 20, "InstanceRecord size mismatch");
static_assert(sizeof(NetlistNetRecord)     == 12, "NetlistNetRecord size mismatch");
static_assert(sizeof(PinConnectionRecord) == 12, "PinConnectionRecord size mismatch");
