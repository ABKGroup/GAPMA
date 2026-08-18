#!/usr/bin/env bash
# This script was written and developed by ABKGroup students at UCSD.
# However, the underlying commands and reports are copyrighted by Cadence.
# We thank Cadence for granting permission to share our research to help
# promote and foster the next generation of innovators.

# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.


set -euo pipefail

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    cat <<'EOF'
Usage: single_cell.sh <CELL>

Generates GDS, LEF, PEX, .lib and .db for one SO3 cell under $PDK_ROOT,
skipping the run entirely when GDS, LEF and DB are all already present.

Environment:
  PDK_ROOT           required, the SO3 PDK tree the outputs are written into
  CELLGEN_ROOT       SO3-Cell CellGen framework (default: ./SO3-Cell/Framework/CellGen)
  CELLGEN_WORK_ROOT  per-cell work dir (default: $PDK_ROOT/cellgen_runs)
  CELLGEN_MODULES    environment modules to load (default: innovus ruby klayout
                     pegasus quantus liberate library_compiler)
  KLAYOUT PEGASUS QUANTUS LIBERATE LC_SHELL PYTHON
                     tool binaries, each defaulting to its own name on PATH
  PROCESS VDD TEMP   characterization corner (default: tt 0.7 25)
EOF
    exit 0
fi

CELL="${1:?ERROR: cell name required as first argument}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
[[ -f "$REPO_ROOT/.env.local" ]] && source "$REPO_ROOT/.env.local"

: "${PDK_ROOT:?ERROR: PDK_ROOT is not set. Define it in .env.local.}"

CELLGEN_ROOT="${CELLGEN_ROOT:-$HERE/SO3-Cell/Framework/CellGen}"
SCRIPTS_DIR="${SCRIPTS_DIR:-$HERE/scripts/char}"
WORK_ROOT="${CELLGEN_WORK_ROOT:-$PDK_ROOT/cellgen_runs}"

STDQRC_DIR="${STDQRC_DIR:-$HERE/SO3-Cell/Framework/PostCellGen/inputs/stdqrc}"

PROCESS="${PROCESS:-tt}"
VDD="${VDD:-0.7}"
TEMP="${TEMP:-25}"
MIN_TRAN="${MIN_TRAN:-6e-12}"
MAX_TRAN="${MAX_TRAN:-7e-11}"
MIN_OUT_CAP="${MIN_OUT_CAP:-1e-16}"
INV_X1_PIN_CAP="${INV_X1_PIN_CAP:-0.0003096}"

PYTHON="${PYTHON:-python3}"
KLAYOUT="${KLAYOUT:-klayout}"
PEGASUS="${PEGASUS:-pegasus}"
QUANTUS="${QUANTUS:-quantus}"
LIBERATE="${LIBERATE:-liberate}"
LC_SHELL="${LC_SHELL:-lc_shell}"

CELLGEN_MODULES="${CELLGEN_MODULES-innovus ruby klayout pegasus quantus liberate library_compiler}"

load_modules() {
    [[ -z "$CELLGEN_MODULES" ]] && return 0
    if ! command -v module >/dev/null 2>&1; then
        if [[ -r /etc/profile.d/modules.sh ]]; then
            source /etc/profile.d/modules.sh
        fi
    fi
    if ! command -v module >/dev/null 2>&1; then
        echo "[info] no environment-module system found; expecting tools on PATH" >&2
        return 0
    fi
    for m in $CELLGEN_MODULES; do
        if ! module load "$m" 2>/dev/null; then
            echo "[warn] module load $m failed; expecting that tool on PATH" >&2
        fi
    done
}

GDS_OUT="$PDK_ROOT/gds/${CELL}.gds"
LEF_OUT="$PDK_ROOT/lef/${CELL}.lef"
LIB_OUT="$PDK_ROOT/lib/${CELL}_${PROCESS}_${VDD}_${TEMP}_nldm.lib"
DB_OUT="$PDK_ROOT/db/${CELL}_${PROCESS}_${VDD}_${TEMP}_nldm.db"

if [[ -f "$GDS_OUT" && -f "$LEF_OUT" && -f "$DB_OUT" ]]; then
    echo "[SKIP] $CELL - GDS+LEF+DB already exist"
    exit 0
fi

echo "==== [$CELL] starting cell generation ===="
load_modules

