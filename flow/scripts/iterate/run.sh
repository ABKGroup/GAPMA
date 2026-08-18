#!/usr/bin/env bash

set -uo pipefail

GAPMA_ROOT="${GAPMA_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}"
[ -f "$GAPMA_ROOT/.env.local" ] && source "$GAPMA_ROOT/.env.local"

usage() {
    cat <<'USAGE'
run.sh — D_m -> D_{m+1} per-iteration orchestrator.

Required:
    --design        <aes|jpeg|eth|sha256|keccak>
    --prev-dir      EITHER (a) a single FC run dir (has qor.csv), e.g.
                            aes_cipher_top_L0/420_5.0_2
                    OR     (b) the sweep parent dir (children look like
                            <area>_<freq>_<rep>/). The orchestrator detects
                            the form and acts accordingly.
    --arity         <3in|4in>
    --next-label    <D{m+1} label>
    --next-fc-root  <root for next-iter dirs>

Optional (auto-detected if omitted):
    --server        <SP&R server; default runs setup/sweep locally, no server needed>
    --prev-best-run <run name inside prev-dir, e.g. 420_5.0_2>
                    If omitted AND --prev-dir is a sweep parent:
                    scan_pass.py picks the --prev-metric best PASS run.
                    If omitted AND --prev-dir is a single FC run dir:
                    derived from basename.
    --prev-metric   {min-area|min-ecp}   default: min-area
                    Used by the auto-discovery above. min-area filters
                    by design's baseline freq; min-ecp filters by canonical
                    area (per the canonical-area rules).

Required unless --infer-only:
    --sweep-areas   "a1 a2 a3 ..."
    --sweep-freq    <GHz>

Required unless --canonicals (i.e. inference must run):
    --model         <model.pt>
    --norm-stats    <model.norm_stats.json>
    --features-l0   <features_l0_with_x2_4in.csv>
    --p-to-np       <global_p_to_np.csv>

Two-phase / loop-friendly:
    --infer-only         run phase_infer.sh only, then exit 0
    --canonicals "c1..c5"  skip inference + rank-select; use these 5 canonicals
    --pred-x1    "p1..p5"  paired with --canonicals
    --pred-x2    "p1..p5"  paired with --canonicals
    --rank-tag   <str>     log label (e.g. rank1, rank2)

Sub-step isolation (--step runs ONE phase and exits):
    --step infer         same as --infer-only; writes INF_CSV path on stdout
    --step rank-select   run phase_rank_select on derived INF_CSV; print 3 lines
                         (canonicals, pred-x1, pred-x2). Use --rank N (default 1).
    --step resolve       requires --canonicals + --pred-x1 + --pred-x2;
                         print final cell list (1 per line) to stdout.
    --step setup         requires --cells-file <path>; stage NEW_DIR (local by default, or on --server).
    --step sweep         requires NEW_DIR already staged; launch sweep.
    --rank <N>           used by --step rank-select (default 1).
    --cells-file <path>  used by --step setup (cell list, 1 per line).

Optional:
    --reps           <int, default 3>
    --parallel       <int, default 6>
    --k1             <int, default 10>
    --k2             <int, default 5>
    --inf-payload-root   <override location>
    --x2-ratio       <float, default 0.10>
    --pred-x1-floor  <float, default 0.001>
    --cellgen-output-dir <dir>
    --cellgen-tr-max <int, default 20>
    --cellgen-timeout <seconds, default 3600>
    --lib2cdb-bin    <path>
    --pdk-root       <default from env PDK_ROOT>
    --force          overwrite existing setup
    --wait-sweep     block until "SWEEP DONE"
    --dry-run        print steps; no real launches
USAGE
}

