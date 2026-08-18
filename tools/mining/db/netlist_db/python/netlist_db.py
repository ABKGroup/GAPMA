
from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Dict, Union

NETLIST_DB_MAGIC = 0x4E455430

_HEADER_V1_FMT = "<7I"
_HEADER_V1_SIZE = struct.calcsize(_HEADER_V1_FMT)
_HEADER_V2_FMT = "<7I 5f"
_HEADER_V2_SIZE = struct.calcsize(_HEADER_V2_FMT)
_HEADER_V3_FMT = "<7I 5f I"
_HEADER_V3_SIZE = struct.calcsize(_HEADER_V3_FMT)

NETLIST_DB_SRC_NETLIST = 1 << 0
NETLIST_DB_SRC_DEF     = 1 << 1

_INST_FMT = "<i II ff"
_INST_SIZE = struct.calcsize(_INST_FMT)

_NET_FMT = "<I I H 2x"
_NET_SIZE = struct.calcsize(_NET_FMT)

_PIN_FMT = "<i I B 3x"
_PIN_SIZE = struct.calcsize(_PIN_FMT)

@dataclass
class InstanceEntry:
    id: int
    name: str
    master: str
    x: float
    y: float

@dataclass
class PinConnectionEntry:
    instance_id: int
    pin_name: str
    is_driver: bool

@dataclass
class NetlistNetEntry:
    name: str
    pins: List[PinConnectionEntry]

@dataclass
class DieAreaEntry:
    llx: float = 0.0
    lly: float = 0.0
    urx: float = 0.0
    ury: float = 0.0
    dbu_per_um: float = 0.0

@dataclass
class NetlistDb:
    top_name: str = ""
    die: DieAreaEntry = field(default_factory=DieAreaEntry)
    instances: List[InstanceEntry] = field(default_factory=list)
    nets: List[NetlistNetEntry] = field(default_factory=list)
    instance_by_id: Dict[int, InstanceEntry] = field(default_factory=dict)
    instance_by_name: Dict[str, InstanceEntry] = field(default_factory=dict)
    net_by_name: Dict[str, NetlistNetEntry] = field(default_factory=dict)
    source_flags: int = 0
    def has_netlist(self) -> bool: return bool(self.source_flags & NETLIST_DB_SRC_NETLIST)
    def has_def(self) -> bool: return bool(self.source_flags & NETLIST_DB_SRC_DEF)

def read_netlist_db(path: Union[str, Path]) -> NetlistDb:
    with open(str(path), "rb") as f:
        data = f.read()

    off = 0
    magic, version = struct.unpack_from("<II", data, 0)
    if magic != NETLIST_DB_MAGIC:
        raise ValueError(f"bad magic in netlist_db: {path}")
    die_llx = die_lly = die_urx = die_ury = dbu_per_um = 0.0
    source_flags = 0
    if version >= 3:
        hdr = struct.unpack_from(_HEADER_V3_FMT, data, off)
        off += _HEADER_V3_SIZE
        _, _, inst_count, net_count, total_pins, sp_bytes, top_off, die_llx, die_lly, die_urx, die_ury, dbu_per_um, source_flags = hdr
    elif version >= 2:
        hdr = struct.unpack_from(_HEADER_V2_FMT, data, off)
        off += _HEADER_V2_SIZE
        _, _, inst_count, net_count, total_pins, sp_bytes, top_off, die_llx, die_lly, die_urx, die_ury, dbu_per_um = hdr
    else:
        hdr = struct.unpack_from(_HEADER_V1_FMT, data, off)
        off += _HEADER_V1_SIZE
        _, _, inst_count, net_count, total_pins, sp_bytes, top_off = hdr

    inst_recs = []
    for _ in range(inst_count):
        r = struct.unpack_from(_INST_FMT, data, off)
        off += _INST_SIZE
        inst_recs.append(r)

    net_recs = []
    for _ in range(net_count):
        r = struct.unpack_from(_NET_FMT, data, off)
        off += _NET_SIZE
        net_recs.append(r)

    pin_recs = []
    for _ in range(total_pins):
        r = struct.unpack_from(_PIN_FMT, data, off)
        off += _PIN_SIZE
        pin_recs.append(r)

    pool = data[off: off + sp_bytes]

    def str_at(offset):
        if offset >= len(pool):
            raise ValueError(f"str_at: offset {offset} out of bounds (pool size {len(pool)}) — netlist.cdb may be corrupt")
        try:
            end = pool.index(0, offset)
        except ValueError:
            raise ValueError(f"str_at: no null terminator after offset {offset} (pool size {len(pool)}) — netlist.cdb may be corrupt")
        return pool[offset:end].decode("utf-8", errors="strict")

    db = NetlistDb()
    db.top_name = str_at(top_off)
    db.die = DieAreaEntry(llx=die_llx, lly=die_lly, urx=die_urx, ury=die_ury, dbu_per_um=dbu_per_um)
    db.source_flags = source_flags

    db.instances = [
        InstanceEntry(id=r[0], name=str_at(r[1]), master=str_at(r[2]), x=r[3], y=r[4])
        for r in inst_recs
    ]

    for r in net_recs:
        name_off, pin_start, pin_count = r
        pins = [
            PinConnectionEntry(instance_id=pin_recs[pin_start + j][0],
                                pin_name=str_at(pin_recs[pin_start + j][1]),
                                is_driver=pin_recs[pin_start + j][2] != 0)
            for j in range(pin_count)
        ]
        db.nets.append(NetlistNetEntry(name=str_at(name_off), pins=pins))

    db.instance_by_id = {i.id: i for i in db.instances}
    db.instance_by_name = {i.name: i for i in db.instances}
    db.net_by_name = {n.name: n for n in db.nets}
    return db
