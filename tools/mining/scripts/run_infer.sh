#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
INFER_PY="$SCRIPT_DIR/demand_predictor/infer_demand.py"
PYTHON3="${PYTHON3:-/usr/bin/python3}"

PAYLOAD_DIR=""
MODEL_CKPT=""
NORM_STATS=""
FEATURES_L0="$REPO_ROOT/tools/mining/scripts/demand_predictor/features_l0.csv"
P_TO_NP="$REPO_ROOT/data/mining/global_p_to_np.csv"
N_INPUTS_MIN="2"
N_INPUTS_MAX="3"
TOP_K="10"
N_COMBO="3"
OUT=""

usage() {
    cat <<'EOF'
Usage: run_infer.sh [options]

Options:
  --payload-dir <value>
  --model-ckpt <value>
  --norm-stats <value>
  --features-l0 <value>
  --p-to-np <value>
  --n-inputs <value> <value>
  --top-k <value>
  --n-combo <value>
  --out <value>
  --help|-h
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --payload-dir)  PAYLOAD_DIR="$2";  shift 2 ;;
        --model-ckpt)   MODEL_CKPT="$2";   shift 2 ;;
        --norm-stats)   NORM_STATS="$2";   shift 2 ;;
        --features-l0)  FEATURES_L0="$2";  shift 2 ;;
        --p-to-np)      P_TO_NP="$2";      shift 2 ;;
        --n-inputs)     N_INPUTS_MIN="$2"; N_INPUTS_MAX="$3"; shift 3 ;;
        --top-k)        TOP_K="$2";        shift 2 ;;
        --n-combo)      N_COMBO="$2";      shift 2 ;;
        --out)          OUT="$2";          shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) echo "ERROR: unknown argument: $1" >&2; exit 1 ;;
    esac
done

[[ -z "$PAYLOAD_DIR" ]] && { echo "ERROR: --payload-dir required" >&2; exit 1; }
[[ -z "$MODEL_CKPT"  ]] && { echo "ERROR: --model-ckpt required"  >&2; exit 1; }
[[ ! -d "$PAYLOAD_DIR" ]] && { echo "ERROR: --payload-dir does not exist: $PAYLOAD_DIR" >&2; exit 1; }
[[ ! -f "$MODEL_CKPT"  ]] && { echo "ERROR: --model-ckpt does not exist: $MODEL_CKPT"  >&2; exit 1; }

if [[ -z "$OUT" ]]; then
    OUT="$PAYLOAD_DIR/inference_results_ni${N_INPUTS_MIN}${N_INPUTS_MAX}_top${TOP_K}_c${N_COMBO}.csv"
fi

echo "===== run_infer.sh ====="
echo "  payload-dir : $PAYLOAD_DIR"
echo "  model-ckpt  : $MODEL_CKPT"
echo "  features-l0 : $FEATURES_L0"
echo "  p-to-np     : $P_TO_NP"
echo "  n-inputs    : $N_INPUTS_MIN $N_INPUTS_MAX"
echo "  top-k       : $TOP_K"
echo "  n-combo     : $N_COMBO"
echo "  out         : $OUT"
echo "========================"

NORM_ARGS=()
if [[ -n "$NORM_STATS" ]]; then
    NORM_ARGS=(--norm-stats "$NORM_STATS")
fi

AREA_GAIN_ARGS=()
if [[ -f "$PAYLOAD_DIR/area_gain.csv" ]]; then
    AREA_GAIN_ARGS=(--area-gain "$PAYLOAD_DIR/area_gain.csv")
    echo "  area-gain   : $PAYLOAD_DIR/area_gain.csv (frac_x_gain sort enabled)"
else
    echo "  area-gain   : (not found, using pred_area_weighted sort)"
fi

"$PYTHON3" "$INFER_PY" \
    --payload-dir  "$PAYLOAD_DIR" \
    --model-ckpt   "$MODEL_CKPT" \
    "${NORM_ARGS[@]}" \
    --l0           "$FEATURES_L0" \
    --p-to-np      "$P_TO_NP" \
    --n-inputs     "$N_INPUTS_MIN" "$N_INPUTS_MAX" \
    --top-k        "$TOP_K" \
    --n-combo      "$N_COMBO" \
    "${AREA_GAIN_ARGS[@]}" \
    --out          "$OUT"

echo ""
echo "===== run_infer.sh complete ====="
echo "Results: $OUT"
