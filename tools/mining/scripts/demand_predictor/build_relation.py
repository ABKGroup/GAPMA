#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import multiprocessing as mp
import os
import sys
import time

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
if _THIS_DIR not in sys.path:
    sys.path.insert(0, _THIS_DIR)

from decomp import parse_canonical, find_decomposition

_REQUIRED_IN_COLS = ['name', 'n_inputs', 'canonical_key']

def read_input_csv(path):
    rows = []
    with open(path, newline='') as f:
        reader = csv.DictReader(f)
        missing = [c for c in _REQUIRED_IN_COLS if c not in reader.fieldnames]
        if missing:
            raise ValueError(
                f"Input CSV {path!r} missing required columns: {missing}. "
                f"Required: {_REQUIRED_IN_COLS}; got: {reader.fieldnames}"
            )
        for i, raw in enumerate(reader, start=2):
            try:
                n = int(raw['n_inputs'])
            except (TypeError, ValueError) as e:
                raise ValueError(
                    f"{path}:{i}: n_inputs is not an integer: "
                    f"{raw['n_inputs']!r}"
                ) from e
            if n < 2 or n > 4:
                raise ValueError(
                    f"{path}:{i}: n_inputs={n} outside supported range [2,4]"
                )
            key = raw['canonical_key'].strip()
            kn, _ = parse_canonical(key)
            if kn != n:
                raise ValueError(
                    f"{path}:{i}: canonical_key {key!r} implies n_inputs={kn}, "
                    f"but row says n_inputs={n}"
                )
            rows.append({
                'name': raw['name'].strip(),
                'n_inputs': n,
                'canonical_key': key,
            })
    if not rows:
        raise ValueError(f"Input CSV {path!r} contains no data rows")
    return rows

def _compute_one_pair(args):
    (target, base, max_gates, timeout_s, disable_affine_check) = args
    tn, tt = parse_canonical(target['canonical_key'])
    bn, bb = parse_canonical(base['canonical_key'])
    t0 = time.time()
    r = find_decomposition(tt, tn, bb, bn,
                           max_gates=max_gates, timeout_s=timeout_s,
                           disable_affine_check=disable_affine_check)
    dt = time.time() - t0
    return {
        'target_name':  target['name'],
        'target_key':   target['canonical_key'],
        'base_name':    base['name'],
        'base_key':     base['canonical_key'],
        'status':       r['status'],
        'base_count':   r.get('base_count', ''),
        'inv_count':    r.get('inv_count', ''),
        'total':        r.get('total', ''),
        'reason':       r.get('reason', ''),
        'elapsed_s':    f'{dt:.2f}',
    }

def compute_relation(cells, max_gates, timeout_s, n_workers,
                     disable_affine_check, skip_pairs=None, log=sys.stderr):
    if skip_pairs is None:
        skip_pairs = set()

    pairs = [(t, b) for t in cells for b in cells
             if t['canonical_key'] != b['canonical_key']
             and (t['canonical_key'], b['canonical_key']) not in skip_pairs]
    n_total = len(pairs)
    job_args = [(t, b, max_gates, timeout_s, disable_affine_check)
                for (t, b) in pairs]

    results = []
    t_start = time.time()
    log.write(f"[compute] launching {n_total} pairs across "
              f"{n_workers} workers (max_gates={max_gates}, "
              f"timeout_s={timeout_s}, "
              f"disable_affine_check={disable_affine_check})\n")
    log.flush()

    n_done = 0
    with mp.Pool(processes=n_workers) as pool:
        for row in pool.imap_unordered(_compute_one_pair, job_args,
                                       chunksize=1):
            n_done += 1
            results.append(row)
            log.write(
                f"[{n_done}/{n_total}] {row['target_name']} <- "
                f"{row['base_name']}: {row['status']} "
                f"base={row['base_count']} inv={row['inv_count']} "
                f"({row['elapsed_s']}s)\n"
            )
            log.flush()

    order = {c['canonical_key']: i for i, c in enumerate(cells)}
    results.sort(key=lambda r: (order[r['target_key']],
                                order[r['base_key']]))
    log.write(
        f"--- relation computation done: {len(results)} pairs in "
        f"{time.time()-t_start:.1f}s ---\n"
    )
    return results

def derive_overlap(relation_rows, threshold):
    idx = {}
    for r in relation_rows:
        t = r['total']
        idx[(r['target_key'], r['base_key'])] = (
            int(t) if t != '' else None,
            r['status'],
        )

    achievable_statuses = ('OK', 'PROJECTION')

    keys = sorted({r['target_key'] for r in relation_rows})
    out = []
    for i, a in enumerate(keys):
        for b in keys[i+1:]:
            ab = idx.get((a, b))
            ba = idx.get((b, a))
            status_ab = ab[1] if ab else 'MISSING'
            status_ba = ba[1] if ba else 'MISSING'
            cost_ab = ab[0] if ab and ab[1] in achievable_statuses else None
            cost_ba = ba[0] if ba and ba[1] in achievable_statuses else None
            opts = []
            if cost_ab is not None:
                opts.append((cost_ab, b))
            if cost_ba is not None:
                opts.append((cost_ba, a))
            row = {
                'key_a': a, 'key_b': b,
                'cost_a_from_b': '' if cost_ab is None else cost_ab,
                'status_a_from_b': status_ab,
                'cost_b_from_a': '' if cost_ba is None else cost_ba,
                'status_b_from_a': status_ba,
            }
            if not opts:
                row.update({'min_cost': '', 'absorber_key': '',
                            'overlap': 'NO'})
            else:
                min_cost, absorber = min(opts, key=lambda t: t[0])
                row.update({
                    'min_cost': min_cost,
                    'absorber_key': absorber,
                    'overlap': 'YES' if min_cost <= threshold else 'NO',
                })
            out.append(row)
    return out

