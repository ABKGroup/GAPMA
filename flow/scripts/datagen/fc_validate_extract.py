#!/usr/bin/env python3
"""
fc_validate_extract.py

Design integrity check + feature/label extraction for D0 FC P&R run dirs.

Validates each run (FC log abort check, dont_use compliance) then extracts
cell timing features and cell counts into the demand_predictor payload format
expected by GAT inference (build_graph + DiffDemandGNN.forward).

Source layout (per run, under SRC_ROOT):
  <design>_L0/<sweep_id>/
    outputs_fc/route_opt.v        -- post-route Verilog (mapped netlist)
    filters/dont_use.tcl          -- cell exclusion list (run_fc.py output)
    cell_timing_features.csv      -- per-cell-type timing stats (run_fc.py output,
                                      via its own extract_cell_features.tcl call)

For each (design, sweep_id), produces under DEST_ROOT:
  designs/<design>/<sweep_id>/
    cell_counts.csv               -- cell_name, count (from timing csv)
    cell_timing_features.csv      -- copy of the source run's timing csv
    filters/only_use_lib_cells.list -- L0 lib cells used (stub for now)

This script targets the inference-time (L0 baseline) flow, not training. The
runs come from the D0 L0-only P&R sweep (per Table 5 of MLCAD2026.tex).

Usage
-----
  # Process one run dir (test first):
  python3 fc_validate_extract.py \
      --src-run "$FC_RUN_ROOT/aes_cipher_top_L0/420_5.0_2" \
      --design aes \
      --dest-root <repo>/flow/runs/l0_inference_payload

  # Process all 10 table-matched runs:
  python3 fc_validate_extract.py --all --dest-root <repo>/flow/runs/...
"""
from __future__ import annotations

import argparse
import csv
import fnmatch
import os
import re
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent

_FC_RUN_ROOT = os.environ.get("FC_RUN_ROOT")
DEFAULT_SRC_ROOT = Path(_FC_RUN_ROOT) if _FC_RUN_ROOT else None

TABLE5_RUNS = [
    ("aes",    "aes_cipher_top_L0", "420_5.0_2", "area"),
    ("aes",    "aes_cipher_top_L0", "540_7.5_2", "freq"),
    ("jpeg",   "jpeg_encoder_L0",   "1760_1.5_1", "area"),
    ("jpeg",   "jpeg_encoder_L0",   "1950_5.0_2", "freq"),
    ("sha256", "sha256_L0",         "580_2.0_3", "area"),
    ("sha256", "sha256_L0",         "760_5.5_3", "freq"),
    ("keccak", "keccak_L0",         "840_2.0_3", "area"),
    ("keccak", "keccak_L0",         "1050_6.0_2", "freq"),
    ("ldpc",   "ldpc_L0",           "4000_1.2_3", "area"),
    ("ldpc",   "ldpc_L0",           "4500_2.5_3", "freq"),
]

_FC_STAGE_LOGS = (
    "init_design.log",
    "compile.log",
    "clock_opt_cts.log",
    "clock_opt_opto.log",
    "route_auto.log",
    "route_opt.log",
)

_FC_ABORT_PATTERNS: list[tuple[str, re.Pattern]] = [
    ("PLACE_FAIL",  re.compile(r"Fail in placement")),
    ("NOT_PLACED",  re.compile(r"is not placed\.")),
    ("OVERLAP",     re.compile(r"placed overlapping")),
    ("OVER_UTIL",   re.compile(r"Over Utilization")),
    ("OPT_FAIL",    re.compile(r"Optimization failed at")),
    ("FLOW_ERR",    re.compile(r"Flow status set to Error")),
    ("SYN_ABORT",   re.compile(r"synthesis command aborted")),
]

def parse_excluded_patterns(dont_use_tcl: Path) -> list[str]:
    patterns: list[str] = []
    pat = re.compile(r'set_lib_cell_purpose\s+\*/(.*?)\s+-exclude')
    for line in dont_use_tcl.read_text().splitlines():
        m = pat.search(line)
        if m:
            patterns.append(m.group(1).strip())
    return patterns

def is_excluded(cell_name: str, patterns: list[str]) -> bool:
    return any(fnmatch.fnmatch(cell_name, p) for p in patterns)

def check_fc_logs_abort(run_dir: Path) -> None:
    logs_dir = run_dir / "logs_fc"
    if not logs_dir.is_dir():
        raise RuntimeError(
            f"logs_fc/ missing in {run_dir} — FC run may be incomplete"
        )

    hits: list[str] = []
    for stage_name in _FC_STAGE_LOGS:
        log_path = logs_dir / stage_name
        if not log_path.is_file():
            continue
        with open(log_path, "r", errors="replace") as f:
            for lineno, line in enumerate(f, 1):
                if not line.startswith("Error:"):
                    continue
                for tag, rx in _FC_ABORT_PATTERNS:
                    if rx.search(line):
                        hits.append(
                            f"  [{stage_name}:{lineno}] {tag}: {line.rstrip()[:200]}"
                        )
                        break

    if hits:
        raise RuntimeError(
            f"FC abort patterns found in {run_dir}:\n" + "\n".join(hits)
        )

