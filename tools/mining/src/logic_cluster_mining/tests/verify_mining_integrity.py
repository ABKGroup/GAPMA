#!/usr/bin/env python3

from __future__ import annotations
import re
import sys
import struct
from collections import defaultdict
from dataclasses import dataclass, field
from itertools import combinations, permutations
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

_TOOLS = Path(__file__).resolve().parents[4]
_MINING = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(_MINING / "db" / "cluster_db" / "python"))
sys.path.insert(0, str(_MINING / "db" / "netlist_db" / "python"))
sys.path.insert(0, str(_MINING / "db"))
sys.path.insert(0, str(_MINING / "db" / "netlist_db" / "python"))
sys.path.insert(0, str(_MINING / "db" / "cell_db" / "python"))
sys.path.insert(0, str(_TOOLS))
from cluster_db import read_cluster_db, ClusterDB, ClusterEntry
from netlist_db import read_netlist_db, NetlistDb
from cell_db import read_cell_db, CellDb
from utils.runtime_logger import RuntimeLogger

class BoolExprEval:

    def __init__(self, expr: str):
        self._s = expr
        self._i = 0
        self._tok = ""
        self._tt = ""

    def evaluate(self, lookup):
        self._lookup = lookup
        self._i = 0
        self._next()
        return self._parse_or()

    def _next(self):
        while self._i < len(self._s) and self._s[self._i].isspace():
            self._i += 1
        if self._i >= len(self._s):
            self._tt = "END"; self._tok = ""; return
        c = self._s[self._i]
        if c == '!':
            self._i += 1; self._tt = "NOT"; self._tok = "!"; return
        if c in '&*':
            self._i += 1
            if c == '&' and self._i < len(self._s) and self._s[self._i] == '&':
                self._i += 1
            self._tt = "AND"; self._tok = "&"; return
        if c in '|+':
            self._i += 1
            if c == '|' and self._i < len(self._s) and self._s[self._i] == '|':
                self._i += 1
            self._tt = "OR"; self._tok = "|"; return
        if c == '^':
            self._i += 1; self._tt = "XOR"; self._tok = "^"; return
        if c == '(':
            self._i += 1; self._tt = "LP"; self._tok = "("; return
        if c == ')':
            self._i += 1; self._tt = "RP"; self._tok = ")"; return
        if c.isalnum() or c in '_/$':
            b = self._i
            while self._i < len(self._s) and (self._s[self._i].isalnum() or self._s[self._i] in '_/$'):
                self._i += 1
            self._tok = self._s[b:self._i]
            if self._i < len(self._s) and self._s[self._i] == "'":
                self._i += 1; self._tok = "!" + self._tok
            self._tt = "ID"
            return
        self._i += 1; self._next()

    def _parse_or(self):
        v = self._parse_xor()
        while self._tt == "OR":
            self._next(); r = self._parse_xor(); v = v | r
        return v

    def _parse_xor(self):
        v = self._parse_and()
        while self._tt == "XOR":
            self._next(); r = self._parse_and(); v = (v != r)
        return v

    def _parse_and(self):
        v = self._parse_unary()
        while self._tt == "AND":
            self._next(); r = self._parse_unary(); v = v & r
        return v

    def _parse_unary(self):
        if self._tt == "NOT":
            self._next(); return not self._parse_unary()
        if self._tt == "LP":
            self._next(); v = self._parse_or()
            if self._tt == "RP": self._next()
            return v
        if self._tt == "ID":
            tok = self._tok; self._next()
            if tok.startswith("!"):
                return not self._lookup(tok[1:])
            u = tok.upper()
            if u in ("1", "TRUE"): return True
            if u in ("0", "FALSE"): return False
            return self._lookup(tok)
        return False

def canonical_bits_cpp(bits_str: str, n: int) -> str:
    if n <= 1:
        return bits_str
    rows = 1 << n
    best = None
    for perm in permutations(range(n)):
        remap = [0] * rows
        for idx in range(rows):
            bi = 0
            for i in range(n):
                bi |= ((idx >> (n - 1 - i)) & 1) << (n - 1 - perm[i])
            remap[idx] = bi
        candidate = ''.join(bits_str[remap[idx]] for idx in range(rows))
        if best is None or candidate < best:
            best = candidate
    return best

