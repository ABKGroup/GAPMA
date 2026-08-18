#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import os
import sys
from collections import defaultdict

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
if _THIS_DIR not in sys.path:
    sys.path.insert(0, _THIS_DIR)

try:
    from fet_cost import fet_cost as _fet_cost, n_inputs_from_key
except ModuleNotFoundError:
    _fet_cost = None
    n_inputs_from_key = None

_REQUIRED_MINING_COLS = ['canonical_key', 'count']
_REQUIRED_RELATION_COLS = [
    'target_name', 'target_key', 'base_name', 'base_key',
    'status', 'base_count', 'inv_count', 'total',
]
_REQUIRED_CELLS_COLS = ['name', 'canonical_key']

def _check_columns(actual, required, path):
    actual_set = set(actual)
    missing = [c for c in required if c not in actual_set]
    if missing:
        raise SystemExit(
            f"FATAL: {path}: missing required columns: {missing}\n"
            f"       got columns: {actual}"
        )

def _parse_int(value, field, row_idx, path):
    try:
        return int(value)
    except (TypeError, ValueError):
        raise SystemExit(
            f"FATAL: {path}: row {row_idx}: field '{field}' is not an "
            f"integer (got {value!r})"
        )

def _validate_canonical_key(key, field, row_idx, path):
    if not isinstance(key, str) or not key.startswith('0b'):
        raise SystemExit(
            f"FATAL: {path}: row {row_idx}: '{field}' must start with '0b' "
            f"(got {key!r})"
        )
    body = key[2:]
    if len(body) == 0 or any(c not in '01' for c in body):
        raise SystemExit(
            f"FATAL: {path}: row {row_idx}: '{field}' body must be non-empty "
            f"binary (got {key!r})"
        )
    n = len(body)
    if n & (n - 1) != 0:
        raise SystemExit(
            f"FATAL: {path}: row {row_idx}: '{field}' length must be a "
            f"power of two (got {n} bits)"
        )

def read_mining(path):
    out = defaultdict(int)
    with open(path, newline='') as f:
        reader = csv.DictReader(f)
        _check_columns(reader.fieldnames or [], _REQUIRED_MINING_COLS, path)
        for i, row in enumerate(reader, start=2):
            key = row['canonical_key']
            _validate_canonical_key(key, 'canonical_key', i, path)
            cnt = _parse_int(row['count'], 'count', i, path)
            if cnt < 0:
                raise SystemExit(
                    f"FATAL: {path}: row {i}: 'count' must be non-negative "
                    f"(got {cnt})"
                )
            out[key] += cnt
    return [{'canonical_key': k, 'count': c} for k, c in out.items()]

def read_cells(path):
    out = {}
    with open(path, newline='') as f:
        reader = csv.DictReader(f)
        _check_columns(reader.fieldnames or [], _REQUIRED_CELLS_COLS, path)
        for i, row in enumerate(reader, start=2):
            key = row['canonical_key']
            _validate_canonical_key(key, 'canonical_key', i, path)
            name = row['name']
            if not name:
                raise SystemExit(
                    f"FATAL: {path}: row {i}: 'name' must not be empty"
                )
            if key not in out:
                out[key] = name
    return out

def read_relation(path):
    best = {}
    with open(path, newline='') as f:
        reader = csv.DictReader(f)
        _check_columns(reader.fieldnames or [], _REQUIRED_RELATION_COLS, path)
        for i, row in enumerate(reader, start=2):
            if row['status'] != 'OK':
                continue
            tkey = row['target_key']
            bkey = row['base_key']
            _validate_canonical_key(tkey, 'target_key', i, path)
            _validate_canonical_key(bkey, 'base_key', i, path)
            base_count = _parse_int(row['base_count'], 'base_count', i, path)
            inv_count = _parse_int(row['inv_count'], 'inv_count', i, path)
            total = _parse_int(row['total'], 'total', i, path)
            if total != base_count + inv_count:
                raise SystemExit(
                    f"FATAL: {path}: row {i}: total ({total}) != "
                    f"base_count ({base_count}) + inv_count ({inv_count})"
                )
            cand = {
                'target_name': row['target_name'],
                'base_name': row['base_name'],
                'base_count': base_count,
                'inv_count': inv_count,
                'total': total,
            }
            cur = best.get((tkey, bkey))
            if cur is None:
                best[(tkey, bkey)] = cand
            else:
                key_new = (total, base_count, inv_count)
                key_old = (cur['total'], cur['base_count'], cur['inv_count'])
                if key_new < key_old:
                    best[(tkey, bkey)] = cand
    return best

