#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import itertools
import json
import math
import os
import re
import sys
from pathlib import Path
from typing import Optional

import torch

_HERE = Path(__file__).parent
if str(_HERE) not in sys.path:
    sys.path.insert(0, str(_HERE))

from train.model_diff import DiffDemandGNN
from train.dataset_diff import (
    _read_mining_edge_features,
    _read_mining_node_rank,
    _read_overlap_ratio,
    _read_cell_counts,
    _cell_count_lookup,
    _is_buf_or_seq,
    _load_cell_classes,
    _read_p_to_np_map,
    _compute_p_to_np_map,
    _read_cell_timing_features,
    _design_feat_row,
    _canonical_freq_to_cell_counts,
    _read_canonical_freq,
    _remap_dict_keys,
    _role_onehot,
    _p_canonical_of_bits,
    CellNameResolver,
    DiffPairDataset,
)
from graph_builder import (
    build_graph,
    load_features_csv,
    compute_norm_stats,
    apply_norm_stats,
)

try:
    from torch_geometric.data import Data as _PyGData
    from torch_geometric.data import Batch as _PyGBatch
    _HAVE_PYG = True
except ImportError:
    _HAVE_PYG = False

def _n_inputs_from_canonical(p_can: str) -> int:
    bit_len = len(p_can) - 2 if p_can.startswith("0b") else 0
    if bit_len > 0 and (bit_len & (bit_len - 1)) == 0:
        return int(math.log2(bit_len))
    raise ValueError(f"canonical {p_can!r}: bit_len={bit_len} not a power of 2")

def _build_npn_per_cell(
    l0_features: dict[str, dict],
    p_to_np_map: dict[str, tuple],
) -> dict[str, tuple[float, float, float, float]]:
    if not p_to_np_map:
        raise ValueError("p_to_np_map is empty — cannot build NPN features.")
    np_class_size: dict[str, int] = {}
    for p, (np_, _, _) in p_to_np_map.items():
        np_class_size[np_] = np_class_size.get(np_, 0) + 1
    max_size = max(np_class_size.values()) if np_class_size else 1

    npn_per_cell: dict[str, tuple] = {}
    for cname, cfeat in l0_features.items():
        p_can_raw = cfeat.get("canonical", "")
        bit_len = len(p_can_raw) - 2 if p_can_raw.startswith("0b") else 0
        if bit_len > 0 and (bit_len & (bit_len - 1)) == 0:
            n_in = int(math.log2(bit_len))
        else:
            raise ValueError(
                f"NPN: cell {cname!r} canonical={p_can_raw!r}: bad bit_len={bit_len}"
            )

        p_can = _p_canonical_of_bits(p_can_raw)
        if p_can not in p_to_np_map and n_in > 4:
            print(f"[infer] NPN: skip {cname!r} (n_inputs={n_in} > 4, "
                  f"outside npn_p_class_enum's supported range)")
            continue
        if p_can in p_to_np_map:
            np_, ei, eo = p_to_np_map[p_can]
            is_rep = 1.0 if p_can == np_ else 0.0
            sz_norm = np_class_size.get(np_, 1) / max_size
            npn_per_cell[cname] = (is_rep, float(ei), float(eo), sz_norm)
        elif p_can == "0b10":
            npn_per_cell[cname] = (0.0, 0.0, 1.0, 2.0 / max_size)
        elif n_in >= 2:
            width = 1 << n_in
            try:
                v = int(p_can, 2)
            except ValueError:
                raise ValueError(
                    f"NPN: cell {cname!r} canonical={p_can!r} is not valid binary."
                )
            inv_can = _p_canonical_of_bits(
                "0b" + bin(((1 << width) - 1) ^ v)[2:].zfill(width)
            )
            if inv_can in p_to_np_map:
                np_, ei, eo = p_to_np_map[inv_can]
                is_rep = 1.0 if inv_can == np_ else 0.0
                sz_norm = np_class_size.get(np_, 1) / max_size
                npn_per_cell[cname] = (is_rep, float(ei), int(eo) + 1, sz_norm)
                print(f"[infer] NPN bit-invert fallback: {cname!r} {p_can!r} -> {inv_can!r}")
            else:
                raise ValueError(
                    f"NPN: cell {cname!r} canonical={p_can!r} and bit-inverse "
                    f"{inv_can!r} both missing from p_to_np_map."
                )
        else:
            raise ValueError(
                f"NPN: cell {cname!r} canonical={p_can!r} n_inputs={n_in} unhandled."
            )
    print(f"[infer] NPN features: {len(npn_per_cell)}/{len(l0_features)} cells "
          f"(max NP-class size = {max_size})")
    return npn_per_cell