DESIGN=""; PREV_DIR=""; PREV_BEST_RUN=""; ARITY=""; NEXT_LABEL=""; NEXT_FC_ROOT=""
SERVER=""
SWEEP_AREAS=""; SWEEP_FREQ=""
MODEL=""; NORM_STATS=""; FEATURES_L0=""; P_TO_NP=""
MODEL_BUNDLE=""
PREV_METRIC="min-area"
REPS=3; PARALLEL=6; K1=10; K2=5
INF_PAYLOAD_ROOT=""
X2_RATIO=0.10; PRED_X1_FLOOR=0.001
CELLGEN_OUTPUT_DIR=""; CELLGEN_TR_MAX=20; CELLGEN_TIMEOUT=3600
LIB2CDB_BIN=""
PDK_ROOT="${PDK_ROOT:-}"
FORCE=0; WAIT_SWEEP=0; DRY_RUN=0
INFER_ONLY=0
CANONICALS_OVERRIDE=""; PRED_X1_OVERRIDE=""; PRED_X2_OVERRIDE=""
RANK_TAG="rank1"
STEP=""
RANK=1
CELLS_FILE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --design)            DESIGN="$2"; shift 2 ;;
        --prev-dir)          PREV_DIR="$2"; shift 2 ;;
        --prev-best-run)     PREV_BEST_RUN="$2"; shift 2 ;;
        --arity)             ARITY="$2"; shift 2 ;;
        --next-label)        NEXT_LABEL="$2"; shift 2 ;;
        --next-fc-root)      NEXT_FC_ROOT="$2"; shift 2 ;;
        --server)            SERVER="$2"; shift 2 ;;
        --sweep-areas)       SWEEP_AREAS="$2"; shift 2 ;;
        --sweep-freq)        SWEEP_FREQ="$2"; shift 2 ;;
        --model)             MODEL="$2"; shift 2 ;;
        --norm-stats)        NORM_STATS="$2"; shift 2 ;;
        --features-l0)       FEATURES_L0="$2"; shift 2 ;;
        --p-to-np)           P_TO_NP="$2"; shift 2 ;;
        --model-bundle)      MODEL_BUNDLE="$2"; shift 2 ;;
        --prev-metric)       PREV_METRIC="$2"; shift 2 ;;
        --reps)              REPS="$2"; shift 2 ;;
        --parallel)          PARALLEL="$2"; shift 2 ;;
        --k1)                K1="$2"; shift 2 ;;
        --k2)                K2="$2"; shift 2 ;;
        --inf-payload-root)  INF_PAYLOAD_ROOT="$2"; shift 2 ;;
        --x2-ratio)          X2_RATIO="$2"; shift 2 ;;
        --pred-x1-floor)     PRED_X1_FLOOR="$2"; shift 2 ;;
        --cellgen-output-dir) CELLGEN_OUTPUT_DIR="$2"; shift 2 ;;
        --cellgen-tr-max)    CELLGEN_TR_MAX="$2"; shift 2 ;;
        --cellgen-timeout)   CELLGEN_TIMEOUT="$2"; shift 2 ;;
        --lib2cdb-bin)       LIB2CDB_BIN="$2"; shift 2 ;;
        --pdk-root)          PDK_ROOT="$2"; shift 2 ;;
        --infer-only)        INFER_ONLY=1; shift ;;
        --canonicals)        CANONICALS_OVERRIDE="$2"; shift 2 ;;
        --pred-x1)           PRED_X1_OVERRIDE="$2"; shift 2 ;;
        --pred-x2)           PRED_X2_OVERRIDE="$2"; shift 2 ;;
        --rank-tag)          RANK_TAG="$2"; shift 2 ;;
        --step)              STEP="$2"; shift 2 ;;
        --rank)              RANK="$2"; shift 2 ;;
        --cells-file)        CELLS_FILE="$2"; shift 2 ;;
        --force)             FORCE=1; shift ;;
        --wait-sweep)        WAIT_SWEEP=1; shift ;;
        --dry-run)           DRY_RUN=1; shift ;;
        -h|--help)           usage; exit 0 ;;
        *) echo "ERROR: unknown arg: $1" >&2; usage; exit 1 ;;
    esac
done

if [[ -z "$PDK_ROOT" ]]; then
    ENV_LOCAL="${GAPMA_ROOT}/.env.local"
    echo "ERROR: PDK_ROOT is not set (no --pdk-root arg, and PDK_ROOT is not exported)." >&2
    if [[ -f "$ENV_LOCAL" ]]; then
        echo "       Fix: 'source ${ENV_LOCAL}' (exists, exports PDK_ROOT), or export PDK_ROOT, or pass --pdk-root <dir>." >&2
    else
        echo "       Fix: export PDK_ROOT, or pass --pdk-root <dir> (${ENV_LOCAL} not found on this host)." >&2
    fi
    exit 1
