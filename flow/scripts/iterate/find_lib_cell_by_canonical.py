#!/usr/bin/env python3
"""
find_lib_cell_by_canonical.py

Given a set of target P-canonical truth-table keys (e.g. "0b10101000"),
search all SO3 .lib files for cells whose output-pin `function` attribute
evaluates to the *same* P-canonical truth table. Returns the cell names
that match.

The cell name is COMPLETELY IGNORED. Only the lib `function` attribute is
trusted, evaluated by Python boolean evaluation, then reduced to the mining
P-canonical key (perm-min MSB-first truth table over all input permutations).

Usage:
    python3 find_lib_cell_by_canonical.py \\
        --lib-dir   "$PDK_ROOT/lib" \\
        --canonicals 0b10101000 0b00011111 ... \\
        --prefer-suffix _X1 \\
        --out       mapping.json

Output: JSON dict mapping each canonical -> list of matching cell names.
"""
import argparse
import json
import os
import re
import sys
from itertools import permutations, product
from pathlib import Path

def mining_canonical_int(lsb_tt: int, n: int) -> int:
    rows = 1 << n
    bits = ['0'] * rows
    for k in range(rows):
        a = sum(((k >> j) & 1) << (n - 1 - j) for j in range(n))
        bits[a] = '1' if (lsb_tt >> k) & 1 else '0'
    bits_str = ''.join(bits)
    best = bits_str
    for perm in permutations(range(n)):
        remap = [
            sum(((idx >> (n - 1 - i)) & 1) << (n - 1 - perm[i]) for i in range(n))
            for idx in range(rows)
        ]
        cand = ''.join(bits_str[remap[idx]] for idx in range(rows))
        if cand < best:
            best = cand
    return int(best, 2)

def canonical_to_int(spec: str) -> int:
    s = spec.strip()
    if s.startswith("0b"):
        return int(s, 2)
    if s.startswith("0x"):
        return int(s, 16)
    return int(s)

def n_inputs_from_canonical(spec: str) -> int:
    s = spec.strip()
    if s.startswith("0b"):
        nbits = len(s) - 2
    elif s.startswith("0x"):
        nbits = (len(s) - 2) * 4
    else:
        raise ValueError(f"cannot derive bit width from {spec!r} — use 0b... or 0x... prefix")
    n = 0
    while (1 << n) < nbits:
        n += 1
    if (1 << n) != nbits:
        raise ValueError(f"TT bit width {nbits} (from {spec!r}) is not a power of 2")
    return n

_CELL_RE = re.compile(r"\bcell\s*\(\s*([A-Za-z0-9_]+)\s*\)\s*\{", re.MULTILINE)
_PIN_RE = re.compile(r"\bpin\s*\(\s*([A-Za-z0-9_\[\]]+)\s*\)\s*\{")
_FUNCTION_RE = re.compile(
    r"function\s*:\s*\"([^\"]*)\"\s*;", re.MULTILINE
)
_DIRECTION_RE = re.compile(r"direction\s*:\s*(\w+)\s*;")