WORKDIR="$WORK_ROOT/${CELL}"
mkdir -p "$WORKDIR"/{gds,pex,results/{cell_info,libchar,lib,pex},logs,inputs}
mkdir -p "$PDK_ROOT"/{gds,lef,lib,db,pex,cdl}

CDL_SRC="$PDK_ROOT/cdl/${CELL}.cdl"
if [[ ! -f "$CDL_SRC" ]]; then
    CDL_SRC="$PDK_ROOT/cdl/${CELL%_DH_?}.cdl"
fi
if [[ ! -f "$CDL_SRC" ]]; then
    echo "ERROR: no CDL for $CELL under $PDK_ROOT/cdl/" >&2
    exit 1
fi

if [[ ! -f "$GDS_OUT" ]]; then
    echo "[$CELL] STEP 1: CellGen (CDL -> GDS)"
    [[ -f "$CELLGEN_ROOT/run_cell.sh" ]] || {
        echo "ERROR: $CELLGEN_ROOT/run_cell.sh not found. Clone with --recurse-submodules." >&2
        exit 1
    }

    ARCH_ARGS=()
    case "$CELL" in
        *_DH_N) ARCH_ARGS=(--arch DH --mh-order N_FIRST) ;;
        *_DH_P) ARCH_ARGS=(--arch DH --mh-order P_FIRST) ;;
    esac

    PYTHON="$PYTHON" KLAYOUT="$KLAYOUT" \
    bash "$CELLGEN_ROOT/run_cell.sh" \
        --cdl "$CDL_SRC" \
        --cells "$CELL" \
        --gds-out "$WORKDIR/gds" \
        "${ARCH_ARGS[@]}" \
        > "$WORKDIR/logs/cellgen.log" 2>&1

    if [[ ! -f "$WORKDIR/gds/${CELL}.gds" ]]; then
        echo "ERROR: CellGen produced no GDS for $CELL" >&2
        tail -10 "$WORKDIR/logs/cellgen.log" >&2
        exit 1
    fi
    cp "$WORKDIR/gds/${CELL}.gds" "$GDS_OUT"
    echo "[$CELL] STEP 1 done - $GDS_OUT"
fi

if [[ ! -f "$LEF_OUT" ]]; then
    echo "[$CELL] STEP 2: LEF (GDS -> LEF)"

    # A KLayout whose shared libraries or RUBYLIB are unresolved still exists on
    # PATH and only fails once it tries to run a script, so probe it here rather
    # than reading that failure out of a per-cell conversion log.
    if ! KL_VER=$("$KLAYOUT" -v 2>&1); then
        echo "ERROR: '$KLAYOUT' is present but does not run:" >&2
        echo "  $KL_VER" >&2
        echo "  Set KLAYOUT, LD_LIBRARY_PATH and RUBYLIB for a working KLayout in .env.local." >&2
        exit 1
    fi

    LEF_RC=0
    "$KLAYOUT" -b -r "$SCRIPTS_DIR/gds_to_lef/run.py" \
        -rd config="$SCRIPTS_DIR/gds_to_lef/so3.json" \
        -rd gds="$GDS_OUT" \
        -rd cell="$CELL" \
        -rd out="$LEF_OUT" \
        > "$WORKDIR/logs/lef.log" 2>&1 || LEF_RC=$?

    if (( LEF_RC != 0 )); then
        echo "ERROR: gds_to_lef/run.py exited $LEF_RC for $CELL" >&2
        tail -10 "$WORKDIR/logs/lef.log" >&2
        exit 1
    fi
    if [[ ! -f "$LEF_OUT" ]]; then
        echo "ERROR: gds_to_lef/run.py produced no LEF for $CELL" >&2
        tail -5 "$WORKDIR/logs/lef.log" >&2
        exit 1
    fi
    ERR=$(awk 'tolower($0)~/error|fatal/{c++} END{print c+0}' "$WORKDIR/logs/lef.log")
    if [[ $ERR -gt 0 ]]; then
        echo "ERROR: KLayout reported $ERR error(s) for $CELL:" >&2
        awk 'tolower($0)~/error|fatal/{print; if(++n>=3) exit}' "$WORKDIR/logs/lef.log" >&2
        exit 1
    fi
    echo "[$CELL] STEP 2 done - $LEF_OUT"
fi

