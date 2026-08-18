#!/usr/bin/env python3
"""Scan D1/D2/D3 FC runs and report per-(design, iter, arity):
   - min-area PASS run (filter: freq_t == BASELINE_FREQ[design],
     overridable per-invocation with --baseline-freq)
   - min-ECP PASS run (filter: area_t == CANONICAL_AREA[design])
PASS (paper §4.2):
   WNS_in2reg >= -0.020 ns, WNS_clock >= -0.020 ns,
   DRC_total <= 200, DRC_shorts <= 20.

Two modes:
   - default (no args): full DESIGNS x ITERS x ARITIES table to stdout.
   - --target-dir <PATH> --design <D> --emit {min-area,min-ecp} [--strict]:
        scan exactly one sweep dir and print the winning run name on stdout.
        Used by auto_iterate_loop.sh to pick the next iteration's seed.
        Exits non-zero with ERROR on stderr if no PASS candidate matches the
        emit filter (data-loss policy: never silently empty-print).
"""
import argparse
import re
import sys
from pathlib import Path

def _root():
    """Sweep-tree root for the no-argument table mode.

    A literal placeholder here made every scan_runs() lookup miss and the table
    print as empty rather than reporting that the root was never configured.
    """
    import os
    r = os.environ.get("GAPMA_FC_ROOT")
    if not r:
        raise SystemExit(
            "ERROR: GAPMA_FC_ROOT is not set. The full-table mode scans "
            "$GAPMA_FC_ROOT/fc_D{1,2,3}/ for sweep dirs.\n"
            "  Set it, or use --target-dir to scan one sweep dir."
        )
    p = Path(r)
    if not p.is_dir():
        raise SystemExit(f"ERROR: GAPMA_FC_ROOT is not a directory: {p}")
    return p

DESIGNS = ["aes", "jpeg", "eth", "sha256", "keccak"]
DESIGN_DIR_NAME = {
    "aes": "aes_cipher_top", "jpeg": "jpeg_encoder",
    "eth": "ethtop", "sha256": "sha256", "keccak": "keccak",
}
ITERS = ["D1", "D2", "D3"]
ARITIES = ["3in", "4in"]

BASELINE_FREQ = {"aes": 5.0, "jpeg": 1.5, "eth": 3.0, "sha256": 2.0, "keccak": 2.0}

CANONICAL_AREA = {"aes": 440, "jpeg": 1750, "eth": 2700, "sha256": 650, "keccak": 750}

def parse_qor(qor):
    if not qor.is_file():
        return None
    lines = qor.read_text().splitlines()
    summary = next((ln for ln in lines if ln.startswith("Summary,")), None)
    cella = None
    for i, ln in enumerate(lines):
        if ln.startswith("CAP, FANOUT") and i + 1 < len(lines):
            cella = lines[i + 1]
            break
    if summary is None:
        return None
    cols = [c.strip() for c in summary.split(",")]
    try:
        wns_in2reg = float(cols[1])
        freq_mhz = float(cols[4].replace("MHz", "").strip())
        wns_clock = float(cols[5])
    except (IndexError, ValueError):
        return None
    cell_area = None
    if cella:
        try:
            cell_area = float([c.strip() for c in cella.split(",")][4])
        except (IndexError, ValueError) as e:
            # min-area selection filters on cell_area being present, so a run
            # whose area failed to parse drops out of the pool silently and the
            # smallest PASS point can be missed.
            raise SystemExit(
                f"ERROR: cannot read cell area from {qor}: {e}\n"
                f"  CELLA row: {cella!r}"
            )
    return dict(wns_in2reg=wns_in2reg, wns_clock=wns_clock,
                freq_mhz=freq_mhz, cell_area=cell_area)

def parse_drc(run_dir):
    rd = run_dir / "rpts_fc" / "route_opt" / "report_drc"
    if rd.is_file():
        text = rd.read_text()
        total = None
        shorts = 0
        for ln in text.splitlines():
            m = re.match(r"^Design\s*:\s*\S+\s+(\d+)", ln)
            if m:
                total = int(m.group(1))
                continue
            m = re.match(r"^\s{12}Short\s+(\d+)", ln)
            if m:
                shorts = int(m.group(1))
        if total is not None:
            return (total, shorts)
    rrd = run_dir / "rpts_fc" / "route_opt" / "rm_report_drc"
    if rrd.is_file() and "No DRCs to report" in rrd.read_text():
        return (0, 0)
    return (None, None)

_ABORT_PATTERNS = [
    re.compile(r"Fail in placement"),
    re.compile(r"is not placed\."),
    re.compile(r"placed overlapping"),
    re.compile(r"Over Utilization"),
    re.compile(r"Optimization failed at"),
    re.compile(r"Flow status set to Error"),
    re.compile(r"synthesis command aborted"),
]

_FC_STAGE_LOGS = (
    "init_design.log", "compile.log", "clock_opt_cts.log",
    "clock_opt_opto.log", "route_auto.log", "route_opt.log",
)

