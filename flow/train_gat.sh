#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

REPO="${GAPMA_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
[[ -f "$REPO/.env.local" ]] && source "$REPO/.env.local"
RUN_ROOTS=(
    $REPO/data/training/blake2s_core
    $REPO/data/training/gcd
    $REPO/data/training/ibex_core
    $REPO/data/training/picorv32
    $REPO/data/training/poly1305_core
)
EXCLUDE_RUNS=""
DATA_MODE=lazy
CACHE=$REPO/flow/runs/gat_training_all/graph_cache_datagen.pt
VAL_DESIGN=ibex_core
EPOCHS=10
TEST_EPOCHS=5
BATCH_SIZE=64
LR=1e-4
WEIGHT_DECAY=1e-4
WARMUP_EPOCHS=5
NUM_WORKERS=8
PREWARM_WORKERS=48
PYTHON_BIN=python3.9
GPU=${CUDA_VISIBLE_DEVICES:-0}
TRAIN_HOST=""
SSH_JUMP=""
MODEL_DIR=$REPO/data/GATv2
BUNDLE_DIR=$REPO/data/GATv2
LOG_DIR=$REPO/flow/runs/gat_training_all

CONFIG=""
LOCAL=1
TEST=0
INIT_FROM=""
EPOCHS_OVERRIDE=""

usage() {
    cat <<'EOF'
Usage: train_gat.sh [options]

Options:
  --config <value>
  --local
  --host <value>
  --ssh-jump <value>
  --test
  --init-from <value>
  --epochs <value>
  --data-mode <value>
  --help|-h
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)     CONFIG="$2";               shift 2 ;;
        --local)      LOCAL=1;                   shift ;;
        --host)       TRAIN_HOST="$2"; LOCAL=0;  shift 2 ;;
        --ssh-jump)   SSH_JUMP="$2";              shift 2 ;;
        --test)       TEST=1;                    shift ;;
        --init-from)  INIT_FROM="$2";            shift 2 ;;
        --epochs)     EPOCHS_OVERRIDE="$2";      shift 2 ;;
        --data-mode)  DATA_MODE="$2";            shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *)            echo "[train_gat] unknown arg: $1" >&2; exit 1 ;;
    esac
done

if [[ -n "$CONFIG" ]]; then
    [[ -f "$CONFIG" ]] || { echo "[train_gat] config not found: $CONFIG" >&2; exit 1; }
    source "$CONFIG"
fi
case "$DATA_MODE" in cache|lazy) ;; *)
    echo "[train_gat] invalid --data-mode: $DATA_MODE (cache|lazy)" >&2; exit 1 ;;
esac
WORK_DIR=$REPO/tools/mining/scripts/demand_predictor
RELATION=${RELATION:-$WORK_DIR/relation_clean.csv}
FEATURES_L0=${FEATURES_L0:-$WORK_DIR/features_l0.csv}
PYTHON=$PYTHON_BIN

[[ $TEST -eq 1 ]] && EPOCHS=$TEST_EPOCHS
[[ -n "$EPOCHS_OVERRIDE" ]] && EPOCHS=$EPOCHS_OVERRIDE

PREFLIGHT_OK=1
for f in "$RELATION" "$FEATURES_L0" "$WORK_DIR/train/train_diff.py"; do
    [ -f "$f" ] || { echo "[train_gat] MISSING: $f"; PREFLIGHT_OK=0; }
done
for d in "${RUN_ROOTS[@]}"; do
    [ -d "$d" ] || { echo "[train_gat] MISSING dir: $d"; PREFLIGHT_OK=0; }
done
if [[ -n "$INIT_FROM" ]]; then
    [ -f "$INIT_FROM" ] || { echo "[train_gat] MISSING checkpoint: $INIT_FROM"; PREFLIGHT_OK=0; }
fi
[[ $PREFLIGHT_OK -eq 0 ]] && { echo "[train_gat] Pre-flight FAILED — aborting"; exit 1; }

mkdir -p "$MODEL_DIR" "$LOG_DIR"

TS=$(date +%Y%m%d_%H%M%S)
CONT_SUFFIX=$( [[ -n "$INIT_FROM" ]] && echo _cont || echo '' )
TEST_SUFFIX=$( [[ $TEST -eq 1 ]] && echo _test || echo '' )
SUFFIX="${TS}${TEST_SUFFIX}${CONT_SUFFIX}"
OUT=$MODEL_DIR/model_${SUFFIX}.pt
LOG=$LOG_DIR/train_${SUFFIX}.log
CSV=$LOG_DIR/train_${SUFFIX}.csv
PID_FILE=$LOG_DIR/train_${SUFFIX}.pid

