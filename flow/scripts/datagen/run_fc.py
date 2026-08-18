# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path
from typing import Optional

_REPO_ROOT = Path(__file__).resolve().parents[3]
_FC_FLOW_DIR_ENV = os.environ.get("FC_FLOW_ROOT")
_FC_FLOW_DIR = (
    Path(_FC_FLOW_DIR_ENV) if _FC_FLOW_DIR_ENV
    else _REPO_ROOT / "flow" / "scripts" / "fc_reference"
)
_FC_SHELL = os.environ.get(
    "FC_SHELL_BIN",
    "fc_shell",
)
_FC_LICENSE = os.environ.get("FC_LICENSE")
_FC_MAX_CORES = 4

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from subset_sampler import generate_subsets_so3

BASELINE_CELLS_SO3 = [
    'NAND2_X1',
    'NOR2_X1',
    'INV_X1', 'INV_X2', 'INV_X4', 'INV_X8',
    'BUF_X1', 'BUF_X2', 'BUF_X4', 'BUF_X8',
    'DFFRNQ_X1_DH',
    'DFFHQN_X1_DH',
    '2BDFFHQN_X1_DH',
    'LHQ_X1',
]

MANDATORY_LOGIC_SIZES: dict[str, list[str]] = {
    'SO3': [],
}

PDK_BASELINE: dict[str, list[str]] = {
    'SO3':   BASELINE_CELLS_SO3,
}

_DATA_GEN = os.path.dirname(os.path.abspath(__file__))

PDK_CLOCK: dict[str, dict[str, object]] = {
    'SO3':   {'aes_cipher_top': 50, 'ibex_core': [100, 150, 200], 'ldpc_decoder_mod': 65,
              'gcd': [100, 150, 200], 'picorv32': [100, 150, 200],
              'jpeg_encoder': 300, 'sha256': 200,
              'sha1_core': 200, 'sha256_core': 200, 'sha512_core': 200,
              'md5_core': 200, 'chacha_core': 200, 'poly1305_core': [100, 150, 200],
              'blake2s_core': [100, 150, 200], 'siphash_core': 200,
              'serv_rf_top': 200, 'darkriscv': 200,
              'spi': 500, 'uart': 500},
}

PDK_AREA: dict[str, dict[str, int]] = {
    'SO3': {
        'blake2s_core': 1000,
        'gcd':          20,
        'ibex_core':    4000,
        'picorv32':     6000,
        'poly1305_core': 1200,
    },
}

PDK_PER_CELL_DB_SUFFIX = "_tt_0.7_25_nldm.db"

def _pdk_root_from_env(pdk: str) -> str:
    root = os.environ.get("PDK_ROOT")
    if root:
        return f"{root}/lib"
    raise SystemExit("ERROR: PDK_ROOT is not set. Export PDK_ROOT in .env.local.")

PDK_LIB_DIR = type("PdkLibDir", (), {
    "__class_getitem__": lambda cls, pdk: _pdk_root_from_env(pdk),
    "__getitem__":       lambda self, pdk: _pdk_root_from_env(pdk),
})()

PDK_LIB_GLOBS: dict[str, list[str]] = {
    'SO3':   ['*' + PDK_PER_CELL_DB_SUFFIX.replace('.db', '.lib')],
}

PDK_CDL_SUFFIX: dict[str, str] = {
    'SO3':   '',
}

DESIGN_CLOCK_PORT: dict[str, str] = {
    'ibex_core': 'clk_i',
    'darkriscv': 'CLK',
    'ethmac': 'wb_clk_i',
    'eth_top': 'wb_clk_i',
}

PDK_BASELINE_CANONICAL_KEYS: dict[str, set[str]] = {
    'SO3':   {'0b1110', '0b1000'},
}

PDK_PER_CELL_LIBS = {'SO3'}

def _pdk_has_per_cell_libs(pdk: str) -> bool:
    return pdk in PDK_PER_CELL_LIBS