@dataclass
class CircuitPin:
    instance_id: int
    pin_name: str
    net_name: str
    is_driver: bool

@dataclass
class CircuitNet:
    name: str
    driver: Optional[CircuitPin] = None
    loads: List[CircuitPin] = field(default_factory=list)

@dataclass
class CircuitInstance:
    id: int
    name: str
    master_name: str
    pins: Dict[str, CircuitPin] = field(default_factory=dict)

def _is_buffer_master(master) -> bool:
    input_pins = [p for p in master.pins if p.direction == 0]
    if len(input_pins) != 1 or len(master.functions) != 1:
        return False
    in_name = input_pins[0].name.strip()
    expr = master.functions[0].expr.strip()
    return expr == in_name

def _is_inv_master(master) -> bool:
    input_pins = [p for p in master.pins if p.direction == 0]
    if len(input_pins) != 1 or len(master.functions) != 1:
        return False
    in_name = input_pins[0].name.strip().replace(" ", "")
    expr = master.functions[0].expr.strip().replace(" ", "")
    return expr == f"!{in_name}" or expr == f"{in_name}'"

def build_circuit(ldb: NetlistDb, cdb_cell: CellDb):
    instances: Dict[int, CircuitInstance] = {}
    nets: Dict[str, CircuitNet] = {}

    for inst in ldb.instances:
        instances[inst.id] = CircuitInstance(
            id=inst.id, name=inst.name, master_name=inst.master
        )

    for net_entry in ldb.nets:
        net = CircuitNet(name=net_entry.name)
        for pin in net_entry.pins:
            raw_pin_name = pin.pin_name
            short_pin = raw_pin_name.rsplit("/", 1)[-1] if "/" in raw_pin_name else raw_pin_name
            cp = CircuitPin(
                instance_id=pin.instance_id,
                pin_name=short_pin,
                net_name=net_entry.name,
                is_driver=pin.is_driver,
            )
            if pin.is_driver:
                net.driver = cp
            else:
                net.loads.append(cp)
            if pin.instance_id in instances:
                instances[pin.instance_id].pins[short_pin] = cp
        nets[net_entry.name] = net

    return instances, nets

def _resolve_through_buffers(
    net_name: str,
    nets: Dict[str, CircuitNet],
    instances: Dict[int, CircuitInstance],
    cell_db: CellDb,
    is_buffer_cache: Dict[str, bool],
    buf_in_pin_cache: Dict[str, str],
) -> Optional[CircuitNet]:
    seen: Set[str] = set()
    cur = net_name
    while cur not in seen:
        seen.add(cur)
        net = nets.get(cur)
        if not net or not net.driver:
            return net
        inst = instances.get(net.driver.instance_id)
        if not inst:
            return net
        is_buf = is_buffer_cache.get(inst.master_name)
        if is_buf is None:
            master = cell_db.master_by_name.get(inst.master_name)
            is_buf = bool(master) and _is_buffer_master(master)
            is_buffer_cache[inst.master_name] = is_buf
            if is_buf:
                in_pins = [p for p in master.pins if p.direction == 0]
                buf_in_pin_cache[inst.master_name] = in_pins[0].name
        if not is_buf:
            return net
        cp = inst.pins.get(buf_in_pin_cache[inst.master_name])
        if not cp:
            return net
        cur = cp.net_name
    return nets.get(cur)