def _parse_whitelist_included(text: str) -> set[str]:
    direct = re.compile(r'set_lib_cell_purpose\s+\*/(.*?)\s+-include')
    indirect = re.compile(r'get_lib_cells\s+-quiet\s+\{?\*/([^\s}]+)\}?')
    found = {m.group(1).strip() for m in direct.finditer(text)}
    found |= {m.group(1).strip() for m in indirect.finditer(text)}
    return found

def validate_dont_use_compliance(run_dir: Path, counts: dict[str, int]) -> None:
    dont_use_tcl = run_dir / "filters" / "dont_use.tcl"
    if not dont_use_tcl.is_file():
        raise FileNotFoundError(
            f"dont_use.tcl missing: {dont_use_tcl} — cannot validate cell filter"
        )

    text = dont_use_tcl.read_text()
    excluded_patterns = parse_excluded_patterns(dont_use_tcl)

    if not excluded_patterns:
        if re.search(r'set_lib_cell_purpose\s+-exclude\s+all\s+\[get_lib_cells \*/\*\]', text):
            allowed = _parse_whitelist_included(text)
            if not allowed:
                raise ValueError(
                    f"dont_use.tcl whitelist format detected but 0 -include cells found: {dont_use_tcl}"
                )
            hard_violations = []
            drive_variants = []
            sequential_extras = []
            for cn in sorted(counts):
                if cn in allowed:
                    continue
                base = re.sub(r'_X[248](_.*)?$', '_X1', cn)
                if base in allowed:
                    drive_variants.append(cn)
                elif cn.endswith('_SH'):
                    sequential_extras.append(cn)
                else:
                    hard_violations.append(cn)
            if drive_variants:
                print(
                    f"  [validate] NOTE: {len(drive_variants)} X2/X4/X8 drive-strength variant(s) "
                    f"used by hold/cts (base X1 in whitelist): "
                    + ", ".join(f"{cn}({counts[cn]})" for cn in drive_variants)
                )
            if sequential_extras:
                print(
                    f"  [validate] WARNING: {len(sequential_extras)} _SH sequential cell(s) "
                    f"outside whitelist (probe DFF artifact, skipped for logic inference): "
                    + ", ".join(f"{cn}({counts[cn]})" for cn in sequential_extras)
                )
            if hard_violations:
                raise ValueError(
                    f"Cell filter violation in {run_dir} (whitelist check):\n"
                    f"Cells in cell_counts not in dont_use -include list (not X-variants either):\n"
                    + "\n".join(f"  {cn!r} (count={counts[cn]})" for cn in hard_violations)
                )
            print(
                f"  [validate] whitelist compliance OK: {len(allowed)} allowed cells, "
                f"0 hard violations in {len(counts)} counted cells"
            )
            return
        raise ValueError(
            f"dont_use.tcl parsed but returned 0 patterns: {dont_use_tcl}\n"
            f"Expected 'set_lib_cell_purpose */<CELL> -exclude' lines."
        )

    violations: list[str] = []
    for cell_name, count in sorted(counts.items()):
        if is_excluded(cell_name, excluded_patterns):
            violations.append(f"  {cell_name!r} (count={count})")

    if violations:
        raise ValueError(
            f"Cell filter violation in {run_dir}:\n"
            f"The following cells appear in cell_counts but are in dont_use.tcl:\n"
            + "\n".join(violations)
        )

    print(
        f"  [validate] dont_use compliance OK: {len(excluded_patterns)} excluded patterns, "
        f"0 violations in {len(counts)} counted cells"
    )

def derive_cell_counts_from_timing(timing_csv: Path) -> dict[str, int]:
    if not timing_csv.exists() or timing_csv.stat().st_size == 0:
        raise FileNotFoundError(
            f"cell_timing_features.csv missing or empty: {timing_csv}"
        )
    counts: dict[str, int] = {}
    with open(timing_csv) as f:
        for row in csv.DictReader(f):
            cell_type = row.get("cell_type", "").strip()
            if not cell_type:
                continue
            try:
                n = int(float(row.get("n_instances", 0)))
            except (ValueError, TypeError):
                raise ValueError(
                    f"Non-numeric n_instances for cell_type={cell_type!r} in "
                    f"{timing_csv}: row={row!r}"
                )
            if n > 0:
                counts[cell_type] = n
    if not counts:
        raise ValueError(
            f"No cell types with n_instances > 0 in {timing_csv}"
        )
    return counts