INIT_FROM_ARG=""
[[ -n "$INIT_FROM" ]] && INIT_FROM_ARG="--init-from $INIT_FROM --warmup-epochs 0"

CACHE_ARG=""
[[ "$DATA_MODE" == "cache" ]] && CACHE_ARG="--cache-graphs $CACHE"

PREWARM_CMD="cd $WORK_DIR && CUDA_VISIBLE_DEVICES= $PYTHON train/train_diff.py \
  --run-root ${RUN_ROOTS[*]} \
  ${EXCLUDE_RUNS:+--exclude-runs-file $EXCLUDE_RUNS} \
  --relation $RELATION \
  --l0 $FEATURES_L0 \
  --prewarm-only \
  --data-mode cache \
  --cache-graphs $CACHE \
  --prewarm-workers $PREWARM_WORKERS \
  --require-cell-counts"

PUBLISH_SUFFIX=""
if [[ $TEST -eq 0 ]]; then
    PUBLISH_SUFFIX=" && mkdir -p $BUNDLE_DIR \
 && cp -f $OUT $BUNDLE_DIR/model_current.pt.tmp.\$\$ \
 && mv -f $BUNDLE_DIR/model_current.pt.tmp.\$\$ $BUNDLE_DIR/model_current.pt \
 && echo train_gat: published $BUNDLE_DIR/model_current.pt from $OUT"
fi

CMD="cd $WORK_DIR && CUDA_VISIBLE_DEVICES=$GPU nohup bash -c \"$PYTHON train/train_diff.py \
  --run-root ${RUN_ROOTS[*]} \
  ${EXCLUDE_RUNS:+--exclude-runs-file $EXCLUDE_RUNS} \
  --relation $RELATION \
  --l0 $FEATURES_L0 \
  --out $OUT \
  --val-design $VAL_DESIGN \
  --epochs $EPOCHS \
  --batch-size $BATCH_SIZE \
  --lr $LR \
  --weight-decay $WEIGHT_DECAY \
  --warmup-epochs $WARMUP_EPOCHS \
  --num-workers $NUM_WORKERS \
  --data-mode $DATA_MODE \
  $CACHE_ARG \
  --require-cell-counts \
  $INIT_FROM_ARG$PUBLISH_SUFFIX\" \
  > $LOG 2>&1 & echo \$! > $PID_FILE && echo '[train_gat] PID:' \$(cat $PID_FILE)"

MODE_STR="$( [[ $TEST -eq 1 ]] && echo 'TEST' || echo 'FULL' ) ($EPOCHS epochs)"
[[ -n "$INIT_FROM" ]] && MODE_STR="$MODE_STR, warm-start from $(basename $INIT_FROM)"

echo "[train_gat] Timestamp : $TS"
echo "[train_gat] Mode      : $MODE_STR"
echo "[train_gat] Data mode : $DATA_MODE"
echo "[train_gat] Model out : $OUT"
echo "[train_gat] Log       : $LOG"
[[ -n "$INIT_FROM" ]] && echo "[train_gat] Init from : $INIT_FROM"
[[ $TEST -eq 0 ]] && echo "[train_gat] On success: publishes $BUNDLE_DIR/model_current.pt"

if [[ "$DATA_MODE" == "lazy" ]]; then
    echo "[train_gat] data-mode=lazy — skipping prewarm/cache entirely."
elif [[ ! -f "$CACHE" ]]; then
    echo "[train_gat] Cache not found — building with CUDA_VISIBLE_DEVICES='' (fork-safe, ~10 min) ..."
    if [[ $LOCAL -eq 1 ]]; then
        eval "$PREWARM_CMD"
    else
        ssh ${SSH_JUMP:+-J "$SSH_JUMP"} "$TRAIN_HOST" "bash -lc '$PREWARM_CMD'"
    fi
    echo "[train_gat] Cache ready: $CACHE"
else
    echo "[train_gat] Cache already exists: $CACHE"
fi

if [[ $LOCAL -eq 1 ]]; then
    echo "[train_gat] Running locally..."
    eval "$CMD"
else
    echo "[train_gat] Launching on $TRAIN_HOST via $SSH_JUMP..."
    ssh ${SSH_JUMP:+-J "$SSH_JUMP"} "$TRAIN_HOST" "bash -lc '$CMD'"
fi