def simulate_cluster_tt(
    cluster: ClusterEntry,
    instances: Dict[int, CircuitInstance],
    nets: Dict[str, CircuitNet],
    cell_db: CellDb,
) -> Optional[str]:
    cell_id_set = set(cluster.cell_ids)
    is_buffer_cache: Dict[str, bool] = {}
    buf_in_pin_cache: Dict[str, str] = {}

    cell_funcs: Dict[int, Dict[str, str]] = {}
    cell_in_terms: Dict[int, Set[str]] = {}
    for cid in cluster.cell_ids:
        inst = instances.get(cid)
        if not inst:
            return None
        master = cell_db.master_by_name.get(inst.master_name)
        if not master:
            return None
        funcs = {}
        for f in master.functions:
            funcs[f.pin_name] = f.expr
        cell_funcs[cid] = funcs
        cell_in_terms[cid] = {p.name for p in master.pins if p.direction == 0}

    boundary_input_keys = []
    seen_keys = set()
    for cid in cluster.cell_ids:
        inst = instances.get(cid)
        if not inst:
            continue
        for pin_name in sorted(cell_in_terms.get(cid, set())):
            cp = inst.pins.get(pin_name)
            if not cp:
                continue
            net = _resolve_through_buffers(cp.net_name, nets, instances, cell_db,
                                            is_buffer_cache, buf_in_pin_cache)
            if not net or not net.driver:
                key = (net.name if net and net.name else cp.net_name) or f"{inst.name}/{pin_name}"
                if key not in seen_keys:
                    seen_keys.add(key)
                    boundary_input_keys.append(key)
                continue
            driver_id = net.driver.instance_id
            if driver_id not in cell_id_set:
                key = net.name
                if key not in seen_keys:
                    seen_keys.add(key)
                    boundary_input_keys.append(key)

    root_output_net = cluster.outputs[0] if cluster.outputs else None
    if not root_output_net:
        return None

    root_id = None
    root_out_pin = None
    for cid in cluster.cell_ids:
        inst = instances.get(cid)
        if not inst:
            continue
        for pin_name, cp in inst.pins.items():
            if cp.is_driver and cp.net_name == root_output_net:
                root_id = cid
                root_out_pin = pin_name
                break
        if root_id is not None:
            break

    if root_id is None:
        return None

    n = len(boundary_input_keys)
    if n == 0 or n > 16:
        return None

    rows = 1 << n
    tt_bits = []
    cycle_found = False

    for a in range(rows):
        net_vals: Dict[str, bool] = {}
        for i in range(n):
            net_vals[boundary_input_keys[i]] = bool((a >> (n - 1 - i)) & 1)

        memo: Dict[Tuple[int, str], bool] = {}
        active: Set[int] = set()

        def eval_cell_output(inst_id: int, out_pin: str) -> bool:
            nonlocal cycle_found
            key = (inst_id, out_pin)
            if key in memo:
                return memo[key]
            if inst_id in active:
                cycle_found = True
                memo[key] = False
                return False

            inst = instances.get(inst_id)
            if not inst:
                memo[key] = False
                return False

            expr = cell_funcs.get(inst_id, {}).get(out_pin)
            if not expr:
                memo[key] = False
                return False

            active.add(inst_id)

            def lookup(tok: str) -> bool:
                cp = inst.pins.get(tok)
                if not cp:
                    return False
                net = _resolve_through_buffers(cp.net_name, nets, instances, cell_db,
                                                is_buffer_cache, buf_in_pin_cache)
                if not net:
                    return net_vals.get(cp.net_name, False)

                if net.name in net_vals:
                    return net_vals[net.name]

                if net.driver:
                    drv_id = net.driver.instance_id
                    if drv_id in cell_id_set:
                        return eval_cell_output(drv_id, net.driver.pin_name)

                return net_vals.get(net.name, False)

            ev = BoolExprEval(expr)
            val = ev.evaluate(lookup)
            active.discard(inst_id)
            memo[key] = val
            return val

        bit = eval_cell_output(root_id, root_out_pin)
        tt_bits.append('1' if bit else '0')

    if cycle_found:
        return None
    return ''.join(tt_bits)

