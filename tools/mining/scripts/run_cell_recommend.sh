#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
# Generated artifacts only. flow/runs is run output and is not committed, so
# nothing the script needs as an input may default to a path under it.
FLOW_RUNS="${GAPMA_FLOW_RUNS:-$REPO_ROOT/flow/runs}"

if [[ -z "${PDK_ROOT:-}" ]]; then
  echo "ERROR: PDK_ROOT is not set. Source the repo-root .env.local or export PDK_ROOT." >&2
  exit 1
fi

SRC_RUN=""
DESIGN=""
DEST_ROOT="$FLOW_RUNS/l0_inference_payload"
MODEL="$REPO_ROOT/data/GATv2/model_pretrained.pt"
FEATURES_L0="$REPO_ROOT/tools/mining/scripts/demand_predictor/features_l0.csv"
PKD_LIB_DIR="$PDK_ROOT/lib"
P_TO_NP="$REPO_ROOT/data/mining/global_p_to_np.csv"
N_INPUTS_MIN="2"
N_INPUTS_MAX="3"
K1="10"
K2="5"
FORCE=0

usage() {
    cat <<'EOF'
Usage: run_cell_recommend.sh [options]

Options:
  --src-run <value>
  --design <value>
  --dest-root <value>
  --model <value>
  --features-l0 <value>
  --pdk-lib-dir <value>
  --p-to-np <value>
  --max-inputs <value>
  --k1 <value>
  --k2 <value>
  --force
  --help|-h
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --src-run)    SRC_RUN="$2";       shift 2 ;;
        --design)     DESIGN="$2";        shift 2 ;;
        --dest-root)  DEST_ROOT="$2";     shift 2 ;;
        --model)      MODEL="$2";         shift 2 ;;
        --features-l0) FEATURES_L0="$2"; shift 2 ;;
        --pdk-lib-dir) PKD_LIB_DIR="$2"; shift 2 ;;
        --p-to-np)    P_TO_NP="$2";     shift 2 ;;
        --max-inputs) N_INPUTS_MAX="$2"; shift 2 ;;
        --k1)         K1="$2";            shift 2 ;;
        --k2)         K2="$2";            shift 2 ;;
        --force)      FORCE=1;            shift ;;
        --help|-h) usage; exit 0 ;;
        *) echo "ERROR: unknown argument: $1" >&2; exit 1 ;;
    esac
done

[[ -z "$SRC_RUN" ]] && { echo "ERROR: --src-run required" >&2; exit 1; }
[[ -z "$DESIGN"  ]] && { echo "ERROR: --design required"  >&2; exit 1; }
[[ ! -d "$SRC_RUN" ]] && { echo "ERROR: --src-run does not exist: $SRC_RUN" >&2; exit 1; }
[[ ! -f "$MODEL"   ]] && { echo "ERROR: --model not found: $MODEL" >&2; exit 1; }
[[ -n "$FEATURES_L0" && ! -f "$FEATURES_L0" ]] && { echo "ERROR: --features-l0 not found: $FEATURES_L0" >&2; exit 1; }

SWEEP_ID="$(basename "$SRC_RUN")"
DEST_DIR="$DEST_ROOT/designs/$DESIGN/$SWEEP_ID"
TAG="ni${N_INPUTS_MIN}${N_INPUTS_MAX}_k1${K1}_k2${K2}"

if [[ -z "$FEATURES_L0" ]]; then
    mkdir -p "$DEST_DIR"
    FEATURES_L0="$DEST_DIR/features_l0_from_ckpt.csv"
    if ! python3 - "$MODEL" "$FEATURES_L0" <<'PY'
import sys, torch
ck = torch.load(sys.argv[1], map_location="cpu", weights_only=False)
csv = ck.get("features_l0_csv") if isinstance(ck, dict) else None
if not csv:
    sys.exit(1)
open(sys.argv[2], "w").write(csv)
PY
    then
        echo "ERROR: --features-l0 not given and checkpoint $MODEL embeds no vocab. Supply --features-l0 or retrain with the current code." >&2
        exit 1
    fi
    echo "[pipeline] features_l0 resolved from checkpoint embed -> $FEATURES_L0"
fi
OUT_CSV="$DEST_DIR/inference_results_x2_${TAG}.csv"

mkdir -p "$DEST_DIR"

echo "========================================"
echo "  run_cell_recommend.sh"
echo "  src-run    : $SRC_RUN"
echo "  design     : $DESIGN"
echo "  dest-dir   : $DEST_DIR"
echo "  n-inputs   : $N_INPUTS_MIN $N_INPUTS_MAX"
echo "  k1         : $K1  k2 : $K2"
echo "  out        : $OUT_CSV"
echo "========================================"

_skip() { [[ $FORCE -eq 0 && -f "$1" ]]; }

VALIDATE_PY="$SCRIPT_DIR/demand_predictor/validate_cell_lib_names.py"
CELL_NAME_MAPPING="$DEST_DIR/cell_name_mapping.csv"