fi
export PDK_ROOT

if (( INFER_ONLY )) && [[ -n "$CANONICALS_OVERRIDE" ]]; then
    echo "ERROR: --infer-only and --canonicals are mutually exclusive" >&2; exit 1
fi
if [[ -n "$CANONICALS_OVERRIDE" || -n "$PRED_X1_OVERRIDE" || -n "$PRED_X2_OVERRIDE" ]]; then
    if [[ -z "$CANONICALS_OVERRIDE" || -z "$PRED_X1_OVERRIDE" || -z "$PRED_X2_OVERRIDE" ]]; then
        echo "ERROR: --canonicals/--pred-x1/--pred-x2 must be supplied together" >&2; exit 1
    fi
fi
if [[ -n "$STEP" ]]; then
    case "$STEP" in
        infer|rank-select|resolve|setup|sweep) : ;;
        *) echo "ERROR: --step must be one of: infer rank-select resolve setup sweep (got '$STEP')" >&2; exit 1 ;;
    esac
    if (( INFER_ONLY )); then
        echo "ERROR: --step and --infer-only are mutually exclusive (--step infer == --infer-only)" >&2; exit 1
    fi
fi
if ! [[ "$RANK" =~ ^[1-9][0-9]*$ ]]; then
    echo "ERROR: --rank must be a positive integer (got '$RANK')" >&2; exit 1
fi
case "$PREV_METRIC" in
    min-area|min-ecp) : ;;
    *) echo "ERROR: --prev-metric must be 'min-area' or 'min-ecp' (got '$PREV_METRIC')" >&2; exit 1 ;;
esac

RUN_INFER=1; RUN_RANK=1; RUN_RESOLVE=1; RUN_SETUP=1; RUN_SWEEP=1
if (( INFER_ONLY )); then
    RUN_RANK=0; RUN_RESOLVE=0; RUN_SETUP=0; RUN_SWEEP=0
fi
if [[ -n "$CANONICALS_OVERRIDE" ]]; then
    RUN_INFER=0; RUN_RANK=0
fi
if [[ -n "$STEP" ]]; then
    RUN_INFER=0; RUN_RANK=0; RUN_RESOLVE=0; RUN_SETUP=0; RUN_SWEEP=0
    case "$STEP" in
        infer)       RUN_INFER=1 ;;
        rank-select) RUN_RANK=1 ;;
        resolve)     RUN_RESOLVE=1 ;;
        setup)       RUN_SETUP=1 ;;
        sweep)       RUN_SWEEP=1 ;;
    esac
fi

miss=()
for v in DESIGN PREV_DIR ARITY NEXT_LABEL NEXT_FC_ROOT; do
    [[ -z "${!v}" ]] && miss+=("--${v,,}")
done
if (( RUN_SWEEP )); then
    for v in SWEEP_AREAS SWEEP_FREQ; do
        [[ -z "${!v}" ]] && miss+=("--${v,,}")
    done
fi
if (( RUN_INFER )); then
    if [[ -n "$MODEL_BUNDLE" ]]; then
        if [[ -n "$MODEL$NORM_STATS$FEATURES_L0$P_TO_NP" ]]; then
            echo "ERROR: --model-bundle is mutually exclusive with --model/--norm-stats/--features-l0/--p-to-np" >&2
            exit 1
        fi
        if [[ ! -d "$MODEL_BUNDLE" ]]; then
            echo "ERROR: --model-bundle is not a directory: $MODEL_BUNDLE" >&2
            exit 1
        fi
    else
        for v in MODEL P_TO_NP; do
            [[ -z "${!v}" ]] && miss+=("--${v,,}")
        done
    fi
fi
if [[ "$STEP" == "resolve" ]] && [[ -z "$CANONICALS_OVERRIDE" ]]; then
    miss+=("--canonicals" "--pred-x1" "--pred-x2")
fi
if (( ${#miss[@]} > 0 )); then
    echo "ERROR: missing required args: ${miss[*]}" >&2; usage; exit 1
fi

case "$ARITY" in
    3in) N_INPUTS_MIN=2; N_INPUTS_MAX=3 ;;
    4in) N_INPUTS_MIN=2; N_INPUTS_MAX=4 ;;
    *)   echo "ERROR: --arity must be 3in or 4in" >&2; exit 1 ;;