def test_A_truth_table(cdb: ClusterDB, instances, net_map, cell_db, limit=None):
    print("\n" + "=" * 70)
    print("  [A] Truth Table Correctness — Independent Circuit Simulation")
    print("=" * 70)

    tested = 0
    matched = 0
    mismatched = []
    skipped = 0
    input_mismatch = 0

    clusters = cdb.clusters if limit is None else cdb.clusters[:limit]
    for c in clusters:
        sim_tt = simulate_cluster_tt(c, instances, net_map, cell_db)
        if sim_tt is None:
            skipped += 1
            continue
        tested += 1
        if len(sim_tt) != len(c.truth_table):
            input_mismatch += 1
            if len(mismatched) < 5:
                mismatched.append((c.id, c.truth_table, sim_tt, "length_mismatch",
                                   len(c.inputs), len(c.truth_table), len(sim_tt)))
            continue
        if sim_tt == c.truth_table:
            matched += 1
        else:
            n_in = len(c.inputs)
            stored_canon = canonical_bits_cpp(c.truth_table, n_in)
            sim_canon = canonical_bits_cpp(sim_tt, n_in)
            if stored_canon == sim_canon:
                matched += 1
            else:
                if len(mismatched) < 10:
                    mismatched.append((c.id, c.truth_table, sim_tt, "value_mismatch",
                                       n_in, stored_canon, sim_canon))

    fail_count = tested - matched - input_mismatch
    status = "PASS" if fail_count == 0 and input_mismatch == 0 else "FAIL"
    if tested == 0 and len(clusters) > 0:
        status = "FAIL"
        print(f"  FAIL: all {skipped}/{len(clusters)} clusters were skipped — CDB may be corrupt or master lookup broken")
    print(f"  Total clusters: {len(clusters)}")
    print(f"  Simulated: {tested}, Skipped: {skipped}")
    print(f"  Exact or perm-equiv match: {matched}/{tested}")
    print(f"  Input count mismatch: {input_mismatch}")
    print(f"  Value mismatch: {fail_count}")
    print(f"  Status: {status}")
    for cid, stored, sim, reason, *extra in mismatched[:5]:
        print(f"    cluster {cid}: {reason} stored={stored} sim={sim} extra={extra}")
    return status, tested, matched, fail_count, input_mismatch

def test_B_graph_construction(ldb: NetlistDb, graph_snapshot_path: str):
    print("\n" + "=" * 70)
    print("  [B] Graph Construction — graph_snapshot.out vs netlist.cdb")
    print("=" * 70)

    p = Path(graph_snapshot_path)
    if not p.exists():
        print(f"  SKIP: {graph_snapshot_path} not found")
        print("  (Run mining with --verification_mode to generate)")
        return "SKIP", 0, 0, 0, 0

    snap_cells = {}
    snap_nets = {}

    with open(p) as f:
        section = None
        for line in f:
            line = line.strip()
            if line.startswith("=== CELLS"):
                section = "cells"; continue
            if line.startswith("=== NETS"):
                section = "nets"; continue
            if line.startswith("==="):
                section = None; continue

            if section == "cells" and line:
                m = re.match(r'id=(\d+)\s+name=(\S+)\s+master=(\S+)', line)
                if m:
                    snap_cells[m.group(2)] = m.group(3)

            if section == "nets" and line:
                m = re.match(r'net=(\S+)\s+driver=(\S+)\s+loads=(.*)', line)
                if m:
                    net_name = m.group(1)
                    driver = m.group(2)
                    loads_str = m.group(3).strip()
                    loads = [x.strip() for x in loads_str.split(",") if x.strip()]
                    snap_nets[net_name] = {"driver": driver, "loads": loads}

    layout_cells = {inst.name: inst.master for inst in ldb.instances}
    layout_nets = {}
    for net in ldb.nets:
        driver = None
        loads = []
        for pin in net.pins:
            inst = ldb.instance_by_id.get(pin.instance_id)
            if not inst:
                continue
            label = f"{inst.name}/{pin.pin_name}"
            if pin.is_driver:
                driver = label
            else:
                loads.append(label)
        layout_nets[net.name] = {"driver": driver, "loads": sorted(loads)}

    cell_match = 0
    cell_mismatch = []
    for name, master in snap_cells.items():
        if name in layout_cells:
            if layout_cells[name] == master:
                cell_match += 1
            else:
                cell_mismatch.append((name, master, layout_cells[name]))

    net_match = 0
    net_mismatch = 0
    common_nets = set(snap_nets.keys()) & set(layout_nets.keys())
    for net_name in common_nets:
        sn = snap_nets[net_name]
        ln = layout_nets[net_name]
        if sn["driver"] == ln["driver"] and sorted(sn["loads"]) == sorted(ln["loads"]):
            net_match += 1
        else:
            net_mismatch += 1

    total_snap_cells = len(snap_cells)
    total_snap_nets = len(snap_nets)
    status = "PASS" if len(cell_mismatch) == 0 and net_mismatch == 0 else "FAIL"
    if total_snap_cells == 0:
        status = "SKIP"
    print(f"  Graph snapshot cells: {total_snap_cells}, Layout cells: {len(layout_cells)}")
    print(f"  Cell master match: {cell_match}/{total_snap_cells}")
    print(f"  Cell master mismatch: {len(cell_mismatch)}")
    print(f"  Common nets: {len(common_nets)}, Net match: {net_match}, Net mismatch: {net_mismatch}")
    print(f"  Status: {status}")
    for name, snap_m, layout_m in cell_mismatch[:5]:
        print(f"    cell '{name}': snap={snap_m} layout={layout_m}")
    return status, total_snap_cells, cell_match, len(cell_mismatch), net_mismatch