def _db_files_for_pdk(pdk: str, cell_names: Optional[list[str]] = None) -> list[Path]:
    pdk_lib = Path(PDK_LIB_DIR[pdk])
    pdk_root = pdk_lib.parent

    if cell_names and _pdk_has_per_cell_libs(pdk):
        db_dir = pdk_root / "db"
        return [db_dir / f"{c}{PDK_PER_CELL_DB_SUFFIX}" for c in cell_names]

    cfg_path = _FC_FLOW_DIR / "configs" / "pdk_config.json"
    with open(cfg_path) as f:
        cfg = json.load(f)
    pdk_cfg = cfg.get(pdk, {})
    worst_db = pdk_cfg.get("worst_db", [])
    db_files: list[Path] = []
    for rel in worst_db:
        target = pdk_root / rel
        if any(ch in rel for ch in "*?["):
            db_files.extend(sorted(pdk_root.glob(rel)))
        else:
            db_files.append(target)
    return db_files

def _extract_cell_features_fc(
    run_dir: Path,
    design: str,
    pdk: str,
    netlist: Path,
    def_path: Optional[Path] = None,
    spef_path: Optional[Path] = None,
) -> Optional[Path]:
    if pdk != 'SO3':
        return None

    out_csv = run_dir / "cell_timing_features.csv"
    if out_csv.exists():
        return out_csv

    sdc_path = run_dir / "sdc" / f"{design}_fc.sdc"
    if not sdc_path.exists():
        raise FileNotFoundError(
            f"SDC not found for cell feature extraction: {sdc_path}\n"
            "Ensure _run_synthesis_fc() has been called first."
        )

    clibs_dir = run_dir / "CLIBs"
    db_files = sorted(clibs_dir.resolve().glob("*.ndm")) if clibs_dir.is_dir() else []
    if not db_files:
        cell_names: Optional[list[str]] = None
        if _pdk_has_per_cell_libs(pdk):
            only_use_list = run_dir / "filters" / "only_use_lib_cells.list"
            if only_use_list.exists():
                cell_names = [
                    ln.strip() for ln in only_use_list.read_text().splitlines()
                    if ln.strip()
                ]
        db_files = _db_files_for_pdk(pdk, cell_names=cell_names)
    if not db_files:
        raise FileNotFoundError(
            f"No reference libraries found for feature extraction: neither "
            f"{clibs_dir}/*.ndm nor pdk_config.json worst_db for PDK {pdk!r}"
        )
    missing_db = [str(db) for db in db_files if not db.exists()]
    if missing_db:
        raise FileNotFoundError(
            f"Reference library path(s) not found: {missing_db}"
        )

    tcl_script = Path(_DATA_GEN) / "extract_cell_features.tcl"
    if not tcl_script.exists():
        raise FileNotFoundError(
            f"Extraction Tcl script not found: {tcl_script}"
        )

    parasitic_tcl = _FC_FLOW_DIR / "configs" / "parasitic" / f"{pdk}.tcl"
    fc_env = {
        **os.environ,
        "LM_LICENSE_FILE":      _FC_LICENSE,
        "SNPSLMD_LICENSE_FILE": _FC_LICENSE,
        "FEAT_NETLIST_V":       str(netlist.resolve()),
        "FEAT_DB_FILES":        ":".join(str(db) for db in db_files),
        "FEAT_SDC_FILE":        str(sdc_path.resolve()),
        "FEAT_DESIGN_NAME":     design,
        "FEAT_OUT_CSV":         str(out_csv.resolve()),
        "PDK_DIR":              str(Path(PDK_LIB_DIR[pdk]).parent.parent),
        "FEAT_PARASITIC_SETUP_FILE": str(parasitic_tcl) if parasitic_tcl.is_file() else "",
    }
    if def_path and def_path.exists():
        fc_env["FEAT_DEF_FILE"] = str(def_path.resolve())
    if spef_path and spef_path.exists():
        fc_env["FEAT_SPEF_FILE"] = str(spef_path.resolve())

    log_path = run_dir / "cell_features_extract.log"
    with open(log_path, 'w') as log:
        ret = subprocess.run(
            [_FC_SHELL, '-f', str(tcl_script)],
            env=fc_env,
            cwd=str(run_dir),
            stdout=log,
            stderr=subprocess.STDOUT,
        )
    if ret.returncode != 0:
        raise RuntimeError(
            f"Cell feature extraction failed (exit {ret.returncode}). "
            f"Log: {log_path}"
        )
    if not out_csv.exists():
        raise RuntimeError(
            f"cell_timing_features.csv not produced after extraction. "
            f"Log: {log_path}"
        )
    return out_csv

