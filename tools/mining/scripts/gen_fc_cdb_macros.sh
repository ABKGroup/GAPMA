#!/usr/bin/env bash
set -uo pipefail

usage() {
    cat <<'USAGE'
gen_fc_cdb_macros.sh — regenerate flow/runs/fc_cdb_macros.txt from the PDK.

Required:
    --pdk-lib-dir <path to PDK_ROOT>
Optional:
    --out <output path; default $GAPMA_ROOT/flow/runs/fc_cdb_macros.txt>
    --db-suffix <per-cell .db filename suffix; default _tt_0.7_25_nldm.db;
                 must match run_fc.py's PDK_PER_CELL_DB_SUFFIX>
USAGE
}

PDK_LIB_DIR=""
OUT=""
DB_SUFFIX="_tt_0.7_25_nldm.db"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --pdk-lib-dir) PDK_LIB_DIR="$2"; shift 2 ;;
        --out)         OUT="$2"; shift 2 ;;
        --db-suffix)   DB_SUFFIX="$2"; shift 2 ;;
        -h|--help)     usage; exit 0 ;;
        *) echo "ERROR: unknown arg: $1" >&2; usage; exit 1 ;;
    esac
done

[[ -z "$PDK_LIB_DIR" ]] && { echo "ERROR: --pdk-lib-dir required" >&2; exit 1; }
[[ ! -d "$PDK_LIB_DIR" ]] && { echo "ERROR: --pdk-lib-dir not found: $PDK_LIB_DIR" >&2; exit 1; }

GAPMA_ROOT="${GAPMA_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}"
OUT="${OUT:-$GAPMA_ROOT/flow/runs/fc_cdb_macros.txt}"

LEF_DIR="$PDK_LIB_DIR/lef"
DB_DIR="$PDK_LIB_DIR/db"
[[ ! -d "$LEF_DIR" ]] && { echo "ERROR: LEF dir not found: $LEF_DIR" >&2; exit 1; }
[[ ! -d "$DB_DIR" ]]  && { echo "ERROR: DB dir not found: $DB_DIR" >&2; exit 1; }

mkdir -p "$(dirname "$OUT")" "$GAPMA_ROOT/flow/runs"
LEF_LIST="$(mktemp -p "$GAPMA_ROOT/flow/runs")"
DB_LIST="$(mktemp -p "$GAPMA_ROOT/flow/runs")"
trap 'rm -f "$LEF_LIST" "$DB_LIST"' EXIT

find "$LEF_DIR" -maxdepth 1 -name '*.lef' -printf '%f\n' | sed 's/\.lef$//' | sort > "$LEF_LIST"
find "$DB_DIR" -maxdepth 1 -name "*${DB_SUFFIX}" -printf '%f\n' | sed "s/${DB_SUFFIX}\$//" | sort > "$DB_LIST"

comm -12 "$LEF_LIST" "$DB_LIST" > "$OUT"

n=$(wc -l < "$OUT")
echo "[gen_fc_cdb_macros] $n macros (LEF and DB both present) written to $OUT"
