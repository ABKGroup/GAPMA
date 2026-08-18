#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import sys
from collections import defaultdict
from pathlib import Path

def parse_pattern_instances(path: Path) -> tuple[dict[str, list[list[int]]], dict[str, list[str]]]:
    blocks: dict[str, list[list[int]]] = defaultdict(list)
    clst_ids: dict[str, list[str]] = defaultdict(list)
    current_key: str | None = None

    with open(path) as f:
        for line in f:
            line = line.rstrip('\n')
            stripped = line.strip()
            if not stripped:
                continue
            if stripped.startswith('canonical_key='):
                current_key = stripped.split('=', 1)[1].strip()
                blocks.setdefault(current_key, [])
                clst_ids.setdefault(current_key, [])
                continue
            if current_key is None:
                continue
            if stripped.startswith('clst') and ':' in stripped:
                head, ids_str = stripped.split(':', 1)
                clst_id = head.strip()
                ids = [int(x) for x in ids_str.split() if x.strip()]
                blocks[current_key].append(ids)
                clst_ids[current_key].append(clst_id)

    if not blocks:
        raise ValueError(f"No canonical_key blocks parsed from {path}")
    return blocks, clst_ids

def parse_overlap_clusters(path: Path) -> dict[str, set[str]]:
    overlap: dict[str, set[str]] = defaultdict(set)
    if not path.exists():
        return overlap
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            head = parts[0]
            for other in parts[1:]:
                if other == head:
                    continue
                overlap[head].add(other)
                overlap[other].add(head)
    return overlap

def parse_cell_id_map(path: Path) -> dict[int, str]:
    out: dict[int, str] = {}
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            cid = int(row['cell_id'])
            out[cid] = row['master_cell']
    if not out:
        raise ValueError(f"cell_id_map.csv is empty: {path}")
    return out

def compute_canonical_overlap_ratio(
    clst_ids_by_canon: dict[str, list[str]],
    overlap: dict[str, set[str]],
) -> dict[tuple[str, str], float]:
    clst_to_canon: dict[str, str] = {}
    for canon, ids in clst_ids_by_canon.items():
        for cid in ids:
            clst_to_canon[cid] = canon

    out: dict[tuple[str, str], float] = {}
    for canon_a, a_clusters in clst_ids_by_canon.items():
        N_a = len(a_clusters)
        if N_a == 0:
            continue
        per_b_count: dict[str, int] = defaultdict(int)
        for a_cid in a_clusters:
            seen_b: set[str] = set()
            for neighbor in overlap.get(a_cid, ()):
                cb = clst_to_canon.get(neighbor)
                if cb is None or cb == canon_a:
                    continue
                if cb in seen_b:
                    continue
                seen_b.add(cb)
                per_b_count[cb] += 1
        for cb, cnt in per_b_count.items():
            out[(canon_a, cb)] = cnt / N_a
    return out

def compute_m1_m2(
    blocks: dict[str, list[list[int]]],
    id2master: dict[int, str],
) -> list[tuple[str, str, float, float]]:
    rows: list[tuple[str, str, float, float]] = []

    for canonical, clusters in blocks.items():
        N = len(clusters)
        if N == 0:
            raise ValueError(
                f"canonical={canonical} has 0 clusters in pattern_instances.out — "
                "valid mining output must have at least one cluster per canonical_key block."
            )
        master_stats: dict[str, list[int]] = defaultdict(lambda: [0, 0])
        for cluster_ids in clusters:
            seen_in_cluster: set[str] = set()
            for cid in cluster_ids:
                master = id2master.get(cid)
                if master is None:
                    raise ValueError(
                        f"cell_id={cid} in canonical={canonical} has no entry in "
                        "cell_id_map.csv — mining output and ID map are inconsistent."
                    )
                master_stats[master][1] += 1
                if master not in seen_in_cluster:
                    master_stats[master][0] += 1
                    seen_in_cluster.add(master)
        for master, (n_clst_with, n_inst_total) in master_stats.items():
            m1 = n_clst_with / N
            m2 = n_inst_total / N
            rows.append((canonical, master, m1, m2))
    return rows

def parse_p_to_np(path: Path) -> set[tuple[str, str, int, int]]:
    out: set[tuple[str, str, int, int]] = set()
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            p = row['p_canonical_key'].strip()
            np_ = row['np_canonical_key'].strip()
            if not p or not np_:
                continue
            try:
                ei = int(row.get('erased_input_inverters', '0') or '0')
                eo = int(row.get('erased_output_inverters', '0') or '0')
            except ValueError:
                ei = eo = 0
            out.add((p, np_, ei, eo))
    return out

def minimum_cost_p_to_np(
    rows: set[tuple[str, str, int, int]],
) -> dict[str, tuple[tuple[str, int, int], ...]]:
    best_cost: dict[str, int] = {}
    best_rows: dict[str, dict[str, tuple[int, int]]] = {}
    for p, np_, ei, eo in sorted(rows):
        cost = ei + eo
        if p not in best_cost or cost < best_cost[p]:
            best_cost[p] = cost
            best_rows[p] = {}
        if cost == best_cost[p]:
            current = best_rows[p].get(np_)
            if current is None or (ei, eo) < current:
                best_rows[p][np_] = (ei, eo)
    return {
        p: tuple((np_, *best_rows[p][np_]) for np_ in sorted(best_rows[p]))
        for p in sorted(best_rows)
    }