def _repo_root() -> Path:
    env = os.environ.get('REPO')
    if env:
        return Path(env)
    candidate = _REPO_ROOT
    if candidate.exists():
        return candidate
    raise RuntimeError(
        "Cannot locate GAPMA repo root. "
        "Set the REPO environment variable to the repo root path."
    )

def _tool(name: str) -> Path:
    repo = _repo_root()
    tools = {
        'lib2cdb':      repo / 'tools/mining/db/build/lib2cdb',
        'design2cdb':   repo / 'tools/mining/db/build/design2cdb',
        'mining':       repo / 'tools/mining/src/logic_cluster_mining/build/logic_func_minor_cpp',
    }
    if name not in tools:
        raise ValueError(f"unknown tool: {name!r}")
    p = tools[name]
    if not p.exists():
        raise FileNotFoundError(
            f"Tool binary not found: {p}\n"
            "Build the GAPMA tools first (see repo README)."
        )
    return p

def _lib_args(pdk: str) -> list[str]:
    lib_dir = Path(PDK_LIB_DIR[pdk])
    globs = PDK_LIB_GLOBS[pdk]
    libs = sorted({f for g in globs for f in lib_dir.glob(g)})
    if not libs:
        raise FileNotFoundError(
            f"No lib files found in {lib_dir} matching {globs!r}"
        )
    args = []
    for lib in libs:
        args += ['--libs', str(lib)]
    return args

def _make_only_use_dont_use_tcl(only_use_patterns: list[str]) -> str:
    lines = [
        "# Per-run dont_use.tcl generated by run_fc.py",
        "# only_use semantics matching flow/sub_scripts/fc_reference/data/dont_use.tcl:",
        "#   exclude all -> re-include each allowed cell with -include all.",
        "# Guard: each only_use pattern must match >=1 lib cell, else abort.",
        "",
        "# Block every cell in every lib",
        "set_lib_cell_purpose -exclude all [get_lib_cells */*]",
        "",
        "# Re-include only the allowed cells (with empty-match guard)",
    ]
    for pat in only_use_patterns:
        lines.append(f"set _luc [get_lib_cells -quiet {{*/{pat}}}]")
        lines.append(f'if {{[sizeof_collection $_luc] == 0}} {{ error "RM-error: only_use pattern \'{pat}\' matched 0 lib cells (silent-miss guard) -- aborting" }}')
        lines.append("set_lib_cell_purpose -include all $_luc")
    lines.append("")
    return "\n".join(lines) + "\n"

