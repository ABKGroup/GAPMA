#!/usr/bin/env python3
from __future__ import annotations

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from decomp import (parse_canonical, find_decomposition,
                    synthesize_and_extract, simulate_circuit)

CELLS = [
    ('AND2',  '0b0001'),
    ('OR2',   '0b0111'),
    ('NAND2', '0b1110'),
    ('NOR2',  '0b1000'),
    ('XOR2',  '0b0110'),
    ('XNOR2', '0b1001'),
]

def verify_one(target_name, target_key, base_name, base_key,
               max_gates=10, timeout_s=60):
    tn, t_tt = parse_canonical(target_key)
    bn, b_tt = parse_canonical(base_key)
    r = find_decomposition(t_tt, tn, b_tt, bn,
                           max_gates=max_gates, timeout_s=timeout_s)
    if r['status'].startswith('IMPOSSIBLE'):
        return ('SKIP', f"{r['status']} ({r.get('reason','')})")
    if r['status'] == 'TIMEOUT':
        return ('FAIL', f"TIMEOUT at gates={r.get('tried_gates')}")
    if r['status'] == 'PROJECTION':
        return ('SKIP', "projection (0 gates)")
    if r['status'] != 'OK':
        return ('FAIL', f"unexpected status: {r}")

    total = r['total']
    bc_claimed = r['base_count']
    ic_claimed = r['inv_count']

    s = synthesize_and_extract(t_tt, tn, b_tt, bn,
                               total_gates=total, timeout_s=timeout_s)
    if not s.get('sat', False):
        return ('FAIL', f"re-synth at total={total} did not return SAT: {s}")

    bc_re = s['base_count']
    ic_re = s['inv_count']
    if bc_re + ic_re != total:
        return ('FAIL',
                f"re-synth counts don't sum to total: "
                f"base={bc_re} inv={ic_re} total={total}")

    pred_tt = simulate_circuit(s['circuit'], tn, b_tt, bn)
    if pred_tt != t_tt:
        return ('FAIL',
                f"simulated tt {pred_tt} != target {t_tt} "
                f"(circuit={s['circuit']})")

    return ('PASS',
            f"total={total} (claimed {bc_claimed}/{ic_claimed} ; "
            f"re-synth {bc_re}/{ic_re})")

def main():
    print("=== verify_all_pairs: Z3 circuit + independent simulation ===\n")
    n_pass = n_fail = n_skip = 0
    t_start = time.time()
    for tn_name, tk in CELLS:
        for bn_name, bk in CELLS:
            t0 = time.time()
            status, msg = verify_one(tn_name, tk, bn_name, bk)
            dt = time.time() - t0
            mark = {'PASS': 'PASS', 'FAIL': 'FAIL', 'SKIP': 'skip'}[status]
            print(f"  [{mark}] {tn_name:6s} <- {bn_name:6s} "
                  f"({dt:5.2f}s)  {msg}")
            if status == 'PASS':
                n_pass += 1
            elif status == 'FAIL':
                n_fail += 1
            else:
                n_skip += 1
    print()
    print(f"=== Result: PASS={n_pass}  FAIL={n_fail}  skip={n_skip}  "
          f"(total time {time.time()-t_start:.1f}s) ===")
    sys.exit(0 if n_fail == 0 else 1)

if __name__ == '__main__':
    main()
