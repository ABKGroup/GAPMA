

#pragma once

#include <cstdint>

constexpr uint32_t CELL_DB_MAGIC   = 0x43454C30;
constexpr uint32_t CELL_DB_VERSION = 4;

constexpr uint32_t CELL_DB_SRC_LIBERTY = 1 << 0;
constexpr uint32_t CELL_DB_SRC_LEF     = 1 << 1;
constexpr uint32_t CELL_DB_SRC_CDL     = 1 << 2;

struct CellDbHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t master_count;
  uint32_t total_pins;
  uint32_t total_functions;
  uint32_t string_pool_bytes;
  uint32_t source_flags;
};

struct MasterRecord {
  uint32_t name_offset;
  uint32_t family_offset;
  float    area;
  uint16_t is_seq;
  uint16_t output_count;
  uint32_t pin_start;
  uint16_t pin_count;
  uint16_t _pad0;
  uint32_t func_start;
  uint16_t func_count;
  uint16_t _pad1;

  float    width;
  float    height;
  uint32_t site_offset;
  uint8_t  symmetry;
  uint8_t  _pad2;

  uint16_t tr_nmos;
  uint16_t tr_pmos;

  uint8_t  shared_diffusion;
  uint8_t  _pad3;
};

struct MasterRecordV2 {
  uint32_t name_offset;
  uint32_t family_offset;
  float    area;
  uint16_t is_seq;
  uint16_t output_count;
  uint32_t pin_start;
  uint16_t pin_count;
  uint16_t _pad0;
  uint32_t func_start;
  uint16_t func_count;
  uint16_t _pad1;
  float    width;
  float    height;
  uint32_t site_offset;
  uint8_t  symmetry;
  uint8_t  _pad2[3];
};

struct MasterPinRecord {
  uint32_t name_offset;
  uint8_t  direction;
  uint8_t  _pad[3];
};

struct MasterFunctionRecord {
  uint32_t pin_name_offset;
  uint32_t expr_offset;
  uint32_t canonical_offset;

  int16_t  stage_count;
  int16_t  max_pu_stack_depth;
  int16_t  max_pd_stack_depth;
  uint8_t  _pad[2];
  float    resistance_proxy_max;
  float    capacitance_proxy_max;
};

struct MasterFunctionRecordV2 {
  uint32_t pin_name_offset;
  uint32_t expr_offset;
  uint32_t canonical_offset;
};

static_assert(sizeof(MasterRecordV2)        == 48, "MasterRecordV2 size mismatch");
static_assert(sizeof(MasterRecord)          == 52, "MasterRecord size mismatch");
static_assert(sizeof(MasterPinRecord)       ==  8, "MasterPinRecord size mismatch");
static_assert(sizeof(MasterFunctionRecordV2)== 12, "MasterFunctionRecordV2 size mismatch");
static_assert(sizeof(MasterFunctionRecord)  == 28, "MasterFunctionRecord size mismatch");
