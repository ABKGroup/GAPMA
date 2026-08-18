#!/usr/bin/env python3

from __future__ import annotations
import sys
from collections import defaultdict
from itertools import permutations
from pathlib import Path

_TOOLS = Path(__file__).resolve().parents[4]
_MINING = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(_MINING / "db" / "cluster_db" / "python"))
sys.path.insert(0, str(_MINING / "db" / "netlist_db" / "python"))
sys.path.insert(0, str(_MINING / "db" / "cell_db" / "python"))
sys.path.insert(0, str(_MINING / "db" / "netlist_db" / "python"))
sys.path.insert(0, str(_MINING / "db" / "cell_db" / "python"))
sys.path.insert(0, str(_TOOLS))
from cluster_db import read_cluster_db
from netlist_db import read_netlist_db
from utils.runtime_logger import RuntimeLogger

def expand_truth_table(tt: str, orig_inputs: list, union_inputs: list) -> str:
    if not tt or not orig_inputs:
        return ""
    m = len(union_inputs)
    n = len(orig_inputs)
    rows = 1 << m

    mapping = [-1] * m
    for ui in range(m):
        for oi in range(n):
            if union_inputs[ui] == orig_inputs[oi]:
                mapping[ui] = oi
                break

    expanded = ['0'] * rows
    for row in range(rows):
        orig_idx = 0
        for oi in range(n):
            for ui in range(m):
                if mapping[ui] == oi:
                    orig_idx |= (((row >> (m - 1 - ui)) & 1) << (n - 1 - oi))
                    break
        if orig_idx < len(tt):
            expanded[row] = tt[orig_idx]
    return ''.join(expanded)

def canonical_bits(bits_str: str, n: int) -> str:
    if n <= 1:
        return bits_str
    rows = 1 << n
    best = None
    for perm in permutations(range(n)):
        remap = [0] * rows
        for idx in range(rows):
            bi = 0
            for i in range(n):
                bi |= ((idx >> (n - 1 - i)) & 1) << (n - 1 - perm[i])
            remap[idx] = bi
        candidate = ''.join(bits_str[remap[idx]] for idx in range(rows))
        if best is None or candidate < best:
            best = candidate
    return best

def canonical_bits_joint(output_bits: list, n: int) -> list:
    k = len(output_bits)
    if k == 0:
        return []
    if k == 1:
        return [canonical_bits(output_bits[0], n)]
    if n <= 0:
        return output_bits

    rows = 1 << n
    all_remaps = []
    for perm in permutations(range(n)):
        remap = [0] * rows
        for idx in range(rows):
            bi = 0
            for i in range(n):
                bi |= ((idx >> (n - 1 - i)) & 1) << (n - 1 - perm[i])
            remap[idx] = bi
        all_remaps.append(remap)

    best = None
    for remap in all_remaps:
        result = []
        for oi in range(k):
            result.append(''.join(output_bits[oi][remap[idx]] for idx in range(rows)))
        result.sort()
        if best is None or result < best:
            best = result
    return best

def test_M1_expand_truth_table():
    print("\n" + "=" * 70)
    print("  [M1] expand_truth_table Correctness")
    print("=" * 70)

    tests = [
        ("1010", ["A", "B"], ["A", "B"], "1010"),
        ("1010", ["A", "B"], ["B", "A"], "1100"),
        ("0001", ["A", "B"], ["A", "B", "C"], "00000011"),
        ("0001", ["A", "B"], ["C", "A", "B"], "00010001"),
        ("0110", ["A", "B"], ["B", "C", "A"], "01011010"),
    ]

    passed = 0
    failed = 0
    for tt, orig, union, expected in tests:
        result = expand_truth_table(tt, orig, union)
        if result == expected:
            passed += 1
        else:
            failed += 1
            print(f"  FAIL: tt={tt} orig={orig} union={union}")
            print(f"    expected={expected} got={result}")

    import random
    random.seed(42)
    for _ in range(100):
        n = random.randint(2, 4)
        inputs = [f"x{i}" for i in range(n)]
        tt = ''.join(random.choice('01') for _ in range(1 << n))
        extra = random.randint(0, 2)
        if extra + n > 6:
            extra = 0
        union = list(inputs) + [f"e{i}" for i in range(extra)]
        random.shuffle(union)
        expanded = expand_truth_table(tt, inputs, union)
        ok = True
        m = len(union)
        for orig_row in range(1 << n):
            exp_row = 0
            for ui in range(m):
                for oi in range(n):
                    if union[ui] == inputs[oi]:
                        exp_row |= ((orig_row >> (n - 1 - oi)) & 1) << (m - 1 - ui)
                        break
            if expanded[exp_row] != tt[orig_row]:
                ok = False
                break
        if ok:
            passed += 1
        else:
            failed += 1
            if failed <= 3:
                print(f"  FAIL (property): tt={tt} inputs={inputs} union={union}")

    total = passed + failed
    status = "PASS" if failed == 0 else "FAIL"
    print(f"  Tests: {total}, Passed: {passed}, Failed: {failed}")
    print(f"  Status: {status}")
    return status, total, passed, failed

