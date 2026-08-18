from __future__ import annotations

import csv
import fnmatch
import itertools
import math
import os
import re
import sys
import warnings
from collections import defaultdict
from pathlib import Path
from typing import Optional

import torch

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_HERE)
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from baseline_split import read_relation
from graph_builder import build_graph, load_features_csv, NODE_FEATURE_NAMES, compute_norm_stats, apply_norm_stats

DESIGN_FEAT_DIM       = 21
DESIGN_FEAT_DIM_EXTRA = 23
ROLE_FLAG_DIM         = 3
NPN_FEAT_DIM          = 4
DIFF_NODE_FEAT_DIM       = len(NODE_FEATURE_NAMES) + DESIGN_FEAT_DIM       + ROLE_FLAG_DIM + NPN_FEAT_DIM
DIFF_NODE_FEAT_DIM_EXTRA = len(NODE_FEATURE_NAMES) + DESIGN_FEAT_DIM_EXTRA + ROLE_FLAG_DIM + NPN_FEAT_DIM

try:
    from torch_geometric.data import Data as _PyGData
    _HAVE_PYG = True
except ImportError:
    _HAVE_PYG = False

_ALWAYS_ACTIVE_CANONICALS: frozenset[str] = frozenset({"0b01"})

def _is_buf_or_seq(name: str) -> bool:
    classes = _load_cell_classes()
    entry = classes.get(name)
    if entry is not None:
        return entry[0] in ("buf_inv", "seq", "const")
    u = name.upper()
    return (
        u.startswith("BUF")
        or u.startswith("INV")
        or "DFF" in u
        or "LATCH" in u
        or u.startswith("LHQ")
        or u.startswith("LHL")
        or u.startswith("LSQ")
    )

def _count_logic_cells(cell_counts: dict[str, float]) -> float:
    total = sum(v for k, v in cell_counts.items() if not _is_buf_or_seq(k))
    if total <= 0:
        raise ValueError(
            "n_prev = 0 after excluding BUF/SEQ cells — "
            "cell_counts.csv contains only non-logic cells."
        )
    return total

_VERILOG_KW = {
    "module", "endmodule", "input", "output", "inout", "wire", "reg",
    "assign", "always", "begin", "end", "if", "else", "case", "endcase",
    "parameter", "localparam", "integer", "supply0", "supply1", "tri",
    "posedge", "negedge", "initial", "generate", "endgenerate",
}

def _parse_cell_fractions(netlist_path: str) -> dict[str, float]:
    counts: dict[str, int] = {}
    try:
        with open(netlist_path) as f:
            for raw in f:
                line = raw.rstrip("\n")
                if not line:
                    continue
                stripped = line.lstrip()
                if not stripped:
                    continue
                if stripped.startswith("//") or stripped.startswith("/*"):
                    continue
                if stripped[0] == '\\':
                    i = 1
                    while i < len(stripped) and not stripped[i].isspace():
                        i += 1
                    cell_type = stripped[1:i]
                elif stripped[0].isalpha() or stripped[0] == '_':
                    i = 0
                    while i < len(stripped) and (stripped[i].isalnum() or stripped[i] == '_'):
                        i += 1
                    cell_type = stripped[:i]
                else:
                    continue
                if not cell_type:
                    continue
                rest = stripped[i:]
                if not rest or not rest[0].isspace():
                    continue
                rest = rest.lstrip()
                if not rest or not (rest[0].isalnum() or rest[0] == '_' or rest[0] == '\\'):
                    continue
                if cell_type.lower() in _VERILOG_KW:
                    continue
                counts[cell_type] = counts.get(cell_type, 0) + 1
    except OSError as e:
        raise OSError(f"Failed to open netlist {netlist_path!r}: {e}") from e
    total = sum(counts.values())
    if total == 0:
        return {}
    return {k: v / total for k, v in counts.items()}

def _read_canonical_keys(run_dir: Path) -> Optional[set[str]]:
    ck = run_dir / "filters" / "canonical_keys.txt"
    if not ck.exists():
        return None
    keys = {
        line.strip() for line in ck.read_text().splitlines()
        if line.strip().startswith("0b")
    }
    return keys if keys else None

def _read_only_use_patterns(run_dir: Path) -> Optional[list[str]]:
    only_use = run_dir / "filters" / "only_use_lib_cells.list"
    if not only_use.exists():
        return None
    patterns = [
        line.strip() for line in only_use.read_text().splitlines()
        if line.strip() and not line.strip().startswith("#")
    ]
    return patterns if patterns else None

def _compute_lib_mask(
    all_cell_features: dict[str, dict],
    canonical_keys: Optional[set[str]] = None,
    patterns: Optional[list[str]] = None,
) -> dict[str, bool]:
    mask: dict[str, bool] = {name: False for name in all_cell_features}
    if canonical_keys is not None:
        for name, feats in all_cell_features.items():
            if feats.get("canonical") in canonical_keys:
                mask[name] = True
    elif patterns:
        for name in all_cell_features:
            for pat in patterns:
                if fnmatch.fnmatch(name, pat):
                    mask[name] = True
                    break
    for name, feats in all_cell_features.items():
        if feats.get("canonical") in _ALWAYS_ACTIVE_CANONICALS:
            mask[name] = True
    return mask

def _read_cell_counts(run_dir: Path) -> Optional[dict[str, float]]:
    cc = run_dir / "cell_counts.csv"
    if not cc.exists():
        return None
    counts: dict[str, float] = {}
    with open(cc) as f:
        for row in csv.DictReader(f):
            name  = row.get("cell_name", "").strip()
            count = row.get("count",     "").strip()
            if name and count:
                counts[name] = float(count)
    return counts if counts else None

def _cell_count_lookup(cell_counts: dict[str, float], name: str) -> float:
    v = cell_counts.get(name)
    if v is not None:
        return v
    return 0.0

_CELL_CLASSES_CSV = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "cell_classes.csv",
)
_cell_classes_cache: Optional[dict[str, tuple[str, str]]] = None

def _load_cell_classes() -> dict[str, tuple[str, str]]:
    global _cell_classes_cache
    if _cell_classes_cache is None:
        if not os.path.isfile(_CELL_CLASSES_CSV):
            raise FileNotFoundError(
                f"cell class map not found: {_CELL_CLASSES_CSV}. Regenerate with "
                "gen_cell_classes.py --lib-dir $PDK_ROOT/lib"
            )
        m: dict[str, tuple[str, str]] = {}
        with open(_CELL_CLASSES_CSV) as f:
            for row in csv.DictReader(f):
                m[row["name"]] = (row["cell_class"], row["canonical_key"])
        if not m:
            raise ValueError(f"cell class map is empty: {_CELL_CLASSES_CSV}")
        _cell_classes_cache = m
    return _cell_classes_cache

def _p_canonical_of_bits(canon: str) -> str:
    bits = canon[2:] if canon.startswith("0b") else canon
    n = (len(bits)).bit_length() - 1
    if (1 << n) != len(bits):
        raise ValueError(f"canonical bit length not a power of 2: {canon!r}")
    return _p_canonical_str(int(bits, 2), n)