esac

case "$DESIGN" in
    aes)    DESIGN_LONG="aes_cipher_top" ;;
    jpeg)   DESIGN_LONG="jpeg_encoder" ;;
    eth)    DESIGN_LONG="ethtop" ;;
    sha256) DESIGN_LONG="sha256" ;;
    keccak) DESIGN_LONG="keccak" ;;
    *) echo "ERROR: unknown design: $DESIGN (expected aes|jpeg|eth|sha256|keccak)" >&2; exit 1 ;;
esac
if [[ "$DESIGN" == "eth" ]]; then
    ARITY_SUFFIX="_${ARITY}"
elif [[ "$ARITY" == "4in" ]]; then
    ARITY_SUFFIX="_4in"
else
    ARITY_SUFFIX=""
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PHASE_INFER="${SCRIPT_DIR}/phase_infer.sh"
PHASE_RANK="${SCRIPT_DIR}/phase_rank.sh"
PHASE_RESOLVE="${SCRIPT_DIR}/phase_resolve.sh"
PHASE_SETUP="${SCRIPT_DIR}/phase_setup.sh"
PHASE_SWEEP="${SCRIPT_DIR}/phase_sweep.sh"
for p in "$PHASE_INFER" "$PHASE_RANK" "$PHASE_RESOLVE" "$PHASE_SETUP" "$PHASE_SWEEP"; do
    if [[ ! -x "$p" ]]; then
        echo "ERROR: phase script not executable: $p" >&2; exit 1
    fi
done

SCAN_PY="${SCRIPT_DIR}/scan_pass.py"

if [[ -z "$PREV_BEST_RUN" ]]; then
    if [[ -f "${PREV_DIR%/}/qor.csv" ]]; then
        PREV_BEST_RUN="$(basename "${PREV_DIR%/}")"
        PREV_DIR="$(dirname "${PREV_DIR%/}")"
        echo "[auto_iterate] --prev-dir is a single FC run; PREV_BEST_RUN=${PREV_BEST_RUN}"
    else
        if [[ ! -r "$SCAN_PY" ]]; then
            echo "ERROR: scan_pass.py not found; cannot auto-discover best PASS run." >&2; exit 1
        fi
        echo "[auto_iterate] auto-discover ${PREV_METRIC} best PASS run in ${PREV_DIR} (design=${DESIGN})"
        PREV_BEST_RUN="$(python3 "$SCAN_PY" \
            --target-dir "$PREV_DIR" \
            --design "$DESIGN" \
            --baseline-freq "$SWEEP_FREQ" \
            --strict --emit "$PREV_METRIC")"
        rc=$?
        if (( rc != 0 )) || [[ -z "$PREV_BEST_RUN" ]]; then
            echo "ERROR: scan_pass.py failed to pick ${PREV_METRIC} best PASS run for ${PREV_DIR} (rc=${rc})" >&2
            exit 1
        fi
        echo "[auto_iterate] picked PREV_BEST_RUN=${PREV_BEST_RUN}"
    fi
fi

TAG="ni${N_INPUTS_MIN}${N_INPUTS_MAX}_k1${K1}_k2${K2}"
INF_PAYLOAD_ROOT="${INF_PAYLOAD_ROOT:-${NEXT_FC_ROOT%/}/inference_payload}"
INF_CSV="${INF_PAYLOAD_ROOT}/designs/${DESIGN}/${PREV_BEST_RUN}/inference_results_x2_${TAG}.csv"
AREA_GAIN_CSV="${INF_PAYLOAD_ROOT}/designs/${DESIGN}/${PREV_BEST_RUN}/area_gain.csv"
NEW_DIR="${NEXT_FC_ROOT%/}/${DESIGN_LONG}_${NEXT_LABEL}${ARITY_SUFFIX}"

PDK_LIB_DIR="${PDK_ROOT}/lib"
VERIFY_HELPER="${SCRIPT_DIR}/find_lib_cell_by_canonical.py"
MINING_REPO_ROOT="${SCRIPT_DIR}/../../../tools/mining"
CANONICAL_TO_CDL_BIN="${SCRIPT_DIR}/../../data/canonical_to_cdl"
[[ ! -x "${CANONICAL_TO_CDL_BIN}" ]] && CANONICAL_TO_CDL_BIN="${MINING_REPO_ROOT}/src/replacement_scoring/build/canonical_to_cdl"