def test_E_completeness(cdb: ClusterDB, instances, net_map, cell_db, max_cells=3, max_inputs=4):
    print("\n" + "=" * 70)
    print(f"  [E] Completeness — Brute-force Enumeration (max_cells={max_cells})")
    print("=" * 70)

    comb_ids = []
    for inst in instances.values():
        master = cell_db.master_by_name.get(inst.master_name)
        if master and not master.is_seq and master.functions and not _is_buffer_master(master):
            comb_ids.append(inst.id)

    adj_fwd: Dict[int, Set[int]] = defaultdict(set)
    adj_rev: Dict[int, Set[int]] = defaultdict(set)
    comb_set = set(comb_ids)
    for net in net_map.values():
        if not net.driver or net.driver.instance_id not in comb_set:
            continue
        drv_id = net.driver.instance_id
        for load in net.loads:
            if load.instance_id in comb_set:
                adj_fwd[drv_id].add(load.instance_id)
                adj_rev[load.instance_id].add(drv_id)

    mined_cell_sets = set()
    for c in cdb.clusters:
        key = frozenset(c.cell_ids)
        mined_cell_sets.add(key)

    brute_count = 0
    found_in_mining = 0
    not_found = []

    for root_id in comb_ids:
        def enumerate_fanin_subsets(current_set, frontier):
            nonlocal brute_count, found_in_mining
            if len(current_set) > max_cells:
                return

            if len(current_set) >= 2:
                valid = True
                n_inputs_set = set()
                for cid in current_set:
                    if cid == root_id:
                        continue
                    inst = instances.get(cid)
                    if not inst:
                        valid = False; break
                    for pname, cp in inst.pins.items():
                        if cp.is_driver:
                            net = net_map.get(cp.net_name)
                            if net:
                                for load in net.loads:
                                    if load.instance_id not in current_set:
                                        valid = False; break
                            if not valid:
                                break
                    if not valid:
                        break

                if valid:
                    for cid in current_set:
                        inst = instances.get(cid)
                        if not inst:
                            continue
                        master = cell_db.master_by_name.get(inst.master_name)
                        if not master:
                            continue
                        for p in master.pins:
                            if p.direction == 0:
                                cp = inst.pins.get(p.name)
                                if cp:
                                    net = net_map.get(cp.net_name)
                                    if net and net.driver:
                                        if net.driver.instance_id not in current_set:
                                            n_inputs_set.add(net.name)
                                    else:
                                        n_inputs_set.add(cp.net_name)

                    n_in = len(n_inputs_set)
                    if 2 <= n_in <= max_inputs:
                        key = frozenset(current_set)
                        brute_count += 1
                        if key in mined_cell_sets:
                            found_in_mining += 1
                        else:
                            if len(not_found) < 10:
                                not_found.append((root_id, sorted(current_set), n_in))

            for cid in list(frontier):
                for pred_id in adj_rev.get(cid, set()):
                    if pred_id not in current_set and pred_id in comb_set:
                        new_set = current_set | {pred_id}
                        new_frontier = frontier | {pred_id}
                        enumerate_fanin_subsets(new_set, new_frontier)

        enumerate_fanin_subsets({root_id}, {root_id})

    missing = brute_count - found_in_mining
    status = "PASS" if missing == 0 else "FAIL"
    print(f"  Combinational cells: {len(comb_ids)}")
    print(f"  Brute-force valid clusters (size 2-{max_cells}, inputs 2-{max_inputs}): {brute_count}")
    print(f"  Found in mining results: {found_in_mining}/{brute_count}")
    print(f"  Missing from mining: {missing}")
    print(f"  Status: {status}")
    for root, cells, n_in in not_found[:5]:
        root_name = instances[root].name if root in instances else str(root)
        cell_names = [instances[c].name if c in instances else str(c) for c in cells]
        print(f"    root={root_name} cells={cell_names} n_in={n_in}")
    return status, brute_count, found_in_mining, missing