def aggregate_pmnpn_counts(
    canonical_counts: list[tuple[str, int]],
    relation: dict[str, tuple[tuple[str, int, int], ...]],
) -> list[tuple[str, int]]:
    aggregated: dict[str, int] = {}
    for canonical, count in canonical_counts:
        representatives = relation.get(canonical, ((canonical, 0, 0),))
        for representative, _, _ in representatives:
            aggregated[representative] = aggregated.get(representative, 0) + count
    return sorted(aggregated.items(), key=lambda row: (-row[1], row[0]))

def flatten_minimum_cost_p_to_np(
    rows: set[tuple[str, str, int, int]],
) -> list[tuple[str, str, int, int]]:
    relation = minimum_cost_p_to_np(rows)
    return [
        (p, np_, ei, eo)
        for p, representatives in relation.items()
        for np_, ei, eo in representatives
    ]

def _mining_input_dir(run_dir: Path) -> Path:
    if (run_dir / 'pattern_instances.out').exists():
        return run_dir
    if (run_dir / 'mining' / 'pattern_instances.out').exists():
        return run_dir / 'mining'
    return run_dir

def process_run(run_dir: Path, lib_dir=None) -> set[tuple[str, str, int, int]]:
    input_dir = _mining_input_dir(run_dir)
    pi_path  = input_dir / 'pattern_instances.out'
    cm_path  = input_dir / 'cell_id_map.csv'
    oc_path  = input_dir / 'overlap_clusters.out'
    pnp_path = input_dir / 'p_to_np_class.csv'
    if not pnp_path.exists():
        pnp_path = input_dir / 'mining_raw' / 'p_to_np_class.csv'
    for required in (pi_path, cm_path):
        if not required.exists():
            raise FileNotFoundError(f"Missing required input: {required}")

    blocks, clst_ids = parse_pattern_instances(pi_path)
    id2master        = parse_cell_id_map(cm_path)
    overlap          = parse_overlap_clusters(oc_path)

    rows = compute_m1_m2(blocks, id2master)
    out_path = run_dir / 'mining_edge_features.csv'
    with open(out_path, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['canonical', 'master_cell', 'cluster_presence', 'avg_instances'])
        for r in rows:
            w.writerow([r[0], r[1], f'{r[2]:.6f}', f'{r[3]:.6f}'])

    ratios = compute_canonical_overlap_ratio(clst_ids, overlap)
    out2 = run_dir / 'mining_overlap_ratio.csv'
    with open(out2, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['canonical_A', 'canonical_B', 'overlap_ratio'])
        for (a, b), v in ratios.items():
            w.writerow([a, b, f'{v:.6f}'])

    p_to_np = parse_p_to_np(pnp_path) if pnp_path.exists() else set()
    p_to_np_relation = minimum_cost_p_to_np(p_to_np)
    canon_counts_raw = [(c, len(cl)) for c, cl in blocks.items() if cl]
    canon_counts = aggregate_pmnpn_counts(canon_counts_raw, p_to_np_relation)
    N = len(canon_counts)
    out3 = run_dir / 'mining_node_rank.csv'
    with open(out3, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['canonical', 'cluster_count', 'rank', 'rank_norm'])
        for r, (c, cnt) in enumerate(canon_counts):
            rn = (r / (N - 1)) if N > 1 else 0.0
            w.writerow([c, cnt, r, f'{rn:.6f}'])

    return p_to_np

def find_run_dirs(design_dir: Path) -> list[Path]:
    run_dirs: set[Path] = set()
    for pi_path in design_dir.rglob('pattern_instances.out'):
        input_dir = pi_path.parent
        run_dir = input_dir.parent if input_dir.name == 'mining' else input_dir
        run_dirs.add(run_dir)
    return sorted(run_dirs)

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', required=True,
                    help='mining results root (contains <design>/run_NNN/...)')
    ap.add_argument('--designs', nargs='+', required=True,
                    help='design names (e.g. gcd picorv32)')
    args = ap.parse_args()

    root = Path(args.root)
    if not root.is_dir():
        print(f"ERROR: root not a directory: {root}", file=sys.stderr)
        return 1

    global_pnp: set[tuple[str, str, int, int]] = set()
    total_runs = 0
    failed: list[str] = []

    for design in args.designs:
        design_dir = root / design
        if not design_dir.is_dir():
            print(f"  [warn] design dir missing: {design_dir}", file=sys.stderr)
            continue
        for run_dir in find_run_dirs(design_dir):
            try:
                pnp = process_run(run_dir)
                global_pnp |= pnp
                total_runs += 1
            except Exception as e:
                failed.append(f"{design}/{run_dir.name}: {e}")
                print(f"  [fail] {design}/{run_dir.name}: {e}", file=sys.stderr)

    g_path = root / 'global_p_to_np.csv'
    with open(g_path, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['p_canonical', 'np_canonical', 'erased_input_inv', 'erased_output_inv'])
        for p, np_, ei, eo in flatten_minimum_cost_p_to_np(global_pnp):
            w.writerow([p, np_, ei, eo])

    print(f"[done] runs_processed={total_runs} failed={len(failed)} "
          f"unique_p_to_np_pairs={len(global_pnp)}")
    print(f"[done] global_p_to_np={g_path}")
    if failed:
        for f_ in failed:
            print(f"  FAIL {f_}", file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
