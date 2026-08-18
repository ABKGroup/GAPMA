#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DATAGEN_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)/flow/scripts/datagen"
BUILD_MINING_PY="$DATAGEN_DIR/build_mining_edge_features.py"
PYTHON3="${PYTHON3:-python3}"

DEST_DIR=""

usage() {
    cat <<'EOF'
Usage: step3_build_mining.sh [options]

Options:
  --dest-dir <value>
  --help|-h
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dest-dir) DEST_DIR="$2"; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) echo "ERROR: unknown argument: $1" >&2; exit 1 ;;
    esac
done

[[ -z "$DEST_DIR" ]] && { echo "ERROR: --dest-dir required" >&2; exit 1; }
[[ ! -d "$DEST_DIR" ]] && { echo "ERROR: --dest-dir does not exist: $DEST_DIR" >&2; exit 1; }

for FNAME in pattern_instances.out cell_id_map.csv overlap_clusters.out; do
    [[ ! -f "$DEST_DIR/$FNAME" ]] && { echo "ERROR: missing $FNAME in $DEST_DIR (run step2 first)" >&2; exit 1; }
done

echo "[step3_build_mining] dest-dir = $DEST_DIR"
echo "[step3_build_mining] python   = $("$PYTHON3" --version 2>&1)"

"$PYTHON3" - <<PYEOF
import sys
sys.path.insert(0, '$DATAGEN_DIR')
from build_mining_edge_features import process_run
from pathlib import Path

dest = Path('$DEST_DIR')
pnp = process_run(dest)
print(f'[step3_build_mining] OK: {len(pnp)} p->np pairs written')
PYEOF

echo "[step3_build_mining] done."