def _run_synthesis_fc(
    work_dir: Path,
    design: str,
    only_use: list[str],
    clock_period: int,
    pdk_name: str,
    stage: str = "synth",
    area: Optional[int] = None,
) -> Path:
    run_dir = work_dir
    run_dir.mkdir(parents=True, exist_ok=True)

    for item in ("scripts", "configs", "Makefile"):
        src = _FC_FLOW_DIR / item
        dst = run_dir / item
        if dst.exists() or dst.is_symlink():
            shutil.rmtree(str(dst)) if dst.is_dir() else dst.unlink()
        if src.is_dir():
            shutil.copytree(str(src), str(dst), symlinks=True)
        else:
            shutil.copy2(str(src), str(dst))

    design_setup = run_dir / "configs" / "design_setup.tcl"
    setup_text = design_setup.read_text()
    setup_text = re.sub(
        r'^(set SET_HOST_OPTIONS_MAX_CORES\s+)\[exec nproc\].*$',
        rf'\g<1>{_FC_MAX_CORES}',
        setup_text, flags=re.MULTILINE,
    )
    design_setup.write_text(setup_text)

    period_ns = clock_period / 1000.0
    delay_ns  = round(period_ns * 0.1, 6)
    clk_port  = DESIGN_CLOCK_PORT.get(design, 'clk')
    sdc_dir  = run_dir / "sdc"
    sdc_dir.mkdir(exist_ok=True)
    sdc_path = sdc_dir / f"{design}_fc.sdc"
    sdc_path.write_text(
        f"set target_clock_period {period_ns}\n"
        f"create_clock [get_ports {clk_port}] -period $target_clock_period -name clk\n"
        f"set_input_delay {delay_ns} -clock clk"
        f" [remove_from_collection [all_inputs] [get_ports {clk_port}]]\n"
        f"set_output_delay {delay_ns} -clock clk [all_outputs]\n"
    )

    filters_dir = run_dir / "filters"
    filters_dir.mkdir(exist_ok=True)
    (filters_dir / "dont_use.tcl").write_text(
        _make_only_use_dont_use_tcl(only_use)
    )

    fc_env = {
        **os.environ,
        "PDK_NAME":            pdk_name,
        "TOP_DESIGN_NAME":     design,
        "LM_LICENSE_FILE":     _FC_LICENSE,
        "SNPSLMD_LICENSE_FILE": _FC_LICENSE,
        "FC_MAX_CORES":        str(_FC_MAX_CORES),
    }
    fc_env["PDK_DIR"] = str(Path(PDK_LIB_DIR[pdk_name]).parent.parent)
    fc_env["RTL_ROOT"] = os.environ.get("RTL_ROOT") or str(_REPO_ROOT / "rtl")
    if _pdk_has_per_cell_libs(pdk_name):
        fc_env["CELL_NAMES"] = " ".join(only_use)
    if stage == "route_opt" and area is not None:
        fc_env["RUN_AREA"] = str(int(area))
    pdk_cfg_sh = run_dir / "configs" / "pdk_config.sh"
    ret = subprocess.run(
        ["bash", str(pdk_cfg_sh)], env=fc_env,
        capture_output=True, text=True,
    )
    if ret.returncode != 0:
        raise RuntimeError(
            f"pdk_config.sh failed (exit {ret.returncode}):\n{ret.stderr}"
        )

    make_target = "route_opt" if stage == "route_opt" else "compile_pre_dft"
    make_log = run_dir / f"{make_target}.log"
    with open(make_log, 'w') as log:
        ret = subprocess.run(
            ["make", "-B", "-C", str(run_dir), f"FC_EXEC={_FC_SHELL}", make_target],
            env=fc_env, stdout=log, stderr=subprocess.STDOUT,
        )
    if ret.returncode != 0:
        raise RuntimeError(
            f"make {make_target} failed (exit {ret.returncode}). Log: {make_log}"
        )

    if stage == "route_opt":
        netlist_path = run_dir / "outputs_fc" / "route_opt.v"
    else:
        netlist_path = (
            run_dir / "outputs_fc" / "compile_logic_opto.ascii_files" / f"{design}.v"
        )
    if not netlist_path.exists():
        raise RuntimeError(
            f"FC netlist not found after make {make_target}: {netlist_path}"
        )
    return netlist_path

def _run_synthesis(
    work_dir: Path,
    pdk: str,
    design: str,
    only_use: list[str],
    clock: int,
    stage: str = "synth",
    area: Optional[int] = None,
) -> Path:
    if clock is None:
        raise ValueError(f"No clock period provided for design {design!r} (PDK {pdk!r}). "
                         f"Known: {list(PDK_CLOCK[pdk].keys())}")

    return _run_synthesis_fc(work_dir, design, only_use, clock, pdk,
                             stage=stage, area=area)