SP_RESULT="$WORKDIR/results/pex/${CELL}_${CELL}.sp"
if [[ ! -f "$SP_RESULT" ]]; then
    echo "[$CELL] STEP 3: PEX (Pegasus + Quantus)"
    if [[ ! -d "$STDQRC_DIR" ]]; then
        echo "ERROR: QRC technology data not found at $STDQRC_DIR." >&2
        echo "       Expand it once with SO3-Cell/Framework/PostCellGen/inputs/unzip_stdqrc.sh," >&2
        echo "       or point STDQRC_DIR at an existing copy." >&2
        exit 1
    fi

    cp "$CDL_SRC" "$WORKDIR/pex/${CELL}.cdl"
    ( cd "$WORKDIR/pex"
      "$PEGASUS" -ext \
        -gds "$GDS_OUT" \
        -spice "${CELL}.cdl" \
        -rc_data \
        -top_cell "$CELL" \
        "$SCRIPTS_DIR/lvs.pvl" \
        > "$WORKDIR/logs/pegasus.log" 2>&1 )

    if [[ ! -d "$WORKDIR/pex/svdb" ]]; then
        echo "ERROR: Pegasus extraction failed for $CELL (no svdb)" >&2
        tail -10 "$WORKDIR/logs/pegasus.log" >&2
        exit 1
    fi
    ERR_P=$(awk '/^ERROR|^Error|^FATAL/{c++} END{print c+0}' "$WORKDIR/logs/pegasus.log")
    if [[ $ERR_P -gt 0 ]]; then
        echo "ERROR: Pegasus reported $ERR_P error(s) for $CELL:" >&2
        awk '/^ERROR|^Error|^FATAL/{print; if(++n>=5) exit}' "$WORKDIR/logs/pegasus.log" >&2
        exit 1
    fi

    cat > "$WORKDIR/inputs/techlib.defs" << TEOF
DEFINE ${CELL} ${STDQRC_DIR}
TEOF

    cat > "$WORKDIR/pex/run_quantus.cmd" << QEOF
distributed_processing -multi_cpu 4

process_technology -technology_library_file "${WORKDIR}/inputs/techlib.defs" -technology_name ${CELL} -temperature ${TEMP}

output_setup -file_name ${CELL}_${CELL}.sp

input_db -type pvs -run_name ${CELL} -directory_name ./svdb -format GDS -design_cell_name ${CELL}
output_db -type spice -include_cap_model true -include_res_model true -include_parasitic_cap_model true -include_parasitic_res_model true
capacitance -ground_net VSS -decoupling_factor 1.0
extract -selection all -type rc_coupled
QEOF

    ( cd "$WORKDIR/pex"
      "$QUANTUS" -cmd "$WORKDIR/pex/run_quantus.cmd" > "$WORKDIR/logs/quantus.log" 2>&1 )

    QTERM=$(awk '/Quantus terminated normally/{c++} END{print c+0}' "$WORKDIR/logs/quantus.log")
    if [[ ! -f "$WORKDIR/pex/${CELL}_${CELL}.sp" || "$QTERM" -eq 0 ]]; then
        echo "ERROR: Quantus did not complete for $CELL" >&2
        tail -10 "$WORKDIR/logs/quantus.log" >&2
        exit 1
    fi
    ERR_Q=$(awk '/^ERROR|^Error|^FATAL/{c++} END{print c+0}' "$WORKDIR/logs/quantus.log")
    if [[ $ERR_Q -gt 0 ]]; then
        echo "WARNING: Quantus printed $ERR_Q error line(s) for $CELL but terminated normally and produced output:"
        awk '/^ERROR|^Error|^FATAL/{print; if(++n>=5) exit}' "$WORKDIR/logs/quantus.log"
    fi

    cp "$WORKDIR/pex/${CELL}_${CELL}.sp" "$WORKDIR/results/pex/"
    cp "$WORKDIR/pex/${CELL}_${CELL}.sp" "$PDK_ROOT/pex/${CELL}_${CELL}.sp"
    echo "[$CELL] STEP 3 done - $SP_RESULT"
fi

