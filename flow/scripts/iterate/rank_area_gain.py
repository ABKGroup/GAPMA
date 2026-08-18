#!/usr/bin/env python3
import argparse
import csv
import subprocess
import sys
from pathlib import Path

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--area-gain-csv", required=True, type=Path)
    ap.add_argument("--lib-dir", required=True)
    ap.add_argument("--lef-dir", required=True)
    ap.add_argument("--verify-helper", required=True)
    ap.add_argument("--cdb-list", default=None,
                    help="path to fc_cdb_macros.txt; forwarded to verify helper "
                         "to reject cells absent from loaded FC LEF (SEL-005 prevention)")
    ap.add_argument("--exclude", nargs="*", default=[],
                    help="canonicals already chosen (do not re-offer)")
    args = ap.parse_args()

    if not args.area_gain_csv.is_file():
        sys.exit(f"ERROR: area_gain.csv not found: {args.area_gain_csv}")

    gains = {}
    with open(args.area_gain_csv) as f:
        reader = csv.DictReader(f)
        for col in ("canonical_key", "b_cell", "area_gain"):
            if col not in reader.fieldnames:
                sys.exit(f"ERROR: area_gain.csv missing column '{col}' "
                         f"(have: {reader.fieldnames})")
        for row in reader:
            canon = (row["canonical_key"] or "").strip()
            bcell = (row["b_cell"] or "").strip()
            gain = (row["area_gain"] or "").strip()
            if not canon or bcell in ("", "(none)") or gain == "":
                continue
            try:
                gval = float(gain)
            except ValueError:
                # A dropped row removes that canonical from the ranking, so a
                # candidate silently loses its area gain and its position.
                raise SystemExit(
                    f"ERROR: non-numeric area_gain {gain!r} for canonical "
                    f"{canon!r} (b_cell {bcell!r}) in {args.area_gain_csv}"
                )
            if canon not in gains or gval > gains[canon]:
                gains[canon] = gval

    excl = set(args.exclude)
    ranked = [c for c in sorted(gains, key=lambda k: gains[k], reverse=True)
              if c not in excl]
    if not ranked:
        return

    cmd = ["python3", args.verify_helper,
           "--lib-dir", args.lib_dir,
           "--lef-dir", args.lef_dir,
           "--canonicals", *ranked,
           "--prefer-suffix", "_X1",
           "--require-suffix", "_X1",
           "--single"]
    if args.cdb_list:
        cmd += ["--cdb-list", args.cdb_list]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    lines = proc.stdout.splitlines()
    if len(lines) != len(ranked):
        sys.exit(f"ERROR: verify helper returned {len(lines)} lines for "
                 f"{len(ranked)} canonicals (stderr tail: "
                 f"{proc.stderr.strip().splitlines()[-1] if proc.stderr.strip() else ''})")

    for canon, cell in zip(ranked, lines):
        cell = cell.strip()
        if cell:
            print(f"{canon} {cell}")

if __name__ == "__main__":
    main()