def _pmin_bits(canon: str) -> str:
    if not canon:
        return ""
    return _p_canonical_of_bits(canon)[2:]

def _read_a_lib(payload_dir: Path, l0_universe: set[str],
                resolver: "CellNameResolver") -> set[str]:
    only_use = payload_dir / "filters" / "only_use_lib_cells.list"
    if only_use.exists():
        raw_cells = {ln.strip() for ln in only_use.read_text().splitlines()
                     if ln.strip() and not ln.strip().startswith("#")}
        classes = _load_cell_classes()
        cells: set[str] = set()
        dropped_nonlogic: list[str] = []
        for name in sorted(raw_cells):
            if name in l0_universe:
                cells.add(name)
                continue
            entry = classes.get(name)
            if entry is not None and entry[0] in ("buf_inv", "seq", "const"):
                dropped_nonlogic.append(name)
                continue
            resolved = resolver.resolve(name)
            if resolved != name and resolved in l0_universe:
                print(f"[infer] A_lib: {name!r} resolved to vocab twin "
                      f"{resolved!r} via (canonical, drive).")
                cells.add(resolved)
                continue
            raise ValueError(
                f"only_use_lib_cells.list: {name!r} is a logic cell (or unknown "
                "to the lib class map) with no vocab match by name or by "
                "(canonical, drive) — refusing to silently drop it from A_lib."
            )
        if dropped_nonlogic:
            print(f"[infer] A_lib: {len(dropped_nonlogic)} non-logic cells dropped "
                  f"(lib-verified buf_inv/seq/const): {dropped_nonlogic}")
        if not cells:
            raise ValueError(
                f"only_use_lib_cells.list: ALL cells ({sorted(raw_cells)}) are "
                "absent from features.csv. Check features.csv path."
            )
        return cells
    ck = payload_dir / "filters" / "canonical_keys.txt"
    if ck.exists():
        text = ck.read_text()
        if "placeholder" in text.lower():
            raise ValueError(
                f"{ck}: placeholder detected — A_lib cannot be determined. "
                "Run step1_convert.sh first to populate filters/."
            )
        keys = {ln.strip() for ln in text.splitlines()
                if ln.strip() and not ln.strip().startswith("#")}
        cells = {n for n in l0_universe
                 if _canonical_of_raw.get(n) in keys}
        if not cells:
            raise ValueError(
                f"{ck}: no L0 cells matched canonical keys {keys}."
            )
        return cells
    raise ValueError(
        f"{payload_dir}/filters/: no only_use_lib_cells.list or "
        "canonical_keys.txt found — cannot determine A_lib."
    )