def test_M2_canonical_bits_joint():
    print("\n" + "=" * 70)
    print("  [M2] canonical_bits_joint Correctness")
    print("=" * 70)

    passed = 0
    failed = 0

    import random
    random.seed(42)
    for _ in range(50):
        n = random.randint(2, 4)
        tt = ''.join(random.choice('01') for _ in range(1 << n))
        joint = canonical_bits_joint([tt], n)
        regular = canonical_bits(tt, n)
        if joint == [regular]:
            passed += 1
        else:
            failed += 1
            if failed <= 3:
                print(f"  FAIL (single): tt={tt} joint={joint} regular={regular}")

    for _ in range(50):
        n = random.randint(2, 3)
        rows = 1 << n
        tt_a = ''.join(random.choice('01') for _ in range(rows))
        tt_b = ''.join(random.choice('01') for _ in range(rows))
        joint_ab = canonical_bits_joint([tt_a, tt_b], n)
        joint_ba = canonical_bits_joint([tt_b, tt_a], n)
        if joint_ab == joint_ba:
            passed += 1
        else:
            failed += 1
            if failed <= 3:
                print(f"  FAIL (order): n={n} tt_a={tt_a} tt_b={tt_b}")
                print(f"    joint_ab={joint_ab} joint_ba={joint_ba}")

    for _ in range(30):
        n = random.randint(2, 3)
        rows = 1 << n
        tt_a = ''.join(random.choice('01') for _ in range(rows))
        tt_b = ''.join(random.choice('01') for _ in range(rows))
        joint_orig = canonical_bits_joint([tt_a, tt_b], n)

        perm = list(range(n))
        random.shuffle(perm)
        remap = [0] * rows
        for idx in range(rows):
            bi = 0
            for i in range(n):
                bi |= ((idx >> (n - 1 - i)) & 1) << (n - 1 - perm[i])
            remap[idx] = bi
        perm_a = ''.join(tt_a[remap[idx]] for idx in range(rows))
        perm_b = ''.join(tt_b[remap[idx]] for idx in range(rows))
        joint_perm = canonical_bits_joint([perm_a, perm_b], n)

        if joint_orig == joint_perm:
            passed += 1
        else:
            failed += 1
            if failed <= 3:
                print(f"  FAIL (perm-invariance): perm={perm}")

    total = passed + failed
    status = "PASS" if failed == 0 else "FAIL"
    print(f"  Tests: {total}, Passed: {passed}, Failed: {failed}")
    print(f"  Status: {status}")
    return status, total, passed, failed

