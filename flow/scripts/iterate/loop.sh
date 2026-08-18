#!/usr/bin/env bash

set -uo pipefail

usage() {
    cat <<'USAGE'
loop.sh — D_0 -> D_M iterative driver.

Required:
    --design        <aes|jpeg|eth|sha256|keccak>
    --d0-dir        <path to D_0 sweep dir>
    --d0-best-run   <run name inside d0-dir>
    --arity         <3in|4in>
    --loop-until    <Dm label, e.g. D3>
    --fc-root       <base for fc_D1/, fc_D2/, ...>
    --sweep-areas   "a1 a2 a3 ..."
    --sweep-freq    <GHz>
    --model         <model.pt>
    --norm-stats    <norm_stats.json>
    --features-l0   <features_l0_with_x2_4in.csv>
    --p-to-np       <global_p_to_np.csv>

Optional (forwarded to run.sh):
    --server         <SP&R server; default runs setup/sweep locally, no server needed>
    --reps           --parallel  --k1  --k2
    --x2-ratio       --cellgen-server  --cellgen-output-dir
    --cellgen-tr-max --cellgen-timeout --pred-x1-floor
    --lib2cdb-bin    --inf-payload-root
    --max-rank-tries <int, default 5>
    --force          force overwrite of existing dirs
    --dry-run        print steps only
USAGE
}

DESIGN=""
D0_DIR=""
D0_BEST_RUN=""
ARITY=""
LOOP_UNTIL=""
FC_ROOT=""
SERVER=""
SWEEP_AREAS=""
SWEEP_FREQ=""
MODEL=""
NORM_STATS=""
FEATURES_L0=""
P_TO_NP=""
REPS=3
PARALLEL=6
K1=10
K2=5
X2_RATIO=0.10
CELLGEN_SERVER=""
CELLGEN_OUTPUT_DIR=""
CELLGEN_TR_MAX=20
CELLGEN_TIMEOUT=3600
PRED_X1_FLOOR=0.001
LIB2CDB_BIN=""
INF_PAYLOAD_ROOT=""
MAX_RANK_TRIES=5
FORCE=0
DRY_RUN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --design)            DESIGN="$2"; shift 2 ;;
        --d0-dir)            D0_DIR="$2"; shift 2 ;;
        --d0-best-run)       D0_BEST_RUN="$2"; shift 2 ;;
        --arity)             ARITY="$2"; shift 2 ;;
        --loop-until)        LOOP_UNTIL="$2"; shift 2 ;;
        --fc-root)           FC_ROOT="$2"; shift 2 ;;
        --server)            SERVER="$2"; shift 2 ;;
        --sweep-areas)       SWEEP_AREAS="$2"; shift 2 ;;
        --sweep-freq)        SWEEP_FREQ="$2"; shift 2 ;;
        --reps)              REPS="$2"; shift 2 ;;
        --parallel)          PARALLEL="$2"; shift 2 ;;
        --k1)                K1="$2"; shift 2 ;;
        --k2)                K2="$2"; shift 2 ;;
        --model)             MODEL="$2"; shift 2 ;;
        --norm-stats)        NORM_STATS="$2"; shift 2 ;;
        --features-l0)       FEATURES_L0="$2"; shift 2 ;;
        --p-to-np)           P_TO_NP="$2"; shift 2 ;;
        --x2-ratio)          X2_RATIO="$2"; shift 2 ;;
        --cellgen-server)    CELLGEN_SERVER="$2"; shift 2 ;;
        --cellgen-output-dir) CELLGEN_OUTPUT_DIR="$2"; shift 2 ;;
        --cellgen-tr-max)    CELLGEN_TR_MAX="$2"; shift 2 ;;
        --cellgen-timeout)   CELLGEN_TIMEOUT="$2"; shift 2 ;;
        --pred-x1-floor)     PRED_X1_FLOOR="$2"; shift 2 ;;
        --lib2cdb-bin)       LIB2CDB_BIN="$2"; shift 2 ;;
        --inf-payload-root)  INF_PAYLOAD_ROOT="$2"; shift 2 ;;
        --max-rank-tries)    MAX_RANK_TRIES="$2"; shift 2 ;;
        --force)             FORCE=1; shift ;;
        --dry-run)           DRY_RUN=1; shift ;;
        -h|--help)           usage; exit 0 ;;
        *) echo "ERROR: unknown arg: $1" >&2; usage; exit 1 ;;
    esac
done

miss=()
for v in DESIGN D0_DIR D0_BEST_RUN ARITY LOOP_UNTIL FC_ROOT \
         SWEEP_AREAS SWEEP_FREQ MODEL P_TO_NP; do
    [[ -z "${!v}" ]] && miss+=("--${v,,}")
