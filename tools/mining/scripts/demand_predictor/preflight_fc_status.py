from __future__ import annotations

import argparse
import csv
import re
import sys
from collections import Counter
from pathlib import Path

REAL_ERROR_PATTERNS: list[tuple[re.Pattern, str]] = [
    (re.compile(r"Fail in placement"),         "PLACE_FAIL"),
    (re.compile(r"is not placed\."),           "NOT_PLACED"),
    (re.compile(r"placed overlapping"),        "OVERLAP"),
    (re.compile(r"Over Utilization"),          "OVER_UTIL"),
    (re.compile(r"Optimization failed at"),    "OPT_FAIL"),
    (re.compile(r"Flow status set to Error"),  "FLOW_ERR"),
    (re.compile(r"synthesis command aborted"), "SYN_ABORT"),
]

def _read_provenance(run_dir: Path) -> dict[str, str]:
    f = run_dir / "PROVENANCE"
    if not f.is_file():
        return {}
    out: dict[str, str] = {}
    for line in f.read_text().splitlines():
        line = line.strip()
        if not line or "=" not in line:
            continue
        k, v = line.split("=", 1)
        out[k.strip()] = v.strip()
    return out

FC_STAGE_LOGS = (
    "init_design.log",
    "compile.log",
    "clock_opt_cts.log",
    "clock_opt_opto.log",
    "route_auto.log",
    "route_opt.log",
)

def _find_stage_logs(juj_dir: Path) -> list[Path]:
    parent = juj_dir.parent
    logs_dir = parent / "logs_fc"
    if not logs_dir.is_dir():
        return []
    out: list[Path] = []
    for name in FC_STAGE_LOGS:
        p = logs_dir / name
        if p.is_file():
            out.append(p)
    return out

def _find_qor_csv(juj_dir: Path) -> Path | None:
    parent = juj_dir.parent
    qor = parent / "qor.csv"
    return qor if qor.is_file() else None

def check_fc_log(juj_dir: Path) -> tuple[bool, dict, str | None, str, list[str]]:
    stage_logs = _find_stage_logs(juj_dir)
    present = [p.name for p in stage_logs]
    missing = [name for name in FC_STAGE_LOGS if name not in present]

    if not stage_logs:
        return True, {}, None, f"no logs_fc dir under {juj_dir}", []

    per_stage: dict[str, dict[str, int]] = {}
    sample_error: str | None = None

    for log_path in stage_logs:
        counts: Counter[str] = Counter()
        with open(log_path, "r", errors="replace") as f:
            for line in f:
                if not line.startswith("Error:"):
                    continue
                for rx, tag in REAL_ERROR_PATTERNS:
                    if rx.search(line):
                        counts[tag] += 1
                        if sample_error is None:
                            sample_error = (
                                f"[{log_path.name}] " + line.rstrip("\n")[:200]
                            )
                        break
        if counts:
            per_stage[log_path.name] = dict(counts)

    is_broken = (len(per_stage) > 0) or (len(missing) > 0)
    missing_summary = (
        f"missing_stage_logs={missing}" if missing else ""
    )
    return is_broken, per_stage, sample_error, missing_summary, [str(p) for p in stage_logs]

def check_cell_counts(cc_path: Path) -> tuple[bool, list[str]]:
    issues: list[str] = []
    if not cc_path.is_file():
        return True, [f"missing file: {cc_path}"]
    if cc_path.stat().st_size == 0:
        return True, [f"empty file: {cc_path}"]
    seen: dict[str, int] = {}
    row_count = 0
    with open(cc_path, "r", errors="replace") as f:
        reader = csv.reader(f)
        try:
            header = next(reader)
        except StopIteration:
            return True, [f"no header: {cc_path}"]
        if [h.strip() for h in header] != ["cell_name", "count"]:
            issues.append(f"unexpected header {header!r} (expected ['cell_name','count'])")
        for raw in reader:
            if not raw or all(not c.strip() for c in raw):
                continue
            row_count += 1
            if len(raw) < 2:
                issues.append(f"row with <2 fields: {raw!r}")
                continue
            name  = raw[0].strip()
            count = raw[1].strip()
            if not name:
                issues.append(f"empty cell_name on row {row_count}")
            if count.lower() == "nan" or count == "":
                issues.append(f"NaN/empty count for cell={name!r}")
                continue
            try:
                val = int(count)
            except ValueError:
                try:
                    fval = float(count)
                except ValueError:
                    issues.append(f"non-numeric count for cell={name!r}: {count!r}")
                    continue
                if fval != fval:
                    issues.append(f"NaN count for cell={name!r}")
                    continue
                if not fval.is_integer():
                    issues.append(f"non-integer count for cell={name!r}: {fval}")
                val = int(fval)
            if val < 0:
                issues.append(f"negative count for cell={name!r}: {val}")
            if name in seen:
                issues.append(f"duplicate cell_name {name!r} (rows {seen[name]} and {row_count})")
            else:
                seen[name] = row_count
    if row_count == 0:
        issues.append("zero rows after header")
    return (len(issues) > 0), issues

def check_qor(juj_dir: Path) -> tuple[bool, list[str]]:
    issues: list[str] = []
    qor = _find_qor_csv(juj_dir)
    if qor is None:
        return True, [f"missing qor.csv under {juj_dir}"]
    if qor.stat().st_size == 0:
        return True, [f"empty qor.csv: {qor}"]

    summary_line: str | None = None
    for line in qor.read_text().splitlines():
        if line.startswith("Summary,"):
            summary_line = line
            break
    if summary_line is None:
        return True, [f"no Summary line in qor.csv: {qor}"]

    parts = [p.strip() for p in summary_line.split(",")]
    if len(parts) < 4:
        issues.append(f"Summary row has only {len(parts)} fields: {summary_line!r}")
        return True, issues
    wns_raw = parts[1]
    if wns_raw == "~":
        pass
    else:
        try:
            wns = float(wns_raw)
            if wns < -100.0:
                issues.append(f"catastrophic WNS = {wns} ns (< -100)")
        except ValueError:
            issues.append(f"unparseable WNS in Summary: {wns_raw!r}")
    return (len(issues) > 0), issues