def test_M3_merge_instance_consistency(cluster_cdb_path, layout_cdb_path, merge_instances_path):
    print("\n" + "=" * 70)
    print("  [M3] Merge Instance Consistency")
    print("=" * 70)

    p = Path(merge_instances_path)
    if not p.exists():
        print(f"  SKIP: {merge_instances_path} not found")
        return "SKIP", 0, 0, 0

    cdb = read_cluster_db(cluster_cdb_path)
    ldb = read_netlist_db(layout_cdb_path)

    net_to_cells = defaultdict(set)
    for net in ldb.nets:
        for pin in net.pins:
            if pin.instance_id > 0:
                net_to_cells[net.name].add(pin.instance_id)

    instances = []
    current = {}
    with open(p) as f:
        for line in f:
            line = line.strip()
            if line.startswith("clst"):
                if current:
                    instances.append(current)
                cell_ids = [int(x) for x in line.split(":")[1].split()]
                current = {"cell_ids": cell_ids}
            elif line.startswith("inputs:"):
                current["inputs"] = line[len("inputs:"):].split()
            elif line.startswith("outputs:"):
                current["outputs"] = line[len("outputs:"):].split()
    if current:
        instances.append(current)

    print(f"  Merge instances loaded: {len(instances)}")

    cluster_by_id = cdb.cluster_by_id

    checked = 0
    cell_errors = 0
    output_errors = 0
    output_boundary_errors = 0

    for mi in instances:
        cell_set = set(mi["cell_ids"])
        for out_net in mi.get("outputs", []):
            cells_on_net = net_to_cells.get(out_net, set())
            is_boundary = any(c not in cell_set for c in cells_on_net) or not cells_on_net
            if not is_boundary:
                output_boundary_errors += 1
                if output_boundary_errors <= 3:
                    print(f"  FAIL: output '{out_net}' is not boundary (all cells in merged set)")
        checked += 1

    status = "PASS" if output_boundary_errors == 0 else "FAIL"
    print(f"  Checked: {checked}")
    print(f"  Output boundary errors: {output_boundary_errors}")
    print(f"  Status: {status}")
    return status, checked, checked - output_boundary_errors, output_boundary_errors