done
if (( ${#miss[@]} > 0 )); then
    echo "ERROR: missing required args: ${miss[*]}" >&2
    usage; exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SINGLE_STEP="${SCRIPT_DIR}/run.sh"
if [[ ! -x "$SINGLE_STEP" ]]; then
    echo "ERROR: cannot find $SINGLE_STEP" >&2
    exit 1
fi

TARGET_N=${LOOP_UNTIL#D}
if ! [[ "$TARGET_N" =~ ^[0-9]+$ ]]; then
    echo "ERROR: --loop-until must be of form D<n> (got ${LOOP_UNTIL})" >&2
    exit 1
fi

PREV_DIR="$D0_DIR"
PREV_BEST_RUN="$D0_BEST_RUN"

PHASE_RANK="${SCRIPT_DIR}/phase_rank.sh"
if [[ ! -x "$PHASE_RANK" ]]; then
    echo "ERROR: phase_rank.sh not executable: $PHASE_RANK" >&2; exit 1
fi

COMMON_ARGS=(
    --design "$DESIGN"
    --arity "$ARITY"
    --next-fc-root "$FC_ROOT"
    --server "$SERVER"
    --reps "$REPS"
    --parallel "$PARALLEL"
    --k1 "$K1" --k2 "$K2"
    --x2-ratio "$X2_RATIO"
    --cellgen-tr-max "$CELLGEN_TR_MAX"
    --cellgen-timeout "$CELLGEN_TIMEOUT"
    --pred-x1-floor "$PRED_X1_FLOOR"
    --sweep-areas "$SWEEP_AREAS"
    --sweep-freq "$SWEEP_FREQ"
)
[[ -n "$CELLGEN_SERVER" ]]     && COMMON_ARGS+=( --cellgen-server "$CELLGEN_SERVER" )
[[ -n "$CELLGEN_OUTPUT_DIR" ]] && COMMON_ARGS+=( --cellgen-output-dir "$CELLGEN_OUTPUT_DIR" )
[[ -n "$LIB2CDB_BIN" ]]        && COMMON_ARGS+=( --lib2cdb-bin "$LIB2CDB_BIN" )
[[ -n "$INF_PAYLOAD_ROOT" ]]   && COMMON_ARGS+=( --inf-payload-root "$INF_PAYLOAD_ROOT" )
(( DRY_RUN )) && COMMON_ARGS+=( --dry-run )

INFER_ONLY_EXTRA=(
    --model "$MODEL"
    --norm-stats "$NORM_STATS"
    --features-l0 "$FEATURES_L0"
    --p-to-np "$P_TO_NP"
)

case "$ARITY" in
    3in) ARITY_MIN=2; ARITY_MAX=3 ;;
    4in) ARITY_MIN=2; ARITY_MAX=4 ;;
    *)   echo "ERROR: --arity must be 3in or 4in" >&2; exit 1 ;;
esac
CSV_TAG="ni${ARITY_MIN}${ARITY_MAX}_k1${K1}_k2${K2}"

case "$DESIGN" in
    aes)    DESIGN_LONG="aes_cipher_top" ;;
    jpeg)   DESIGN_LONG="jpeg_encoder" ;;
    eth)    DESIGN_LONG="ethtop" ;;
    sha256) DESIGN_LONG="sha256" ;;
    keccak) DESIGN_LONG="keccak" ;;
    *) echo "ERROR: unknown design: $DESIGN" >&2; exit 1 ;;
esac
if [[ "$DESIGN" == "eth" ]]; then
    ARITY_SUFFIX="_${ARITY}"
elif [[ "$ARITY" == "4in" ]]; then
    ARITY_SUFFIX="_4in"
else
    ARITY_SUFFIX=""
fi

