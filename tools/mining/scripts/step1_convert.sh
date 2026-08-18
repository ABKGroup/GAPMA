#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DATAGEN_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)/flow/scripts/datagen"
CONVERT_PY="$DATAGEN_DIR/fc_validate_extract.py"
PYTHON3="${PYTHON3:-python3}"

SRC_RUN=""
DESIGN=""
DEST_ROOT=""

usage() {
    cat <<'EOF'
Usage: step1_convert.sh [options]

Options:
  --src-run <value>
  --design <value>
  --dest-root <value>
  --help|-h
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --src-run)   SRC_RUN="$2";   shift 2 ;;
        --design)    DESIGN="$2";    shift 2 ;;
        --dest-root) DEST_ROOT="$2"; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) echo "ERROR: unknown argument: $1" >&2; exit 1 ;;
    esac
done

[[ -z "$SRC_RUN"   ]] && { echo "ERROR: --src-run required"   >&2; exit 1; }
[[ -z "$DESIGN"    ]] && { echo "ERROR: --design required"    >&2; exit 1; }
[[ -z "$DEST_ROOT" ]] && { echo "ERROR: --dest-root required" >&2; exit 1; }
[[ ! -d "$SRC_RUN" ]] && { echo "ERROR: --src-run does not exist: $SRC_RUN" >&2; exit 1; }

DESIGN_SUBDIR="$(basename "$(dirname "$SRC_RUN")")"
SWEEP_ID="$(basename "$SRC_RUN")"

echo "[step1_convert] src-run      = $SRC_RUN"
echo "[step1_convert] design       = $DESIGN"
echo "[step1_convert] design-subdir= $DESIGN_SUBDIR"
echo "[step1_convert] sweep-id     = $SWEEP_ID"
echo "[step1_convert] dest-root    = $DEST_ROOT"

SRC_ROOT="$(cd "$(dirname "$SRC_RUN")/.." && pwd)"

"$PYTHON3" "$CONVERT_PY" \
    --src-root   "$SRC_ROOT" \
    --src-run    "$SRC_RUN" \
    --design     "$DESIGN" \
    --sweep-id   "$SWEEP_ID" \
    --design-subdir "$DESIGN_SUBDIR" \
    --dest-root  "$DEST_ROOT"

echo "[step1_convert] done."
