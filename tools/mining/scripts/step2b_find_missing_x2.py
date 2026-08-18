#!/usr/bin/env python3
import argparse
import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from canonical_index import CanonicalIndex

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--mining-rank',   required=True, help='mining_node_rank.csv')
    ap.add_argument('--features-l0',   required=True, help='features_l0_with_x2.csv')
    ap.add_argument('--so3-cells',     required=True, help='cells.csv (name,n_inputs,canonical_key)')
    ap.add_argument('--top-k',         type=int, default=10)
    ap.add_argument('--out-cells-csv', required=True)
    args = ap.parse_args()

    with open(args.mining_rank, newline='') as f:
        all_rows = list(csv.DictReader(f))
    all_rows.sort(key=lambda r: float(r['rank']))
    top_canonicals = [r['canonical'] for r in all_rows[:args.top_k]]

    idx = CanonicalIndex(args.so3_cells, args.features_l0)

    so3_n_inputs: dict[str, str] = {}
    with open(args.so3_cells, newline='') as f:
        for r in csv.DictReader(f):
            so3_n_inputs[r['name']] = r['n_inputs']

    feat_names: set[str] = set()
    feat_n_inputs: dict[str, str] = {}
    with open(args.features_l0, newline='') as f:
        for r in csv.DictReader(f):
            feat_names.add(r['cell_name'])
            feat_n_inputs[r['cell_name']] = r['n_inputs']

    x1_to_extract: dict[str, dict] = {}

    def n_inputs_of(name: str) -> str:
        n = so3_n_inputs.get(name) or feat_n_inputs.get(name)
        if n is None:
            raise ValueError(f"n_inputs unknown for cell: {name}")
        return n

    def add_x1(x1_name: str, canonical: str) -> None:
        if x1_name not in x1_to_extract:
            x1_to_extract[x1_name] = {
                'name': x1_name,
                'n_inputs': n_inputs_of(x1_name),
                'canonical_key': canonical,
            }

    for canonical in top_canonicals:
        all_cells = idx.all_cells(canonical)
        if not all_cells:
            continue

        for cell_name in all_cells:
            if not cell_name.endswith('_X1'):
                continue
            if cell_name not in feat_names:
                continue
            x2_name = cell_name.replace('_X1', '_X2')
            if x2_name not in feat_names:
                add_x1(cell_name, canonical)

        for cell_name in all_cells:
            if not cell_name.endswith('_X1'):
                continue
            x2_name = cell_name.replace('_X1', '_X2')
            if not (cell_name in feat_names and x2_name in feat_names):
                add_x1(cell_name, canonical)

    if not x1_to_extract:
        print("[step2b] All cells present — nothing to extract.", flush=True)
        sys.exit(0)

    will_generate = []
    for x1 in x1_to_extract.values():
        for name in (x1['name'], x1['name'].replace('_X1', '_X2')):
            if name not in feat_names:
                will_generate.append(name)

    print(f"[step2b] {len(will_generate)} cell(s) will be generated: "
          f"{will_generate}", flush=True)

    with open(args.out_cells_csv, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=['name', 'n_inputs', 'canonical_key'], lineterminator='\n')
        w.writeheader()
        w.writerows(x1_to_extract.values())
    print(f"[step2b] cells_for_extract written: {args.out_cells_csv}", flush=True)

if __name__ == '__main__':
    main()