def _run_mining(netlist: Path, work_dir: Path, pdk: str, top: str, mine_params: dict) -> Path:
    work_dir.mkdir(parents=True, exist_ok=True)
    cell_full = work_dir / 'cell_full.cdb'
    cell_cdb  = work_dir / 'cell.cdb'
    netlist_cdb = work_dir / 'netlist.cdb'
    cluster_cdb = work_dir / 'cluster.cdb'

    lib_args = _lib_args(pdk)

    if not cell_full.exists():
        cmd = [str(_tool('lib2cdb'))] + lib_args + ['--out', str(cell_full)]
        _run_or_raise(cmd, work_dir / 'lib2cdb.log', 'lib2cdb')

    shutil.copy2(cell_full, cell_cdb)

    if not netlist_cdb.exists():
        cmd = [str(_tool('design2cdb')),
               '--netlist', str(netlist)] + lib_args + [
               '--top', top,
               '--out', str(netlist_cdb)]
        _run_or_raise(cmd, work_dir / 'design2cdb.log', 'design2cdb')

    if not cluster_cdb.exists():
        cmd = [str(_tool('mining')),
               '--cell-db', str(cell_cdb),
               '--netlist-db', str(netlist_cdb),
               '--out-dir', str(work_dir),
               '--min_cells', str(mine_params['min_cells']),
               '--max_cells', str(mine_params['max_cells']),
               '--min_inputs', str(mine_params['min_inputs']),
               '--max_inputs', str(mine_params['max_inputs']),
               '--max_depth', str(mine_params['max_depth']),
               '--max_outputs', str(mine_params['max_outputs']),
               '--num_cores', str(mine_params['num_cores'])]
        _run_or_raise(cmd, work_dir / 'mining.log', 'mining')
        clusters = work_dir / 'clusters.cdb'
        if clusters.exists():
            clusters.rename(cluster_cdb)
        if not cluster_cdb.exists():
            raise RuntimeError(f"cluster.cdb not found after mining in {work_dir}")

    return cluster_cdb

def _run_or_raise(cmd: list[str], log_path: Path, tool_name: str) -> None:
    with open(log_path, 'w') as log:
        ret = subprocess.run(cmd, stdout=log, stderr=log)
    if ret.returncode != 0:
        raise RuntimeError(
            f"{tool_name} failed (exit {ret.returncode}). Log: {log_path}"
        )

_VERILOG_KW = frozenset({
    'module', 'endmodule', 'input', 'output', 'inout', 'wire', 'reg',
    'assign', 'always', 'begin', 'end', 'if', 'else', 'case', 'endcase',
    'parameter', 'localparam', 'integer', 'supply0', 'supply1', 'tri',
    'posedge', 'negedge', 'initial', 'generate', 'endgenerate',
})
_INST_RE = re.compile(r'^\s*([A-Za-z_]\w*)\s+\w')

def _parse_netlist_cell_counts(
    netlist_path: Path,
    cdl_suffix: str = '',
) -> dict[str, int]:
    counts: dict[str, int] = {}
    with open(netlist_path) as f:
        for line in f:
            m = _INST_RE.match(line)
            if not m:
                continue
            cell_type = m.group(1)
            if cell_type.lower() in _VERILOG_KW:
                continue
            if cdl_suffix and cell_type.endswith(cdl_suffix):
                cell_type = cell_type[: -len(cdl_suffix)]
            counts[cell_type] = counts.get(cell_type, 0) + 1
    return counts

def _write_cell_counts(run_dir: Path, counts: dict[str, int]) -> None:
    out = run_dir / 'cell_counts.csv'
    with open(out, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['cell_name', 'count'])
        for name in sorted(counts):
            w.writerow([name, counts[name]])

def run_baseline(out_root: Path, pdk: str, design: str, clock: int, mine_params: dict) -> Path:
    baseline_dir = out_root / design / f'clk_{clock}' / 'baseline'
    freq_csv = baseline_dir / 'canonical_freq.csv'
    if freq_csv.exists():
        print(f"[run_fc] Baseline already done: {freq_csv}")
        return freq_csv

    print(f"[run_fc] Running baseline synthesis: {design} / {pdk} @ clk {clock} ...")
    result = run_one(baseline_dir, pdk, design, only_use=[], canonical_keys=[], clock=clock,
                     mine_params=mine_params)
    if result is None:
        raise RuntimeError(f"Baseline synthesis returned None for {design}")
    return result

