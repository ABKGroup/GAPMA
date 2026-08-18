
from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Dict, Union

CELL_DB_MAGIC = 0x43454C30

_HEADER_V2V3_FMT = "<6I"
_HEADER_V2V3_SIZE = struct.calcsize(_HEADER_V2V3_FMT)
_HEADER_V4_FMT = "<7I"
_HEADER_V4_SIZE = struct.calcsize(_HEADER_V4_FMT)

CELL_DB_SRC_LIBERTY = 1 << 0
CELL_DB_SRC_LEF     = 1 << 1
CELL_DB_SRC_CDL     = 1 << 2

_MASTER_V1_FMT = "<II f HH I H 2x I H 2x"
_MASTER_V1_SIZE = struct.calcsize(_MASTER_V1_FMT)
_MASTER_V2_FMT = "<II f HH I H 2x I H 2x ff I B 3x"
_MASTER_V2_SIZE = struct.calcsize(_MASTER_V2_FMT)
_MASTER_V3_FMT = "<II f HH I H 2x I H 2x ff I B 1x HH B 1x"
_MASTER_V3_SIZE = struct.calcsize(_MASTER_V3_FMT)

_PIN_FMT = "<I B 3x"
_PIN_SIZE = struct.calcsize(_PIN_FMT)

_FUNC_V2_FMT = "<III"
_FUNC_V2_SIZE = struct.calcsize(_FUNC_V2_FMT)
_FUNC_V3_FMT = "<III hhh 2x ff"
_FUNC_V3_SIZE = struct.calcsize(_FUNC_V3_FMT)

@dataclass
class MasterPinEntry:
    name: str
    direction: int

@dataclass
class MasterFunctionEntry:
    pin_name: str
    expr: str
    canonical: str
    stage_count: int = -1
    max_pu_stack_depth: int = -1
    max_pd_stack_depth: int = -1
    resistance_proxy_max: float = 0.0
    capacitance_proxy_max: float = 0.0

@dataclass
class MasterEntry:
    name: str
    family: str
    area: float
    is_seq: bool
    output_count: int
    pins: List[MasterPinEntry]
    functions: List[MasterFunctionEntry]
    width: float = 0.0
    height: float = 0.0
    site: str = ""
    symmetry: int = 0
    tr_nmos: int = 0
    tr_pmos: int = 0
    shared_diffusion: int = 0

@dataclass
class CellDb:
    masters: List[MasterEntry] = field(default_factory=list)
    master_by_name: Dict[str, MasterEntry] = field(default_factory=dict)
    source_flags: int = 0
    def has_liberty(self) -> bool: return bool(self.source_flags & CELL_DB_SRC_LIBERTY)
    def has_lef(self) -> bool: return bool(self.source_flags & CELL_DB_SRC_LEF)
    def has_cdl(self) -> bool: return bool(self.source_flags & CELL_DB_SRC_CDL)

def read_cell_db(path: Union[str, Path]) -> CellDb:
    with open(str(path), "rb") as f:
        data = f.read()

    off = 0
    magic, version = struct.unpack_from("<II", data, 0)
    if magic != CELL_DB_MAGIC:
        raise ValueError(f"bad magic in cell_db: {path}")
    source_flags = 0
    if version >= 4:
        hdr = struct.unpack_from(_HEADER_V4_FMT, data, off)
        off += _HEADER_V4_SIZE
        _, _, master_count, total_pins, total_funcs, sp_bytes, source_flags = hdr
    else:
        hdr = struct.unpack_from(_HEADER_V2V3_FMT, data, off)
        off += _HEADER_V2V3_SIZE
        _, _, master_count, total_pins, total_funcs, sp_bytes = hdr

    if version >= 3:
        master_fmt, master_size = _MASTER_V3_FMT, _MASTER_V3_SIZE
    elif version >= 2:
        master_fmt, master_size = _MASTER_V2_FMT, _MASTER_V2_SIZE
    else:
        master_fmt, master_size = _MASTER_V1_FMT, _MASTER_V1_SIZE
    master_recs = []
    for _ in range(master_count):
        r = struct.unpack_from(master_fmt, data, off)
        off += master_size
        master_recs.append(r)

    pin_recs = []
    for _ in range(total_pins):
        r = struct.unpack_from(_PIN_FMT, data, off)
        off += _PIN_SIZE
        pin_recs.append(r)

    func_fmt = _FUNC_V3_FMT if version >= 3 else _FUNC_V2_FMT
    func_size = _FUNC_V3_SIZE if version >= 3 else _FUNC_V2_SIZE
    func_recs = []
    for _ in range(total_funcs):
        r = struct.unpack_from(func_fmt, data, off)
        off += func_size
        func_recs.append(r)

    pool = data[off: off + sp_bytes]

    def str_at(offset):
        if offset >= len(pool):
            raise ValueError(f"str_at: offset {offset} out of bounds (pool size {len(pool)}) — cell.cdb may be corrupt")
        try:
            end = pool.index(0, offset)
        except ValueError:
            raise ValueError(f"str_at: no null terminator after offset {offset} (pool size {len(pool)}) — cell.cdb may be corrupt")
        return pool[offset:end].decode("utf-8", errors="strict")

    db = CellDb()
    for r in master_recs:
        if version >= 3:
            name_off, fam_off, area, is_seq, out_count, pin_start, pin_count, func_start, func_count, width, height, site_off, sym, tr_nmos, tr_pmos, shared_diff = r
        elif version >= 2:
            name_off, fam_off, area, is_seq, out_count, pin_start, pin_count, func_start, func_count, width, height, site_off, sym = r
            tr_nmos = tr_pmos = 0; shared_diff = 0
        else:
            name_off, fam_off, area, is_seq, out_count, pin_start, pin_count, func_start, func_count = r
            width = height = 0.0; site_off = 0; sym = 0
            tr_nmos = tr_pmos = 0; shared_diff = 0
        pins = [MasterPinEntry(name=str_at(pin_recs[pin_start + j][0]), direction=pin_recs[pin_start + j][1])
                for j in range(pin_count)]
        funcs = []
        for j in range(func_count):
            fr = func_recs[func_start + j]
            fe = MasterFunctionEntry(pin_name=str_at(fr[0]), expr=str_at(fr[1]), canonical=str_at(fr[2]))
            if version >= 3:
                (fe.stage_count, fe.max_pu_stack_depth, fe.max_pd_stack_depth,
                 fe.resistance_proxy_max, fe.capacitance_proxy_max) = fr[3], fr[4], fr[5], fr[6], fr[7]
            funcs.append(fe)
        me = MasterEntry(name=str_at(name_off), family=str_at(fam_off), area=area,
                         is_seq=is_seq != 0, output_count=out_count, pins=pins, functions=funcs,
                         width=width, height=height, site=str_at(site_off), symmetry=sym,
                         tr_nmos=tr_nmos, tr_pmos=tr_pmos, shared_diffusion=shared_diff)
        db.masters.append(me)

    db.master_by_name = {m.name: m for m in db.masters}
    db.source_flags = source_flags
    return db