def run_completed(run_dir):
    logs = run_dir / "logs_fc"
    rlog = logs / "route_opt.log"
    if not rlog.is_file():
        return False
    sz = rlog.stat().st_size
    if sz == 0:
        return False
    try:
        with open(rlog, "rb") as f:
            if sz > 2048:
                f.seek(-2048, 2)
            tail = f.read().decode("utf-8", errors="replace")
    except OSError:
        return False
    if "Thank you for using Fusion Compiler." not in tail:
        return False
    outputs = run_dir / "outputs_fc"
    if not outputs.is_dir():
        return False
    rv = list(outputs.glob("*.route_opt.v")) + list(outputs.glob("route_opt.v"))
    if not rv:
        return False
    if not any(p.stat().st_size > 1024 for p in rv):
        return False
    for stage in _FC_STAGE_LOGS:
        log = logs / stage
        if not log.is_file():
            continue
        try:
            with open(log, "r", errors="replace") as f:
                for ln in f:
                    if not ln.startswith("Error:"):
                        continue
                    for rx in _ABORT_PATTERNS:
                        if rx.search(ln):
                            return False
        except OSError:
            return False
    return True

def is_pass(qor, total, shorts):
    if qor is None:
        return False
    if qor["wns_in2reg"] < -0.020 or qor["wns_clock"] < -0.020:
        return False
    if total is None or total > 200:
        return False
    if shorts is None or shorts > 20:
        return False
    return True

def scan_target_dir(target):
    rows = []
    for run in sorted(target.iterdir()):
        m = re.match(r"^(\d+)_([\d.]+)_(\d+)$", run.name)
        if not (run.is_dir() and m):
            continue
        area_t = int(m.group(1))
        freq_t = float(m.group(2))
        rep = int(m.group(3))
        completed = run_completed(run)
        qor = parse_qor(run / "qor.csv")
        total, shorts = parse_drc(run)
        pass_ = completed and is_pass(qor, total, shorts)
        rows.append(dict(
            name=run.name, area_t=area_t, freq_t=freq_t, rep=rep,
            qor=qor, total_drc=total, short_drc=shorts,
            completed=completed, pass_=pass_,
        ))
    return rows

def scan_runs(iteration, arity, design):
    base = _root() / f"fc_{iteration}"
    dn = DESIGN_DIR_NAME[design]
    suffix = "" if arity == "3in" else "_4in"
    candidates = [
        base / f"{dn}_{iteration}{suffix}",
        base / f"{dn}_{iteration}_{arity}",
    ]
    target = next((c for c in candidates if c.is_dir()), None)
    if target is None:
        return []
    return scan_target_dir(target)

def derive_design(dir_name):
    for short, long_dn in DESIGN_DIR_NAME.items():
        if dir_name.startswith(long_dn + "_") or dir_name.startswith(short + "_"):
            return short
    return None

def emit_single(target_dir, design, emit, strict, baseline_freq, area):
    target = Path(target_dir)
    if not target.is_dir():
        print(f"ERROR: target dir not found: {target}", file=sys.stderr)
        sys.exit(2)
    if design is None:
        design = derive_design(target.name)
    if design is None:
        print(f"ERROR: unknown design (could not derive from {target.name!r}); "
              f"pass --design explicitly", file=sys.stderr)
        sys.exit(2)
    if emit == "min-area":
        if baseline_freq is not None:
            base_freq = baseline_freq
        elif design in BASELINE_FREQ:
            base_freq = BASELINE_FREQ[design]
        else:
            print(f"ERROR: design {design!r} not in BASELINE_FREQ table "
                  f"(known: {', '.join(BASELINE_FREQ)}); either add it to "
                  f"BASELINE_FREQ in scan_pass.py or pass --baseline-freq <GHz>",
                  file=sys.stderr)
            sys.exit(2)
    elif emit == "min-ecp":
        if area is not None:
            fixed_area = area
        elif design in CANONICAL_AREA:
            fixed_area = CANONICAL_AREA[design]
        else:
            print(f"ERROR: design {design!r} not in CANONICAL_AREA table "
                  f"(known: {', '.join(CANONICAL_AREA)}); either add it to "
                  f"CANONICAL_AREA in scan_pass.py or pass --area <um^2>",
                  file=sys.stderr)
            sys.exit(2)
    rows = scan_target_dir(target)
    if not rows:
        print(f"ERROR: no parseable run dirs inside {target}", file=sys.stderr)
        sys.exit(2)
    if emit == "min-area":
        pool = [r for r in rows if r["pass_"] and
                abs(r["freq_t"] - base_freq) < 1e-6 and
                r["qor"] is not None and r["qor"].get("cell_area") is not None]
        if not pool:
            n_total = len(rows)
            n_pass = sum(1 for r in rows if r["pass_"])
            print(f"ERROR: no min-area PASS in {target} "
                  f"(freq_t=={base_freq} GHz, design={design}); "
                  f"scanned {n_total} runs, {n_pass} PASS overall",
                  file=sys.stderr)
            sys.exit(3)
        winner = min(pool, key=lambda r: r["qor"]["cell_area"])
    elif emit == "min-ecp":
        pool = [r for r in rows if r["pass_"] and
                r["area_t"] == fixed_area and
                r["qor"] is not None and r["qor"].get("freq_mhz") is not None]
        if not pool:
            n_total = len(rows)
            n_pass = sum(1 for r in rows if r["pass_"])
            print(f"ERROR: no min-ECP PASS in {target} "
                  f"(area_t=={fixed_area}, design={design}); "
                  f"scanned {n_total} runs, {n_pass} PASS overall",
                  file=sys.stderr)
            sys.exit(3)
        winner = max(pool, key=lambda r: r["qor"]["freq_mhz"])
    else:
        print(f"ERROR: --emit must be min-area or min-ecp (got {emit!r})",
              file=sys.stderr)
        sys.exit(2)
    print(winner["name"])

