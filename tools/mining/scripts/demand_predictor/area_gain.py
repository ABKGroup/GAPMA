#!/usr/bin/env python3
import argparse
import csv
import os
from collections import defaultdict

_INV_CPP_MIN = {
    "INV_X1": 1,
    "INV_X2": 2,
    "INV_X4": 4,
    "INV_X8": 8,
}

_DEFAULT_FEATURES_L0 = os.path.join(os.path.dirname(__file__), "features_l0.csv")

def _load_cpp_min(features_l0_path: str) -> dict:
    cpp = dict(_INV_CPP_MIN)
    seen: set = set()
    with open(features_l0_path) as f:
        for row in csv.DictReader(f):
            name = row["cell_name"]
            if name not in seen:
                seen.add(name)
                cpp[name] = int(row["cpp_min"])
    return cpp

def _load_cell_id_map(path: str) -> dict:
    mapping: dict = {}
    with open(path) as f:
        for row in csv.DictReader(f):
            mapping[int(row["cell_id"])] = row["master_cell"]
    return mapping

def _parse_clusters(path: str) -> dict:
    result: dict = defaultdict(list)
    current_key = None
    with open(path) as f:
        for raw in f:
            line = raw.rstrip("\n")
            if line.startswith("canonical_key="):
                current_key = line.split("=", 1)[1].strip()
            elif current_key and line.startswith("clst") and ":" in line:
                colon = line.index(":")
                ids_str = line[colon + 1:].strip()
                if ids_str:
                    result[current_key].append([int(x) for x in ids_str.split()])
    return result

def _canon_bits(s: str) -> str:
    return s[2:] if s.startswith("0b") else s

def _load_canon_to_b_cells(features_l0_path: str) -> dict:
    mapping: dict = defaultdict(list)
    seen: set = set()
    with open(features_l0_path) as f:
        for row in csv.DictReader(f):
            name = row["cell_name"]
            if name not in seen:
                seen.add(name)
                key = _canon_bits(row["canonical"])
                mapping[key].append((name, int(row["cpp_min"])))
    return mapping

def compute(payload_dir: str, features_l0_path: str) -> list:
    pattern_path = os.path.join(payload_dir, "pattern_instances.out")
    id_map_path = os.path.join(payload_dir, "cell_id_map.csv")

    if not os.path.exists(pattern_path):
        raise FileNotFoundError(f"pattern_instances.out not found: {pattern_path}")
    if not os.path.exists(id_map_path):
        raise FileNotFoundError(f"cell_id_map.csv not found: {id_map_path}")

    cpp_map = _load_cpp_min(features_l0_path)
    id_to_cell = _load_cell_id_map(id_map_path)
    clusters_by_key = _parse_clusters(pattern_path)
    canon_to_b_cells = _load_canon_to_b_cells(features_l0_path)

    rows = []
    for canonical_key, cluster_list in sorted(clusters_by_key.items(),
                                               key=lambda kv: int(kv[0], 2)):
        canon_bits = _canon_bits(canonical_key)

        cluster_areas = []
        for node_ids in cluster_list:
            total = 0
            for nid in node_ids:
                if nid not in id_to_cell:
                    raise ValueError(
                        f"node ID {nid} not in cell_id_map.csv (payload: {payload_dir})"
                    )
                cell = id_to_cell[nid]
                if cell not in cpp_map:
                    raise ValueError(
                        f"cell {cell!r} has no cpp_min — add to _INV_CPP_MIN or features_l0.csv"
                    )
                total += cpp_map[cell] + 1
            cluster_areas.append(total)

        n = len(cluster_areas)
        avg_area = sum(cluster_areas) / n
        min_area = min(cluster_areas)
        max_area = max(cluster_areas)

        b_cells = canon_to_b_cells.get(canon_bits, [])
        if not b_cells:
            rows.append({
                "canonical_key": canonical_key,
                "b_cell": "(none)",
                "n_clusters": n,
                "avg_cluster_area": f"{avg_area:.4f}",
                "min_cluster_area": min_area,
                "max_cluster_area": max_area,
                "new_cell_area": "",
                "area_gain": "",
            })
        else:
            for cell_name, cell_cpp in b_cells:
                new_cell_area = cell_cpp + 1
                rows.append({
                    "canonical_key": canonical_key,
                    "b_cell": cell_name,
                    "n_clusters": n,
                    "avg_cluster_area": f"{avg_area:.4f}",
                    "min_cluster_area": min_area,
                    "max_cluster_area": max_area,
                    "new_cell_area": new_cell_area,
                    "area_gain": f"{avg_area - new_cell_area:.4f}",
                })
    return rows

def main():
    parser = argparse.ArgumentParser(
        description="Compute area gain per canonical logic function from mining clusters."
    )
    parser.add_argument("--payload-dir", required=True,
                        help="Design payload directory (contains pattern_instances.out, cell_id_map.csv)")
    parser.add_argument("--features-l0", default=_DEFAULT_FEATURES_L0,
                        help="Path to features_l0.csv (default: same dir as this script)")
    parser.add_argument("--output", default=None,
                        help="Output CSV path (default: <payload-dir>/area_gain.csv)")
    args = parser.parse_args()

    out_path = args.output or os.path.join(args.payload_dir, "area_gain.csv")
    rows = compute(args.payload_dir, args.features_l0)

    fieldnames = [
        "canonical_key", "b_cell", "n_clusters",
        "avg_cluster_area", "min_cluster_area", "max_cluster_area",
        "new_cell_area", "area_gain",
    ]
    with open(out_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"Written {len(rows)} rows → {out_path}")

if __name__ == "__main__":
    main()