class CellNameResolver:

    _DRIVE_TOKEN_RE = re.compile(r'_X(\d+)(?:_|$)')

    def __init__(self, features: dict[str, dict]):
        self.vocab_names: set[str] = set(features)
        self.vocab_by_canon_ds: dict[tuple[str, str], list[str]] = {}
        self.vocab_pmin: dict[str, str] = {}
        for nm, ft in features.items():
            can = ft.get("canonical")
            if not can:
                continue
            pmin = _p_canonical_of_bits(can)
            self.vocab_pmin[nm] = pmin
            m = self._DRIVE_TOKEN_RE.search(nm)
            if not m:
                continue
            self.vocab_by_canon_ds.setdefault(
                (pmin, "X" + m.group(1)), []).append(nm)
        for _k in self.vocab_by_canon_ds:
            self.vocab_by_canon_ds[_k].sort(
                key=lambda n: (0 if re.search(r'_X\d+$', n) else 1, n))
        self._cache: dict[str, str] = {}
        self._integrity_warned: set[str] = set()

    def _exact_hit(self, vocab_name: str, map_entry) -> str:
        if (map_entry is not None and map_entry[0] == "logic" and map_entry[1]
                and vocab_name in self.vocab_pmin):
            lib_pmin = _p_canonical_of_bits(map_entry[1])
            if (lib_pmin != self.vocab_pmin[vocab_name]
                    and vocab_name not in self._integrity_warned):
                self._integrity_warned.add(vocab_name)
                print(
                    f"[resolver] INTEGRITY: vocab cell {vocab_name!r} canonical "
                    f"(perm-min {self.vocab_pmin[vocab_name]}) != current lib "
                    f"function (perm-min {lib_pmin}). Exact-name match "
                    "kept; one side is wrong — see the PDK validity audit "
                    "(swapped-lib pairs / X2-inherited canonicals).",
                    flush=True,
                )
        return vocab_name

    def resolve(self, name: str) -> str:
        classes = _load_cell_classes()
        key = name

        def _map_entry():
            return classes.get(key)

        if name in self.vocab_names:
            return self._exact_hit(name, _map_entry())
        cached = self._cache.get(name)
        if cached is not None:
            return cached
        resolved = name
        entry = _map_entry()
        m = self._DRIVE_TOKEN_RE.search(key)
        if entry is not None and entry[0] == "logic" and entry[1] and m:
            cands = self.vocab_by_canon_ds.get(
                (_p_canonical_of_bits(entry[1]), "X" + m.group(1))
            )
            if cands:
                resolved = cands[0]
        self._cache[name] = resolved
        return resolved

def _p_canonical_str(tt_int: int, n: int) -> str:
    rows = 1 << n
    base = ''.join(str((tt_int >> ((rows - 1) - a)) & 1) for a in range(rows))
    best = base
    for perm in itertools.permutations(range(n)):
        remap = []
        for idx in range(rows):
            old = 0
            for i in range(n):
                old |= ((idx >> (n - 1 - i)) & 1) << (n - 1 - perm[i])
            remap.append(old)
        cand = ''.join(base[remap[idx]] for idx in range(rows))
        if cand < best:
            best = cand
    return "0b" + best

def _remap_dict_keys(d: dict, resolver) -> dict:
    out: dict = {}
    for k, v in d.items():
        nk = resolver(k)
        if nk in out and isinstance(v, (int, float)) and isinstance(out[nk], (int, float)):
            out[nk] += v
        elif nk not in out:
            out[nk] = v
    return out

def _read_canonical_freq(run_dir: Path) -> Optional[dict[str, float]]:
    freq_csv = run_dir / "canonical_freq.csv"
    if not freq_csv.exists():
        return None
    counts: dict[str, float] = {}
    with open(freq_csv) as f:
        for row in csv.DictReader(f):
            key   = row.get("canonical", "").strip()
            count = row.get("count",     "").strip()
            if key and count:
                try:
                    counts[key] = counts.get(key, 0.0) + float(count)
                except ValueError as e:
                    raise ValueError(
                        f"canonical_freq.csv: cannot parse count as float "
                        f"for key={key!r}, count={count!r} in {freq_csv}"
                    ) from e
    return counts if counts else None

def _canonical_freq_to_cell_counts(
    canonical_counts: dict[str, float],
    all_cell_features: dict[str, dict],
    lib_mask: dict[str, bool],
) -> dict[str, float]:
    key_to_active: dict[str, list[str]] = {}
    for name, feats in all_cell_features.items():
        if lib_mask.get(name, False):
            key_to_active.setdefault(feats["canonical"], []).append(name)
    cell_counts: dict[str, float] = {}
    for key, count in canonical_counts.items():
        active = key_to_active.get(key, [])
        if not active:
            continue
        per_cell = count / len(active)
        for name in active:
            cell_counts[name] = per_cell
    return cell_counts

def _read_mining_edge_features(run_dir: Path) -> Optional[dict[tuple[str, str], tuple[float, float]]]:
    f = run_dir / "mining_edge_features.csv"
    if not f.exists():
        return None
    out: dict[tuple[str, str], tuple[float, float]] = {}
    with open(f) as fp:
        reader = csv.DictReader(fp)
        for row in reader:
            try:
                key = (row["canonical"].strip(), row["master_cell"].strip())
                out[key] = (float(row["cluster_presence"]), float(row["avg_instances"]))
            except (KeyError, ValueError) as e:
                raise ValueError(
                    f"mining_edge_features.csv: malformed row {dict(row)} in {f}: {e}"
                ) from e
    return out if out else None

def _read_mining_node_rank(run_dir: Path) -> Optional[dict[str, float]]:
    f = run_dir / "mining_node_rank.csv"
    if not f.exists():
        return None
    out: dict[str, float] = {}
    with open(f) as fp:
        for r in csv.DictReader(fp):
            try:
                out[r["canonical"].strip()] = float(r["rank_norm"])
            except (KeyError, ValueError) as e:
                raise ValueError(
                    f"mining_node_rank.csv: malformed row {dict(r)} in {f}: {e}"
                ) from e
    return out if out else None

def _read_overlap_ratio(run_dir: Path) -> Optional[dict[tuple[str, str], float]]:
    f = run_dir / "mining_overlap_ratio.csv"
    if not f.exists():
        return None
    out: dict[tuple[str, str], float] = {}
    with open(f) as fp:
        reader = csv.DictReader(fp)
        for row in reader:
            try:
                key = (row["canonical_A"].strip(), row["canonical_B"].strip())
                out[key] = float(row["overlap_ratio"])
            except (KeyError, ValueError) as e:
                raise ValueError(
                    f"mining_overlap_ratio.csv: malformed row {dict(row)} in {f}: {e}"
                ) from e
    return out if out else None

_PREWARM_DATASET = None

def _prewarm_build_shard(task):
    start, end, shard_path = task
    ds = _PREWARM_DATASET
    shard = {idx: ds[idx] for idx in range(start, end)}
    torch.save(shard, shard_path)
    return shard_path

def _read_p_to_np_map(csv_path: str) -> dict[str, tuple]:
    out: dict[str, tuple] = {}
    scores: dict[str, tuple] = {}
    with open(csv_path) as fp:
        reader = csv.DictReader(fp)
        for row in reader:
            try:
                p  = row["p_canonical"].strip()
                np_ = row["np_canonical"].strip()
                ei = int(row.get("erased_input_inv",  "0") or "0")
                eo = int(row.get("erased_output_inv", "0") or "0")
                score = (ei + eo, np_, ei, eo)
                if p not in scores or score < scores[p]:
                    scores[p] = score
                    out[p] = (np_, ei, eo)
            except KeyError as e:
                raise ValueError(
                    f"p_to_np_map CSV: missing column {e} in row {dict(row)} "
                    f"in {csv_path}"
                ) from e
    return out