def build_candidates(mined, cells, relation, max_cost, allow_larger_fanin):
    if max_cost < 1:
        raise SystemExit(
            f"FATAL: --max-cost must be >= 1 (got {max_cost})"
        )
    rows = []
    uncovered = []
    by_target = defaultdict(list)
    for (tkey, bkey), rec in relation.items():
        by_target[tkey].append((bkey, rec))
    for m in mined:
        tkey = m['canonical_key']
        cnt = m['count']
        target_n = n_inputs_from_key(tkey)
        budget = []
        if tkey in cells:
            budget.append((tkey, {
                'base_name': cells[tkey],
                'base_count': 1,
                'inv_count': 0,
                'total': 1,
            }))
        for bk, rec in by_target.get(tkey, []):
            if bk == tkey:
                continue
            if rec['total'] > max_cost:
                continue
            if not allow_larger_fanin:
                if bk not in cells:
                    continue
                if n_inputs_from_key(bk) > target_n:
                    continue
            budget.append((bk, rec))
        if not budget:
            uncovered.append({'logic_key': tkey, 'logic_count': cnt})
            continue
        for bk, rec in budget:
            rows.append({
                'logic_key': tkey,
                'logic_count': cnt,
                'candidate_name': rec['base_name'],
                'candidate_key': bk,
                'base_count': rec['base_count'],
                'inv_count': rec['inv_count'],
                'total': rec['total'],
            })
    return rows, uncovered

def _delta_cost(r):
    base_n   = n_inputs_from_key(r['candidate_key'])
    base_fet = _fet_cost(r['candidate_key'], base_n)
    indirect = r['base_count'] * base_fet + r['inv_count'] * 2
    direct_n = n_inputs_from_key(r['logic_key'])
    direct   = _fet_cost(r['logic_key'], direct_n)
    delta    = max(0, indirect - direct)
    return delta, indirect, direct

def filter_by_delta(candidates, max_delta):
    if max_delta is None:
        return candidates, []
    if max_delta < 0:
        raise SystemExit(
            f"FATAL: --max-delta must be >= 0 (got {max_delta})"
        )
    by_logic = defaultdict(list)
    for r in candidates:
        by_logic[r['logic_key']].append(r)
    filtered = []
    newly_uncovered = []
    for logic_key, group in by_logic.items():
        kept = []
        for r in group:
            delta, _, _ = _delta_cost(r)
            if delta <= max_delta:
                kept.append(r)
        if kept:
            filtered.extend(kept)
        else:
            newly_uncovered.append({
                'logic_key': logic_key,
                'logic_count': group[0]['logic_count'],
            })
    return filtered, newly_uncovered

def apply_delta_weighted_split(candidates):
    by_logic = defaultdict(list)
    for r in candidates:
        by_logic[r['logic_key']].append(r)
    out = []
    for logic_key, group in by_logic.items():
        deltas = []
        directs = []
        indirects = []
        for r in group:
            if r['total'] <= 0:
                raise SystemExit(
                    f"FATAL: candidate {r['candidate_name']} "
                    f"({r['candidate_key']}) for logic {logic_key} has "
                    f"non-positive total={r['total']}"
                )
            delta, indirect, direct = _delta_cost(r)
            deltas.append(delta)
            indirects.append(indirect)
            directs.append(direct)
        weights = [1.0 / (1 + d) for d in deltas]
        w_sum = sum(weights)
        if w_sum <= 0:
            raise SystemExit(
                f"FATAL: zero weight sum for logic {logic_key}"
            )
        n = len(group)
        for r, delta, indirect, direct, w in zip(
            group, deltas, indirects, directs, weights,
        ):
            ratio = w / w_sum
            share = ratio * r['logic_count']
            out.append({
                **r,
                'direct_fet': direct,
                'indirect_fet': indirect,
                'delta_fet': delta,
                'n_candidates': n,
                'weight': w,
                'split_ratio': ratio,
                'base_cells_used': share * r['base_count'],
                'inv_cells_used': share * r['inv_count'],
            })
    return out

def aggregate_per_cell(split_rows):
    base_acc = defaultdict(lambda: {
        'candidate_name': None, 'base_cells_used': 0.0,
    })
    inv_total = 0.0
    for r in split_rows:
        b = base_acc[r['candidate_key']]
        if b['candidate_name'] is None:
            b['candidate_name'] = r['candidate_name']
        b['base_cells_used'] += r['base_cells_used']
        inv_total += r['inv_cells_used']
    rows = []
    for k, v in base_acc.items():
        rows.append({
            'candidate_key': k,
            'candidate_name': v['candidate_name'],
            'base_cells_used': v['base_cells_used'],
        })
    rows.sort(key=lambda r: -r['base_cells_used'])
    return rows, inv_total