echo "[pipeline] step0: validating features_l0 cell names vs SO3 lib..."
${PYTHON3:-python3} "$VALIDATE_PY" \
    --features-l0 "$FEATURES_L0" \
    --lib-dir     "$PKD_LIB_DIR" \
    --out         "$CELL_NAME_MAPPING"

if _skip "$DEST_DIR/filters/only_use_lib_cells.list"; then
    echo "[pipeline] step1: SKIP (filters/only_use_lib_cells.list exists)"
else
    echo "[pipeline] step1: convert FC run dir..."
    bash "$SCRIPT_DIR/step1_convert.sh" \
        --src-run    "$SRC_RUN" \
        --design     "$DESIGN" \
        --dest-root  "$DEST_ROOT"
fi

if _skip "$DEST_DIR/pattern_instances.out"; then
    echo "[pipeline] step2: SKIP (pattern_instances.out exists)"
else
    echo "[pipeline] step2: mining..."
    bash "$SCRIPT_DIR/step2_mine.sh" \
        --src-run  "$SRC_RUN" \
        --dest-dir "$DEST_DIR"
fi

if _skip "$DEST_DIR/mining_node_rank.csv"; then
    echo "[pipeline] step3: SKIP (mining_node_rank.csv exists)"
else
    echo "[pipeline] step3: build mining edge features..."
    bash "$SCRIPT_DIR/step3_build_mining.sh" \
        --dest-dir "$DEST_DIR"
fi

if _skip "$DEST_DIR/step2b.done"; then
    echo "[pipeline] step2b: SKIP (step2b.done exists)"
else
    echo "[pipeline] step2b: augmenting missing X2 features..."
    bash "$SCRIPT_DIR/step2b_augment_x2.sh" \
        --dest-dir    "$DEST_DIR" \
        --features-l0 "$FEATURES_L0" \
        --top-k       "$K1"
    touch "$DEST_DIR/step2b.done"
fi
[[ -f "$DEST_DIR/features_augmented.csv" ]] && FEATURES_L0="$DEST_DIR/features_augmented.csv"

if _skip "$DEST_DIR/area_gain.csv"; then
    echo "[pipeline] step4: SKIP (area_gain.csv exists)"
else
    echo "[pipeline] step4: compute area gain..."
    bash "$SCRIPT_DIR/step4_area_gain.sh" \
        --dest-dir    "$DEST_DIR" \
        --features-l0 "$FEATURES_L0"
fi

if _skip "$OUT_CSV"; then
    echo "[pipeline] step5: SKIP (output CSV exists)"
else
    echo "[pipeline] step5: GAT inference..."
    bash "$SCRIPT_DIR/run_infer.sh" \
        --payload-dir "$DEST_DIR" \
        --model-ckpt  "$MODEL" \
        --features-l0 "$FEATURES_L0" \
        --p-to-np     "$P_TO_NP" \
        --n-inputs    "$N_INPUTS_MIN" "$N_INPUTS_MAX" \
        --n-combo     "$K2" \
        --top-k       "$K1" \
        --out         "$OUT_CSV"
fi

echo ""
echo "========================================"
echo "  TOP-1 RESULT (area-gain sort)"
echo "========================================"
${PYTHON3:-python3} - <<PYEOF
import csv, os

mapping = {}
mapping_path = '$CELL_NAME_MAPPING'
if os.path.exists(mapping_path):
    for row in csv.DictReader(open(mapping_path)):
        mapping[row['features_name']] = (row['lib_name'], row['status'])

def resolve(name):
    if not name:
        return name
    lib_name, status = mapping.get(name, (name, 'exact'))
    if status == 'cell_prefix':
        return f"{lib_name} (was {name})"
    if status == 'missing':
        return f"MISSING:{name}"
    return name

rows = list(csv.DictReader(open('$OUT_CSV')))
r = rows[0]
cells = [r[f'cell_{i}'] for i in range(1, 6) if r.get(f'cell_{i}')]
x2s   = [r.get(f'cell_{i}_x2', '') for i in range(1, 6)]
x2s   = [x for x in x2s if x]
lib_cells = [resolve(c) for c in cells]
lib_x2s   = [resolve(x) for x in x2s if x]
print(f"  frac_x_gain : {float(r['pred_frac_x_gain']):.4f}")
print(f"  frac_sum    : {float(r['pred_added_frac_sum']):.4f}")
print(f"  X1 cells    : {', '.join(cells)}")
print(f"  X1 lib names: {', '.join(lib_cells)}")
print(f"  X2 cells    : {', '.join(x2s) if x2s else '(none)'}")
if lib_x2s:
    print(f"  X2 lib names: {', '.join(lib_x2s)}")
print(f"  result CSV  : $OUT_CSV")
if mapping:
    missing = [n for n in cells + x2s if n and mapping.get(n, ('', ''))[1] == 'missing']
    if missing:
        print(f"  WARNING: {len(missing)} recommended cell(s) NOT in SO3 lib: {missing}")
PYEOF