def _compute_p_to_np_map(l0_features: dict) -> dict[str, tuple]:
    def _npn_brute(tt_int: int, n: int) -> tuple[str, int, int]:
        width = 1 << n
        best = None
        best_ei = 0
        best_eo = 0
        for perm in itertools.permutations(range(n)):
            for in_neg in range(1 << n):
                for out_neg in range(2):
                    transformed = 0
                    for row in range(width):
                        new_row = 0
                        for i, p in enumerate(perm):
                            bit = (row >> p) & 1
                            if (in_neg >> p) & 1:
                                bit ^= 1
                            new_row |= (bit << i)
                        out_bit = (tt_int >> new_row) & 1
                        if out_neg:
                            out_bit ^= 1
                        transformed |= (out_bit << row)
                    if best is None or transformed < best:
                        best = transformed
                        best_ei = bin(in_neg).count("1")
                        best_eo = out_neg
        return "0b" + bin(best)[2:].zfill(width), best_ei, best_eo

    result: dict[str, tuple] = {}
    for cell_data in l0_features.values():
        p_can_raw = cell_data.get("canonical", "")
        if not p_can_raw:
            continue
        bit_len = len(p_can_raw) - 2 if p_can_raw.startswith("0b") else 0
        if bit_len <= 0 or (bit_len & (bit_len - 1)) != 0:
            continue
        n = int(math.log2(bit_len))
        p_can = _p_canonical_of_bits(p_can_raw)
        if p_can in result:
            continue
        if p_can == "0b10":
            result[p_can] = ("0b01", 0, 1)
            continue
        tt_int = int(p_can, 2)
        if n <= 4:
            np_can, ei, eo = _npn_brute(tt_int, n)
        else:
            width = 1 << n
            inv_can = _p_canonical_of_bits(
                "0b" + bin(((1 << width) - 1) ^ tt_int)[2:].zfill(width)
            )
            if inv_can in result:
                np_can, ei, eo = result[inv_can][0], result[inv_can][1], result[inv_can][2] + 1
            else:
                np_can, ei, eo = p_can, 0, 0
        result[p_can] = (np_can, ei, eo)
    return result

def _read_provenance(run_dir: Path) -> dict[str, str]:
    f = run_dir / "PROVENANCE"
    if not f.exists():
        return {}
    out: dict[str, str] = {}
    for line in f.read_text().splitlines():
        line = line.strip()
        if not line or "=" not in line:
            continue
        k, v = line.split("=", 1)
        out[k.strip()] = v.strip()
    return out

_DONT_USE_RE = re.compile(r"set_lib_cell_purpose\s+\*/([A-Za-z0-9_]+)\s+-exclude")

def _cell_fracs_from_id_map(run_dir: Path) -> Optional[dict[str, float]]:
    f = run_dir / "cell_id_map.csv"
    if not f.is_file():
        return None
    counts: dict[str, int] = {}
    with open(f) as fh:
        for row in csv.DictReader(fh):
            mc = (row.get("master_cell") or "").strip()
            if mc:
                counts[mc] = counts.get(mc, 0) + 1
    total = sum(counts.values())
    if total == 0:
        return None
    return {k: v / total for k, v in counts.items()}

def _parse_dont_use_tcl(tcl_path: Path) -> set[str]:
    if not tcl_path.is_file():
        return set()
    out: set[str] = set()
    for line in tcl_path.read_text().splitlines():
        m = _DONT_USE_RE.search(line)
        if m:
            out.add(m.group(1))
    return out

def _resolve_lib_subset(
    run_dir: Path,
    l0_universe: set[str],
    canonical_to_cells: Optional[dict[int, set[str]]] = None,
) -> Optional[tuple[set[str], str]]:
    direct_tcl = run_dir / "data" / "dont_use.tcl"
    if direct_tcl.is_file():
        excluded = _parse_dont_use_tcl(direct_tcl)
        return (l0_universe - excluded, "dont_use_tcl")

    prov = _read_provenance(run_dir)
    juj = prov.get("juj_dir")
    if juj:
        juj_path = Path(juj)
        candidates = [
            juj_path / "data" / "dont_use.tcl",
            juj_path.parent / "data" / "dont_use.tcl",
        ]
        tcl = next((p for p in candidates if p.is_file()), None)
        if tcl is not None:
            excluded = _parse_dont_use_tcl(tcl)
            return (l0_universe - excluded, "dont_use_tcl")

    filters_tcl = run_dir / "filters" / "dont_use.tcl"
    if filters_tcl.is_file():
        excluded = _parse_dont_use_tcl(filters_tcl)
        return (l0_universe - excluded, "dont_use_tcl")

    only_use = run_dir / "filters" / "only_use_lib_cells.list"
    if only_use.is_file():
        lines = only_use.read_text().splitlines()
        non_comment = [
            ln.strip() for ln in lines
            if ln.strip() and not ln.strip().startswith("#")
        ]
        if non_comment:
            named = set(non_comment) & l0_universe
            if named:
                return (named, "only_use_list")

    if canonical_to_cells is not None:
        ck = run_dir / "filters" / "canonical_keys.txt"
        if ck.is_file():
            try:
                key_ints = {
                    int(ln.strip(), 2)
                    for ln in ck.read_text().splitlines()
                    if ln.strip().startswith("0b")
                }
            except ValueError:
                key_ints = set()
            if key_ints:
                kept: set[str] = set()
                for ci, cells in canonical_to_cells.items():
                    if ci in key_ints:
                        kept |= cells
                kept &= l0_universe
                if kept:
                    return (kept, "canonical_keys")

    return None

def _read_cell_timing_features(run_dir: Path) -> Optional[dict[str, dict[str, float]]]:
    feat_csv = run_dir / "cell_timing_features.csv"
    if not feat_csv.exists():
        return None
    result: dict[str, dict[str, float]] = {}
    with open(feat_csv) as f:
        for row in csv.DictReader(f):
            cell_type = row.get("cell_type", "").strip()
            if not cell_type:
                continue
            try:
                result[cell_type] = {
                    "avg_fanout":       float(row.get("avg_fanout",       0) or 0),
                    "min_fanout":       float(row.get("min_fanout",       0) or 0),
                    "max_fanout":       float(row.get("max_fanout",       0) or 0),
                    "avg_output_cap":   float(row.get("avg_output_cap",   0) or 0),
                    "min_output_cap":   float(row.get("min_output_cap",   0) or 0),
                    "max_output_cap":   float(row.get("max_output_cap",   0) or 0),
                    "avg_input_slew":   float(row.get("avg_input_slew",   0) or 0),
                    "min_input_slew":   float(row.get("min_input_slew",   0) or 0),
                    "max_input_slew":   float(row.get("max_input_slew",   0) or 0),
                    "avg_output_slew":  float(row.get("avg_output_slew",  0) or 0),
                    "min_output_slew":  float(row.get("min_output_slew",  0) or 0),
                    "max_output_slew":  float(row.get("max_output_slew",  0) or 0),
                    "violation_ratio":  float(row.get("violation_ratio",  0) or 0),
                    "path_coverage":    float(row.get("path_coverage",    0) or 0),
                    "worst_slack_rank": float(row.get("worst_slack_rank", 0.5) or 0.5),
                    "avg_dynamic_power": float(row.get("avg_dynamic_power", 0) or 0),
                    "min_dynamic_power": float(row.get("min_dynamic_power", 0) or 0),
                    "max_dynamic_power": float(row.get("max_dynamic_power", 0) or 0),
                }
            except (ValueError, KeyError) as e:
                raise ValueError(
                    f"cell_timing_features.csv: malformed row for "
                    f"cell_type={cell_type!r} in {feat_csv}: {e}\nrow={dict(row)}"
                ) from e
    return result if result else None

