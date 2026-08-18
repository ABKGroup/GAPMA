#!/usr/bin/env python3
import argparse
import csv
import itertools
import sys
from typing import Optional

def _p_min_bits(canon: str) -> str:
    bits = canon[2:] if canon.startswith("0b") else canon
    n = (len(bits)).bit_length() - 1
    if (1 << n) != len(bits):
        raise ValueError(f"canonical bit length not a power of 2: {canon!r}")
    rows = 1 << n
    best = bits
    for perm in itertools.permutations(range(n)):
        remap = [
            sum(((idx >> (n - 1 - i)) & 1) << (n - 1 - perm[i]) for i in range(n))
            for idx in range(rows)
        ]
        cand = ''.join(bits[remap[idx]] for idx in range(rows))
        if cand < best:
            best = cand
    return "0b" + best

class CanonicalIndex:

    def __init__(self, so3_cells_path: str, features_l0_path: Optional[str] = None):
        self._p_to_cells: dict[str, list[str]] = {}
        self._name_to_p: dict[str, str] = {}

        with open(so3_cells_path, newline='') as f:
            for r in csv.DictReader(f):
                name = r['name']
                key  = _p_min_bits(r['canonical_key'])
                self._name_to_p[name] = key
                self._p_to_cells.setdefault(key, []).append(name)

        self._feat_names: set[str] = set()
        if features_l0_path:
            with open(features_l0_path, newline='') as f:
                for r in csv.DictReader(f):
                    name = r['cell_name']
                    self._feat_names.add(name)
                    if name not in self._name_to_p:
                        key = _p_min_bits(r['canonical'])
                        self._name_to_p[name] = key
                        if name not in self._p_to_cells.get(key, []):
                            self._p_to_cells.setdefault(key, []).append(name)

    def all_cells(self, canonical: str) -> list[str]:
        return list(self._p_to_cells.get(_p_min_bits(canonical), []))

    def cells_with_features(self, canonical: str) -> list[str]:
        if not self._feat_names:
            raise ValueError("CanonicalIndex: features_l0 not loaded — pass features_l0_path")
        return [c for c in self.all_cells(canonical) if c in self._feat_names]

    def cells_missing_features(self, canonical: str) -> list[str]:
        if not self._feat_names:
            raise ValueError("CanonicalIndex: features_l0 not loaded — pass features_l0_path")
        return [c for c in self.all_cells(canonical) if c not in self._feat_names]

    def p_canonical_of(self, cell_name: str) -> Optional[str]:
        return self._name_to_p.get(cell_name)

    def known_canonicals(self) -> list[str]:
        return sorted(self._p_to_cells.keys())

    def has_features_loaded(self) -> bool:
        return bool(self._feat_names)

def main() -> None:
    ap = argparse.ArgumentParser(
        description="Look up SO3 library cells for a P-canonical form "
                     "(permutation-only match, not NPN/polarity-aware, "
                     "see this module's docstring).")
    ap.add_argument('--canonical',    help='P-canonical to look up (e.g. 0b10001011)')
    ap.add_argument('--so3-cells',    required=True, help='cells.csv')
    ap.add_argument('--features-l0',  help='features_l0_with_x2.csv (optional)')
    ap.add_argument('--list-all',     action='store_true',
                    help='Print all P-canonicals with cell counts and exit')
    args = ap.parse_args()

    if not args.canonical and not args.list_all:
        ap.error("provide --canonical or --list-all")

    idx = CanonicalIndex(args.so3_cells, args.features_l0)

    if args.list_all:
        for canon in idx.known_canonicals():
            cells = idx.all_cells(canon)
            feat_tag = ""
            if idx.has_features_loaded():
                n_feat = sum(1 for c in cells if c in idx._feat_names)
                feat_tag = f"  features={n_feat}/{len(cells)}"
            print(f"{canon}  cells={len(cells)}{feat_tag}  [{', '.join(cells)}]")
        return

    canonical = args.canonical
    all_c = idx.all_cells(canonical)

    if not all_c:
        print(f"Canonical {canonical}: NO matching cells at the P-canonical "
              f"(permutation-only) level.")
        print("This is NOT a full NPN check: a cell that implements this "
              "function under a different input negation or output "
              "complement (a polarity-equivalent cell) would NOT be found "
              "here. To check the library for a match under any polarity, "
              "use `pdk/SO3/cell_finder/find_cells_by_canonical.py "
              f"{canonical} --mode npn` instead.")
        sys.exit(0)

    print(f"Canonical : {canonical}")
    print(f"All cells : {', '.join(all_c)}")
    if idx.has_features_loaded():
        with_feat    = idx.cells_with_features(canonical)
        missing_feat = idx.cells_missing_features(canonical)
        print(f"With L0   : {', '.join(with_feat) if with_feat else '(none)'}")
        print(f"Missing L0: {', '.join(missing_feat) if missing_feat else '(none)'}")

if __name__ == '__main__':
    main()