def _select_candidates(
    l0_features_raw: dict[str, dict],
    a_lib: set[str],
    mining_rank: dict[str, float],
    canonical_of: dict[str, str],
    n_inputs_min: int,
    n_inputs_max: int,
    top_k: int,
) -> list[str]:
    candidates = []
    for name, feats in l0_features_raw.items():
        if name in a_lib:
            continue
        if _is_buf_or_seq(name):
            continue
        if not name.endswith("_X1"):
            continue
        n_in_str = feats.get("n_inputs", "")
        try:
            n_in = int(float(n_in_str))
        except (TypeError, ValueError):
            raise ValueError(
                f"Cell {name!r}: n_inputs={n_in_str!r} is not a valid integer."
            )
        if not (n_inputs_min <= n_in <= n_inputs_max):
            continue
        canon = canonical_of.get(name)
        if canon is None:
            raise ValueError(f"Cell {name!r} has no canonical in features.csv.")
        canon_pmin = _p_canonical_of_bits(canon)
        if canon_pmin not in mining_rank:
            print(f"  [skip] {name!r} canonical={canon!r} (perm-min {canon_pmin!r}) "
                  "not in mining_node_rank — function not detected in this "
                  "design's netlist; excluded from candidates.",
                  file=sys.stderr)
            continue
        rank = mining_rank[canon_pmin]
        candidates.append((name, rank))

    if not candidates:
        raise ValueError(
            f"No candidate cells found for n_inputs in [{n_inputs_min},{n_inputs_max}] "
            f"outside A_lib={sorted(a_lib)}. Check features.csv and n_inputs filter."
        )

    candidates.sort(key=lambda x: x[1])
    selected = [name for name, _ in candidates[:top_k]]
    print(f"[infer] {len(candidates)} candidates (n_inputs {n_inputs_min}-{n_inputs_max}), "
          f"selecting top {top_k}:")
    for name, rank in candidates[:top_k]:
        print(f"  {name:30s}  rank_norm={rank:.4f}")
    return selected

def _build_infer_graph(
    payload_dir: Path,
    a_lib: set[str],
    combo: tuple[str, ...],
    l0_features_zscored: dict[str, dict],
    l0_features_raw: dict[str, dict],
    canonical_of: dict[str, str],
    npn_per_cell: dict[str, tuple],
    proxy_delay: dict[str, float],
    design_mean: torch.Tensor,
    design_std: torch.Tensor,
    mining_edges: Optional[dict],
    mining_rank: Optional[dict[str, float]],
    overlap: Optional[dict],
    cell_counts: dict[str, float],
    timing_feats: Optional[dict[str, dict]],
    usage_rank: dict[str, float],
    p_to_np_map: dict[str, tuple],
) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor, list[str], set]:
    oov_combo = [c for c in combo if c not in canonical_of]
    if oov_combo:
        raise ValueError(
            f"_build_infer_graph: {len(oov_combo)} combo cells not in "
            f"features_l0 vocab (canonical_of): {sorted(oov_combo)}. "
            f"Add via extract.sh --cells-csv or drop from combo."
        )
    combo_canonicals = {canonical_of[c] for c in combo}
    combo_expanded = {
        name for name in l0_features_zscored
        if canonical_of.get(name) in combo_canonicals
        and not _is_buf_or_seq(name)
    }
    B_lib = a_lib | combo_expanded
    all_feats = l0_features_zscored

    union_mask = {
        name: (
            (_cell_count_lookup(cell_counts, name) > 0 or name in B_lib)
            and not _is_buf_or_seq(name)
            and name in all_feats
        )
        for name in all_feats
    }

    g = build_graph(
        all_cell_features    = all_feats,
        lib_mask             = union_mask,
        mining_edge_features = mining_edges,
        p_to_np_map          = p_to_np_map,
        overlap_ratio        = overlap,
    )
    node_keys: list[str] = g["node_keys"]

    total_logic = sum(
        v for n, v in cell_counts.items()
        if not _is_buf_or_seq(n) and v > 0
    )
    if total_logic <= 0:
        raise ValueError(
            f"cell_counts in {payload_dir}: no logic cells found (total=0)."
        )
    cell_fracs = {n: v / total_logic for n, v in cell_counts.items() if v > 0}

    role_rows:   list[list[float]] = []
    design_rows: list[list[float]] = []
    loss_mask_list: list[float]    = []

    for name in node_keys:
        in_b = name in B_lib
        role_rows.append(_role_onehot(
            name,
            in_a_lib = name in a_lib,
            in_b_lib = in_b,
            count_a  = _cell_count_lookup(cell_counts, name),
            context  = (f"payload={payload_dir}  "
                        f"a_lib={sorted(a_lib)[:5]}... (lib_subset error or "
                        "only_use_lib_cells.list mismatch)"),
        ))

        loss_mask_list.append(1.0 if in_b else 0.0)

        pd_val = proxy_delay.get(name, 0.0)
        ur_val = usage_rank.get(name, 0.0)
        _extra_kwargs: dict = {}
        if design_mean.shape[0] >= 22:
            _extra_kwargs["proxy_delay"] = pd_val
        if design_mean.shape[0] >= 23:
            _extra_kwargs["usage_rank"] = ur_val
        design_rows.append(_design_feat_row(
            name, cell_fracs, timing_feats,
            mining_rank, canonical_of,
            context=f"payload={payload_dir}",
            **_extra_kwargs,
        ))

    x_base   = g["x"]
    x_design = torch.tensor(design_rows, dtype=torch.float)
    x_design = (x_design - design_mean) / design_std
    x_role   = torch.tensor(role_rows,   dtype=torch.float)

    npn_rows: list[list[float]] = []
    for name in node_keys:
        v = npn_per_cell.get(name)
        if v is None:
            raise ValueError(
                f"NPN block: node {name!r} not in npn_per_cell. "
                "Cell is in graph but missing from L0 NPN map."
            )
        npn_rows.append(list(v))
    x_npn = torch.tensor(npn_rows, dtype=torch.float)

    x_full = torch.cat([x_base, x_design, x_role, x_npn], dim=1)
    loss_mask = torch.tensor(loss_mask_list, dtype=torch.float)

    return x_full, g["edge_index"], g["edge_attr"], loss_mask, node_keys, combo_expanded