def _design_feat_row(
    name: str,
    cell_fracs: dict[str, float],
    timing_features: Optional[dict[str, dict]],
    rank_norm_by_canonical: Optional[dict[str, float]] = None,
    canonical_of: Optional[dict[str, str]] = None,
    proxy_delay: Optional[float] = None,
    usage_rank: Optional[float] = None,
    context: str = "",
) -> list[float]:
    if timing_features is None:
        raise ValueError(
            "_design_feat_row: timing_features is None — refusing to build a "
            "node feature row with zero-initialized timing fields. Ensure the "
            "run dir has cell_timing_features.csv."
        )
    if not cell_fracs:
        raise ValueError(
            "_design_feat_row: cell_fracs is empty — refusing to silently "
            "default cell_fraction=0 for every node. Check that the run's "
            "synthesized Verilog parsed correctly."
        )
    cf = cell_fracs.get(name, 0.0)
    if cf > 0.0 and name not in timing_features:
        raise ValueError(
            f"_design_feat_row: cell {name!r} has cell_fraction={cf!r} > 0 but "
            "no row in the run's cell_timing_features.csv. Refusing to emit a "
            "zero-filled 18-dim timing block for a cell the run really used. "
            "Regenerate cell_timing_features.csv for this run (extract_cell_"
            "features.tcl / fc_validate_extract.py), or check the cell-name "
            f"resolver mapping for this name.{(' ' + context) if context else ''}"
        )
    tf = timing_features.get(name, {})
    is_present = 1.0 if cf > 0.0 else 0.0
    row = [
        is_present,
        cf,
        tf.get("avg_fanout",        0.0),
        tf.get("min_fanout",        0.0),
        tf.get("max_fanout",        0.0),
        tf.get("violation_ratio",   0.0),
        tf.get("path_coverage",     0.0),
        tf.get("worst_slack_rank",  0.0),
        tf.get("avg_output_cap",    0.0),
        tf.get("min_output_cap",    0.0),
        tf.get("max_output_cap",    0.0),
        tf.get("avg_input_slew",    0.0),
        tf.get("min_input_slew",    0.0),
        tf.get("max_input_slew",    0.0),
        tf.get("avg_output_slew",   0.0),
        tf.get("min_output_slew",   0.0),
        tf.get("max_output_slew",   0.0),
        tf.get("avg_dynamic_power", 0.0),
        tf.get("min_dynamic_power", 0.0),
        tf.get("max_dynamic_power", 0.0),
        _rank_norm_lookup(name, rank_norm_by_canonical, canonical_of),
    ]
    if proxy_delay is not None:
        row.append(float(proxy_delay))
    if usage_rank is not None:
        row.append(float(usage_rank))
    return row

def _role_onehot(name: str, in_a_lib: bool, in_b_lib: bool,
                 count_a: float, context: str) -> list[float]:
    in_a = bool(in_a_lib) or count_a > 0
    in_b = bool(in_b_lib)
    if in_a and in_b:
        return [0.0, 1.0, 0.0]
    if in_a:
        return [1.0, 0.0, 0.0]
    if in_b:
        return [0.0, 0.0, 1.0]
    raise ValueError(
        f"Data integrity error: node {name!r} is in the graph but absent from "
        f"A_lib, from B_lib, and from run A's cell counts. The cell's canonical "
        f"key is missing from the library subset of both sides. {context}"
    )

def _rank_norm_lookup(name, rank_norm_by_canonical, canonical_of):
    if rank_norm_by_canonical is None or canonical_of is None:
        raise ValueError(
            "_design_feat_row: rank_norm_by_canonical / canonical_of is None "
            "— refusing to default mining_rank_norm=0. Ensure the run dir has "
            "mining_node_rank.csv (rebuild via build_mining_edge_features.py) "
            "and that features_l0 provides each cell's canonical."
        )
    canon = canonical_of.get(name)
    if canon is None:
        raise ValueError(
            f"_design_feat_row: cell {name!r} has no canonical mapping; "
            "cannot assign mining_rank_norm."
        )
    canon_pmin = _p_canonical_of_bits(canon)
    return rank_norm_by_canonical.get(canon_pmin, 1.0)

