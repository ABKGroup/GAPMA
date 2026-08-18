from __future__ import annotations

import csv
import itertools
import re
import random
from collections import defaultdict
from typing import Iterator, NamedTuple

MAX_ENUM_SIZE = 10
SAMPLES_PER_LARGE = 50

class CellSubset(NamedTuple):
    canonical_keys: frozenset
    lib_patterns: list

def _read_mined_keys(mining_csv: str) -> set[str]:
    keys: set[str] = set()
    with open(mining_csv) as f:
        reader = csv.DictReader(f)
        fields = reader.fieldnames or []
        if 'canonical_key' in fields:
            col = 'canonical_key'
        elif 'canonical' in fields:
            col = 'canonical'
        else:
            raise ValueError(
                f"Mining CSV {mining_csv!r} has no 'canonical_key' or 'canonical' column. "
                f"Got: {fields}"
            )
        for row in reader:
            k = row[col].strip()
            if k.startswith('0b'):
                keys.add(k)
    if not keys:
        raise ValueError(f"Mining CSV {mining_csv!r} contains no valid canonical keys")
    return keys

def _random_subsets(members: list[str], n: int, rng: random.Random) -> Iterator[frozenset]:
    seen: set[frozenset] = set()
    attempts = 0
    max_attempts = n * 10
    while len(seen) < n and attempts < max_attempts:
        attempts += 1
        size = rng.randint(2, len(members))
        s = frozenset(rng.sample(members, size))
        if s not in seen:
            seen.add(s)
            yield s

def generate_subsets_so3(
    mining_csv: str,
    cells_csv: str,
    baseline_canonical_keys: set = frozenset({'0b1110', '0b1000'}),
    max_delta: int = 16,
    seed: int = 42,
) -> list[CellSubset]:
    mined_keys = _read_mined_keys(mining_csv)

    key_to_cells: dict[str, list[str]] = defaultdict(list)
    with open(cells_csv) as f:
        for row in csv.DictReader(f):
            key = row['canonical_key'].strip()
            if key in baseline_canonical_keys:
                continue
            if key not in mined_keys:
                continue
            key_to_cells[key].append(row['name'].strip())

    if not key_to_cells:
        raise ValueError(
            f"No candidate canonical keys found after filtering mining CSV "
            f"{mining_csv!r} against cells CSV {cells_csv!r}. "
            f"Check that the mining run used the SO3 baseline."
        )

    def _drive(name: str) -> int:
        m = re.search(r'_X(\d+)', name)
        return int(m.group(1)) if m else 1

    key_to_modes: dict[str, list[list[str]]] = {}
    for key, cells in key_to_cells.items():
        min_drive = min(_drive(c) for c in cells)
        base  = [c for c in cells if _drive(c) == min_drive]
        extra = [c for c in cells if _drive(c) != min_drive]
        key_to_modes[key] = [base] if not extra else [base, base + extra]

    candidate_keys = sorted(key_to_cells.keys())
    rng = random.Random(seed)
    seen: set[tuple] = set()
    results: list[CellSubset] = []

    def _emit(keys: frozenset, cells: list[str]) -> None:
        sig = (keys, frozenset(cells))
        if sig in seen:
            return
        seen.add(sig)
        results.append(CellSubset(canonical_keys=keys, lib_patterns=list(cells)))

    for key in candidate_keys:
        for mode_cells in key_to_modes[key]:
            _emit(frozenset({key}), mode_cells)

    def _emit_combo(combo: tuple) -> None:
        keys = frozenset(combo)
        base_cells  = [c for k in combo for c in key_to_modes[k][0]]
        _emit(keys, base_cells)
        if any(len(key_to_modes[k]) > 1 for k in combo):
            extra_cells = [c for k in combo for c in key_to_modes[k][-1]]
            _emit(keys, extra_cells)

    if len(candidate_keys) <= MAX_ENUM_SIZE:
        for r in range(2, len(candidate_keys) + 1):
            for combo in itertools.combinations(candidate_keys, r):
                _emit_combo(combo)
    else:
        for keys in _random_subsets(candidate_keys, SAMPLES_PER_LARGE, rng):
            _emit_combo(tuple(sorted(keys)))

    return results

if __name__ == '__main__':
    import argparse

    p = argparse.ArgumentParser(description='Generate SO3 cell subsets for FC training runs.')
    p.add_argument('--mining',    required=True, help='canonical_freq.csv from baseline synthesis')
    p.add_argument('--cells-csv', required=True, help='candidate cell list CSV')
    p.add_argument('--baseline-keys', nargs='*', default=['0b1110', '0b1000'],
                   help='Canonical keys of baseline cells to exclude (default: 0b1110 0b1000)')
    p.add_argument('--seed',     type=int, default=42)
    args = p.parse_args()

    subsets = generate_subsets_so3(
        args.mining, args.cells_csv,
        baseline_canonical_keys=set(args.baseline_keys),
        seed=args.seed,
    )
    print(f"Total subsets: {len(subsets)}")
    for s in subsets[:10]:
        print(f"  {sorted(s.canonical_keys)}  ->  {s.lib_patterns}")
    if len(subsets) > 10:
        print(f"  ... ({len(subsets) - 10} more)")