def main() -> None:
    p = argparse.ArgumentParser(
        description="Infer demand for C(K,3) cell combinations using DiffDemandGNN."
    )
    p.add_argument("--payload-dir",  required=True,
                   help="L0 payload run dir (cell_counts.csv, mining_*.csv, etc.).")
    p.add_argument("--model-ckpt",   required=True,
                   help="DiffDemandGNN checkpoint (model_full.pt).")
    p.add_argument("--norm-stats",   default=None,
                   help="Norm-stats JSON (auto-inferred from model-ckpt stem if omitted).")
    p.add_argument("--l0",           required=True,
                   help="features.csv (full L0 SO3 library).")
    p.add_argument("--p-to-np",      default=None,
                   help="global_p_to_np.csv for NPN features. "
                        "If omitted, computed on-the-fly from features.csv.")
    p.add_argument("--n-inputs",     type=int, nargs=2, default=[2, 3],
                   metavar=("MIN", "MAX"),
                   help="n_inputs range for candidate filtering (inclusive).")
    p.add_argument("--top-k",        type=int, default=10,
                   help="Number of top candidate cells to select.")
    p.add_argument("--n-combo",      type=int, default=3,
                   help="Cells per combination (C(top-k, n-combo)).")
    p.add_argument("--area-gain",    default=None,
                   help="area_gain.csv from area_gain.py. When provided, results are "
                        "sorted by frac_x_gain = sum(pred_i * area_gain_i) instead of "
                        "pred_area_weighted. Cells with no entry in area_gain.csv "
                        "contribute 0 (no replacement savings for this design).")
    p.add_argument("--out",          required=True,
                   help="Output CSV path for ranked results.")
    args = p.parse_args()

    payload_dir  = Path(args.payload_dir)
    model_ckpt   = Path(args.model_ckpt)
    features_l0  = Path(args.l0)
    p_to_np_path = Path(args.p_to_np) if args.p_to_np else None
    out_path     = Path(args.out)
    n_inputs_min, n_inputs_max = args.n_inputs[0], args.n_inputs[1]

    for path, label in [
        (payload_dir, "--payload-dir"),
        (model_ckpt,  "--model-ckpt"),
        (features_l0, "--l0"),
    ]:
        if not path.exists():
            raise FileNotFoundError(f"{label} does not exist: {path}")
    if p_to_np_path and not p_to_np_path.exists():
        raise FileNotFoundError(f"--p-to-np does not exist: {p_to_np_path}")

    norm_stats_path = Path(args.norm_stats) if args.norm_stats else None

    area_gain_map: dict = {}
    _drive_re = re.compile(r"_X([1248])(?:_|$)")
    if args.area_gain:
        area_gain_path = Path(args.area_gain)
        if not area_gain_path.exists():
            raise FileNotFoundError(f"--area-gain does not exist: {area_gain_path}")
        with open(area_gain_path) as _fh:
            for _row in csv.DictReader(_fh):
                _val = _row.get("area_gain", "")
                if _val and _val not in ("", "(none)"):
                    _key = _pmin_bits(_row["canonical_key"])
                    _m = _drive_re.search(_row["b_cell"])
                    if not _m:
                        raise ValueError(
                            f"area_gain.csv b_cell {_row['b_cell']!r} has no _X<d> "
                            "drive token — cannot assign a per-drive area gain."
                        )
                    area_gain_map[(_key, f"X{_m.group(1)}")] = float(_val)
        print(f"[infer] Loaded area_gain for {len(area_gain_map)} (canonical, drive) "
              f"pairs from {area_gain_path}")

    _ckpt_raw = torch.load(str(model_ckpt), map_location="cpu", weights_only=False)
    _embedded_stats = _ckpt_raw.get("norm_stats") if isinstance(_ckpt_raw, dict) else None

    if _embedded_stats is not None:
        norm_stats_raw = _embedded_stats
        print(f"[infer] norm_stats loaded from inside {model_ckpt.name}.")
    elif norm_stats_path and norm_stats_path.exists():
        print(f"[infer] Loading norm-stats from {norm_stats_path} (legacy JSON).")
        with open(norm_stats_path) as fh:
            norm_stats_raw = json.load(fh)
    else:
        _legacy = model_ckpt.parent / (model_ckpt.stem + ".norm_stats.json")
        if _legacy.exists():
            print(f"[infer] Loading norm-stats from {_legacy} (legacy JSON).")
            with open(_legacy) as fh:
                norm_stats_raw = json.load(fh)
        else:
            raise FileNotFoundError(
                f"norm_stats not found in {model_ckpt.name} and no legacy "
                f".norm_stats.json alongside it. Re-train with the current code."
            )

    design_mean_list = norm_stats_raw.get("__design_mean__")
    design_std_list  = norm_stats_raw.get("__design_std__")
    if design_mean_list is None or design_std_list is None:
        raise ValueError(
            "norm_stats missing __design_mean__ or __design_std__ keys. "
            "Re-train with the current code."
        )
    design_mean = torch.tensor(design_mean_list, dtype=torch.float)
    design_std  = torch.tensor(design_std_list,  dtype=torch.float)
    cell_norm_stats = {
        k: v for k, v in norm_stats_raw.items()
        if not k.startswith("__")
    }
    print(f"[infer] design_mean/std dim: {design_mean.shape[0]}")

    print(f"[infer] Loading features_l0 from {features_l0}")
    l0_features_raw = load_features_csv(str(features_l0))
    cpp_min_raw      = {n: float(f.get("cpp_min",     1.0) or 1.0) for n, f in l0_features_raw.items()}
    _missing_ds = [n for n, f in l0_features_raw.items() if "cpp_min_ds" not in f]
    if _missing_ds:
        raise ValueError(
            f"features_l0 {features_l0} lacks cpp_min_ds for {len(_missing_ds)} cells "
            f"(e.g. {_missing_ds[:3]}) — regenerate features.csv with the per-drive "
            "extract_features; X2 candidates would be priced at X1 width otherwise."
        )
    cpp_ds_raw       = {n: float(f["cpp_min_ds"]) for n, f in l0_features_raw.items()}
    max_stack_n_raw  = {n: float(f.get("max_stack_n", 0.0) or 0.0) for n, f in l0_features_raw.items()}
    max_stack_p_raw  = {n: float(f.get("max_stack_p", 0.0) or 0.0) for n, f in l0_features_raw.items()}
    proxy_delay = {
        name: (max_stack_n_raw[name] + max_stack_p_raw[name]) * cpp_min_raw[name]
        for name in l0_features_raw
    }
    canonical_of_raw = {n: f.get("canonical", "") for n, f in l0_features_raw.items()}

    l0_features_zscored = load_features_csv(str(features_l0))
    apply_norm_stats(l0_features_zscored, cell_norm_stats)
    print(f"[infer] {len(l0_features_zscored)} L0 cells loaded and z-scored.")

    l0_universe = set(l0_features_zscored.keys())

    if p_to_np_path:
        print(f"[infer] Loading p_to_np from {p_to_np_path}")
        p_to_np_map = _read_p_to_np_map(str(p_to_np_path))
        if not p_to_np_map:
            raise ValueError(
                f"{p_to_np_path}: _read_p_to_np_map returned empty dict. "
                "Check column names: expected p_canonical, np_canonical."
            )
        print(f"[infer] p_to_np_map: {len(p_to_np_map)} P-class entries (from CSV).")
    else:
        print("[infer] No --p-to-np CSV; computing NPN map from features_l0 ...")
        p_to_np_map = _compute_p_to_np_map(l0_features_raw)
        print(f"[infer] p_to_np_map: {len(p_to_np_map)} unique canonicals (computed).")

    npn_per_cell = _build_npn_per_cell(l0_features_raw, p_to_np_map)

    print(f"[infer] Loading payload from {payload_dir}")
    resolver = CellNameResolver(l0_features_raw)
    cell_counts = _read_cell_counts(payload_dir)
    if cell_counts is None:
        has_cf = _read_canonical_freq(payload_dir) is not None
        raise ValueError(
            f"{payload_dir}: cell_counts.csv is missing or unparseable"
            + (" (canonical_freq.csv is present but deriving per-cell counts "
               "from it is an unsupported configuration for inference)"
               if has_cf else " and canonical_freq.csv is absent")
            + ". Regenerate the payload with cell_counts.csv "
              "(fc_validate_extract.py / step1_convert.sh)."
        )
    cell_counts = _remap_dict_keys(cell_counts, resolver.resolve)
    print(f"[infer] cell_counts: {len(cell_counts)} cells (resolver applied).")

    n_prev = sum(v for n, v in cell_counts.items() if not _is_buf_or_seq(n) and v > 0)
    if n_prev <= 0:
        raise ValueError(
            f"{payload_dir}/cell_counts.csv: total logic cell count = 0."
        )
    print(f"[infer] n_prev = {n_prev:.0f} (total logic cells in A synthesis)")

    timing_feats = _read_cell_timing_features(payload_dir)
    if timing_feats is None:
        raise ValueError(
            f"{payload_dir}/cell_timing_features.csv: missing or unreadable. "
            "Run step1_convert.sh first."
        )
    timing_feats = _remap_dict_keys(timing_feats, resolver.resolve)
    print(f"[infer] timing_feats: {len(timing_feats)} cells (resolver applied).")

    mining_edges = _read_mining_edge_features(payload_dir)
    if mining_edges is None:
        raise ValueError(
            f"{payload_dir}/mining_edge_features.csv: missing. Run step3 first."
        )
    mining_edges = {
        (c, resolver.resolve(mc)): v
        for (c, mc), v in mining_edges.items()
    }
    print(f"[infer] mining_edges: {len(mining_edges)} entries (resolver applied).")

    mining_rank = _read_mining_node_rank(payload_dir)
    if mining_rank is None:
        raise ValueError(
            f"{payload_dir}/mining_node_rank.csv: missing. Run step3 first."
        )

    overlap = _read_overlap_ratio(payload_dir)
    if overlap is None:
        raise ValueError(
            f"{payload_dir}/mining_overlap_ratio.csv: missing. Run step3 first."
        )

    usage_rank = DiffPairDataset._build_usage_rank(cell_counts)

    global _canonical_of_raw
    _canonical_of_raw = canonical_of_raw

    a_lib = _read_a_lib(payload_dir, l0_universe, resolver)
    print(f"[infer] A_lib: {len(a_lib)} cells — {sorted(a_lib)}")

    candidates = _select_candidates(
        l0_features_raw, a_lib, mining_rank,
        canonical_of_raw, n_inputs_min, n_inputs_max, args.top_k,
    )
    if len(candidates) < args.n_combo:
        raise ValueError(
            f"Only {len(candidates)} candidates found but n_combo={args.n_combo}. "
            "Lower --top-k, widen --n-inputs, or reduce --n-combo."
        )

    combos = list(itertools.combinations(candidates, args.n_combo))
    n_combos = len(combos)
    print(f"[infer] C({len(candidates)},{args.n_combo}) = {n_combos} combinations.")

    print(f"[infer] Loading model from {model_ckpt}")
    state_dict = _ckpt_raw["model"] if isinstance(_ckpt_raw, dict) and "model" in _ckpt_raw else _ckpt_raw
    state_dict = {(k[len("_orig_mod."):] if k.startswith("_orig_mod.") else k): v
                  for k, v in state_dict.items()}
    in_ch = state_dict["in_proj.weight"].shape[1]
    print(f"[infer] in_channels (auto-detected) = {in_ch}")

    model = DiffDemandGNN(in_channels=in_ch)
    model.load_state_dict(state_dict)
    model.eval()

    results = []
    for idx, combo in enumerate(combos):
        if idx % 20 == 0:
            print(f"[infer]   combo {idx+1}/{n_combos} ...", flush=True)

        x_full, edge_index, edge_attr, loss_mask, node_keys, combo_expanded = _build_infer_graph(
            payload_dir        = payload_dir,
            a_lib              = a_lib,
            combo              = combo,
            l0_features_zscored= l0_features_zscored,
            l0_features_raw    = l0_features_raw,
            canonical_of       = canonical_of_raw,
            npn_per_cell       = npn_per_cell,
            proxy_delay        = proxy_delay,
            design_mean        = design_mean,
            design_std         = design_std,
            mining_edges       = mining_edges,
            mining_rank        = mining_rank,
            overlap            = overlap,
            cell_counts        = cell_counts,
            timing_feats       = timing_feats,
            usage_rank         = usage_rank,
            p_to_np_map        = p_to_np_map,
        )

        batch = torch.zeros(x_full.shape[0], dtype=torch.long)

        with torch.no_grad():
            pred = model(
                x          = x_full,
                edge_index = edge_index,
                edge_attr  = edge_attr,
                batch      = batch,
                loss_mask  = loss_mask,
            )

        pred_np = pred.cpu().numpy()
        name_to_pred = {n: float(pred_np[i]) for i, n in enumerate(node_keys)}

        combo_set = set(combo)
        x2_map: dict[str, str] = {}
        x2_candidates = sorted(
            combo_expanded - combo_set,
            key=lambda n: (0 if n.endswith("_X2") else 1, n),
        )
        for name in x2_candidates:
            canon = _pmin_bits(canonical_of_raw.get(name, ""))
            if canon and canon not in x2_map:
                x2_map[canon] = name

        for _c in combo:
            if _c not in name_to_pred:
                raise ValueError(
                    f"Combo cell {_c!r} has no prediction in name_to_pred — "
                    "it should always be in node_keys; check _build_infer_graph."
                )
        cell_preds_x1 = [name_to_pred[c] for c in combo]
        cell_names_x2 = [x2_map.get(_pmin_bits(canonical_of_raw.get(c, "")), "")
                         for c in combo]
        for _n in cell_names_x2:
            if _n and _n not in name_to_pred:
                raise ValueError(
                    f"X2 cell {_n!r} not in name_to_pred — it should be in combo_expanded "
                    "and thus in node_keys; check _build_infer_graph expansion."
                )
        cell_preds_x2 = [name_to_pred[n] if n else 0.0 for n in cell_names_x2]

        added_total = sum(cell_preds_x1) + sum(cell_preds_x2)
        area_weighted = sum(
            cell_preds_x1[i] * (cpp_ds_raw[combo[i]] + 1.0)
            + (cell_preds_x2[i] * (cpp_ds_raw[cell_names_x2[i]] + 1.0)
               if cell_names_x2[i] else 0.0)
            for i in range(len(combo))
        )
        if area_gain_map:
            _combo_area_gain_ok = True
            for _i, _c in enumerate(combo):
                _canon_key = _pmin_bits(canonical_of_raw.get(_c, ""))
                if (_canon_key, "X1") not in area_gain_map:
                    print(f"[infer] skip combo {idx+1}/{n_combos}: {_c!r} "
                          f"canonical_key[2:]={_canon_key!r} has no (canonical, X1) "
                          f"entry in area_gain_map (likely an UNCLASSIFIED_FALLBACK "
                          f"canonical mismatch between drive-strength siblings)")
                    _combo_area_gain_ok = False
                    break
                if cell_names_x2[_i] and (_canon_key, "X2") not in area_gain_map:
                    print(f"[infer] skip combo {idx+1}/{n_combos}: {_c!r} has X2 twin "
                          f"{cell_names_x2[_i]!r} but no (canonical, X2) entry in "
                          f"area_gain_map")
                    _combo_area_gain_ok = False
                    break
            if not _combo_area_gain_ok:
                continue
        frac_x_gain = sum(
            cell_preds_x1[i]
            * area_gain_map[(_pmin_bits(canonical_of_raw.get(combo[i], "")), "X1")]
            + (cell_preds_x2[i]
               * area_gain_map[(_pmin_bits(canonical_of_raw.get(combo[i], "")), "X2")]
               if cell_names_x2[i] else 0.0)
            for i in range(len(combo))
        ) if area_gain_map else 0.0

        results.append({
            "combo":          combo,
            "added_total":    added_total,
            "area_weighted":  area_weighted,
            "frac_x_gain":    frac_x_gain,
            "cell_preds_x1":  cell_preds_x1,
            "cell_names_x2":  cell_names_x2,
            "cell_preds_x2":  cell_preds_x2,
            "name_to_pred":   name_to_pred,
        })

    if not results:
        raise ValueError(
            f"All {n_combos} combo(s) were skipped (see '[infer] skip combo' lines "
            "above) — no candidate survived area_gain_map coverage. Nothing to write."
        )
    if area_gain_map:
        results.sort(key=lambda r: -r["frac_x_gain"])
        sort_key_name = "frac_x_gain"
    else:
        results.sort(key=lambda r: -r["area_weighted"])
        sort_key_name = "pred_area_weighted"
    print(f"[infer] Sorted by: {sort_key_name}")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", newline="") as fh:
        cell_cols   = [f"cell_{i+1}"        for i in range(args.n_combo)]
        x2_cols     = [f"cell_{i+1}_x2"     for i in range(args.n_combo)]
        pred_cols   = [f"pred_cell_{i+1}"   for i in range(args.n_combo)]
        pred_x2_cols= [f"pred_cell_{i+1}_x2" for i in range(args.n_combo)]
        area_cols   = [f"cpp_min_{i+1}"     for i in range(args.n_combo)]
        canon_cols  = [f"canon_{i+1}"       for i in range(args.n_combo)]
        writer = csv.writer(fh)
        writer.writerow(
            ["rank"] + cell_cols + x2_cols
            + ["pred_added_frac_sum", "pred_area_weighted", "pred_frac_x_gain"]
            + pred_cols + pred_x2_cols + area_cols + canon_cols
        )
        for rank, r in enumerate(results, start=1):
            canons     = [canonical_of_raw.get(c, "") for c in r["combo"]]
            cell_areas = [cpp_min_raw[c] for c in r["combo"]]
            writer.writerow(
                [rank]
                + list(r["combo"])
                + r["cell_names_x2"]
                + [f"{r['added_total']:.6f}",
                   f"{r['area_weighted']:.6f}",
                   f"{r['frac_x_gain']:.6f}"]
                + [f"{v:.6f}" for v in r["cell_preds_x1"]]
                + [f"{v:.6f}" for v in r["cell_preds_x2"]]
                + [f"{v:.4f}" for v in cell_areas]
                + canons
            )

    print(f"\n[infer] Done. {n_combos} combos ranked -> {out_path}")
    print(f"[infer] Top-3 combinations (sorted by {sort_key_name}):")
    for r in results[:3]:
        cells_str = " + ".join(r["combo"])
        print(f"  frac_x_gain={r['frac_x_gain']:.4f}  area_weighted={r['area_weighted']:.4f}  "
              f"frac_sum={r['added_total']:.4f}  cells=[{cells_str}]")

if __name__ == "__main__":
    main()