for m in $(seq 0 $((TARGET_N - 1))); do
    NEXT_LABEL="D$((m + 1))"
    echo "=========================================================================="
    echo "[loop] iter ${m}: D_${m} -> ${NEXT_LABEL}, prev=${PREV_DIR}/${PREV_BEST_RUN}"
    echo "=========================================================================="

    echo "[loop] iter ${m}: phase A — inference (one-shot, --infer-only)"
    if ! "$SINGLE_STEP" \
            "${COMMON_ARGS[@]}" \
            "${INFER_ONLY_EXTRA[@]}" \
            --prev-dir "$PREV_DIR" \
            --prev-best-run "$PREV_BEST_RUN" \
            --next-label "$NEXT_LABEL" \
            --infer-only; then
        echo "ERROR: iter ${m}: --infer-only phase failed" >&2
        exit 1
    fi

    INF_PAYLOAD_ROOT_EFF="${INF_PAYLOAD_ROOT:-${FC_ROOT%/}/inference_payload}"
    INF_CSV="${INF_PAYLOAD_ROOT_EFF}/designs/${DESIGN}/${PREV_BEST_RUN}/inference_results_x2_${CSV_TAG}.csv"
    if (( ! DRY_RUN )) && [[ ! -f "$INF_CSV" ]]; then
        echo "ERROR: iter ${m}: INF_CSV not found after --infer-only: ${INF_CSV}" >&2
        exit 1
    fi

    success=0
    for rank in $(seq 1 ${MAX_RANK_TRIES}); do
        echo "[loop] iter ${m}: phase B — attempt rank=${rank}"

        if (( DRY_RUN )); then
            CANONICALS_STR="0b1110 0b1000 0b0110 0b0001 0b11111110"
            PRED_X1_STR="0.05 0.04 0.03 0.02 0.01"
            PRED_X2_STR="0.02 0.01 0.001 0.0005 0.0"
        else
            ROW_OUT="$("$PHASE_RANK" --csv "$INF_CSV" --rank "$rank")"
            rc=$?
            if (( rc != 0 )); then
                echo "[loop] iter ${m}: rank=${rank} row extraction failed (rc=${rc}); stopping rank loop" >&2
                break
            fi
            CANONICALS_STR="$(echo "$ROW_OUT" | sed -n 1p)"
            PRED_X1_STR="$(echo "$ROW_OUT"   | sed -n 2p)"
            PRED_X2_STR="$(echo "$ROW_OUT"   | sed -n 3p)"
            if [[ -z "$CANONICALS_STR" || -z "$PRED_X1_STR" || -z "$PRED_X2_STR" ]]; then
                echo "ERROR: iter ${m} rank=${rank}: phase_rank returned blank line(s)" >&2
                exit 1
            fi
        fi
        echo "[loop] iter ${m}: rank=${rank} canonicals = ${CANONICALS_STR}"

        ATTEMPT_ARGS=(
            "${COMMON_ARGS[@]}"
            --prev-dir "$PREV_DIR"
            --prev-best-run "$PREV_BEST_RUN"
            --next-label "$NEXT_LABEL"
            --canonicals "$CANONICALS_STR"
            --pred-x1    "$PRED_X1_STR"
            --pred-x2    "$PRED_X2_STR"
            --rank-tag   "rank${rank}"
            --wait-sweep
        )
        if (( rank > 1 )) || (( FORCE )); then
            ATTEMPT_ARGS+=( --force )
        fi

        if "$SINGLE_STEP" "${ATTEMPT_ARGS[@]}"; then
            success=1
            break
        fi
        echo "[loop] iter ${m}: rank=${rank} failed; bumping rank" >&2
    done

    if (( ! success )); then
        echo "ERROR: iter ${m} failed across ${MAX_RANK_TRIES} ranks. Aborting loop." >&2
        exit 1
    fi

    NEW_DIR="${FC_ROOT%/}/${DESIGN_LONG}_${NEXT_LABEL}${ARITY_SUFFIX}"
    SCAN_PY="${SCRIPT_DIR}/scan_pass.py"
    if [[ ! -r "$SCAN_PY" ]]; then
        echo "ERROR: scan_pass.py not found at ${SCAN_PY}" >&2
        exit 1
    fi
    if (( DRY_RUN )); then
        # A dry run never launches the sweep, so there is no result for
        # scan_pass.py to read. Stand in the first swept area at the swept
        # frequency so the remaining iterations still exercise their wiring,
        # which is the whole point of dry-running the loop.
        best_run="$(awk -v f="$SWEEP_FREQ" '{printf "%s_%s_1", $1, f}' <<< "$SWEEP_AREAS")"
        echo "[DRY-RUN MOCK — NOT A REAL PASS RESULT] [loop] iter ${m}: standing in ${NEXT_LABEL} best run = ${best_run}"
    else
        best_run="$(python3 "$SCAN_PY" \
            --target-dir "$NEW_DIR" \
            --design "$DESIGN" \
            --strict \
            --emit min-area)"
        scan_rc=$?
        if (( scan_rc != 0 )) || [[ -z "$best_run" ]]; then
            echo "ERROR: scan_pass.py failed (rc=${scan_rc}) for ${NEW_DIR}; cannot proceed to next iteration." >&2
            exit 1
        fi
    fi
    PREV_DIR="$NEW_DIR"
    PREV_BEST_RUN="$best_run"
    echo "[loop] iter ${m}: ${NEXT_LABEL} best PASS run = ${best_run}"
done

echo "[loop] complete — reached ${LOOP_UNTIL}"
