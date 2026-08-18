
from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Tuple, Union

CLUSTER_DB_MAGIC = 0x43444230
CLUSTER_DB_VERSION = 5

_HEADER_FMT = "<18I"
_HEADER_SIZE = struct.calcsize(_HEADER_FMT)

_CLUSTER_FMT = (
    "<i I I H 2x"      # id, canonical_offset, cell_id_start, cell_id_count
    " I H 2x"          # input_start, input_count
    " I H 2x"          # output_start, output_count
    " I"               # truth_table_offset
    " I H 2x"          # input_order_start, input_order_count
    " f H 2x"          # total_area, depth
    " I H 2x"          # internal_net_start, internal_net_count
    " I H 2x"          # boundary_input_pin_start, boundary_input_pin_count
    " I H 2x"          # boundary_output_pin_start, boundary_output_pin_count
    " I H 2x"          # overlap_start, overlap_count
    " I H 2x"          # superset_start, superset_count
    " I H 2x"          # subset_start, subset_count
)
_CLUSTER_SIZE = struct.calcsize(_CLUSTER_FMT)

_LF_FMT = "<I i I I"
_LF_SIZE = struct.calcsize(_LF_FMT)

_BPIN_FMT = "<i I I"
_BPIN_SIZE = struct.calcsize(_BPIN_FMT)

# cluster_db_format.hpp pins these with static_assert, so a mismatch here means
# the C++ layout moved and every offset below would be read at the wrong stride.
assert _HEADER_SIZE == 72, _HEADER_SIZE
assert _CLUSTER_SIZE == 100, _CLUSTER_SIZE
assert _LF_SIZE == 16, _LF_SIZE
assert _BPIN_SIZE == 12, _BPIN_SIZE


@dataclass
class BoundaryPinEntry:
    cell_id: int
    pin_name: str
    net_name: str


@dataclass
class ClusterEntry:
    id: int
    canonical: str
    cell_ids: List[int] = field(default_factory=list)
    inputs: List[str] = field(default_factory=list)
    outputs: List[str] = field(default_factory=list)
    truth_table: str = ""
    input_order: List[str] = field(default_factory=list)
    total_area: float = 0.0
    depth: int = 0
    internal_nets: List[str] = field(default_factory=list)
    boundary_input_pins: List[BoundaryPinEntry] = field(default_factory=list)
    boundary_output_pins: List[BoundaryPinEntry] = field(default_factory=list)
    overlap_cluster_ids: List[int] = field(default_factory=list)
    superset_cluster_ids: List[int] = field(default_factory=list)
    subset_cluster_ids: List[int] = field(default_factory=list)


@dataclass
class LogicFunctionEntry:
    canonical: str
    count: int
    expr: str
    library_match: str


@dataclass
class ClusterDB:
    clusters: List[ClusterEntry] = field(default_factory=list)
    logic_functions: List[LogicFunctionEntry] = field(default_factory=list)
    overlap_groups: List[List[int]] = field(default_factory=list)
    subset_pairs: List[Tuple[int, int]] = field(default_factory=list)
    # Callers index clusters by id the way netlist_db exposes instance_by_id and
    # cell_db exposes master_by_name.
    cluster_by_id: Dict[int, ClusterEntry] = field(default_factory=dict)


def _take(buf: bytes, pos: int, n: int, what: str, path) -> Tuple[bytes, int]:
    end = pos + n
    if end > len(buf):
        raise ValueError(
            f"{path}: truncated while reading {what}: need {n} bytes at offset {pos}, "
            f"file is {len(buf)} bytes"
        )
    return buf[pos:end], end


