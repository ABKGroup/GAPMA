#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
AREA_GAIN_PY="$SCRIPT_DIR/demand_predictor/area_gain.py"
PYTHON3="${PYTHON3:-python3}"

DEST_DIR=""
FEATURES_L0="$REPO_ROOT/tools/mining/scripts/demand_predictor/features_l0.csv"

usage() {
    cat <<'EOF'
Usage: step4_area_gain.sh [options]

Options:
  --dest-dir <value>
  --features-l0 <value>
  --help|-h
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dest-dir)    DEST_DIR="$2";    shift 2 ;;
        --features-l0) FEATURES_L0="$2"; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) echo "ERROR: unknown argument: $1" >&2; exit 1 ;;
    esac
done

[[ -z "$DEST_DIR" ]] && { echo "ERROR: --dest-dir required" >&2; exit 1; }
[[ ! -d "$DEST_DIR" ]] && { echo "ERROR: --dest-dir does not exist: $DEST_DIR" >&2; exit 1; }

for FNAME in pattern_instances.out cell_id_map.csv; do
    [[ ! -f "$DEST_DIR/$FNAME" ]] && { echo "ERROR: missing $FNAME in $DEST_DIR (run step2 first)" >&2; exit 1; }
done

[[ ! -f "$FEATURES_L0" ]] && { echo "ERROR: features-l0 not found: $FEATURES_L0" >&2; exit 1; }

echo "[step4_area_gain] dest-dir    = $DEST_DIR"
echo "[step4_area_gain] features-l0 = $FEATURES_L0"
echo "[step4_area_gain] python      = $("$PYTHON3" --version 2>&1)"

"$PYTHON3" "$AREA_GAIN_PY" \
    --payload-dir  "$DEST_DIR" \
    --features-l0  "$FEATURES_L0" \
    --output       "$DEST_DIR/area_gain.csv"

echo "[step4_area_gain] done."
