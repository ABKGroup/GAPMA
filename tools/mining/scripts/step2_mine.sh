#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MINE_SH="$SCRIPT_DIR/../mine.sh"
if [[ -z "${PDK_ROOT:-}" ]]; then
  echo "ERROR: PDK_ROOT is not set. Source the repo-root .env.local or export PDK_ROOT." >&2
  exit 1
fi
DEFAULT_LIB_DIR="$PDK_ROOT/lib"

SRC_RUN=""
DEST_DIR=""
PKD_LIB_DIR="$DEFAULT_LIB_DIR"

usage() {
    cat <<'EOF'
Usage: step2_mine.sh [options]

Options:
  --src-run <value>
  --dest-dir <value>
  --pdk-lib-dir <value>
  --help|-h
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --src-run)     SRC_RUN="$2";     shift 2 ;;
        --dest-dir)    DEST_DIR="$2";    shift 2 ;;
        --pdk-lib-dir) PKD_LIB_DIR="$2"; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) echo "ERROR: unknown argument: $1" >&2; exit 1 ;;
    esac
done

[[ -z "$SRC_RUN"  ]] && { echo "ERROR: --src-run required"  >&2; exit 1; }
[[ -z "$DEST_DIR" ]] && { echo "ERROR: --dest-dir required" >&2; exit 1; }
[[ ! -d "$SRC_RUN"  ]] && { echo "ERROR: --src-run does not exist: $SRC_RUN"   >&2; exit 1; }
[[ ! -d "$PKD_LIB_DIR" ]] && { echo "ERROR: --pdk-lib-dir does not exist: $PKD_LIB_DIR" >&2; exit 1; }

NETLIST="$SRC_RUN/outputs_fc/route_opt.v"
[[ ! -f "$NETLIST" ]] && { echo "ERROR: netlist not found: $NETLIST" >&2; exit 1; }

MINING_RAW="$DEST_DIR/mining_raw"
mkdir -p "$MINING_RAW"

echo "[step2_mine] src-run     = $SRC_RUN"
echo "[step2_mine] netlist     = $NETLIST"
echo "[step2_mine] pdk-lib-dir = $PKD_LIB_DIR"
echo "[step2_mine] mining-raw  = $MINING_RAW"

LIBS="$(find "$PKD_LIB_DIR" -name "*_tt_0.7_25_nldm.lib" | sort | tr '\n' ' ')"
[[ -z "$LIBS" ]] && { echo "ERROR: no *_tt_0.7_25_nldm.lib files found under $PKD_LIB_DIR" >&2; exit 1; }

cd "$(dirname "$MINE_SH")"
bash "$MINE_SH" --libs $LIBS --netlist "$NETLIST" --out-dir "$MINING_RAW"

echo "[step2_mine] mining done; copying input files to dest-dir..."

for FNAME in pattern_instances.out cell_id_map.csv p_to_np_class.csv overlap_clusters.out; do
    SRC="$MINING_RAW/$FNAME"
    [[ ! -f "$SRC" ]] && { echo "ERROR: mine.sh did not produce $FNAME" >&2; exit 1; }
    cp "$SRC" "$DEST_DIR/$FNAME"
    echo "[step2_mine]   copied $FNAME"
done

echo "[step2_mine] done."
