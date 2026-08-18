#!/usr/bin/env python3
"""
Generate cell_classes.csv: the name -> (cell_class, canonical_key) map that
train/dataset_diff.py::_load_cell_classes reads.

For every SO3 lib cell:
  cell_class    one of logic | buf_inv | seq | const
  canonical_key raw truth-table bits ("0b...", A=MSB, alphabetical pin order),
                empty for sequential cells.

SO3 liberty output-pin functions use only ! (NOT), * (AND), + (OR) and parens
(XOR/XNOR are written as expanded sum-of-products), so each function maps 1:1 to
a Python boolean expression with identical operator precedence and is evaluated
over every input assignment to build the truth table.

The raw truth table is the SAME space features_l0.csv's 'canonical' column uses
(dataset_diff reduces both sides to perm-min before matching), so the run's
canonical_key for the 272 vocab cells must equal features_l0's 'canonical'. That
equality is asserted as a validation gate before the file is written.

Usage:
    python3 gen_cell_classes.py --lib-dir "$PDK_ROOT/lib"
"""
from __future__ import annotations

import argparse
import csv
import os
import re
import sys

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_DEFAULT_OUT = os.path.join(_THIS_DIR, "cell_classes.csv")
_DEFAULT_VOCAB = os.path.join(_THIS_DIR, "features_l0.csv")

_VAR_RE = re.compile(r"[A-Za-z][A-Za-z0-9_]*")
_CELL_RE = re.compile(r"^\s*cell\s*\(\s*([^)\s]+)\s*\)\s*\{")
_FF_RE = re.compile(r"^\s*(ff|ff_bank|latch|latch_bank|statetable)\s*\(")
_FUNC_RE = re.compile(r'^\s*function\s*:\s*"([^"]*)"\s*;')

def _truth_table(func: str) -> tuple[int, str]:
    variables = sorted(set(_VAR_RE.findall(func)))
    n = len(variables)
    if n == 0:
        raise ValueError(f"no input variables in function {func!r}")
    py = func.replace("!", " not ").replace("*", " and ").replace("+", " or ")
    bits = []
    for combo in range(1 << n):
        env = {v: bool((combo >> (n - 1 - i)) & 1) for i, v in enumerate(variables)}
        val = eval(py, {"__builtins__": {}}, env)
        bits.append("1" if val else "0")
    return n, "".join(bits)

def _is_constant_bits(bits: str) -> bool:
    return set(bits) == {"0"} or set(bits) == {"1"}

def _is_buf_inv_bits(n: int, bits: str) -> bool:
    return n == 1 and bits in ("01", "10")

def parse_lib(lib_path: str) -> tuple[str, str, list[str]]:
    cell_name = ""
    funcs: list[str] = []
    is_seq = False
    with open(lib_path) as f:
        for line in f:
            if not cell_name:
                m = _CELL_RE.match(line)
                if m:
                    cell_name = m.group(1)
            if _FF_RE.match(line):
                is_seq = True
            m = _FUNC_RE.match(line)
            if m and m.group(1).strip():
                funcs.append(m.group(1).strip())
    if not cell_name:
        cell_name = os.path.basename(lib_path).split("_tt_")[0]
    if is_seq:
        return cell_name, "", []
    return cell_name, (funcs[0] if funcs else ""), funcs

def classify(lib_path: str) -> tuple[str, str, str]:
    cell_name, func, all_funcs = parse_lib(lib_path)
    if not func and not all_funcs:
        return cell_name, "seq", ""
    n, bits = _truth_table(func)
    canon = "0b" + bits
    if _is_constant_bits(bits):
        return cell_name, "const", canon
    if _is_buf_inv_bits(n, bits):
        return cell_name, "buf_inv", canon
    return cell_name, "logic", canon