def test_F_overlap_groups(cdb: ClusterDB):
    print("\n" + "=" * 70)
    print("  [F] Overlap Group Completeness")
    print("=" * 70)

    cell_to_clusters: Dict[int, Set[int]] = defaultdict(set)
    for c in cdb.clusters:
        for cid in c.cell_ids:
            cell_to_clusters[cid].add(c.id)

    gt_overlap_pairs: Set[Tuple[int, int]] = set()
    for cid, cluster_ids in cell_to_clusters.items():
        ids = sorted(cluster_ids)
        for i in range(len(ids)):
            for j in range(i + 1, len(ids)):
                gt_overlap_pairs.add((ids[i], ids[j]))

    cdb_group_pairs: Set[Tuple[int, int]] = set()
    for group in cdb.overlap_groups:
        for i in range(len(group)):
            for j in range(i + 1, len(group)):
                a, b = min(group[i], group[j]), max(group[i], group[j])
                cdb_group_pairs.add((a, b))

    missing = gt_overlap_pairs - cdb_group_pairs
    cluster_cells_for_F = {c.id: set(c.cell_ids) for c in cdb.clusters}
    false_group = 0
    false_group_examples = []
    for group in cdb.overlap_groups:
        if len(group) <= 1:
            continue
        adj: Dict[int, Set[int]] = defaultdict(set)
        for i in range(len(group)):
            for j in range(i + 1, len(group)):
                a, b = group[i], group[j]
                if cluster_cells_for_F.get(a, set()) & cluster_cells_for_F.get(b, set()):
                    adj[a].add(b)
                    adj[b].add(a)
        visited: Set[int] = {group[0]}
        queue = [group[0]]
        while queue:
            curr = queue.pop()
            for neighbor in adj[curr]:
                if neighbor not in visited:
                    visited.add(neighbor)
                    queue.append(neighbor)
        disconnected = set(group) - visited
        false_group += len(disconnected)
        if disconnected and len(false_group_examples) < 3:
            false_group_examples.append((group[0], sorted(disconnected)))

    status = "PASS" if len(missing) == 0 and false_group == 0 else "FAIL"
    print(f"  Ground truth direct overlap pairs: {len(gt_overlap_pairs)}")
    print(f"  CDB overlap group pairs (transitive): {len(cdb_group_pairs)}")
    print(f"  Direct overlap pairs missing from groups: {len(missing)}")
    print(f"  Disconnected members in groups (not transitively reachable): {false_group}")
    for root, disconnected in false_group_examples:
        print(f"    group root={root}: disconnected={disconnected}")
    print(f"  Status: {status}")
    if missing:
        for a, b in list(missing)[:5]:
            shared = set(cdb.cluster_by_id[a].cell_ids) & set(cdb.cluster_by_id[b].cell_ids)
            print(f"    missing pair ({a}, {b}): shared cells = {shared}")
    return status, len(gt_overlap_pairs), len(missing), 0