def test_M5_end_to_end_tt(cluster_cdb_path, layout_cdb_path, merge_instances_path):
    print("\n" + "=" * 70)
    print("  [M5] End-to-End Merged TT Verification")
    print("=" * 70)

    p = Path(merge_instances_path)
    if not p.exists():
        print(f"  SKIP: {merge_instances_path} not found")
        return "SKIP", 0, 0, 0

    cdb = read_cluster_db(cluster_cdb_path)

    clusters_by_output = defaultdict(list)
    for c in cdb.clusters:
        if len(c.outputs) == 1:
            clusters_by_output[c.outputs[0]].append(c)

    instances = []
    current = {}
    with open(p) as f:
        for line in f:
            line = line.strip()
            if line.startswith("clst"):
                if current:
                    instances.append(current)
                cell_ids = [int(x) for x in line.split(":")[1].split()]
                current = {"cell_ids": set(cell_ids), "raw_cells": cell_ids}
            elif line.startswith("inputs:"):
                current["inputs"] = line[len("inputs:"):].split()
            elif line.startswith("outputs:"):
                current["outputs"] = line[len("outputs:"):].split()
    if current:
        instances.append(current)

    print(f"  Merge instances: {len(instances)}")

    checked = 0
    passed = 0
    failed = 0
    skipped = 0

    for mi in instances[:2000]:
        merged_cells = mi["cell_ids"]
        merged_inputs = sorted(mi.get("inputs", []))
        merged_outputs = mi.get("outputs", [])

        if len(merged_outputs) != 2:
            skipped += 1
            continue

        # Several clusters can drive the same output with cells inside the merge,
        # and picking purely by size lands on one whose inputs the merge does not
        # carry, which no expansion into union_inputs can represent. Only a
        # cluster whose inputs all appear in union_inputs is a constituent of
        # this merge.
        union_input_set = set(merged_inputs)
        source_clusters = []
        for out_net in merged_outputs:
            best = None
            for c in clusters_by_output.get(out_net, []):
                c_cells = set(c.cell_ids)
                if not c_cells.issubset(merged_cells):
                    continue
                if not set(c.input_order).issubset(union_input_set):
                    continue
                if best is None or len(c.cell_ids) > len(best.cell_ids):
                    best = c
            if best:
                source_clusters.append(best)

        if len(source_clusters) != 2:
            skipped += 1
            continue

        union_inputs = merged_inputs
        expanded_tts = []
        for sc in source_clusters:
            exp = expand_truth_table(sc.truth_table, sc.input_order, union_inputs)
            if not exp:
                break
            expanded_tts.append(exp)

        if len(expanded_tts) != 2:
            skipped += 1
            continue

        n = len(union_inputs)
        joint = canonical_bits_joint(expanded_tts, n)

        if not joint or not all(len(j) == (1 << n) for j in joint):
            failed += 1
            if failed <= 3:
                print(f"  FAIL: bad joint canonical shape for cells={sorted(merged_cells)}")
            checked += 1
            continue

        roundtrip_ok = True
        for sc, exp in zip(source_clusters, expanded_tts):
            m = len(union_inputs)
            n_orig = len(sc.input_order)
            for orig_row in range(1 << n_orig):
                exp_row = 0
                for ui in range(m):
                    for oi, inp in enumerate(sc.input_order):
                        if union_inputs[ui] == inp:
                            exp_row |= ((orig_row >> (n_orig - 1 - oi)) & 1) << (m - 1 - ui)
                            break
                if exp_row < len(exp) and orig_row < len(sc.truth_table):
                    if exp[exp_row] != sc.truth_table[orig_row]:
                        roundtrip_ok = False
                        break
            if not roundtrip_ok:
                break

        if roundtrip_ok:
            passed += 1
        else:
            failed += 1
            if failed <= 3:
                print(f"  FAIL: expanded TT roundtrip mismatch for cells={sorted(merged_cells)}")

        checked += 1

    status = "PASS" if failed == 0 else "FAIL"
    print(f"  Checked: {checked}, Passed: {passed}, Failed: {failed}, Skipped: {skipped}")
    print(f"  Status: {status}")
    return status, checked, passed, failed

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Merge integrity verification")
    parser.add_argument("--cluster-cdb", default="", help="cluster.cdb path")
    parser.add_argument("--layout-cdb", default="", help="netlist.cdb path")
    parser.add_argument("--merge-instances", default="", help="*_instances_2out.out path")
    parser.add_argument("--tests", default="M1,M2", help="Comma-separated test IDs")
    args = parser.parse_args()

    rl = RuntimeLogger("verify_merge_integrity")

    tests = [t.strip().upper() for t in args.tests.split(",")]

    print("=" * 70)
    print("  Merge Integrity Verification Suite")
    print("=" * 70)

    results = {}

    if "M1" in tests:
        with rl.step("test M1: expand_truth_table"):
            results["M1"] = test_M1_expand_truth_table()

    if "M2" in tests:
        with rl.step("test M2: canonical_bits_joint"):
            results["M2"] = test_M2_canonical_bits_joint()

    if "M3" in tests:
        if not args.cluster_cdb or not args.layout_cdb or not args.merge_instances:
            print("\n  [M3] SKIP: requires --cluster-cdb, --layout-cdb, --merge-instances")
            results["M3"] = ("SKIP", 0, 0, 0)
        else:
            with rl.step("test M3: merge instance consistency"):
                results["M3"] = test_M3_merge_instance_consistency(
                    args.cluster_cdb, args.layout_cdb, args.merge_instances)

    if "M5" in tests:
        if not args.cluster_cdb or not args.merge_instances:
            print("\n  [M5] SKIP: requires --cluster-cdb, --merge-instances")
            results["M5"] = ("SKIP", 0, 0, 0)
        else:
            with rl.step("test M5: end-to-end TT"):
                results["M5"] = test_M5_end_to_end_tt(
                    args.cluster_cdb, args.layout_cdb, args.merge_instances)

    print("\n" + "=" * 70)
    print("  SUMMARY")
    print("=" * 70)
    all_pass = True
    for test_id in sorted(results.keys()):
        r = results[test_id]
        status = r[0]
        print(f"  [{test_id}] {status}")
        if status == "FAIL":
            all_pass = False

    if all_pass:
        print("\n  ALL TESTS PASSED")
    else:
        print("\n  SOME TESTS FAILED")
        rl.done()
        sys.exit(1)

    rl.done()

if __name__ == "__main__":
    main()
