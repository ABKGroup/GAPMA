from __future__ import annotations

import csv
import itertools
import os
import subprocess
import sys
from typing import Optional

import numpy as np
import torch

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

_REQUIRED_COLS = {
    "canonical", "cell_name",
    "n_inputs", "tt_weight", "anf_degree", "is_affine", "is_symmetric",
    "is_monotone", "is_anti_monotone", "output_inv_count", "input_inv_count",
    "fet_count",
    "n_nmos", "n_pmos", "nfin_n_total", "nfin_p_total", "nfin_ratio",
    "max_stack_n", "max_stack_p", "n_internal_nets", "total_transistors",
    "cpp_min",
    "cell_fraction",
}

CDL_FEATURE_NAMES = [
    "n_inputs", "n_nmos", "n_pmos", "nfin_n_total", "nfin_p_total",
    "nfin_ratio", "max_stack_n", "max_stack_p",
    "cpp_min",
]
TT_FEATURE_NAMES = [
    "tt_weight", "anf_degree", "is_affine", "is_symmetric",
    "is_monotone", "is_anti_monotone", "output_inv_count",
    "input_inv_count", "fet_count",
]
NODE_FEATURE_NAMES = CDL_FEATURE_NAMES + TT_FEATURE_NAMES

def load_features_csv(path: str) -> dict[str, dict]:
    if not os.path.isfile(path):
        raise FileNotFoundError(f"features CSV not found: {path!r}")

    cells: dict[str, dict] = {}
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        cols = set(reader.fieldnames or [])
        missing = _REQUIRED_COLS - cols
        if missing:
            raise ValueError(
                f"features CSV {path!r} is missing required columns: "
                f"{sorted(missing)}.  "
                "Regenerate with the current version of extract.sh."
            )
        for row in reader:
            raw_name = row["cell_name"].strip()
            if not raw_name:
                continue
            name = raw_name
            parsed: dict = {
                "canonical":       row["canonical"].strip(),
                "cell_name":       name,
                "logic_expr":      row.get("logic_expr", "").strip(),
            }
            for col in NODE_FEATURE_NAMES:
                parsed[col] = float(row[col])
            parsed["cell_fraction"] = float(row["cell_fraction"])
            if "cpp_min_ds" in cols:
                parsed["cpp_min_ds"] = float(row["cpp_min_ds"])
            parsed["count"] = int(row.get("count", 0) or 0)
            if name in cells:
                raise ValueError(
                    f"[load_features_csv] duplicate cell name {name!r} in {path!r}. "
                    "Remove the duplicate row before training."
                )
            cells[name] = parsed
    return cells

def compute_norm_stats(cells: dict) -> dict[str, tuple[float, float]]:
    stats: dict[str, tuple[float, float]] = {}
    names = list(cells.keys())
    if not names:
        return {col: (0.0, 1.0) for col in NODE_FEATURE_NAMES}
    for col in NODE_FEATURE_NAMES:
        vals = [cells[n][col] for n in names]
        mean = sum(vals) / len(vals)
        std  = (sum((v - mean) ** 2 for v in vals) / len(vals)) ** 0.5
        stats[col] = (mean, std if std >= 1e-8 else 1.0)
    return stats

def apply_norm_stats(
    cells: dict,
    stats: dict[str, tuple[float, float]],
) -> None:
    for name in cells:
        for col, (mean, std) in stats.items():
            cells[name][col] = (cells[name][col] - mean) / std

def _n_inputs_from_key(key: str) -> int:
    bits = key[2:]
    n = 0
    while (1 << n) < len(bits):
        n += 1
    return n

def _delta_fet(
    base_fet: float, base_count: int, inv_count: int, target_fet: float
) -> Optional[float]:
    if base_fet < 0 or target_fet < 0:
        return None
    return max(0.0, base_count * base_fet + inv_count * 2.0 - target_fet)

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

def _p_canonical_of_bits(canon: str) -> str:
    bits = canon[2:] if canon.startswith("0b") else canon
    n = (len(bits)).bit_length() - 1
    if (1 << n) != len(bits):
        raise ValueError(f"canonical bit length not a power of 2: {canon!r}")
    return _p_canonical_str(int(bits, 2), n)