if [[ ! -f "$LIB_OUT" ]]; then
    echo "[$CELL] STEP 4: LIB (Liberate)"

    "$PYTHON" "$SCRIPTS_DIR/get_cell_info.py" \
        --gds "$GDS_OUT" \
        --info_dir "$WORKDIR/results/cell_info" \
        > "$WORKDIR/logs/info.log" 2>&1

    if [[ ! -f "$WORKDIR/results/cell_info/${CELL}.info" ]]; then
        echo "ERROR: get_cell_info.py produced no .info for $CELL" >&2
        tail -5 "$WORKDIR/logs/info.log" >&2
        exit 1
    fi

    ln -sfn "$PDK_ROOT/models/PROBE.pm" "$WORKDIR/inputs/PROBE.pm"

    "$SCRIPTS_DIR/genLibTemplate.tcl" \
        "$WORKDIR/inputs" "$WORKDIR/results" "$CELL" "$CELL" \
        > "$WORKDIR/logs/genLibTemplate.log" 2>&1

    "$SCRIPTS_DIR/genCellList.tcl" \
        "$WORKDIR/results" "$SCRIPTS_DIR" "$CELL" "$CELL" \
        > "$WORKDIR/logs/genCellList.log" 2>&1

    [[ -f "$WORKDIR/results/libchar/${CELL}_template.lib" ]] || {
        echo "ERROR: genLibTemplate.tcl produced no template for $CELL" >&2; exit 1; }
    [[ -f "$WORKDIR/results/libchar/cells.tcl" ]] || {
        echo "ERROR: genCellList.tcl produced no cells.tcl" >&2; exit 1; }

    export SCRIPT_DIR="$SCRIPTS_DIR"
    export RESULT_DIR="$WORKDIR/results"
    export INPUT_DIR="$WORKDIR/inputs"
    export LOG_DIR="$WORKDIR/logs"
    ( cd "$WORKDIR"
      "$LIBERATE" --lorder TOKENS \
        "$SCRIPTS_DIR/char.tcl" \
        "$PROCESS" "$VDD" "$TEMP" \
        "$MIN_TRAN" "$MAX_TRAN" "$MIN_OUT_CAP" "$INV_X1_PIN_CAP" \
        > "$WORKDIR/logs/char.log" 2>&1 )

    COMBINED_LIB="$WORKDIR/results/lib/${CELL}_${PROCESS}_${VDD}_${TEMP}_nldm.lib"
    if [[ ! -f "$COMBINED_LIB" ]]; then
        echo "ERROR: Liberate produced no .lib for $CELL" >&2
        tail -10 "$WORKDIR/logs/char.log" >&2
        exit 1
    fi
    ERR_C=$(awk '/^(Error|ERROR|FATAL)/{c++} END{print c+0}' "$WORKDIR/logs/char.log")
    if [[ $ERR_C -gt 0 ]]; then
        echo "ERROR: Liberate reported $ERR_C error(s) for $CELL:" >&2
        awk '/^(Error|ERROR|FATAL)/{print; if(++n>=5) exit}' "$WORKDIR/logs/char.log" >&2
        exit 1
    fi

    cp "$COMBINED_LIB" "$LIB_OUT"
    echo "[$CELL] STEP 4 done - $LIB_OUT"
fi

if [[ ! -f "$DB_OUT" ]]; then
    echo "[$CELL] STEP 5: DB (lc_shell)"
    if [[ -n "${LC_COMPAT_LIBS:-}" ]]; then
        export LD_LIBRARY_PATH="${LC_COMPAT_LIBS}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    fi

    "$LC_SHELL" -f /dev/stdin > "$WORKDIR/logs/db.log" 2>&1 << LCEOF
read_lib ${LIB_OUT}
write_lib ${CELL}_${PROCESS}_${VDD}_${TEMP} -format db -output ${DB_OUT}
quit
LCEOF

    if [[ ! -f "$DB_OUT" ]]; then
        echo "ERROR: lc_shell produced no .db for $CELL" >&2
        tail -5 "$WORKDIR/logs/db.log" >&2
        exit 1
    fi
    ERR_D=$(awk '/^(Error|ERROR|FATAL|error)/{c++} END{print c+0}' "$WORKDIR/logs/db.log")
    if [[ $ERR_D -gt 0 ]]; then
        echo "ERROR: lc_shell reported $ERR_D error(s) for $CELL:" >&2
        awk '/^(Error|ERROR|FATAL|error)/{print; if(++n>=5) exit}' "$WORKDIR/logs/db.log" >&2
        exit 1
    fi
    echo "[$CELL] STEP 5 done - $DB_OUT"
fi

echo "==== [$CELL] complete: GDS, LEF, PEX, LIB, DB installed under $PDK_ROOT ===="