class DiffPairDataset(torch.utils.data.Dataset):

    def __init__(
        self,
        run_root: str,
        relation_csv: str,
        l0_csv: str,
        features_csv: Optional[str] = None,
        max_delta: int = 16,
        require_cell_counts: bool = False,
        p_to_np_csv: Optional[str] = None,
        area_weighted_label: bool = False,
        extra_features: bool = False,
        lib_features: bool = False,
        skip_fc_status_check: bool = False,
        max_cache_size: int = 100000,
        exclude_runs_file: Optional[str] = None,
    ) -> None:
        self.exclude_run_names: set[str] = set()
        if exclude_runs_file:
            with open(exclude_runs_file) as _f:
                for _ln in _f:
                    _ln = _ln.strip().rstrip("/")
                    if _ln:
                        self.exclude_run_names.add("/".join(_ln.split("/")[-2:]))
            if not self.exclude_run_names:
                raise ValueError(f"exclude_runs_file {exclude_runs_file} is empty")
            print(f"[DiffPairDataset] excluding {len(self.exclude_run_names)} "
                  f"contaminated runs (from {exclude_runs_file})")
        self.skip_fc_status_check = bool(skip_fc_status_check)
        print(f"[DiffPairDataset] skip_fc_status_check = {self.skip_fc_status_check}")
        self.relation            = read_relation(relation_csv)
        self.max_delta           = max_delta
        self.require_cell_counts = require_cell_counts
        self.area_weighted_label = bool(area_weighted_label)
        print(f"[DiffPairDataset] label mode = "
              f"{'area-fraction' if self.area_weighted_label else 'count-fraction'}")
        self.extra_features = bool(extra_features)
        print(f"[DiffPairDataset] extra_features = {self.extra_features}")
        self.lib_features = bool(lib_features)
        print(f"[DiffPairDataset] lib_features = {self.lib_features}")

        print(f"[DiffPairDataset] Loading L0 features from {l0_csv!r} ...")
        self.l0_features = load_features_csv(l0_csv)
        print(f"[DiffPairDataset]   {len(self.l0_features)} L0 cells loaded.")

        if p_to_np_csv:
            self._p_to_np_map = _read_p_to_np_map(p_to_np_csv)
            print(f"[DiffPairDataset] Loaded global P→NPN map "
                  f"({len(self._p_to_np_map)} entries) from {p_to_np_csv!r}.")
        else:
            print("[DiffPairDataset] No --p-to-np CSV; computing NPN map from L0 features ...")
            self._p_to_np_map = _compute_p_to_np_map(self.l0_features)
            print(f"[DiffPairDataset] Computed P→NPN map "
                  f"({len(self._p_to_np_map)} unique canonicals).")

        self._canonical_to_cells: dict[int, set[str]] = {}
        for _name, _feats in self.l0_features.items():
            _can = _feats.get("canonical")
            if _can is None:
                continue
            try:
                _ci = int(str(_can), 2)
            except ValueError as e:
                raise ValueError(
                    f"DiffPairDataset: cannot parse canonical {_can!r} as binary int "
                    f"for cell {_name!r}: {e}"
                ) from e
            self._canonical_to_cells.setdefault(_ci, set()).add(_name)
        print(f"[DiffPairDataset]   canonical->cells map: "
              f"{len(self._canonical_to_cells)} unique canonicals.")

        self._cpp_min: dict[str, float] = {
            name: float(feats.get("cpp_min", 1.0))
            for name, feats in self.l0_features.items()
        }
        self._max_stack_n_raw: dict[str, float] = {
            name: float(feats.get("max_stack_n", 0.0) or 0.0)
            for name, feats in self.l0_features.items()
        }
        self._max_stack_p_raw: dict[str, float] = {
            name: float(feats.get("max_stack_p", 0.0) or 0.0)
            for name, feats in self.l0_features.items()
        }
        _cpp_vals = list(self._cpp_min.values())
        print(f"[DiffPairDataset] cpp_min stats: min={min(_cpp_vals):.1f} "
              f"max={max(_cpp_vals):.1f} mean={sum(_cpp_vals)/len(_cpp_vals):.2f}")

        self._proxy_delay: dict[str, float] = {
            name: (self._max_stack_n_raw.get(name, 0.0)
                   + self._max_stack_p_raw.get(name, 0.0))
                  * self._cpp_min.get(name, 1.0)
            for name in self.l0_features
        }
        if self.extra_features and self._proxy_delay:
            _pd_vals = list(self._proxy_delay.values())
            print(f"[DiffPairDataset] proxy_delay stats: "
                  f"min={min(_pd_vals):.2f} max={max(_pd_vals):.2f} "
                  f"mean={sum(_pd_vals)/len(_pd_vals):.2f}")

        self.cand_features: dict[str, dict] = {}
        if features_csv:
            print(f"[DiffPairDataset] Loading candidate features from {features_csv!r} ...")
            self.cand_features = load_features_csv(features_csv)
            print(f"[DiffPairDataset]   {len(self.cand_features)} candidate cells loaded.")

        self._norm_stats = compute_norm_stats(self.l0_features)
        apply_norm_stats(self.l0_features, self._norm_stats)
        if self.cand_features:
            apply_norm_stats(self.cand_features, self._norm_stats)
        print(f"[DiffPairDataset] Z-score normalization applied "
              f"(from {len(self.l0_features)} L0 cells).")

        self._npn_per_cell: dict[str, tuple[float, float, float, float]] = {}
        if True:
            np_class_size: dict[str, int] = {}
            for p, (np_, _, _) in self._p_to_np_map.items():
                np_class_size[np_] = np_class_size.get(np_, 0) + 1
            max_size = max(np_class_size.values()) if np_class_size else 1
            for cname, cfeat in self.l0_features.items():
                p_can_raw = cfeat.get("canonical", "")
                bit_len = len(p_can_raw) - 2 if p_can_raw.startswith("0b") else 0
                if bit_len > 0 and (bit_len & (bit_len - 1)) == 0:
                    n_in = int(math.log2(bit_len))
                else:
                    raise ValueError(
                        f"[DiffPairDataset] cell {cname!r} canonical={p_can_raw!r}: "
                        f"unable to derive n_inputs (bit_len={bit_len} not pow-2)."
                    )
                p_can = _p_canonical_of_bits(p_can_raw)
                if p_can in self._p_to_np_map:
                    np_, ei, eo = self._p_to_np_map[p_can]
                    is_rep = 1.0 if p_can == np_ else 0.0
                    sz_norm = np_class_size.get(np_, 1) / max_size
                    self._npn_per_cell[cname] = (is_rep, float(ei), float(eo), sz_norm)
                elif p_can == "0b10":
                    self._npn_per_cell[cname] = (0.0, 0.0, 1.0, 2.0 / max_size)
                elif n_in >= 2:
                    width = 1 << n_in
                    try:
                        v = int(p_can, 2)
                    except ValueError:
                        raise ValueError(
                            f"[DiffPairDataset] npn_features: cell {cname!r} "
                            f"canonical={p_can!r} is not a valid binary string."
                        )
                    inv_v = ((1 << width) - 1) ^ v
                    inv_can = _p_canonical_of_bits("0b" + bin(inv_v)[2:].zfill(width))
                    if inv_can in self._p_to_np_map:
                        np_, ei, eo = self._p_to_np_map[inv_can]
                        eo_adj = int(eo) + 1
                        is_rep = 1.0 if inv_can == np_ else 0.0
                        sz_norm = np_class_size.get(np_, 1) / max_size
                        self._npn_per_cell[cname] = (is_rep, float(ei), float(eo_adj), sz_norm)
                        print(f"[DiffPairDataset]   NPN bit-invert fallback: "
                              f"{cname!r} canonical={p_can!r} -> "
                              f"inv={inv_can!r} found in csv (eo+=1).",
                              flush=True)
                    else:
                        raise ValueError(
                            f"[DiffPairDataset] npn_features: cell {cname!r} "
                            f"canonical={p_can!r} not in p_to_np_map AND its "
                            f"bit-inverse {inv_can!r} also not found. Cannot "
                            f"resolve NPN class — fix data or disable "
                            f"--npn-features. NEVER zero-fallback."
                        )
                else:
                    raise ValueError(
                        f"[DiffPairDataset] npn_features: cell {cname!r} "
                        f"canonical={p_can!r} n_inputs={n_in} unhandled."
                    )
            print(f"[DiffPairDataset]   NPN features computed for "
                  f"{len(self._npn_per_cell)}/{len(self.l0_features)} L0 cells "
                  f"(max NP-class size = {max_size}).")

        self._samples: list[dict] = []
        self._fracs_cache: dict[str, dict] = {}
        self._item_cache:  dict[int, tuple] = {}
        self._max_cache_size: int = int(max_cache_size)
        print(f"[DiffPairDataset] max_cache_size = {self._max_cache_size} "
              f"({'unlimited' if self._max_cache_size < 0 else 'capped'})")
        roots = [run_root] if isinstance(run_root, (str, Path)) else list(run_root)
        for r in roots:
            self._scan(Path(r))

        if not self._samples:
            raise ValueError(
                f"No valid training samples found under {run_root!r}.\n"
                "Each run dir must contain:\n"
                "  filters/only_use_lib_cells.list\n"
                "  cell_counts.csv  OR  canonical_freq.csv"
            )

        by_design: dict[str, list[int]] = defaultdict(list)
        for idx, s in enumerate(self._samples):
            by_design[s["design"]].append(idx)

        self._pairs: list[tuple[int, int]] = []
        for design_indices in by_design.values():
            for i in design_indices:
                for j in design_indices:
                    if i != j:
                        self._pairs.append((i, j))

        n_runs = len(self._samples)
        print(f"[DiffPairDataset] {n_runs} runs → {len(self._pairs)} ordered pairs", flush=True)

        self._compute_design_norm_stats()

    @staticmethod
    def _build_usage_rank(cell_counts: dict[str, float]) -> dict[str, float]:
        used = sorted(
            ((n, c) for n, c in cell_counts.items() if c > 0),
            key=lambda x: -x[1],
        )
        N = len(used)
        out: dict[str, float] = {}
        if N == 0:
            return out
        if N == 1:
            out[used[0][0]] = 1.0
            return out
        denom = float(N - 1)
        for idx, (n, _) in enumerate(used):
            out[n] = 1.0 - (idx / denom)
        return out

    def _compute_design_norm_stats(self, eps: float = 1e-8,
                                   sample_indices: Optional[list[int]] = None) -> None:
        self._canonical_of = {n: f["canonical"] for n, f in self.l0_features.items()}
        canonical_of = self._canonical_of
        if self.extra_features:
            for s in self._samples:
                s["usage_rank"] = self._build_usage_rank(s["cell_counts"])
        if sample_indices is None:
            stat_samples = self._samples
        else:
            bad = [i for i in sample_indices
                   if not (0 <= int(i) < len(self._samples))]
            if bad:
                raise ValueError(
                    f"_compute_design_norm_stats: run indices {bad[:10]} are out "
                    f"of range for {len(self._samples)} runs."
                )
            stat_samples = [self._samples[int(i)] for i in sample_indices]
            if not stat_samples:
                raise ValueError(
                    "_compute_design_norm_stats: sample_indices is empty — "
                    "refusing to build design-block z-score statistics from no "
                    "runs."
                )
        all_rows: list[list[float]] = []
        for s in stat_samples:
            v = s["generic_v"]
            if v not in self._fracs_cache:
                self._fracs_cache[v] = _parse_cell_fractions(v)
            cell_fracs = self._fracs_cache[v]
            tf = s.get("timing_features")
            rk = s.get("mining_rank_norm")
            ur = s.get("usage_rank") if self.extra_features else None
            for name in self.l0_features:
                pd_val = self._proxy_delay.get(name, 0.0) if self.extra_features else None
                ur_val = (ur.get(name, 0.0) if ur is not None else None)
                all_rows.append(_design_feat_row(
                    name, cell_fracs, tf, rk, canonical_of,
                    proxy_delay=pd_val, usage_rank=ur_val,
                    context=f"run={s['run_dir']}",
                ))
        A = torch.tensor(all_rows, dtype=torch.float)
        mean = A.mean(dim=0)
        std  = A.std(dim=0, unbiased=False)
        inactive = (std < eps)
        std = torch.where(inactive, torch.ones_like(std), std)

        self._design_mean = mean
        self._design_std  = std
        self._norm_stats["__design_mean__"] = mean.tolist()
        self._norm_stats["__design_std__"]  = std.tolist()
        self._norm_stats["__design_inactive__"] = (
            inactive.nonzero(as_tuple=False).flatten().tolist()
        )

        print(
            f"[DiffPairDataset] Design feature z-score over "
            f"{len(stat_samples)}/{len(self._samples)} runs: "
            f"raw mean range [{A.mean(dim=0).min().item():.3f}, "
            f"{A.mean(dim=0).max().item():.3f}], "
            f"raw std range [{A.std(dim=0, unbiased=False).min().item():.3f}, "
            f"{A.std(dim=0, unbiased=False).max().item():.3f}], "
            f"inactive dims = {self._norm_stats['__design_inactive__']}",
            flush=True,
        )

    def recompute_design_norm_stats_for_split(self, train_run_indices) -> None:
        idx = sorted({int(i) for i in train_run_indices})
        if not idx:
            raise ValueError(
                "recompute_design_norm_stats_for_split: no training run indices "
                "— the split produced an empty training set."
            )
        self._compute_design_norm_stats(sample_indices=idx)
        if self._item_cache:
            print(f"[DiffPairDataset] dropping {len(self._item_cache)} cached "
                  "graphs built under the previous design normalization.",
                  flush=True)
            self._item_cache.clear()

    def prewarm_cache(self, cache_path: Optional[str] = None,
                      n_workers: int = 0) -> None:
        import time as _t
        import os as _os
        if cache_path and Path(cache_path).is_file():
            t0 = _t.time()
            print(f"[prewarm] loading cache from {cache_path} ...", flush=True)
            cached = torch.load(cache_path, weights_only=False)
            self._item_cache = {i: v for i, v in enumerate(cached)}
            print(f"[prewarm]   loaded {len(cached)} items in {_t.time()-t0:.1f}s",
                  flush=True)
            return

        if n_workers < 0:
            n_workers = max(1, (_os.cpu_count() or 1) // 2)

        N = len(self._pairs)
        t0 = _t.time()
        print(f"[prewarm] cold-building {N} graphs (n_workers={n_workers}) ...",
              flush=True)

        if n_workers <= 1:
            for i in range(N):
                _ = self[i]
                if (i + 1) % 5000 == 0:
                    print(f"[prewarm]   {i+1}/{N}  ({_t.time()-t0:.1f}s)",
                          flush=True)
        elif cache_path:
            import multiprocessing as _mp
            global _PREWARM_DATASET
            _PREWARM_DATASET = self
            shard_size = (N + n_workers - 1) // n_workers
            tasks = [
                (s, min(s + shard_size, N), f"{cache_path}.shard{k}.pt")
                for k, s in enumerate(range(0, N, shard_size))
            ]
            print(f"[prewarm]   {len(tasks)} fork shards (~{shard_size}/shard) ...",
                  flush=True)
            ctx = _mp.get_context("fork")
            with ctx.Pool(len(tasks)) as pool:
                shard_paths = pool.map(_prewarm_build_shard, tasks)
            _PREWARM_DATASET = None
            self._item_cache = {}
            for sp in shard_paths:
                self._item_cache.update(torch.load(sp, weights_only=False))
                _os.remove(sp)
        else:
            from concurrent.futures import ThreadPoolExecutor

            def _build_one(idx: int) -> None:
                self[idx]

            done_count = 0
            with ThreadPoolExecutor(max_workers=n_workers) as executor:
                for _ in executor.map(_build_one, range(N)):
                    done_count += 1
                    if done_count % 5000 == 0:
                        print(f"[prewarm]   {done_count}/{N}  ({_t.time()-t0:.1f}s)",
                              flush=True)

        elapsed = _t.time() - t0
        print(f"[prewarm]   built {N} items in {elapsed:.1f}s ({N/max(elapsed,1):.1f}/s)",
              flush=True)

        if cache_path:
            Path(cache_path).parent.mkdir(parents=True, exist_ok=True)
            items = [self._item_cache[i] for i in sorted(self._item_cache.keys())]
            t1 = _t.time()
            tmp_path = cache_path + ".tmp"
            torch.save(items, tmp_path)
            os.rename(tmp_path, cache_path)
            print(f"[prewarm]   saved cache to {cache_path} ({_t.time()-t1:.1f}s)",
                  flush=True)

    def _all_features(self) -> dict[str, dict]:
        try:
            return self._all_feats_cache
        except AttributeError:
            self._all_feats_cache = {**self.l0_features, **self.cand_features}
            return self._all_feats_cache

    def _build_resolver_index(self) -> None:
        if getattr(self, "_resolver_ready", False):
            return
        self._resolver = CellNameResolver(self._all_features())
        self._vocab_names = self._resolver.vocab_names
        self._resolver_ready = True

    def _resolve_cell(self, name: str) -> str:
        self._build_resolver_index()
        return self._resolver.resolve(name)

    def _scan(self, root: Path) -> None:
        all_feats = self._all_features()

        n_excluded = 0
        for cc_file in sorted(root.rglob("cell_counts.csv")):
            run_dir = cc_file.parent
            _run_key = f"{root.name}/{run_dir.name}"
            if self.exclude_run_names and _run_key in self.exclude_run_names:
                n_excluded += 1
                continue

            has_provenance = (run_dir / "PROVENANCE").is_file()
            if not has_provenance:
                required_mining = ["mining_node_rank.csv"]
                missing = [
                    f for f in required_mining
                    if not (run_dir / f).is_file()
                ]
                if missing:
                    print(f"[DiffPairDataset] SKIP payload run (incomplete "
                          f"mining artifacts): {run_dir}", flush=True)
                    for f in missing:
                        print(f"  missing: {f}", flush=True)
                    continue

            if not self.skip_fc_status_check and has_provenance:
                marker = run_dir / "FC_STATUS"
                if not marker.is_file():
                    raise ValueError(
                        f"[DiffPairDataset] missing FC_STATUS marker in {run_dir}. "
                        f"Run preflight_fc_status.py first, or pass "
                        f"skip_fc_status_check=True to bypass."
                    )
                marker_text = marker.read_text()
                first_line = marker_text.splitlines()[0] if marker_text else ""
                if first_line.startswith("status=broken"):
                    print(f"[DiffPairDataset] SKIP broken run: {run_dir}",
                          flush=True)
                    for ln in marker_text.splitlines():
                        print(f"  {ln}", flush=True)
                    continue
                elif not first_line.startswith("status=valid"):
                    raise ValueError(
                        f"[DiffPairDataset] FC_STATUS in {run_dir} has "
                        f"unexpected first line: {first_line!r}"
                    )

            cell_counts = _read_cell_counts(run_dir)
            if cell_counts is None:
                print(
                    f"[DiffPairDataset] SKIP {run_dir.name} "
                    f"({root.name}): cell_counts.csv missing or unparseable.",
                    flush=True,
                )
                continue

            if not cell_counts:
                raise ValueError(
                    f"[DiffPairDataset] {run_dir}: cell_counts.csv parsed but "
                    f"contains no valid rows — empty dict is a data integrity error."
                )

            cell_counts = _remap_dict_keys(cell_counts, self._resolve_cell)

            logic_cells_in_counts = {
                n for n, c in cell_counts.items()
                if c > 0 and not _is_buf_or_seq(n)
            }
            oov_cells = logic_cells_in_counts - all_feats.keys()
            if oov_cells:
                print(f"[DiffPairDataset] SKIP {run_dir.name} "
                      f"({root.name}): {len(oov_cells)} OOV cells "
                      f"not in current features_l0 vocab: {sorted(oov_cells)}",
                      flush=True)
                continue

            active_nonzero = sum(
                1 for name, cnt in cell_counts.items()
                if cnt > 0 and name in all_feats and not _is_buf_or_seq(name)
            )
            if active_nonzero < 2:
                print(
                    f"[DiffPairDataset] SKIP {run_dir.name} "
                    f"({root.name}): only {active_nonzero} active "
                    f"logic cell(s) with count>0 — need ≥2.",
                    flush=True,
                )
                continue

            timing_features = _read_cell_timing_features(run_dir)
            if timing_features:
                timing_features = _remap_dict_keys(timing_features, self._resolve_cell)

            netlists = sorted(run_dir.glob(
                "**/outputs_fc/compile_logic_opto.ascii_files/*.v"
            ))
            if not netlists:
                netlists = sorted(run_dir.glob("**/results/syn_results/*.v"))
            if netlists:
                generic_v = str(netlists[0])
            else:
                derived = _cell_fracs_from_id_map(run_dir)
                if derived is None:
                    total_cc = sum(cell_counts.values())
                    if total_cc > 0:
                        derived = {k: v / total_cc for k, v in cell_counts.items()}
                if derived is None:
                    raise ValueError(
                        f"[DiffPairDataset] {run_dir}: no Verilog netlist, "
                        f"no cell_id_map.csv, AND cell_counts.csv totals zero "
                        f"— cannot derive cell_fractions."
                    )
                derived = _remap_dict_keys(derived, self._resolve_cell)
                generic_v = f"<cellid:{run_dir}>"
                self._fracs_cache[generic_v] = derived

            design = root.name

            mining_edges = _read_mining_edge_features(run_dir)
            if mining_edges:
                mining_edges = {
                    (c, self._resolve_cell(mc)): v
                    for (c, mc), v in mining_edges.items()
                }
            ovr          = _read_overlap_ratio(run_dir)
            rank_norm    = _read_mining_node_rank(run_dir)
            l0_universe = set(self.l0_features.keys())
            lib_resolved = _resolve_lib_subset(
                run_dir, l0_universe, self._canonical_to_cells,
            )
            if lib_resolved is None:
                raise ValueError(
                    f"[DiffPairDataset] cannot resolve lib_subset for run "
                    f"{run_dir}. Tried: (1) PROVENANCE -> dont_use.tcl, "
                    f"(2) filters/only_use_lib_cells.list (real), "
                    f"(3) filters/canonical_keys.txt. None succeeded. "
                    f"lib_subset is required (role flag + loss_mask)."
                )
            lib_subset, lib_source = lib_resolved
            lib_subset = {self._resolve_cell(n) for n in lib_subset}
            self._samples.append({
                "run_dir":              run_dir,
                "design":               design,
                "lib_subset":           lib_subset,
                "lib_source":           lib_source,
                "cell_counts":          cell_counts,
                "generic_v":            str(generic_v),
                "timing_features":      timing_features,
                "mining_edge_features": mining_edges,
                "overlap_ratio":        ovr,
                "mining_rank_norm":     rank_norm,
            })

    def __len__(self) -> int:
        return len(self._pairs)

    def _get_pair_node_meta(self, idx: int) -> tuple[list[str], list[bool]]:
        i, j = self._pairs[idx]
        si   = self._samples[i]
        sj   = self._samples[j]
        all_feats = self._all_features()
        ci    = si["cell_counts"]
        B_lib = sj["lib_subset"]
        union_mask = {
            name: (
                (_cell_count_lookup(ci, name) > 0 or name in B_lib)
                and not _is_buf_or_seq(name)
                and name in all_feats
            )
            for name in all_feats
        }
        node_keys = [n for n in all_feats if union_mask[n]]
        in_b = [name in B_lib for name in node_keys]
        return node_keys, in_b

    def __getitem__(self, idx: int):
        if idx in self._item_cache:
            return self._item_cache[idx]
        i, j   = self._pairs[idx]
        si     = self._samples[i]
        sj     = self._samples[j]
        all_feats = self._all_features()

        ci    = si["cell_counts"]
        cj    = sj["cell_counts"]
        A_lib = si["lib_subset"]
        B_lib = sj["lib_subset"]
        union_mask = {
            name: (
                (_cell_count_lookup(ci, name) > 0 or name in B_lib)
                and not _is_buf_or_seq(name)
                and name in all_feats
            )
            for name in all_feats
        }

        g = build_graph(
            all_cell_features    = all_feats,
            lib_mask             = union_mask,
            mining_edge_features = si.get("mining_edge_features"),
            p_to_np_map          = self._p_to_np_map,
            overlap_ratio        = si.get("overlap_ratio"),
        )

        if self.area_weighted_label:
            n_prev = sum(
                _cell_count_lookup(ci, name) * self._cpp_min.get(name, 1.0)
                for name in all_feats
                if _cell_count_lookup(ci, name) > 0 and not _is_buf_or_seq(name)
            )
        else:
            n_prev = sum(
                _cell_count_lookup(ci, name)
                for name in all_feats
                if _cell_count_lookup(ci, name) > 0 and not _is_buf_or_seq(name)
            )
        if n_prev <= 0:
            raise ValueError(
                f"n_prev = 0 for run A '{si['run_dir']}' — no logic cells. "
                "cell_counts.csv may be empty or only contain BUF/seq."
            )
        vi = si["generic_v"]
        if vi not in self._fracs_cache:
            self._fracs_cache[vi] = _parse_cell_fractions(vi)
        cell_fracs_i = self._fracs_cache[vi]
        tf_i         = si.get("timing_features")

        role_rows:    list[list[float]] = []
        design_rows:  list[list[float]] = []
        labels:       list[float]       = []
        loss_mask:    list[float]       = []

        for name in g["node_keys"]:
            in_b = name in B_lib
            role_rows.append(_role_onehot(
                name,
                in_a_lib = name in A_lib,
                in_b_lib = in_b,
                count_a  = _cell_count_lookup(ci, name),
                context  = f"Run A={si['run_dir']}  Run B={sj['run_dir']}",
            ))

            count_j   = _cell_count_lookup(cj, name)
            cpp_c     = self._cpp_min.get(name, 1.0) if self.area_weighted_label else 1.0
            label_val = count_j * cpp_c / n_prev
            labels.append(label_val)

            loss_mask.append(1.0 if in_b else 0.0)

            if self.extra_features:
                ur_i = si.get("usage_rank") or {}
                pd_val = self._proxy_delay.get(name, 0.0)
                ur_val = ur_i.get(name, 0.0)
            else:
                pd_val = None
                ur_val = None
            design_rows.append(_design_feat_row(
                name, cell_fracs_i, tf_i,
                si.get("mining_rank_norm"),
                self._canonical_of,
                proxy_delay=pd_val, usage_rank=ur_val,
                context=f"run A={si['run_dir']}"))

        x_base   = g["x"]
        x_design = torch.tensor(design_rows, dtype=torch.float)
        x_design = (x_design - self._design_mean) / self._design_std
        x_role   = torch.tensor(role_rows,   dtype=torch.float)
        blocks = [x_base, x_design, x_role]
        if self.lib_features:
            l0_n = float(max(1, len(self.l0_features)))
            shared_n  = len(A_lib & B_lib) / l0_n
            a_only_n  = len(A_lib - B_lib) / l0_n
            b_only_n  = len(B_lib - A_lib) / l0_n
            lib_rows = []
            for name in g["node_keys"]:
                in_a = 1.0 if name in A_lib else 0.0
                in_b = 1.0 if name in B_lib else 0.0
                lib_rows.append([in_a, in_b, shared_n, a_only_n, b_only_n])
            x_lib = torch.tensor(lib_rows, dtype=torch.float)
            blocks.append(x_lib)
        npn_rows = []
        for name in g["node_keys"]:
            v = self._npn_per_cell.get(name)
            if v is None:
                raise ValueError(
                    f"NPN block: graph node {name!r} has no NPN features "
                    f"(not in L0 lookup). Fix the data, never zero-fallback."
                )
            npn_rows.append(list(v))
        x_npn = torch.tensor(npn_rows, dtype=torch.float)
        blocks.append(x_npn)
        x_full   = torch.cat(blocks, dim=1)
        y        = torch.tensor(labels,    dtype=torch.float)
        m        = torch.tensor(loss_mask, dtype=torch.float)

        keep_mask = (m > 0.5)
        scale_val = float(y[keep_mask].sum().item()) if keep_mask.any() else 0.0
        if scale_val > 1e-9:
            dist_lbl = (y / scale_val) * m
        else:
            dist_lbl = torch.zeros_like(y)
        scale_lbl = torch.tensor([scale_val], dtype=torch.float)

        if _HAVE_PYG:
            data = _PyGData(
                x          = x_full,
                edge_index = g["edge_index"],
                edge_attr  = g["edge_attr"],
                node_keys  = g["node_keys"],
                loss_mask  = m,
                dist_label = dist_lbl,
                scale_label= scale_lbl,
            )
            result = (data, y)
        else:
            g["x"]          = x_full
            g["loss_mask"]  = m
            g["dist_label"] = dist_lbl
            g["scale_label"]= scale_lbl
            result = (g, y)
        if self._max_cache_size < 0 or len(self._item_cache) < self._max_cache_size:
            self._item_cache[idx] = result
        elif not getattr(self, '_cache_sat_logged', False):
            self._cache_sat_logged = True
            print(
                f"[DiffPairDataset] cache saturated at {len(self._item_cache)} graphs "
                f"(max_cache_size={self._max_cache_size}); "
                "remaining graphs rebuilt per epoch.",
                flush=True,
            )
        return result

_PREWARM_DS = None
def _init_prewarm_ds(ds) -> None:
    global _PREWARM_DS
    _PREWARM_DS = ds

def _prewarm_worker(idx: int):
    return _PREWARM_DS[idx]

if __name__ == "__main__":
    import argparse

    p = argparse.ArgumentParser(description="Inspect DiffPairDataset.")
    p.add_argument("--run-root",       required=True)
    p.add_argument("--relation",       required=True)
    p.add_argument("--l0",            required=True)
    p.add_argument("--features",      default=None)
    p.add_argument("--max-delta",     type=int, default=16)
    args = p.parse_args()

    ds = DiffPairDataset(
        run_root     = args.run_root,
        relation_csv = args.relation,
        l0_csv       = args.l0,
        features_csv = args.features,
        max_delta    = args.max_delta,
    )
    print(f"Total pairs: {len(ds)}")
    g, y = ds[0]
    if _HAVE_PYG:
        print(f"Pair 0: {len(g.node_keys)} nodes, "
              f"edge_index {g.edge_index.shape}, "
              f"y range [{y.min():.4f}, {y.max():.4f}]")
        role_off = 18 + (23 if getattr(ds, "extra_features", False) else 21)
        roles = g.x[:, role_off:role_off + 3]
        print(f"  A_lib_only={int((roles[:, 0] > 0.5).sum())}  "
              f"both_libs={int((roles[:, 1] > 0.5).sum())}  "
              f"B_lib_only={int((roles[:, 2] > 0.5).sum())}")
    else:
        print(f"Pair 0: {len(g['node_keys'])} nodes, y range [{y.min():.4f}, {y.max():.4f}]")
