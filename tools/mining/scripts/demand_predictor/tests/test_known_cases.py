#!/usr/bin/env python3
from __future__ import annotations

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from decomp import parse_canonical, find_decomposition

AND2  = '0b0001'
OR2   = '0b0111'
NAND2 = '0b1110'
NOR2  = '0b1000'
XOR2  = '0b0110'
XNOR2 = '0b1001'

def _run(name, target_key, base_key, expect_status, expect_base=None,
         expect_inv=None, max_gates=12, timeout_s=120):
    tn, tt = parse_canonical(target_key)
    bn, bb = parse_canonical(base_key)
    t0 = time.time()
    r = find_decomposition(tt, tn, bb, bn,
                           max_gates=max_gates, timeout_s=timeout_s)
    dt = time.time() - t0
    ok = (r['status'] == expect_status)
    if ok and expect_status == 'OK':
        if expect_base is not None and r['base_count'] != expect_base:
            ok = False
        if expect_inv is not None and r['inv_count'] != expect_inv:
            ok = False
    mark = 'PASS' if ok else 'FAIL'
    detail = (f"base={r.get('base_count')} inv={r.get('inv_count')} "
              f"total={r.get('total')}" if r['status'] == 'OK'
              else r['status'])
    print(f"  [{mark}] {name:35s} ({dt:5.1f}s)  -> {detail}")
    return ok

def main():
    print("=== demand_predictor Phase 1 sanity tests ===")
    print()
    passed = 0
    failed = 0

    cases = [
        ('AND2 <- AND2 (identity)',   AND2,  AND2,  'OK', 1, 0),
        ('XOR2 <- XOR2 (identity)',   XOR2,  XOR2,  'OK', 1, 0),
        ('NAND2 <- AND2',             NAND2, AND2,  'OK', 1, 1),
        ('AND2 <- NAND2',             AND2,  NAND2, 'OK', 1, 1),
        ('XNOR2 <- XOR2',             XNOR2, XOR2,  'OK', 1, 1),
        ('OR2 <- AND2',               OR2,   AND2,  'OK', 1, 3),
        ('NOR2 <- AND2',              NOR2,  AND2,  'OK', 1, 2),
        ('XOR2 <- NAND2',             XOR2,  NAND2, 'OK', 4, 0),
        ('XOR2 <- AND2 (status only)', XOR2, AND2,  'OK'),
        ('AND2 <- XOR2 (any result)', AND2,  XOR2,  None),
    ]

    for case in cases:
        name = case[0]
        if case[3] is None:
            valid_statuses = {
                'OK', 'IMPOSSIBLE_AFFINE', 'IMPOSSIBLE_MAXGATES',
                'TIMEOUT', 'PROJECTION',
            }
            tn, tt = parse_canonical(case[1])
            bn, bb = parse_canonical(case[2])
            t0 = time.time()
            r = find_decomposition(tt, tn, bb, bn, max_gates=12, timeout_s=120)
            dt = time.time() - t0
            ok = r['status'] in valid_statuses
            mark = 'PASS' if ok else 'FAIL'
            print(f"  [{mark}] {name:35s} ({dt:5.1f}s)  -> {r['status']}")
            if ok:
                passed += 1
            else:
                print(f"         unexpected status: {r!r}")
                failed += 1
            continue
        ok = _run(*case)
        if ok:
            passed += 1
        else:
            failed += 1

    print()
    print(f"=== Result: {passed} passed, {failed} failed ===")
    sys.exit(0 if failed == 0 else 1)

if __name__ == '__main__':
    main()
