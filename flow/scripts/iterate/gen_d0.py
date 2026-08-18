#!/usr/bin/env python3
"""gen_d0.py -- generate and converge a D0 baseline sweep for a design."""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(_REPO_ROOT / "flow" / "scripts" / "datagen"))
import run_fc

SCAN_PASS = _REPO_ROOT / "flow" / "scripts" / "iterate" / "scan_pass.py"

SHORT_TO_FULL_DESIGN_NAME = {
    "aes": "aes_cipher_top",
    "jpeg": "jpeg_encoder",
    "eth": "eth_top",
    "sha256": "sha256_core",
    "keccak": "sha3",
}

def launch_point(out_root: Path, pdk: str, full_design: str, area: int,
                  freq_ghz: float, rep: int, mine_params: dict) -> str:
    run_dir = out_root / f"{area}_{freq_ghz:.6f}_{rep}"
    clock_ps = round(1000.0 / freq_ghz)
    if run_dir.exists() and (run_dir / "qor.csv").exists():
        print(f"[gen_d0] [SKIP EXISTING] {run_dir}")
        return run_dir.name
    print(f"[gen_d0] launching {run_dir} (area={area} freq={freq_ghz}GHz "
          f"clock={clock_ps}ps rep={rep})")
    try:
        run_fc.run_one(run_dir, pdk, full_design, only_use=[], canonical_keys=[],
                        clock=clock_ps, mine_params=mine_params,
                        stage="route_opt", area=area)
    except RuntimeError as e:
        print(f"[gen_d0] FAILED {run_dir}: {e}")
    return run_dir.name

def launch_grid(out_root: Path, pdk: str, full_design: str, points, parallel: int,
                 mine_params: dict):
    with ProcessPoolExecutor(max_workers=parallel) as ex:
        futs = [ex.submit(launch_point, out_root, pdk, full_design, a, f, r, mine_params)
                for (a, f, r) in points]
        for fut in as_completed(futs):
            fut.result()

def probe_area_guess(out_root: Path, pdk: str, full_design: str, clock_guess_ns: float,
                      probe_area: int, target_util: float, mine_params: dict) -> int:
    probe_dir = out_root / f"_probe_{probe_area}"
    clock_ps = round(clock_guess_ns * 1000.0)
    if not (probe_dir / "rpts_fc" / "compile" / "report_utilization").exists():
        print(f"[gen_d0] probing gate count at floorplan area={probe_area}um^2")
        try:
            run_fc.run_one(probe_dir, pdk, full_design, only_use=[], canonical_keys=[],
                            clock=clock_ps, mine_params=mine_params,
                            stage="route_opt", area=probe_area)
        except RuntimeError as e:
            print(f"[gen_d0] probe run_one raised: {e}")
    util_rpt = probe_dir / "rpts_fc" / "compile" / "report_utilization"
    if not util_rpt.is_file():
        raise SystemExit(f"ERROR: probe run did not produce {util_rpt}")
    m = re.search(r"^Total Area of cells:\s*([\d.]+)", util_rpt.read_text(), re.M)
    if not m:
        raise SystemExit(f"ERROR: could not parse 'Total Area of cells' from {util_rpt}")
    cell_area = float(m.group(1))
    return int(cell_area / target_util)