def test_G_subset_pairs(cdb: ClusterDB):
    print("\n" + "=" * 70)
    print("  [G] Subset Pair Completeness")
    print("=" * 70)

    gt_subset_pairs: Set[Tuple[int, int]] = set()
    cluster_cells = {c.id: set(c.cell_ids) for c in cdb.clusters}

    cell_to_clusters: Dict[int, Set[int]] = defaultdict(set)
    for c in cdb.clusters:
        for cid in c.cell_ids:
            cell_to_clusters[cid].add(c.id)

    checked_pairs: Set[Tuple[int, int]] = set()
    for cid, cluster_ids in cell_to_clusters.items():
        ids = sorted(cluster_ids)
        for i in range(len(ids)):
            for j in range(i + 1, len(ids)):
                pair = (ids[i], ids[j])
                if pair in checked_pairs:
                    continue
                checked_pairs.add(pair)
                a_cells = cluster_cells[ids[i]]
                b_cells = cluster_cells[ids[j]]
                if a_cells < b_cells:
                    gt_subset_pairs.add((ids[i], ids[j]))
                if b_cells < a_cells:
                    gt_subset_pairs.add((ids[j], ids[i]))

    cdb_subset_pairs = set(cdb.subset_pairs)

    missing = gt_subset_pairs - cdb_subset_pairs
    extra = cdb_subset_pairs - gt_subset_pairs

    status = "PASS" if len(missing) == 0 and len(extra) == 0 else "FAIL"
    print(f"  Ground truth subset pairs: {len(gt_subset_pairs)}")
    print(f"  CDB subset pairs: {len(cdb_subset_pairs)}")
    print(f"  Missing from CDB: {len(missing)}")
    print(f"  Extra in CDB: {len(extra)}")
    print(f"  Status: {status}")
    if missing:
        for sub, sup in list(missing)[:5]:
            print(f"    missing: cluster {sub} ⊂ cluster {sup}")
            print(f"      sub cells: {sorted(cluster_cells[sub])}")
            print(f"      sup cells: {sorted(cluster_cells[sup])}")
    return status, len(gt_subset_pairs), len(missing), len(extra)

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Mining integrity verification")
    parser.add_argument("--cluster-cdb", required=True, help="Path to cluster.cdb")
    parser.add_argument("--layout-cdb", required=True, help="Path to netlist.cdb")
    parser.add_argument("--cell-cdb", required=True, help="Path to cell.cdb")
    parser.add_argument("--graph-snapshot", default="", help="Path to graph_snapshot.out (for test B)")
    parser.add_argument("--tests", default="A,E,F,G", help="Comma-separated test IDs (A,B,E,F,G)")
    parser.add_argument("--limit-A", type=int, default=None, help="Limit clusters for test A")
    parser.add_argument("--max-cells-E", type=int, default=3, help="Max cells for brute-force (test E)")
    args = parser.parse_args()

    rl = RuntimeLogger("verify_mining_integrity")

    tests = [t.strip().upper() for t in args.tests.split(",")]

    print("=" * 70)
    print("  Mining Integrity Verification Suite")
    print("=" * 70)
    print(f"  cluster.cdb: {args.cluster_cdb}")
    print(f"  netlist.cdb:  {args.layout_cdb}")
    print(f"  cell.cdb:    {args.cell_cdb}")
    print(f"  Tests:       {', '.join(tests)}")

    with rl.step("read CDBs"):
        cdb = read_cluster_db(args.cluster_cdb)
        ldb = read_netlist_db(args.layout_cdb)
        cell_db = read_cell_db(args.cell_cdb)

    with rl.step("build circuit"):
        instances, net_map = build_circuit(ldb, cell_db)

    results = {}

    if "A" in tests:
        with rl.step("test A: truth table"):
            results["A"] = test_A_truth_table(cdb, instances, net_map, cell_db, limit=args.limit_A)

    if "B" in tests:
        with rl.step("test B: graph construction"):
            results["B"] = test_B_graph_construction(ldb, args.graph_snapshot)

    if "E" in tests:
        with rl.step("test E: completeness"):
            results["E"] = test_E_completeness(cdb, instances, net_map, cell_db,
                                                max_cells=args.max_cells_E)

    if "F" in tests:
        with rl.step("test F: overlap groups"):
            results["F"] = test_F_overlap_groups(cdb)

    if "G" in tests:
        with rl.step("test G: subset pairs"):
            results["G"] = test_G_subset_pairs(cdb)

    print("\n" + "=" * 70)
    print("  SUMMARY")
    print("=" * 70)
    all_pass = True
    for test_id in sorted(results.keys()):
        r = results[test_id]
        status = r[0]
        print(f"  [{test_id}] {status}")
        if status == "FAIL":
            all_pass = False

    if all_pass:
        print("\n  ALL TESTS PASSED")
    else:
        print("\n  SOME TESTS FAILED")
        rl.done()
        sys.exit(1)

    rl.done()

if __name__ == "__main__":
    main()