def write_cell_counts(dest: Path, counts: dict[str, int]) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    with open(dest, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["cell_name", "count"])
        for name in sorted(counts):
            w.writerow([name, counts[name]])

def write_filters_stub(dest_dir: Path, cell_counts: dict[str, int]) -> None:
    filters_dir = dest_dir / "filters"
    filters_dir.mkdir(exist_ok=True)
    with open(filters_dir / "only_use_lib_cells.list", "w") as f:
        for name in sorted(cell_counts):
            f.write(name + "\n")
    (filters_dir / "canonical_keys.txt").write_text(
        "# placeholder — filled by inference/top10_select.py\n"
    )

def convert_one(
    *,
    src_run: Path,
    design: str,
    sweep_id: str,
    design_subdir: str,
    dest_root: Path,
) -> Path:
    if not src_run.is_dir():
        raise FileNotFoundError(f"Source run dir missing: {src_run}")

    netlist = src_run / "outputs_fc" / "route_opt.v"
    if not netlist.exists():
        raise FileNotFoundError(
            f"netlist not found: {netlist}. --src-run must be a Fusion Compiler run "
            f"directory, which holds the netlist under outputs_fc/, not a directory "
            f"with route_opt.v at its top level."
        )

    dest_dir = dest_root / "designs" / design / sweep_id
    dest_dir.mkdir(parents=True, exist_ok=True)

    check_fc_logs_abort(src_run)

    src_timing_csv = src_run / "cell_timing_features.csv"
    if not src_timing_csv.is_file():
        raise FileNotFoundError(
            f"cell_timing_features.csv missing in {src_run} — "
            f"run_fc.py's cell feature extraction has not completed for this run"
        )
    timing_csv = dest_dir / "cell_timing_features.csv"
    timing_csv.write_text(src_timing_csv.read_text())

    counts = derive_cell_counts_from_timing(timing_csv)
    write_cell_counts(dest_dir / "cell_counts.csv", counts)
    n_types = len(counts)
    n_total = sum(counts.values())
    print(f"  cell_counts.csv: {n_types} types, {n_total} instances (from timing csv)")

    validate_dont_use_compliance(src_run, counts)
    write_filters_stub(dest_dir, counts)

    print(f"  -> {dest_dir}")
    return dest_dir

def main() -> None:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n", 1)[0])
    p.add_argument(
        "--src-root", type=Path, default=DEFAULT_SRC_ROOT,
        required=(DEFAULT_SRC_ROOT is None),
        help="Root FC run dir. Default: $FC_RUN_ROOT if set, otherwise required."
    )
    p.add_argument("--dest-root", type=Path, required=True,
                   help="Destination root (e.g. .../l0_inference_payload)")

    sel = p.add_mutually_exclusive_group(required=True)
    sel.add_argument("--all", action="store_true",
                     help="Process all 10 Table-5 runs.")
    sel.add_argument("--src-run", type=Path, default=None,
                     help="Single run dir (e.g. .../aes_cipher_top_L0/420_5.0_2)")

    p.add_argument("--design", default=None,
                   help="Design key (aes/jpeg/sha256/keccak/ldpc). Required with --src-run.")
    p.add_argument("--sweep-id", default=None,
                   help="Sweep dir name (e.g. 420_5.0_2). Required with --src-run.")
    p.add_argument("--design-subdir", default=None,
                   help="Source subdir under src-root (e.g. aes_cipher_top_L0). "
                        "Required with --src-run.")

    args = p.parse_args()

    if args.all:
        runs = []
        for design, design_subdir, sweep_id, _ in TABLE5_RUNS:
            src_run = args.src_root / design_subdir / sweep_id
            runs.append((src_run, design, sweep_id, design_subdir))
    else:
        for req, name in (
            (args.design, "--design"),
            (args.sweep_id, "--sweep-id"),
            (args.design_subdir, "--design-subdir"),
        ):
            if req is None:
                sys.exit(f"ERROR: {name} required with --src-run")
        runs = [(args.src_run, args.design, args.sweep_id, args.design_subdir)]

    print(f"Dest root: {args.dest_root}")
    print(f"Processing {len(runs)} run(s)\n")

    failed: list[tuple[Path, Exception]] = []
    for src_run, design, sweep_id, design_subdir in runs:
        print(f"[{design}/{sweep_id}]  src={src_run}")
        try:
            convert_one(
                src_run=src_run,
                design=design,
                sweep_id=sweep_id,
                design_subdir=design_subdir,
                dest_root=args.dest_root,
            )
        except Exception as e:
            print(f"  FAIL: {e}", file=sys.stderr)
            failed.append((src_run, e))
        print()

    if failed:
        print(f"\n{len(failed)} run(s) failed:")
        for sr, e in failed:
            print(f"  - {sr}: {e}")
        sys.exit(1)
    print(f"Done. {len(runs)} run(s) converted.")

if __name__ == "__main__":
    main()