def evaluate_run(run_dir: Path) -> dict:
    prov = _read_provenance(run_dir)
    juj  = prov.get("juj_dir")
    cc   = run_dir / "cell_counts.csv"

    report: dict = {"checks": {}, "broken": False}

    if juj:
        broken_log, per_stage, sample_error, missing_summary, present_paths = \
            check_fc_log(Path(juj))
        report["checks"]["fc_log"] = {
            "broken":         broken_log,
            "per_stage":      per_stage,
            "sample_error":   sample_error,
            "missing_summary": missing_summary,
            "present_paths":  present_paths,
        }
        if broken_log:
            report["broken"] = True
    else:
        report["checks"]["fc_log"] = {"broken": True,
                                      "missing_summary": "no juj_dir in PROVENANCE"}
        report["broken"] = True

    broken_cc, cc_issues = check_cell_counts(cc)
    report["checks"]["cell_counts"] = {"broken": broken_cc, "issues": cc_issues}
    if broken_cc:
        report["broken"] = True

    if juj:
        broken_qor, qor_issues = check_qor(Path(juj))
        report["checks"]["qor"] = {"broken": broken_qor, "issues": qor_issues}
        if broken_qor:
            report["broken"] = True
    else:
        report["checks"]["qor"] = {"broken": False,
                                   "issues": ["skipped (no juj_dir)"]}

    return report

def write_status_file(run_dir: Path, report: dict) -> None:
    status = "broken" if report["broken"] else "valid"
    lines: list[str] = [f"status={status}"]
    fc = report["checks"]["fc_log"]
    if fc.get("broken"):
        for stage, counts in (fc.get("per_stage") or {}).items():
            err_str = ",".join(f"{k}:{v}" for k, v in sorted(counts.items()))
            lines.append(f"fc_log_{stage}={err_str}")
        if fc.get("missing_summary"):
            lines.append(f"fc_log_{fc['missing_summary']}")
        if fc.get("sample_error"):
            sample = fc["sample_error"].replace("\n", " ")
            lines.append(f"fc_log_sample_error={sample}")
    for p in (fc.get("present_paths") or []):
        lines.append(f"fc_log_present={p}")

    cc = report["checks"]["cell_counts"]
    if cc.get("broken"):
        for k, msg in enumerate(cc["issues"]):
            lines.append(f"cell_counts_issue_{k}={msg}")

    q = report["checks"]["qor"]
    if q.get("broken"):
        for k, msg in enumerate(q["issues"]):
            lines.append(f"qor_issue_{k}={msg}")

    (run_dir / "FC_STATUS").write_text("\n".join(lines) + "\n")

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--run-root", nargs="+", required=True,
                    help="One or more per-design roots under demand_training_clean")
    ap.add_argument("--rewrite", action="store_true",
                    help="Re-evaluate even if FC_STATUS exists")
    args = ap.parse_args()

    valid_count   = 0
    broken_count  = 0
    broken_list:  list[tuple[Path, dict]] = []

    for root_str in args.run_root:
        root = Path(root_str)
        if not root.is_dir():
            sys.exit(f"[preflight] not a directory: {root}")
        print(f"[preflight] scanning {root} ...", flush=True)
        for cc in sorted(root.rglob("cell_counts.csv")):
            run_dir = cc.parent
            marker  = run_dir / "FC_STATUS"
            if marker.is_file() and not args.rewrite:
                cached = marker.read_text()
                if cached.startswith("status=valid"):
                    valid_count += 1
                else:
                    broken_count += 1
                    broken_list.append((run_dir, {"cached": cached}))
                continue
            report = evaluate_run(run_dir)
            write_status_file(run_dir, report)
            if report["broken"]:
                broken_count += 1
                broken_list.append((run_dir, report))
            else:
                valid_count += 1

    print()
    print("=" * 70)
    print("PREFLIGHT SUMMARY")
    print("=" * 70)
    total = valid_count + broken_count
    print(f"  total runs : {total}")
    print(f"  valid      : {valid_count}")
    print(f"  broken     : {broken_count}")

    if broken_list:
        print()
        print("BROKEN RUNS (verbatim — will be excluded from training):")
        for run_dir, info in broken_list:
            if "cached" in info:
                print(f"  {run_dir}")
                for ln in info["cached"].strip().splitlines():
                    print(f"     {ln}")
            else:
                rep = info
                print(f"  {run_dir}")
                fc = rep["checks"]["fc_log"]
                if fc.get("broken"):
                    for stage, counts in (fc.get("per_stage") or {}).items():
                        print(f"     fc_log[{stage}]={counts}")
                    if fc.get("missing_summary"):
                        print(f"     fc_log.{fc['missing_summary']}")
                    if fc.get("sample_error"):
                        print(f"     fc_log_sample={fc['sample_error']}")
                cc = rep["checks"]["cell_counts"]
                if cc.get("broken"):
                    for msg in cc["issues"]:
                        print(f"     cell_counts: {msg}")
                q = rep["checks"]["qor"]
                if q.get("broken"):
                    for msg in q["issues"]:
                        print(f"     qor: {msg}")

if __name__ == "__main__":
    main()