def run_one(
    run_dir: Path,
    pdk: str,
    design: str,
    only_use: list[str],
    canonical_keys: list[str],
    clock: int,
    mine_params: dict,
    stage: str = "synth",
    area: Optional[int] = None,
) -> Optional[Path]:
    freq_csv = run_dir / 'canonical_freq.csv'
    if freq_csv.exists():
        return freq_csv

    seed_str = ''.join(sorted(canonical_keys)) if canonical_keys else 'baseline'
    seed_val = int(hashlib.md5(seed_str.encode()).hexdigest(), 16) % (2 ** 32)
    sizes = MANDATORY_LOGIC_SIZES[pdk]
    mandatory = [sizes[seed_val % len(sizes)]] if sizes else []

    full_only_use = PDK_BASELINE[pdk] + mandatory + only_use

    filters_dir = run_dir / 'filters'
    filters_dir.mkdir(parents=True, exist_ok=True)
    (filters_dir / 'only_use_lib_cells.list').write_text('\n'.join(full_only_use) + '\n')

    netlist = _run_synthesis(run_dir, pdk, design, full_only_use, clock,
                             stage=stage, area=area)

    def_path = run_dir / "outputs_fc" / "route_opt.def" if stage == "route_opt" else None
    _extract_cell_features_fc(run_dir, design, pdk, netlist, def_path=def_path)

    cdl_suffix = PDK_CDL_SUFFIX.get(pdk, '')
    cell_counts = _parse_netlist_cell_counts(netlist, cdl_suffix)
    _write_cell_counts(run_dir, cell_counts)

    mining_dir = run_dir / 'mining'
    _run_mining(netlist, mining_dir, pdk, design, mine_params)
    pattern_csv = mining_dir / 'pattern_frequency.csv'
    if not pattern_csv.exists():
        raise RuntimeError(
            f"pattern_frequency.csv not found after mining in {mining_dir}"
        )
    shutil.copy2(pattern_csv, freq_csv)
    return freq_csv

def _run_one_wrapper(args: tuple) -> tuple[str, Optional[str], Optional[str]]:
    run_dir, pdk, design, only_use, canonical_keys, clock, stage, area, mine_params = args
    try:
        freq = run_one(Path(run_dir), pdk, design, only_use, canonical_keys, clock,
                       mine_params, stage=stage, area=area)
        return run_dir, str(freq) if freq else None, None
    except Exception as e:
        return run_dir, None, str(e)