def write_csv(path, rows, columns):
    with open(path, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=columns)
        writer.writeheader()
        for r in rows:
            writer.writerow({c: r.get(c, '') for c in columns})

def main():
    p = argparse.ArgumentParser(
        description=(
            "Phase 2: delta-FET-weighted baseline demand split from "
            "mining + Phase 1 relation. Cost is the FET overhead "
            "(indirect - direct) of substituting a candidate for the "
            "target's direct single-cell implementation. No ML."
        ),
        allow_abbrev=False,
    )
    p.add_argument('--mining', required=True,
                   help='pattern_frequency.csv (canonical_key, count, ...)')
    p.add_argument('--cells', required=True,
                   help='Candidate cell list CSV (name, canonical_key). '
                        'Used to inject self-implementation candidates.')
    p.add_argument('--relation', required=True,
                   help='Phase 1 relation CSV (target_key, base_key, status, '
                        'base_count, inv_count, total, ...)')
    p.add_argument('--candidate-out', required=True,
                   help='Output CSV: one row per (mined_logic, candidate).')
    p.add_argument('--split-out', required=True,
                   help='Output CSV: candidates with cost-weighted split '
                        'ratio + cell counts.')
    p.add_argument('--summary-out', required=True,
                   help='Output CSV: aggregate cell demand across all logics.')
    p.add_argument('--uncovered-out', required=True,
                   help='Output CSV: mined logics with no candidate '
                        'within --max-cost (empty file if all covered).')
    p.add_argument('--max-cost', required=True, type=int,
                   help='Max total gate count (base + INV) for a candidate to '
                        'be considered. Filter is always on gate count, not '
                        'FET count.')
    p.add_argument('--allow-larger-fanin', action='store_true',
                   help='Allow candidate cells with strictly more inputs than '
                        'the target logic (uses input tying). Off by default: '
                        'synthesizers virtually never substitute a bigger '
                        'cell with tied inputs for a smaller logic.')
    p.add_argument('--max-delta', type=int, default=10,
                   help='Maximum allowed delta_fet for a candidate to be '
                        'included in the split (default: 10). Candidates '
                        'with delta_fet > max-delta are dropped. Pass a '
                        'large value (e.g. 9999) to disable. Logics whose '
                        'every candidate exceeds max-delta are added to '
                        'the uncovered-out CSV.')
    args = p.parse_args()

    mined = read_mining(args.mining)
    cells = read_cells(args.cells)
    relation = read_relation(args.relation)

    candidates, uncovered = build_candidates(
        mined, cells, relation, args.max_cost,
        allow_larger_fanin=args.allow_larger_fanin,
    )
    candidates, delta_uncovered = filter_by_delta(candidates, args.max_delta)
    uncovered = uncovered + delta_uncovered
    split_rows = apply_delta_weighted_split(candidates)
    per_cell, inv_total = aggregate_per_cell(split_rows)

    write_csv(args.candidate_out, candidates, columns=[
        'logic_key', 'logic_count', 'candidate_name', 'candidate_key',
        'base_count', 'inv_count', 'total',
    ])
    write_csv(args.split_out, split_rows, columns=[
        'logic_key', 'logic_count', 'candidate_name', 'candidate_key',
        'base_count', 'inv_count', 'total',
        'direct_fet', 'indirect_fet', 'delta_fet',
        'n_candidates', 'weight', 'split_ratio',
        'base_cells_used', 'inv_cells_used',
    ])
    write_csv(args.summary_out, per_cell + [{
        'candidate_key': 'INV',
        'candidate_name': 'INV (implicit helper)',
        'base_cells_used': inv_total,
    }], columns=['candidate_key', 'candidate_name', 'base_cells_used'])
    write_csv(args.uncovered_out, uncovered, columns=[
        'logic_key', 'logic_count',
    ])

    n_mined = len(mined)
    n_uncov = len(uncovered)
    print(
        f"OK  mined_logics={n_mined}  covered={n_mined - n_uncov}  "
        f"uncovered={n_uncov}  candidate_rows={len(candidates)}  "
        f"total_inv_cells={inv_total:.2f}",
        file=sys.stderr,
    )

if __name__ == '__main__':
    main()