def write_relation_csv(path, rows):
    fields = ['target_name', 'target_key', 'base_name', 'base_key',
              'status', 'base_count', 'inv_count', 'total', 'reason',
              'elapsed_s']
    with open(path, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for r in rows:
            w.writerow(r)

def write_overlap_csv(path, rows):
    fields = ['key_a', 'key_b',
              'cost_a_from_b', 'status_a_from_b',
              'cost_b_from_a', 'status_b_from_a',
              'min_cost', 'absorber_key', 'overlap']
    with open(path, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for r in rows:
            w.writerow(r)

def main(argv=None):
    p = argparse.ArgumentParser(
        prog='build_relation',
        description='Build the canonical-form substitution graph via Z3 '
                    'exact synthesis.',
    )
    p.add_argument('--in', dest='in_path', required=True,
                   help='Input CSV with columns: name, n_inputs, canonical_key')
    p.add_argument('--relation-out', dest='relation_out', default=None,
                   help='Output CSV: asymmetric (target, base) relation. '
                        'Required unless --cache is provided.')
    p.add_argument('--cache', dest='cache', default=None,
                   help='Path to persistent relation cache CSV. If the file '
                        'exists, pairs already recorded there are skipped and '
                        'new results are appended; if absent, it is created. '
                        'Enables incremental computation across runs.')
    p.add_argument('--overlap-out', dest='overlap_out', default=None,
                   help='(Optional) Output CSV: symmetric overlap relation. '
                        'Skipped if not specified — not used by downstream steps.')
    p.add_argument('--max-gates', type=int, default=9999,
                   help='Max gates per (target, base) search (default 9999, '
                        'effectively unlimited — Z3 will hit timeout-s first).')
    p.add_argument('--timeout-s', type=int, default=600,
                   help='Z3 timeout per (gate-count) try, in seconds (default 600).')
    p.add_argument('--overlap-threshold', type=int, default=5,
                   help='Min-cost cutoff for overlap=YES (default 5).')
    p.add_argument('--n-workers', type=int,
                   default=max(1, mp.cpu_count()),
                   help='Number of parallel worker processes (default: '
                        'all CPU cores).')
    p.add_argument('--disable-affine-check', action='store_true',
                   help='Skip the analytic affine class L short-circuit. '
                        'When set, every pair is sent to Z3 even if '
                        '{base, INV} is provably restricted to affine '
                        'functions. Use for independent verification '
                        '(expect TIMEOUTs on affine-impossible pairs).')
    args = p.parse_args(argv)

    if args.relation_out is None and args.cache is None:
        p.error("at least one of --relation-out or --cache is required")

    if not os.path.isfile(args.in_path):
        p.error(f"input file not found: {args.in_path}")

    cached_pairs: set[tuple[str, str]] = set()
    if args.cache and os.path.isfile(args.cache):
        with open(args.cache, newline='') as f:
            for row in csv.DictReader(f):
                tk = row.get('target_key', '').strip()
                bk = row.get('base_key',   '').strip()
                if tk and bk:
                    cached_pairs.add((tk, bk))
        print(f"[build_relation] cache: {len(cached_pairs)} pairs already computed "
              f"({args.cache})", file=sys.stderr)

    print(f"[build_relation] reading {args.in_path}", file=sys.stderr)
    cells = read_input_csv(args.in_path)
    seen_keys: dict[str, dict] = {}
    for cell in cells:
        if cell['canonical_key'] not in seen_keys:
            seen_keys[cell['canonical_key']] = cell
    cells = list(seen_keys.values())
    print(f"[build_relation] {len(cells)} unique canonical keys "
          f"(after dedup from input CSV)", file=sys.stderr)
    n_all = len(cells) * (len(cells) - 1)
    n_new = n_all - sum(
        1 for t in cells for b in cells
        if t['canonical_key'] != b['canonical_key']
        and (t['canonical_key'], b['canonical_key']) in cached_pairs
    )
    print(f"[build_relation] {n_all} key pairs total; "
          f"{n_new} to compute (cache hits: {n_all - n_new})", file=sys.stderr)

    if n_new == 0:
        print("[build_relation] all pairs cached — nothing to compute.", file=sys.stderr)
        return

    rel = compute_relation(cells, max_gates=args.max_gates,
                           timeout_s=args.timeout_s,
                           n_workers=args.n_workers,
                           disable_affine_check=args.disable_affine_check,
                           skip_pairs=cached_pairs)

    if args.relation_out:
        write_relation_csv(args.relation_out, rel)
        print(f"[build_relation] wrote {args.relation_out}", file=sys.stderr)

    if args.cache:
        cache_exists = os.path.isfile(args.cache)
        fields = ['target_name', 'target_key', 'base_name', 'base_key',
                  'status', 'base_count', 'inv_count', 'total', 'reason',
                  'elapsed_s']
        with open(args.cache, 'a', newline='') as f:
            w = csv.DictWriter(f, fieldnames=fields)
            if not cache_exists:
                w.writeheader()
            for r in rel:
                w.writerow(r)
        print(f"[build_relation] appended {len(rel)} rows to cache "
              f"{args.cache}", file=sys.stderr)

    if args.overlap_out:
        all_rows = rel
        if cached_pairs and args.cache and os.path.isfile(args.cache):
            with open(args.cache, newline='') as f:
                all_rows = list(csv.DictReader(f))
        ovl = derive_overlap(all_rows, threshold=args.overlap_threshold)
        write_overlap_csv(args.overlap_out, ovl)
        print(f"[build_relation] wrote {args.overlap_out}", file=sys.stderr)

if __name__ == '__main__':
    main()