def build(lib_dir: str) -> dict[str, tuple[str, str]]:
    out: dict[str, tuple[str, str]] = {}
    for fname in sorted(os.listdir(lib_dir)):
        if not fname.endswith(".lib"):
            continue
        try:
            name, cls, canon = classify(os.path.join(lib_dir, fname))
        except ValueError as e:
            # A skipped .lib drops that cell from cell_classes entirely, and every
            # downstream lookup then treats it as a cell the library never had.
            raise SystemExit(
                f"ERROR: cannot classify {os.path.join(lib_dir, fname)}: {e}"
            )
        if name in out and out[name] != (cls, canon):
            raise ValueError(
                f"conflicting classes for {name}: {out[name]} vs {(cls, canon)}"
            )
        out[name] = (cls, canon)
    return out

def validate(cell_map: dict[str, tuple[str, str]], vocab_csv: str) -> None:
    if not os.path.isfile(vocab_csv):
        print(f"[warn] vocab {vocab_csv} absent, skipping validation gate", file=sys.stderr)
        return
    sys.path.insert(0, os.path.join(_THIS_DIR, "train"))
    from dataset_diff import _p_canonical_of_bits

    checked = mism = missing = 0
    with open(vocab_csv, newline="") as f:
        for row in csv.DictReader(f):
            name = row["cell_name"].strip()
            want = row["canonical"].strip()
            if not want:
                continue
            entry = cell_map.get(name)
            if entry is None:
                missing += 1
                continue
            checked += 1
            mine_pc = _p_canonical_of_bits(entry[1]) if entry[1] else ""
            if mine_pc != _p_canonical_of_bits(want):
                mism += 1
                print(f"[pdk-bug?] {name}: lib={entry[1]}->{mine_pc} "
                      f"vocab={want}->{_p_canonical_of_bits(want)}", file=sys.stderr)
    frac = mism / checked if checked else 0.0
    print(f"[validate] P-canonical checked={checked} agree={checked - mism} "
          f"lib-vs-vocab-disagree={mism} ({frac:.1%}) vocab-not-in-lib={missing}")
    if frac > 0.25:
        raise SystemExit(
            f"validation gate FAILED: {frac:.1%} P-canonical mismatch vs {vocab_csv} "
            "indicates a systematic convention error, aborting."
        )

def main(argv=None) -> None:
    p = argparse.ArgumentParser(allow_abbrev=False, description=__doc__)
    p.add_argument("--lib-dir", default=os.path.join(os.environ.get("PDK_ROOT", ""), "lib"),
                   help="SO3 lib directory (default: $PDK_ROOT/lib).")
    p.add_argument("--out", default=_DEFAULT_OUT, help=f"Output CSV (default: {_DEFAULT_OUT}).")
    p.add_argument("--vocab", default=_DEFAULT_VOCAB,
                   help="features_l0.csv for the validation gate.")
    p.add_argument("--no-validate", action="store_true", help="Skip the vocab validation gate.")
    args = p.parse_args(argv)

    if not os.path.isdir(args.lib_dir):
        p.error(f"--lib-dir not found: {args.lib_dir!r} (set PDK_ROOT or pass --lib-dir)")

    cell_map = build(args.lib_dir)
    if not cell_map:
        sys.exit(f"ERROR: no cells parsed from {args.lib_dir}")
    if not args.no_validate:
        validate(cell_map, args.vocab)

    with open(args.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["name", "cell_class", "canonical_key"],
                           lineterminator="\n")
        w.writeheader()
        for name in sorted(cell_map):
            cls, canon = cell_map[name]
            w.writerow({"name": name, "cell_class": cls, "canonical_key": canon})

    by_cls: dict[str, int] = {}
    for cls, _ in cell_map.values():
        by_cls[cls] = by_cls.get(cls, 0) + 1
    print(f"Wrote {len(cell_map)} cells -> {args.out}")
    for cls in sorted(by_cls):
        print(f"  {cls}: {by_cls[cls]}")

if __name__ == "__main__":
    main()