def main():
    parser = argparse.ArgumentParser(add_help=True, description=__doc__.split("\n\n", 1)[0])
    parser.add_argument("--target-dir", default=None,
                        help="If set: scan ONE sweep dir and emit the winning run name on stdout.")
    parser.add_argument("--design", default=None,
                        help="Short design name (aes/jpeg/eth/sha256/keccak); required with "
                             "--target-dir unless derivable from dir basename.")
    parser.add_argument("--emit", choices=["min-area", "min-ecp"], default=None,
                        help="With --target-dir: which PASS run to emit.")
    parser.add_argument("--baseline-freq", type=float, default=None,
                        help="Override BASELINE_FREQ[design] (GHz) for the min-area "
                             "filter; required when the design has no BASELINE_FREQ "
                             "entry. Only valid with --target-dir --emit min-area.")
    parser.add_argument("--area", type=float, default=None,
                        help="Override CANONICAL_AREA[design] (um^2) for the min-ecp "
                             "filter; required when the design has no CANONICAL_AREA "
                             "entry (e.g. an ad-hoc area from a D0 sweep). Only valid "
                             "with --target-dir --emit min-ecp.")
    parser.add_argument("--strict", action="store_true",
                        help="Strict PASS filter (currently default; flag is explicit-no-op).")
    args = parser.parse_args()

    if args.baseline_freq is not None:
        if args.target_dir is None or args.emit != "min-area":
            print("ERROR: --baseline-freq only applies to --target-dir with "
                  "--emit min-area (it would be silently ignored here)",
                  file=sys.stderr)
            sys.exit(2)
        if args.baseline_freq <= 0:
            print(f"ERROR: --baseline-freq must be > 0 GHz "
                  f"(got {args.baseline_freq})", file=sys.stderr)
            sys.exit(2)

    if args.area is not None:
        if args.target_dir is None or args.emit != "min-ecp":
            print("ERROR: --area only applies to --target-dir with "
                  "--emit min-ecp (it would be silently ignored here)",
                  file=sys.stderr)
            sys.exit(2)
        if args.area <= 0:
            print(f"ERROR: --area must be > 0 um^2 (got {args.area})", file=sys.stderr)
            sys.exit(2)

    if args.target_dir is not None:
        if args.emit is None:
            print("ERROR: --emit required with --target-dir", file=sys.stderr)
            sys.exit(2)
        emit_single(args.target_dir, args.design, args.emit, args.strict,
                    args.baseline_freq, args.area)
        return

    print(f"{'design':8s} {'iter':4s} {'arity':5s} "
          f"{'min_area_um2':>12s} {'min_area_run':22s} "
          f"{'min_ecp_ns':>10s} {'min_ecp_run':22s} {'note'}")
    print("-" * 110)
    for design in DESIGNS:
        for iteration in ITERS:
            for arity in ARITIES:
                rows = scan_runs(iteration, arity, design)
                ma_pool = [r for r in rows if r["pass_"] and
                           abs(r["freq_t"] - BASELINE_FREQ[design]) < 1e-6]
                ec_pool = [r for r in rows if r["pass_"] and
                           r["area_t"] == CANONICAL_AREA[design]]
                min_area = min(ma_pool, key=lambda r: r["qor"]["cell_area"]) if ma_pool else None
                min_ecp = max(ec_pool, key=lambda r: r["qor"]["freq_mhz"]) if ec_pool else None
                ma_str = f"{min_area['qor']['cell_area']:.2f}" if min_area else "-"
                ma_run = min_area["name"] if min_area else "-"
                if min_ecp:
                    ecp_ns = 1000.0 / min_ecp["qor"]["freq_mhz"]
                    ec_str = f"{ecp_ns:.4f}"
                    ec_run = min_ecp["name"]
                else:
                    ec_str = "-"
                    ec_run = "-"
                note = ""
                if not rows:
                    note = "(no dir)"
                elif not ma_pool and not ec_pool:
                    note = f"(scanned {len(rows)}, 0 PASS at baseline)"
                print(f"{design:8s} {iteration:4s} {arity:5s} "
                      f"{ma_str:>12s} {ma_run:22s} "
                      f"{ec_str:>10s} {ec_run:22s} {note}")

if __name__ == "__main__":
    main()