def _direct_pmnpn(canonical: str, npn_tool: str) -> tuple[str, int, int]:
    bits = canonical[2:] if canonical.startswith("0b") else canonical
    completed = subprocess.run(
        [npn_tool, "--bits", bits], check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    rows = list(csv.DictReader(completed.stdout.splitlines()))
    if len(rows) != 1:
        raise ValueError(
            f"direct PM-NPN tool returned {len(rows)} rows for {canonical!r}, expected 1. "
            f"stderr: {completed.stderr.strip()}"
        )
    row = rows[0]
    try:
        return (
            "0b" + row["np_canonical_bits"].strip(),
            int(row["erased_input_inv"]),
            int(row["erased_output_inv"]),
        )
    except (KeyError, ValueError) as e:
        raise ValueError(
            f"direct PM-NPN tool emitted malformed output for {canonical!r}: {row}"
        ) from e

def _bit_invert_canonical(p_can: str) -> Optional[str]:
    if not p_can.startswith("0b"):
        return None
    bits = p_can[2:]
    width = len(bits)
    if width == 0 or (width & (width - 1)) != 0:
        return None
    try:
        v = int(bits, 2)
    except ValueError:
        return None
    return "0b" + bin(((1 << width) - 1) ^ v)[2:].zfill(width)

def build_graph(
    all_cell_features: dict[str, dict],
    lib_mask: dict[str, bool],
    mining_edge_features: Optional[dict] = None,
    p_to_np_map: Optional[dict] = None,
    overlap_ratio: Optional[dict] = None,
    self_loops: bool = False,
) -> dict:
    if not all_cell_features:
        raise ValueError("all_cell_features is empty.")

    if not mining_edge_features:
        raise ValueError(
            "build_graph: mining_edge_features is empty/None — refusing to "
            "build a graph with zero-initialized edge features. Ensure the "
            "run dir has mining_edge_features.csv (rebuild via "
            "build_mining_edge_features.py)."
        )
    if not p_to_np_map:
        raise ValueError(
            "build_graph: p_to_np_map is empty/None — refusing to build a "
            "graph with zero-initialized is_npn_class/inv_in_diff/inv_out_diff. "
            "Provide direct PM-NPN results for every graph canonical."
        )
    if not overlap_ratio:
        raise ValueError(
            "build_graph: overlap_ratio is empty/None — refusing to build a "
            "graph with zero-initialized overlap_ratio. Ensure the run dir "
            "has mining_overlap_ratio.csv (rebuild via "
            "build_mining_edge_features.py)."
        )

    missing_mask = [n for n in all_cell_features if n not in lib_mask]
    if missing_mask:
        raise ValueError(
            f"lib_mask is missing entries for: {missing_mask[:10]}{'...' if len(missing_mask) > 10 else ''}. "
            "Provide a lib_mask covering every cell in all_cell_features."
        )

    node_keys = []
    excluded_high_input = []
    for name in all_cell_features:
        if not lib_mask[name]:
            continue
        n_inputs = _n_inputs_from_key(all_cell_features[name]["canonical"])
        if n_inputs > 4:
            excluded_high_input.append((name, n_inputs))
            continue
        node_keys.append(name)
    if excluded_high_input:
        print(
            "Excluded graph cells with more than four functional inputs: "
            + ", ".join(f"{name}({n_inputs})" for name, n_inputs in excluded_high_input)
        )

    feat_rows = []
    for name in node_keys:
        feats = all_cell_features[name]
        cdl_vec = [feats[fn] for fn in CDL_FEATURE_NAMES]
        tt_vec  = [feats[fn] for fn in TT_FEATURE_NAMES]
        feat_rows.append(cdl_vec + tt_vec)

    x = torch.tensor(feat_rows, dtype=torch.float)

    def _as_triple(v, extra_out_inv: int):
        if isinstance(v, tuple):
            np_, e_in, e_out = v
            return (np_, e_in, int(e_out) + extra_out_inv)
        return (v, 0, extra_out_inv)

    def _pnp(p, name):
        p_pmin = _p_canonical_of_bits(p)
        v = p_to_np_map.get(p_pmin)
        if v is not None:
            return _as_triple(v, 0)
        inv_can = _bit_invert_canonical(p_pmin)
        if inv_can is not None:
            inv_can = _p_canonical_of_bits(inv_can)
        if inv_can is not None and inv_can in p_to_np_map:
            return _as_triple(p_to_np_map[inv_can], 1)
        raise ValueError(
            f"build_graph: cell {name!r} canonical={p!r} (perm-min {p_pmin!r}) "
            f"is not in p_to_np_map and its bit-inverse {inv_can!r} is not "
            "either. Refusing to treat the raw P-canonical as its own NPN "
            "class, which would silently make this node NPN-equivalent to "
            "nothing. Rebuild global_p_to_np.csv so it covers every cell in "
            "features.csv."
        )
    node_canonical = [all_cell_features[n]["canonical"] for n in node_keys]
    node_npn_info  = [_pnp(c, n) for c, n in zip(node_canonical, node_keys)]
    node_npn       = [t[0] for t in node_npn_info]

    n_nodes = len(node_keys)

    idx = np.arange(n_nodes, dtype=np.int32)
    src_arr, dst_arr = np.meshgrid(idx, idx, indexing="ij")
    src_arr = src_arr.ravel()
    dst_arr = dst_arr.ravel()

    if not self_loops:
        mask = src_arr != dst_arr
        src_arr = src_arr[mask]
        dst_arr = dst_arr[mask]

    E = len(src_arr)

    canon_arr = np.array(node_canonical)
    same_can  = (canon_arr[src_arr] == canon_arr[dst_arr]).astype(np.float32)

    npn_arr   = np.array(node_npn)
    same_npn  = (npn_arr[src_arr]   == npn_arr[dst_arr]).astype(np.float32)

    m1_mat = np.zeros((n_nodes, n_nodes), dtype=np.float32)
    m2_mat = np.zeros((n_nodes, n_nodes), dtype=np.float32)
    canon_to_dsts: dict[str, list[int]] = {}
    for di, dc in enumerate(node_canonical):
        canon_to_dsts.setdefault(_p_canonical_of_bits(dc), []).append(di)
    cell_to_src: dict[str, int] = {name: si for si, name in enumerate(node_keys)}
    for (dc, src_cell), (v1, v2) in mining_edge_features.items():
        si = cell_to_src.get(src_cell)
        if si is None:
            continue
        for di in canon_to_dsts.get(dc, []):
            m1_mat[si, di] = v1
            m2_mat[si, di] = v2
    m1_arr = m1_mat[src_arr, dst_arr]
    m2_arr = m2_mat[src_arr, dst_arr]

    ovr_mat = np.zeros((n_nodes, n_nodes), dtype=np.float32)
    for (sc, dc), v in overlap_ratio.items():
        for si in canon_to_dsts.get(sc, []):
            for di in canon_to_dsts.get(dc, []):
                ovr_mat[si, di] = float(v)
    ovr_arr = ovr_mat[src_arr, dst_arr]

    ein_arr  = np.array([t[1] for t in node_npn_info], dtype=np.float32)
    eout_arr = np.array([t[2] for t in node_npn_info], dtype=np.float32)
    inv_in_diff  = np.where(same_npn.astype(bool),
                            np.abs(ein_arr[src_arr]  - ein_arr[dst_arr]),
                            0.0).astype(np.float32)
    inv_out_diff = np.where(same_npn.astype(bool),
                            np.abs(eout_arr[src_arr] - eout_arr[dst_arr]),
                            0.0).astype(np.float32)

    keep = (same_npn.astype(bool)
            | (m1_arr  != 0)
            | (m2_arr  != 0)
            | (ovr_arr != 0))
    src_arr      = src_arr[keep]
    dst_arr      = dst_arr[keep]
    same_can     = same_can[keep]
    same_npn     = same_npn[keep]
    m1_arr       = m1_arr[keep]
    m2_arr       = m2_arr[keep]
    ovr_arr      = ovr_arr[keep]
    inv_in_diff  = inv_in_diff[keep]
    inv_out_diff = inv_out_diff[keep]

    edge_index = torch.from_numpy(np.stack([src_arr, dst_arr], axis=0).astype(np.int64))
    edge_attr  = torch.from_numpy(
        np.stack([same_can, same_npn, m1_arr, m2_arr, ovr_arr,
                  inv_in_diff, inv_out_diff], axis=1)
    )

    return {
        "node_keys":  node_keys,
        "x":          x,
        "edge_index": edge_index,
        "edge_attr":  edge_attr,
    }

def graph_summary(g: dict) -> str:
    N = g["x"].shape[0]
    E = g["edge_index"].shape[1]
    lines = [
        f"nodes : {N}",
        f"edges : {E}  (directed)",
        f"x     : {tuple(g['x'].shape)}  (CDL 10 + TT 9; +19 design feats + 3 role flags in dataset)",
        f"e_attr: {tuple(g['edge_attr'].shape)}  "
        f"[is_same_canonical, is_npn_class, cluster_presence, avg_instances, overlap_ratio, inv_in_diff, inv_out_diff]",
    ]
    if E > 0:
        in_deg  = torch.zeros(N, dtype=torch.long)
        out_deg = torch.zeros(N, dtype=torch.long)
        in_deg.scatter_add_(0,  g["edge_index"][1], torch.ones(E, dtype=torch.long))
        out_deg.scatter_add_(0, g["edge_index"][0], torch.ones(E, dtype=torch.long))
        lines += [
            f"in-deg : min={in_deg.min().item()}  max={in_deg.max().item()}  "
            f"mean={in_deg.float().mean().item():.2f}",
            f"out-deg: min={out_deg.min().item()}  max={out_deg.max().item()}  "
            f"mean={out_deg.float().mean().item():.2f}",
        ]
    return "\n".join(lines)

if __name__ == "__main__":
    import argparse

    p = argparse.ArgumentParser(
        description=(
            "Build and inspect the cell substitution graph over L0 ∪ C. "
            "Reads node features from features.csv (L0 base library) "
            "and features.csv for mining candidates, both produced by extract.sh."
        )
    )
    p.add_argument("--l0",           required=True,
                   help="features.csv (L0 library) from: extract.sh --cells-csv cells.csv")
    p.add_argument("--features",     required=True,
                   help="features.csv (mining candidates) from: extract.sh --mining-dir <dir>")
    p.add_argument("--lib-cells",    default=None,
                   help="Optional: file with one active cell_name per line "
                        "(included as graph nodes).  If omitted, all cells are active.")
    p.add_argument("--mining-edge-features", required=True,
                   help="mining_edge_features.csv produced by "
                        "build_mining_edge_features.py.")
    p.add_argument("--mining-overlap-ratio", required=True,
                   help="mining_overlap_ratio.csv produced by "
                        "build_mining_edge_features.py.")
    default_npn_tool = os.path.join(
        os.path.dirname(_HERE), "..", "src", "logic_cluster_mining", "build", "npn_p_class_enum"
    )
    p.add_argument("--npn-tool", default=default_npn_tool,
                   help="direct PM-NPN executable built from logic_cluster_mining.")
    args = p.parse_args()
    if not os.path.isfile(args.npn_tool) or not os.access(args.npn_tool, os.X_OK):
        raise FileNotFoundError(
            f"direct PM-NPN executable is not runnable: {args.npn_tool!r}. Run tools/mining/build.sh first."
        )

    l0_feats   = load_features_csv(args.l0)
    cand_feats = load_features_csv(args.features)
    all_feats  = {**l0_feats, **cand_feats}
    print(f"Loaded {len(l0_feats)} L0 cells + {len(cand_feats)} candidates "
          f"= {len(all_feats)} total nodes.")

    if args.lib_cells:
        with open(args.lib_cells) as f:
            active = {line.strip() for line in f if line.strip()}
        oov_active = active - set(all_feats.keys())
        if oov_active:
            raise ValueError(
                f"--lib-cells: {len(oov_active)} cells not in features vocab "
                f"(l0+cand). OOV: {sorted(oov_active)}. Add to features or "
                f"remove from lib-cells file."
            )
        lib_mask = {name: (name in active) for name in all_feats}
    else:
        lib_mask = {name: True for name in all_feats}

    mining_edge_features: dict = {}
    if args.mining_edge_features:
        import csv as _csv
        with open(args.mining_edge_features) as f:
            for row in _csv.DictReader(f):
                mining_edge_features[(row["canonical"].strip(),
                                      row["master_cell"].strip())] = (
                    float(row["cluster_presence"]), float(row["avg_instances"]))
        print(f"Loaded {len(mining_edge_features)} mining edge feature rows.")

    overlap_ratio: dict = {}
    import csv as _csv
    with open(args.mining_overlap_ratio) as f:
        for row in _csv.DictReader(f):
            overlap_ratio[(row["canonical_A"].strip(), row["canonical_B"].strip())] = float(row["overlap_ratio"])
    print(f"Loaded {len(overlap_ratio)} mining overlap-ratio rows.")

    p_to_np_map: dict = {}
    for features in all_feats.values():
        p_canonical = _p_canonical_of_bits(features["canonical"])
        if _n_inputs_from_key(p_canonical) > 4:
            continue
        if p_canonical not in p_to_np_map:
            p_to_np_map[p_canonical] = _direct_pmnpn(p_canonical, args.npn_tool)
    print(f"Resolved {len(p_to_np_map)} PM-NPN classes with the direct transformation tool.")

    g = build_graph(all_feats, lib_mask,
                    mining_edge_features=mining_edge_features,
                    p_to_np_map=p_to_np_map,
                    overlap_ratio=overlap_ratio)
    print(graph_summary(g))
    print("\nFirst 10 nodes:")
    for i, name in enumerate(g["node_keys"][:10]):
        feats = g["x"][i].tolist()
        print(f"  [{i:3d}] {name:<40s}  "
              f"n_inputs={int(feats[0])}  cpp_min={feats[9]:.3f}")