def main() -> None:
    p = argparse.ArgumentParser(
        description='Generate FC synthesis training runs for demand_predictor.'
    )
    p.add_argument('--cells-csv',
                   default=str(Path(__file__).resolve().parent / 'cells.csv'),
                   help='Candidate cell list. Defaults to cells.csv beside this tool.')
    p.add_argument('--pdk',     required=True, choices=list(PDK_BASELINE.keys()))
    p.add_argument('--design',  required=True,
                   help='Design (top-module) name. SO3 training: gcd, picorv32, '
                        'ibex_core, blake2s_core, poly1305_core.')
    p.add_argument('--clock', type=int, nargs='*', default=None,
                   help='Clock period(s) in ps; overrides PDK_CLOCK. '
                        'Multiple values sweep multiple clocks for the design.')
    p.add_argument('--out-root', required=True,
                   help='Root directory for output run dirs.')
    p.add_argument('--n-runs',  type=int, default=0,
                   help='Max runs to launch (0 = all subsets).')
    p.add_argument('--jobs',    type=int, default=1,
                   help='Parallel synthesis jobs.')
    p.add_argument('--stage', choices=['synth', 'route_opt'], default='route_opt',
                   help="FC stage per subset run. 'synth' = compile_pre_dft only; "
                        "'route_opt' = full P&R (route_opt netlist + DEF). "
                        "Default route_opt (training labels need routed cell_counts).")
    p.add_argument('--area', type=int, default=None,
                   help='Floorplan area in um^2 for --stage route_opt. Overrides '
                        'PDK_AREA[pdk][design]. Required if design not in PDK_AREA.')
    p.add_argument('--max-delta', type=int, default=16)
    p.add_argument('--seed',    type=int, default=42)
    p.add_argument('--mine-min-cells', type=int, default=2)
    p.add_argument('--mine-max-cells', type=int, default=8)
    p.add_argument('--mine-min-inputs', type=int, default=2)
    p.add_argument('--mine-max-inputs', type=int, default=4)
    p.add_argument('--mine-max-depth', type=int, default=3)
    p.add_argument('--mine-max-outputs', type=int, default=1,
                   help='Max outputs per mined candidate cell cluster.')
    p.add_argument('--mine-num-cores', type=int, default=4)
    p.add_argument('--baseline-only', action='store_true',
                   help='Run only the baseline synthesis + mining for each clock '
                        'and exit before subset generation. Lets a launch script '
                        'fire one design per process, each with its own log, '
                        'without triggering the full subset sweep.')
    args = p.parse_args()
    if not _FC_LICENSE:
        p.error("FC_LICENSE is not set. See .env.local.")

    mine_params = {
        'min_cells':  args.mine_min_cells,
        'max_cells':  args.mine_max_cells,
        'min_inputs': args.mine_min_inputs,
        'max_inputs': args.mine_max_inputs,
        'max_depth':  args.mine_max_depth,
        'max_outputs': args.mine_max_outputs,
        'num_cores':  args.mine_num_cores,
    }

    out_root = Path(args.out_root)
    out_root.mkdir(parents=True, exist_ok=True)

    if args.design not in PDK_CLOCK[args.pdk]:
        p.error(f"Unknown design {args.design!r} for PDK {args.pdk}. "
                f"Known: {list(PDK_CLOCK[args.pdk].keys())}")
    clocks = PDK_CLOCK[args.pdk][args.design]
    if not isinstance(clocks, list):
        clocks = [clocks]
    if args.clock:
        clocks = args.clock
    print(f"[run_fc] {args.design}: {len(clocks)} clock(s) {clocks}")

    area = args.area
    if args.stage == 'route_opt' and area is None:
        area = PDK_AREA.get(args.pdk, {}).get(args.design)
        if area is None:
            p.error(f"--stage route_opt needs a floorplan area for {args.design!r}: "
                    f"not in PDK_AREA[{args.pdk!r}] and no --area given. "
                    f"Known: {sorted(PDK_AREA.get(args.pdk, {}))}")
    print(f"[run_fc] stage={args.stage}"
          + (f" area={area} um^2" if area is not None else ""))

    total_ok = 0
    total_failed = 0
    for clock in clocks:
        print(f"\n========== {args.design} @ clk {clock} ps ==========")
        design_root = out_root / args.design / f"clk_{clock}"

        baseline_freq_csv = run_baseline(out_root, args.pdk, args.design, clock, mine_params)
        if args.baseline_only:
            print(f"[run_fc] --baseline-only: DONE: {baseline_freq_csv}")
            continue
        print(f"[run_fc] Generating subsets from baseline mining: {baseline_freq_csv}")
        subsets = generate_subsets_so3(
            mining_csv=str(baseline_freq_csv),
            cells_csv=args.cells_csv,
            baseline_canonical_keys=PDK_BASELINE_CANONICAL_KEYS['SO3'],
            max_delta=args.max_delta,
            seed=args.seed,
        )
        print(f"[run_fc] {len(subsets)} candidate subsets")

        runs = subsets
        if args.n_runs > 0:
            import random
            random.seed(args.seed)
            runs = list(subsets)
            random.shuffle(runs)
            runs = runs[:args.n_runs]
            print(f"[run_fc] Limited to {len(runs)} runs (--n-runs)")

        tasks = []
        for i, subset in enumerate(runs):
            keys_sorted = sorted(subset.canonical_keys)
            dir_name = f"run_{i:04d}_" + "_".join(
                k.replace('0b', '') for k in keys_sorted
            )[:80]
            run_dir = design_root / dir_name
            tasks.append((
                str(run_dir), args.pdk, args.design,
                subset.lib_patterns, keys_sorted, clock,
                args.stage, area, mine_params,
            ))

        if args.jobs == 1:
            for task in tasks:
                run_dir, freq, err = _run_one_wrapper(task)
                if err:
                    print(f"[FAIL] {run_dir}: {err}", file=sys.stderr)
                    total_failed += 1
                else:
                    print(f"[OK] {freq}")
                    total_ok += 1
        else:
            with ProcessPoolExecutor(max_workers=args.jobs) as pool:
                futures = {pool.submit(_run_one_wrapper, t): t for t in tasks}
                for fut in as_completed(futures):
                    run_dir, freq, err = fut.result()
                    if err:
                        print(f"[FAIL] {run_dir}: {err}", file=sys.stderr)
                        total_failed += 1
                    else:
                        print(f"[OK] {freq}")
                        total_ok += 1

    print(f"\n[run_fc] Done: {total_ok} succeeded, {total_failed} failed")
    if total_failed:
        sys.exit(1)

if __name__ == '__main__':
    main()