def _matching_brace(text: str, open_idx: int) -> int:
    depth = 0
    i = open_idx
    while i < len(text):
        ch = text[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1

_FF_PATTERN_RE = re.compile(r"\b(?:ff|latch)\s*\(", re.IGNORECASE)
_AREA_RE = re.compile(r"\barea\s*:\s*([0-9eE.+\-]+)\s*;")

def parse_cells(lib_text: str):
    seq_skipped = 0
    multi_skipped = 0
    for m in _CELL_RE.finditer(lib_text):
        cell_name = m.group(1)
        brace_open = lib_text.find("{", m.end() - 1)
        if brace_open < 0:
            continue
        brace_close = _matching_brace(lib_text, brace_open)
        if brace_close < 0:
            continue
        body = lib_text[brace_open + 1: brace_close]

        if _FF_PATTERN_RE.search(body):
            seq_skipped += 1
            continue

        output_pins = []
        input_pins = []
        i = 0
        while True:
            pm = _PIN_RE.search(body, i)
            if not pm:
                break
            pin_name = pm.group(1)
            pin_brace_open = body.find("{", pm.end() - 1)
            if pin_brace_open < 0:
                break
            pin_brace_close = _matching_brace(body, pin_brace_open)
            if pin_brace_close < 0:
                break
            pin_body = body[pin_brace_open + 1: pin_brace_close]
            dm = _DIRECTION_RE.search(pin_body)
            direction = dm.group(1) if dm else "input"
            if direction == "output":
                fm = _FUNCTION_RE.search(pin_body)
                if fm:
                    output_pins.append((pin_name, fm.group(1).strip()))
            elif direction == "input":
                input_pins.append(pin_name)
            i = pin_brace_close + 1

        if len(output_pins) > 1:
            multi_skipped += 1
            continue

        if output_pins and input_pins:
            out_pin, out_function = output_pins[0]
            area = float("inf")
            am = _AREA_RE.search(body)
            if am:
                try:
                    area = float(am.group(1))
                except ValueError:
                    area = float("inf")
            yield cell_name, out_function, sorted(input_pins), area

    if seq_skipped or multi_skipped:
        print(f"[verify] parser filter: skipped {seq_skipped} sequential and "
              f"{multi_skipped} multi-output cells (project policy)",
              file=sys.stderr)

_OP_REPLACEMENTS = [
    (re.compile(r"\s+"), ""),
    (re.compile(r"\*"), " and "),
    (re.compile(r"&"), " and "),
    (re.compile(r"\+"), " or "),
    (re.compile(r"\|"), " or "),
    (re.compile(r"\^"), " ^ "),
    (re.compile(r"!"), " not "),
    (re.compile(r"~"), " not "),
]

def _to_python_expr(liberty_expr: str, input_pins) -> str:
    s = liberty_expr.replace("\n", " ").strip()
    s = s.replace("'", "")
    s = re.sub(r"([A-Za-z][A-Za-z0-9_]*)\s*'", r"!\1", s)
    for rx, repl in _OP_REPLACEMENTS:
        s = rx.sub(repl, s)
    return s

def evaluate_lsb_tt(function_expr: str, input_pins) -> int:
    py_expr = _to_python_expr(function_expr, input_pins)
    n = len(input_pins)
    tt = 0
    for k in range(1 << n):
        env = {pin: bool((k >> j) & 1) for j, pin in enumerate(input_pins)}
        try:
            val = bool(eval(py_expr, {"__builtins__": {}}, env))
        except Exception as e:
            raise RuntimeError(f"failed to eval '{function_expr}' (translated: '{py_expr}') with pins {input_pins}: {e}")
        if val:
            tt |= (1 << k)
    return tt

def build_canonical_index(lib_dir: Path):
    seen = set()
    for lib_path in sorted(lib_dir.rglob("*.lib")):
        try:
            text = lib_path.read_text(errors="replace")
        except OSError as e:
            # Skipping an unreadable .lib drops every cell it defines from the
            # index, so a cell that exists in the PDK is reported as absent and
            # the caller either regenerates it or discards the candidate.
            raise SystemExit(
                f"ERROR: cannot read library {lib_path}: {e}\n"
                f"  The canonical index must cover every .lib under {lib_dir}."
            )
        for cell_name, func, in_pins, area in parse_cells(text):
            if not in_pins:
                continue
            n = len(in_pins)
            if n > 6:
                continue
            try:
                lsb_tt = evaluate_lsb_tt(func, in_pins)
            except RuntimeError as e:
                # A cell whose function cannot be evaluated is missing from the
                # index, which reads downstream as "this logic function is not
                # in the PDK". That must not pass as a warning.
                raise SystemExit(
                    f"ERROR: cannot evaluate the function of "
                    f"{lib_path.name}/{cell_name}: {e}"
                )
            p_can = mining_canonical_int(lsb_tt, n)
            key = (cell_name, n, p_can)
            if key in seen:
                continue
            seen.add(key)
            yield cell_name, n, p_can, lib_path, area

def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n", 1)[0])
    p.add_argument("--lib-dir", required=True, type=Path)
    p.add_argument("--lef-dir", type=Path, default=None,
                   help="If set, every candidate cell must also have a LEF "
                        "file at <lef-dir>/<cell_name>.lef. Candidates with "
                        "LIB but missing LEF are rejected (logged to stderr). "
                        "Default off only for backward compatibility — pipelines "
                        "should always pass this to avoid silent LEF-missing skips.")
    p.add_argument("--cdb-list", type=Path, default=None,
                   help="If set, every candidate cell must appear in this file "
                        "(one MACRO name per line). This is the authoritative "
                        "FC CDB: only MACROs from LEF files loaded by FC. "
                        "Candidates with LIB+LEF that are absent from the loaded "
                        "LEF (e.g. plain 3input0x37_X1 when only the DH variant "
                        "is loaded) are rejected, preventing SEL-005. "
                        "Recommended: pass fc_cdb_macros.txt here.")
    p.add_argument("--canonicals", nargs="+", required=True,
                   help="target P-canonical keys (e.g. 0b10101000 0b11111110)")
    p.add_argument("--n-inputs", type=int, default=None,
                   help="if set, restrict matches to cells with this input count")
    p.add_argument("--prefer-suffix", default="_X1",
                   help="prefer cell names ending with this suffix (default _X1)")
    p.add_argument("--require-suffix", default=None,
                   help="keep ONLY function-matched cells whose name contains "
                        "this drive token (e.g. _X2). Drive strength exists "
                        "only in the name-index convention, so this is the one "
                        "legitimate name-based filter — applied INSIDE the "
                        "function-based search, never as a post-hoc substring "
                        "check on its output.")
    p.add_argument("--out", type=Path, default=None,
                   help="output JSON; if omitted, prints to stdout")
    p.add_argument("--single", action="store_true",
                   help="emit one preferred cell name per canonical, newline-separated; "
                        "empty line for unmatched. Useful for shell pipelines.")
    args = p.parse_args()

    if not args.lib_dir.is_dir():
        sys.exit(f"ERROR: lib dir not found: {args.lib_dir}")

    targets = {}
    for spec in args.canonicals:
        v = canonical_to_int(spec)
        try:
            n_from_spec = n_inputs_from_canonical(spec)
        except ValueError as e:
            sys.exit(f"ERROR: {e}")
        if args.n_inputs is not None and args.n_inputs != n_from_spec:
            sys.exit(f"ERROR: --n-inputs {args.n_inputs} contradicts bit-width-derived n={n_from_spec} for {spec}")
        rows = 1 << n_from_spec
        bits = format(v, f"0{rows}b")
        best = bits
        for perm in permutations(range(n_from_spec)):
            remap = [
                sum(((idx >> (n_from_spec - 1 - i)) & 1) << (n_from_spec - 1 - perm[i])
                    for i in range(n_from_spec))
                for idx in range(rows)
            ]
            cand = ''.join(bits[remap[idx]] for idx in range(rows))
            if cand < best:
                best = cand
        v_min = int(best, 2)
        if v_min != v:
            print(f"[verify] NOTE: query {spec} is not perm-min; canonicalized "
                  f"0b{bits} -> 0b{best}", file=sys.stderr)
        targets[spec] = (v_min, n_from_spec)

    index = {}
    print(f"[verify] scanning lib dir: {args.lib_dir}", file=sys.stderr)
    n_cells = 0
    for cell_name, n, p_can, lib_path, area in build_canonical_index(args.lib_dir):
        index.setdefault((p_can, n), []).append((cell_name, str(lib_path), n, area))
        n_cells += 1
    print(f"[verify] indexed {n_cells} cell entries", file=sys.stderr)

    result = {}
    for spec, (target_int, n_target) in targets.items():
        candidates = list(index.get((target_int, n_target), []))
        if args.require_suffix:
            candidates = [c for c in candidates if args.require_suffix in c[0]]
        def sort_key(item):
            cell_name, _lib_path, n_in, area = item
            preferred = 0 if cell_name.endswith(args.prefer_suffix) else 1
            return (preferred, n_in, area, cell_name)
        candidates.sort(key=sort_key)

        if args.lef_dir is not None:
            filtered = []
            for cn, lp, n_in, area in candidates:
                lef_path = args.lef_dir / f"{cn}.lef"
                if lef_path.is_file():
                    filtered.append((cn, lp, n_in, area))
                else:
                    print(
                        f"[verify] REJECT (LIB-only, LEF missing): "
                        f"canonical={spec} cell={cn} expected_lef={lef_path}",
                        file=sys.stderr,
                    )
            candidates = filtered
        else:
            print(
                f"[verify] WARNING: --lef-dir not provided — LIB-only cells "
                f"may pass through and produce 0 netlist instances silently. "
                f"Pass --lef-dir <PDK>/lef to enforce LEF coexistence.",
                file=sys.stderr,
            )

        if args.cdb_list is not None:
            cdb_macros = set(args.cdb_list.read_text().splitlines())
            filtered = []
            for cn, lp, n_in, area in candidates:
                if cn in cdb_macros:
                    filtered.append((cn, lp, n_in, area))
                else:
                    print(
                        f"[verify] REJECT (not in FC CDB, SEL-005 risk): "
                        f"canonical={spec} cell={cn}",
                        file=sys.stderr,
                    )
            candidates = filtered
        else:
            print(
                f"[verify] WARNING: --cdb-list not provided — cells absent from "
                f"loaded FC LEF may propagate and cause FC SEL-005. "
                f"Pass --cdb-list <fc_cdb_macros.txt> to enforce CDB membership.",
                file=sys.stderr,
            )

        result[spec] = [
            {"cell_name": cn, "lib_path": lp} for cn, lp, _n, _a in candidates
        ]

    if args.single:
        lines = []
        for spec in args.canonicals:
            cells = result.get(spec, [])
            lines.append(cells[0]["cell_name"] if cells else "")
        body = "\n".join(lines) + "\n"
        if args.out:
            args.out.write_text(body)
        else:
            sys.stdout.write(body)
    else:
        out_json = json.dumps(result, indent=2)
        if args.out:
            args.out.write_text(out_json + "\n")
            print(f"[verify] wrote {args.out}", file=sys.stderr)
        else:
            print(out_json)

    missing = [k for k, v in result.items() if not v]
    if missing:
        print(f"ERROR: no lib cell matches for canonicals: {missing}", file=sys.stderr)
        sys.exit(2)

if __name__ == "__main__":
    main()
