#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DATAGEN_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)/flow/scripts/datagen"
EXTRACT_SH="$SCRIPT_DIR/extract.sh"
SO3_CELLS="$DATAGEN_DIR/cells.csv"

DEST_DIR=""
FEATURES_L0=""
TOP_K="10"

usage() {
    cat <<'EOF'
Usage: step2b_augment_x2.sh [options]

Options:
  --dest-dir <value>
  --features-l0 <value>
  --top-k <value>
  --help|-h
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dest-dir)    DEST_DIR="$2";    shift 2 ;;
        --features-l0) FEATURES_L0="$2"; shift 2 ;;
        --top-k)       TOP_K="$2";       shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) echo "ERROR: unknown argument: $1" >&2; exit 1 ;;
    esac
done

[[ -z "$DEST_DIR"    ]] && { echo "ERROR: --dest-dir required"    >&2; exit 1; }
[[ -z "$FEATURES_L0" ]] && { echo "ERROR: --features-l0 required" >&2; exit 1; }
[[ ! -f "$FEATURES_L0" ]] && { echo "ERROR: --features-l0 not found: $FEATURES_L0" >&2; exit 1; }
[[ ! -f "$EXTRACT_SH"  ]] && { echo "ERROR: extract.sh not found: $EXTRACT_SH"     >&2; exit 1; }
[[ ! -f "$SO3_CELLS"   ]] && { echo "ERROR: cells.csv not found: $SO3_CELLS"   >&2; exit 1; }

MINING_RANK="$DEST_DIR/mining_node_rank.csv"
[[ ! -f "$MINING_RANK" ]] && { echo "ERROR: mining_node_rank.csv not found in $DEST_DIR" >&2; exit 1; }

CELLS_FOR_EXTRACT="$DEST_DIR/x2_cells_to_extract.csv"
NEW_FEATURES="$DEST_DIR/x2_new_features.csv"
AUGMENTED="$DEST_DIR/features_augmented.csv"

rm -f "$CELLS_FOR_EXTRACT" "$NEW_FEATURES" "$AUGMENTED"

${PYTHON3:-python3} "$SCRIPT_DIR/step2b_find_missing_x2.py" \
    --mining-rank   "$MINING_RANK" \
    --features-l0   "$FEATURES_L0" \
    --so3-cells     "$SO3_CELLS" \
    --top-k         "$TOP_K" \
    --out-cells-csv "$CELLS_FOR_EXTRACT"

if [[ ! -f "$CELLS_FOR_EXTRACT" ]]; then
    echo "[step2b] Nothing to augment."
    exit 0
fi

echo "[step2b] Running extract.sh for missing cells..."
bash "$EXTRACT_SH" \
    --cells-csv      "$CELLS_FOR_EXTRACT" \
    --pdk            SO3 \
    --drive-strengths X1,X2 \
    --out-dir        "$DEST_DIR" \
    --out-file       x2_new_features.csv

[[ ! -f "$NEW_FEATURES" ]] && {
    echo "ERROR: extract.sh produced no output at $NEW_FEATURES" >&2
    exit 1
}

echo "[step2b] Writing features_augmented.csv..."
${PYTHON3:-python3} - <<PYEOF
import csv, sys

features_l0_path = '$FEATURES_L0'
new_features_path = '$NEW_FEATURES'
augmented_path   = '$AUGMENTED'

with open(features_l0_path, newline='') as f:
    base_rows = list(csv.DictReader(f))
if not base_rows:
    raise ValueError(f"features_l0 is empty: {features_l0_path}")
fieldnames = list(base_rows[0].keys())
existing_names = {r['cell_name'] for r in base_rows}

with open(new_features_path, newline='') as f:
    raw_rows = list(csv.DictReader(f))

new_rows = [r for r in raw_rows if r['cell_name'] not in existing_names]

if not new_rows:
    raise ValueError(
        f"extract.sh output contained no new cells "
        f"(all already in features_l0). Output: {new_features_path}"
    )

new_cols  = set(new_rows[0].keys())
base_cols = set(fieldnames)
extra   = new_cols - base_cols
missing = base_cols - new_cols
if extra or missing:
    raise ValueError(
        f"Column mismatch — new features vs features_l0: "
        f"extra={sorted(extra)}, missing={sorted(missing)}"
    )

with open(augmented_path, 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=fieldnames)
    w.writeheader()
    w.writerows(base_rows)
    w.writerows(new_rows)

added = [r['cell_name'] for r in new_rows]
print(f"[step2b] features_augmented.csv: "
      f"{len(base_rows)} base + {len(new_rows)} new ({added}) -> {augmented_path}")
PYEOF