def scan_emit(out_root: Path, design: str, emit: str, **kw) -> str | None:
    cmd = [sys.executable, str(SCAN_PASS), "--target-dir", str(out_root),
           "--design", design, "--emit", emit]
    for k, v in kw.items():
        cmd += [f"--{k.replace('_', '-')}", str(v)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"[gen_d0] scan_pass ({emit}) stderr: {result.stderr.strip()}")
        return None
    return result.stdout.strip()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--design", required=True)
    p.add_argument("--pdk", default="SO3")
    p.add_argument("--clock-guess", type=float, required=True,
                   help="starting clock period, ns")
    p.add_argument("--out-root", required=True)
    p.add_argument("--reps", type=int, default=3)
    p.add_argument("--parallel", type=int, default=6)
    p.add_argument("--max-iters", type=int, default=5)
    p.add_argument("--target-util", type=float, default=0.75)
    p.add_argument("--probe-area", type=int, default=4000)
    p.add_argument("--area-step-pct", type=float, default=5.0)
    p.add_argument("--tcp-step-pct", type=float, default=5.0)
    p.add_argument("--grid-points", type=int, default=6)
    p.add_argument("--mine-min-cells", type=int, default=2)
    p.add_argument("--mine-max-cells", type=int, default=8)
    p.add_argument("--mine-min-inputs", type=int, default=2)
    p.add_argument("--mine-max-inputs", type=int, default=4)
    p.add_argument("--mine-max-depth", type=int, default=3)
    p.add_argument("--mine-max-outputs", type=int, default=1)
    p.add_argument("--mine-num-cores", type=int, default=4)
    args = p.parse_args()

    mine_params = {
        'min_cells': args.mine_min_cells, 'max_cells': args.mine_max_cells,
        'min_inputs': args.mine_min_inputs, 'max_inputs': args.mine_max_inputs,
        'max_depth': args.mine_max_depth, 'max_outputs': args.mine_max_outputs,
        'num_cores': args.mine_num_cores,
    }

    full_design = SHORT_TO_FULL_DESIGN_NAME.get(args.design, args.design)
    design_setup_tcl = (_REPO_ROOT / "flow" / "scripts" / "fc_reference"
                        / "configs" / "design_setup.tcl").read_text()
    if f'DESIGN_NAME eq "{full_design}"' not in design_setup_tcl:
        raise SystemExit(f"ERROR: design_setup.tcl has no DESIGN_NAME==\"{full_design}\" "
                          f"branch.")

    out_root = Path(args.out_root)
    out_root.mkdir(parents=True, exist_ok=True)

    print("[gen_d0] === Phase 0: gate-count area probe ===")
    area_guess = probe_area_guess(out_root, args.pdk, full_design,
                                   args.clock_guess, args.probe_area,
                                   args.target_util, mine_params)
    print(f"[gen_d0] area guess: {area_guess} um^2 (target util {args.target_util})")

    cur_area = area_guess
    cur_tcp_ns = args.clock_guess
    prev_min_area = None
    d0_best_run = None

    for it in range(1, args.max_iters + 1):
        print(f"[gen_d0] === Iteration {it}/{args.max_iters}: "
              f"Phase A (TCP sweep @ area={cur_area}) ===")
        start_offset_pct = args.tcp_step_pct * (args.grid_points // 2 - 1)
        mults = [1.0 + (start_offset_pct - i * args.tcp_step_pct) / 100.0
                 for i in range(args.grid_points)]
        tcps = [cur_tcp_ns * m for m in mults]
        points = [(cur_area, round(1.0 / t, 6), rep)
                  for t in tcps for rep in range(1, args.reps + 1)]
        launch_grid(out_root, args.pdk, full_design, points, args.parallel, mine_params)

        winner_dir = scan_emit(out_root, args.design, "min-ecp", area=cur_area)
        if not winner_dir:
            raise SystemExit(f"ERROR: no PASS point in Phase A at area={cur_area} "
                              f"across TCPs {tcps}. Widen --clock-guess or inspect "
                              f"{out_root}")
        won_freq = float(winner_dir.split("_")[1])
        cur_tcp_ns = 1.0 / won_freq
        print(f"[gen_d0] Phase A winner: {winner_dir} -> TCP={cur_tcp_ns:.4f}ns")

        print(f"[gen_d0] === Iteration {it}/{args.max_iters}: "
              f"Phase B (area sweep @ TCP={cur_tcp_ns:.4f}) ===")
        areas = []
        a = float(cur_area)
        for _ in range(args.grid_points):
            areas.append(int(a))
            a *= (1 - args.area_step_pct / 100.0)
        points = [(a, won_freq, rep) for a in areas for rep in range(1, args.reps + 1)]
        launch_grid(out_root, args.pdk, full_design, points, args.parallel, mine_params)

        min_area_run = scan_emit(out_root, args.design, "min-area",
                                  **{"baseline-freq": won_freq})
        if not min_area_run:
            raise SystemExit(f"ERROR: no PASS point in Phase B at TCP={cur_tcp_ns:.4f} "
                              f"across areas {areas}. Inspect {out_root}")
        won_area = int(min_area_run.split("_")[0])
        print(f"[gen_d0] Phase B winner: {min_area_run} -> area={won_area}um^2")

        d0_best_run = min_area_run
        if prev_min_area is not None and won_area == prev_min_area:
            print(f"[gen_d0] converged: min area unchanged at {won_area}um^2 "
                  f"across iteration {it}")
            break
        prev_min_area = won_area
        cur_area = won_area

    print(f"[gen_d0] === D0 done: {out_root / d0_best_run} ===")
    print(f"[gen_d0] autorun.conf COMBOS entry: d0_dir={out_root} d0_best_run={d0_best_run}")

if __name__ == "__main__":
    main()
