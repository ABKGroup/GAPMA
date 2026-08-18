#!/usr/bin/env python3

import argparse
import csv
import re
import sys
from pathlib import Path

_NINPUT_RE = re.compile(r"^[23]input0x[0-9A-Fa-f]+_X[12]$")

_CELL_DEF_RE = re.compile(r'^\s*cell\s*\(([^)]+)\)', re.MULTILINE)

def scan_lib_cells(lib_dir: Path) -> set[str]:
    cells: set[str] = set()
    for lib_file in lib_dir.rglob("*.lib"):
        stem = lib_file.stem
        m = re.match(r"^(.+?)_tt_", stem)
        filename_name = m.group(1) if m else stem
        cells.add(filename_name)
        try:
            text = lib_file.read_text(errors="replace")
            for cell_name in _CELL_DEF_RE.findall(text):
                cells.add(cell_name.strip())
        except OSError as _e:
            raise OSError(f"Failed to read lib file {lib_file}: {_e}") from _e
    return cells

def load_features_ninput_cells(features_csv: Path) -> list[dict]:
    rows = []
    with open(features_csv, newline="") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            name = row.get("cell_name", "").strip()
            if _NINPUT_RE.match(name):
                rows.append({"cell_name": name, "n_inputs": row.get("n_inputs", "")})
    return rows

def build_mapping(feature_rows: list[dict], lib_cells: set[str]) -> list[dict]:
    mapping = []
    for r in feature_rows:
        name = r["cell_name"]
        n_inputs = r["n_inputs"]
        if name in lib_cells:
            status = "exact"
            lib_name = name
        else:
            status = "missing"
            lib_name = "MISSING"
        mapping.append({
            "features_name": name,
            "lib_name": lib_name,
            "status": status,
            "n_inputs": n_inputs,
        })
    return mapping

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Validate features_l0 NinputXXX cell names against SO3 lib."
    )
    parser.add_argument("--features-l0", required=True,
                        help="features_l0_with_x2.csv path")
    parser.add_argument("--lib-dir",     required=True,
                        help="SO3 lib directory (*.lib scanned recursively)")
    parser.add_argument("--out",         required=True,
                        help="Output cell_name_mapping.csv path")
    args = parser.parse_args()

    features_l0 = Path(args.features_l0)
    lib_dir     = Path(args.lib_dir)
    out_path    = Path(args.out)

    if not features_l0.exists():
        raise FileNotFoundError(f"--features-l0 not found: {features_l0}")
    if not lib_dir.is_dir():
        raise FileNotFoundError(f"--lib-dir not found: {lib_dir}")

    lib_cells    = scan_lib_cells(lib_dir)
    feature_rows = load_features_ninput_cells(features_l0)
    mapping      = build_mapping(feature_rows, lib_cells)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", newline="") as fh:
        writer = csv.DictWriter(
            fh, fieldnames=["features_name", "lib_name", "status", "n_inputs"]
        )
        writer.writeheader()
        writer.writerows(mapping)

    exact     = [r for r in mapping if r["status"] == "exact"]
    missing   = [r for r in mapping if r["status"] == "missing"]

    print(f"[validate_cell_lib] features_l0  : {features_l0}")
    print(f"[validate_cell_lib] lib_dir      : {lib_dir}")
    print(f"[validate_cell_lib] total NinputXXX cells : {len(mapping)}")
    print(f"[validate_cell_lib]   exact match         : {len(exact)}")
    print(f"[validate_cell_lib]   missing from lib    : {len(missing)}")

    if missing:
        print("[validate_cell_lib] WARNING — cells NOT in SO3 lib (will show MISSING in results):")
        for r in missing:
            print(f"  MISSING: {r['features_name']!r}  (n_inputs={r['n_inputs']})")
        candidate_missing = [
            r for r in missing
            if r["features_name"].endswith("_X1") and r["n_inputs"] in ("2", "3", "4")
        ]
        if candidate_missing:
            names = [r["features_name"] for r in candidate_missing]
            raise ValueError(
                f"[validate_cell_lib] ERROR: {len(candidate_missing)} X1 candidate(s) "
                f"are missing from SO3 lib — cannot proceed:\n  {names}\n"
                f"Fix: add the missing .lib files to --lib-dir, or remove these cells from features_l0."
            )

    print(f"[validate_cell_lib] mapping written → {out_path}")

if __name__ == "__main__":
    main()