echo "[auto_iterate] design=${DESIGN} arity=${ARITY} next=${NEXT_LABEL} ${RANK_TAG}"
echo "[auto_iterate] new_dir=${NEW_DIR}"

if (( RUN_INFER )); then
    INFER_ARGS=(
        --design "$DESIGN"
        --prev-dir "$PREV_DIR"
        --prev-best-run "$PREV_BEST_RUN"
        --arity "$ARITY"
        --next-label "$NEXT_LABEL"
        --inf-payload-root "$INF_PAYLOAD_ROOT"
        --k1 "$K1" --k2 "$K2"
    )
    if [[ -n "$MODEL_BUNDLE" ]]; then
        INFER_ARGS+=( --model-bundle "$MODEL_BUNDLE" )
    else
        INFER_ARGS+=(
            --model "$MODEL"
            --norm-stats "$NORM_STATS"
            --features-l0 "$FEATURES_L0"
            --p-to-np "$P_TO_NP"
        )
    fi
    (( DRY_RUN )) && INFER_ARGS+=( --dry-run )
    INF_CSV_OUT="$("$PHASE_INFER" "${INFER_ARGS[@]}" | tail -1)"
    if (( ! DRY_RUN )) && [[ ! -f "$INF_CSV_OUT" ]]; then
        echo "ERROR: phase_infer returned non-existent CSV path: ${INF_CSV_OUT}" >&2; exit 1
    fi
    INF_CSV="$INF_CSV_OUT"
fi

if [[ "$STEP" == "infer" ]] || (( INFER_ONLY )); then
    echo "[auto_iterate] step=infer: stop. INF_CSV=${INF_CSV}"
    exit 0
fi

if (( RUN_RANK )); then
    if (( DRY_RUN )); then
        CANONICALS_STR="0b1110 0b1000 0b0110 0b0001 0b11111110"
        PRED_X1_STR="0.05 0.04 0.03 0.02 0.01"
        PRED_X2_STR="0.02 0.01 0.001 0.0005 0.0"
    else
        ROW_OUT="$("$PHASE_RANK" --csv "$INF_CSV" --rank "$RANK")"
        rc=$?
        if (( rc != 0 )); then
            echo "ERROR: phase_rank.sh failed (rc=${rc}) for ${INF_CSV} rank=${RANK}" >&2; exit 1
        fi
        CANONICALS_STR="$(echo "$ROW_OUT" | sed -n 1p)"
        PRED_X1_STR="$(echo "$ROW_OUT"   | sed -n 2p)"
        PRED_X2_STR="$(echo "$ROW_OUT"   | sed -n 3p)"
    fi
    echo "[auto_iterate] rank=${RANK} canonicals = ${CANONICALS_STR}"
elif [[ -n "$CANONICALS_OVERRIDE" ]]; then
    CANONICALS_STR="$CANONICALS_OVERRIDE"
    PRED_X1_STR="$PRED_X1_OVERRIDE"
    PRED_X2_STR="$PRED_X2_OVERRIDE"
    echo "[auto_iterate] ${RANK_TAG} canonicals = ${CANONICALS_STR}"
fi

if [[ "$STEP" == "rank-select" ]]; then
    echo "$CANONICALS_STR"
    echo "$PRED_X1_STR"
    echo "$PRED_X2_STR"
    exit 0
fi

if (( RUN_RESOLVE )); then
RESOLVE_ARGS=(
    --canonicals "$CANONICALS_STR"
    --pred-x1    "$PRED_X1_STR"
    --pred-x2    "$PRED_X2_STR"
    --pdk-lib-dir "$PDK_LIB_DIR"
    --verify-helper "$VERIFY_HELPER"
    --canonical-to-cdl-bin "$CANONICAL_TO_CDL_BIN"
    --mining-repo-root "$MINING_REPO_ROOT"
    --x2-ratio "$X2_RATIO"
    --pred-x1-floor "$PRED_X1_FLOOR"
    --cellgen-tr-max "$CELLGEN_TR_MAX"
    --cellgen-timeout "$CELLGEN_TIMEOUT"
    --pdk-root "$PDK_ROOT"
    --rank-tag "$RANK_TAG"
)
[[ -n "$CELLGEN_OUTPUT_DIR" ]] && RESOLVE_ARGS+=( --cellgen-output-dir "$CELLGEN_OUTPUT_DIR" )
[[ -n "$LIB2CDB_BIN" ]]        && RESOLVE_ARGS+=( --lib2cdb-bin "$LIB2CDB_BIN" )
[[ -f "$AREA_GAIN_CSV" ]]      && RESOLVE_ARGS+=( --area-gain-csv "$AREA_GAIN_CSV" )
(( DRY_RUN )) && RESOLVE_ARGS+=( --dry-run )