def read_cluster_db(path: Union[str, Path]) -> ClusterDB:
    path = Path(path)
    buf = path.read_bytes()

    hdr, pos = _take(buf, 0, _HEADER_SIZE, "header", path)
    (magic, version, cluster_count, logic_function_count, overlap_group_count,
     overlap_entry_count, subset_pair_count, total_cell_ids, total_input_offsets,
     total_output_offsets, total_input_order_offsets, total_internal_net_offsets,
     total_boundary_input_pins, total_boundary_output_pins, total_overlap_cluster_ids,
     total_superset_cluster_ids, total_subset_cluster_ids,
     string_pool_bytes) = struct.unpack(_HEADER_FMT, hdr)

    if magic != CLUSTER_DB_MAGIC:
        raise ValueError(f"{path}: bad magic 0x{magic:08X} (expected 0x{CLUSTER_DB_MAGIC:08X})")
    if version != CLUSTER_DB_VERSION:
        raise ValueError(f"{path}: unsupported cluster_db version {version} (expected {CLUSTER_DB_VERSION})")

    raw, pos = _take(buf, pos, _CLUSTER_SIZE * cluster_count, "cluster records", path)
    cluster_recs = [struct.unpack_from(_CLUSTER_FMT, raw, i * _CLUSTER_SIZE)
                    for i in range(cluster_count)]

    raw, pos = _take(buf, pos, _LF_SIZE * logic_function_count, "logic function records", path)
    lf_recs = [struct.unpack_from(_LF_FMT, raw, i * _LF_SIZE)
               for i in range(logic_function_count)]

    raw, pos = _take(buf, pos, 4 * overlap_entry_count, "overlap entries", path)
    overlap_entries = list(struct.unpack(f"<{overlap_entry_count}i", raw))

    raw, pos = _take(buf, pos, 8 * subset_pair_count, "subset pairs", path)
    flat = struct.unpack(f"<{subset_pair_count * 2}i", raw)
    subset_pairs = [(flat[i * 2], flat[i * 2 + 1]) for i in range(subset_pair_count)]

    def i32(n, what):
        nonlocal pos
        raw, pos = _take(buf, pos, 4 * n, what, path)
        return struct.unpack(f"<{n}i", raw)

    def u32(n, what):
        nonlocal pos
        raw, pos = _take(buf, pos, 4 * n, what, path)
        return struct.unpack(f"<{n}I", raw)

    def bpins(n, what):
        nonlocal pos
        raw, pos = _take(buf, pos, _BPIN_SIZE * n, what, path)
        return [struct.unpack_from(_BPIN_FMT, raw, i * _BPIN_SIZE) for i in range(n)]

    packed_cell_ids = i32(total_cell_ids, "cell ids")
    packed_input_offsets = u32(total_input_offsets, "input offsets")
    packed_output_offsets = u32(total_output_offsets, "output offsets")
    packed_input_order_offsets = u32(total_input_order_offsets, "input order offsets")
    packed_internal_net_offsets = u32(total_internal_net_offsets, "internal net offsets")
    packed_bip = bpins(total_boundary_input_pins, "boundary input pins")
    packed_bop = bpins(total_boundary_output_pins, "boundary output pins")
    packed_ov = i32(total_overlap_cluster_ids, "overlap cluster ids")
    packed_sup = i32(total_superset_cluster_ids, "superset cluster ids")
    packed_sub = i32(total_subset_cluster_ids, "subset cluster ids")

    pool, pos = _take(buf, pos, string_pool_bytes, "string pool", path)

    def str_at(offset: int) -> str:
        if offset >= string_pool_bytes:
            return ""
        end = pool.find(b"\0", offset)
        if end < 0:
            end = string_pool_bytes
        return pool[offset:end].decode("utf-8", errors="replace")

    db = ClusterDB()
    for r in cluster_recs:
        (cid, canonical_offset, cell_id_start, cell_id_count,
         input_start, input_count, output_start, output_count,
         truth_table_offset, input_order_start, input_order_count,
         total_area, depth, internal_net_start, internal_net_count,
         bip_start, bip_count, bop_start, bop_count,
         overlap_start, overlap_count, superset_start, superset_count,
         subset_start, subset_count) = r

        db.clusters.append(ClusterEntry(
            id=cid,
            canonical=str_at(canonical_offset),
            cell_ids=list(packed_cell_ids[cell_id_start:cell_id_start + cell_id_count]),
            inputs=[str_at(packed_input_offsets[input_start + j]) for j in range(input_count)],
            outputs=[str_at(packed_output_offsets[output_start + j]) for j in range(output_count)],
            truth_table=str_at(truth_table_offset),
            input_order=[str_at(packed_input_order_offsets[input_order_start + j])
                         for j in range(input_order_count)],
            total_area=total_area,
            depth=depth,
            internal_nets=[str_at(packed_internal_net_offsets[internal_net_start + j])
                           for j in range(internal_net_count)],
            boundary_input_pins=[
                BoundaryPinEntry(packed_bip[bip_start + j][0],
                                 str_at(packed_bip[bip_start + j][1]),
                                 str_at(packed_bip[bip_start + j][2]))
                for j in range(bip_count)],
            boundary_output_pins=[
                BoundaryPinEntry(packed_bop[bop_start + j][0],
                                 str_at(packed_bop[bop_start + j][1]),
                                 str_at(packed_bop[bop_start + j][2]))
                for j in range(bop_count)],
            overlap_cluster_ids=list(packed_ov[overlap_start:overlap_start + overlap_count]),
            superset_cluster_ids=list(packed_sup[superset_start:superset_start + superset_count]),
            subset_cluster_ids=list(packed_sub[subset_start:subset_start + subset_count]),
        ))

    db.logic_functions = [
        LogicFunctionEntry(str_at(c_off), count, str_at(e_off), str_at(m_off))
        for c_off, count, e_off, m_off in lf_recs
    ]

    group: List[int] = []
    for v in overlap_entries:
        if v == -1:
            if group:
                db.overlap_groups.append(group)
            group = []
        else:
            group.append(v)
    if group:
        db.overlap_groups.append(group)

    db.subset_pairs = subset_pairs
    db.cluster_by_id = {c.id: c for c in db.clusters}
    return db