CELL_LIST_FILE="$(mktemp -p "${HOME}" cells.XXXXXX)"
trap 'rm -f "${CELL_LIST_FILE}"' EXIT
"$PHASE_RESOLVE" "${RESOLVE_ARGS[@]}" > "$CELL_LIST_FILE" \
    || { echo "ERROR: phase_resolve.sh failed" >&2; exit 1; }
N_CELLS=$(wc -l < "$CELL_LIST_FILE")
if (( N_CELLS < 5 )); then
    echo "ERROR: phase_resolve emitted ${N_CELLS} cells (expected >= 5)" >&2; exit 1
fi
echo "[auto_iterate] resolved ${N_CELLS} cells:"
sed 's/^/  /' "$CELL_LIST_FILE"
fi

if [[ "$STEP" == "resolve" ]]; then
    cat "$CELL_LIST_FILE"
    exit 0
fi

if (( RUN_SETUP )); then
    if [[ "$STEP" == "setup" ]]; then
        if [[ -z "$CELLS_FILE" ]]; then
            echo "ERROR: --step setup requires --cells-file <path>" >&2; exit 1
        fi
        if [[ ! -f "$CELLS_FILE" ]]; then
            echo "ERROR: --cells-file not readable: ${CELLS_FILE}" >&2; exit 1
        fi
        CELL_LIST_FILE="$CELLS_FILE"
    fi
    PREV_RUN_DIR="$PREV_DIR"
    [[ -n "$PREV_BEST_RUN" ]] && PREV_RUN_DIR="${PREV_DIR%/}/${PREV_BEST_RUN}"
    SETUP_ARGS=(
        --design "$DESIGN"
        --prev-dir "$PREV_RUN_DIR"
        --next-label "$NEXT_LABEL"
        --arity "$ARITY"
        --server "$SERVER"
        --new-dir "$NEW_DIR"
        --x2-ratio "$X2_RATIO"
    )
    (( FORCE ))   && SETUP_ARGS+=( --force )
    (( DRY_RUN )) && SETUP_ARGS+=( --dry-run )
    "$PHASE_SETUP" "${SETUP_ARGS[@]}" < "$CELL_LIST_FILE" \
        || { echo "ERROR: phase_setup.sh failed" >&2; exit 1; }
fi

if [[ "$STEP" == "setup" ]]; then
    NEW_DIR_LABEL="${NEW_DIR}"
    [[ -n "$SERVER" ]] && NEW_DIR_LABEL="${SERVER}:${NEW_DIR}"
    echo "[auto_iterate] step=setup: stop. NEW_DIR staged at ${NEW_DIR_LABEL}"
    exit 0
fi

if (( RUN_SWEEP )); then
    SWEEP_ARGS=(
        --design "$DESIGN"
        --next-label "$NEXT_LABEL"
        --arity "$ARITY"
        --server "$SERVER"
        --new-dir "$NEW_DIR"
        --sweep-areas "$SWEEP_AREAS"
        --sweep-freq "$SWEEP_FREQ"
        --reps "$REPS"
        --parallel "$PARALLEL"
    )
    (( WAIT_SWEEP )) && SWEEP_ARGS+=( --wait-sweep )
    (( DRY_RUN ))    && SWEEP_ARGS+=( --dry-run )
    "$PHASE_SWEEP" "${SWEEP_ARGS[@]}" \
        || { echo "ERROR: phase_sweep.sh failed" >&2; exit 1; }
fi

echo "[auto_iterate] complete. New sweep dir: ${SERVER:+${SERVER}:}${NEW_DIR}"
