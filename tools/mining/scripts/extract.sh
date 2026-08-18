#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DATAGEN_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)/flow/scripts/datagen"
EXTRACT_BIN="$SCRIPT_DIR/../src/replacement_scoring/build/extract_features_cpp"

if [ ! -x "$EXTRACT_BIN" ]; then
  echo "ERROR: binary not found: $EXTRACT_BIN" >&2
  echo "       Run ./build.sh first." >&2
  exit 1
fi

usage() {
    cat <<'EOF'
Usage: extract.sh (--mining-dir <dir> | --cells-csv <file>) [options]

Turns mining output into the per-candidate feature table the demand predictor
reads, via extract_features_cpp.

  --mining-dir <dir>       a mine.sh output directory
  --cells-csv <file>       candidate cells instead of a mining directory
  --pdk <name>             default SO3
  --top-k1 <int>           candidates kept per design, default 10
  --drive-strengths <list> drive strengths to emit rows for
  --netlist <file.v>       gate-level netlist for relation features
  --out-dir <dir>          writes <dir>/features.csv
  --out-file <file>        explicit output path instead of --out-dir
  --relation-cache <file>  reuse a previously built relation table
  --l0-cells <file>        baseline library cell list
  --rel-workers <int>      relation-build workers
  --rel-timeout <sec>      per-relation timeout, default 60
  --help|-h
EOF
}

MINING_DIR=""
CELLS_CSV=""
PDK="SO3"
TOP_K1=10
DRIVE_STRENGTHS=""
NETLIST=""
OUT_DIR=""
OUT_FILE=""
RELATION_CACHE=""
L0_CELLS=""
REL_WORKERS=""
REL_TIMEOUT=60

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mining-dir)      MINING_DIR="$2";      shift 2 ;;
    --cells-csv)       CELLS_CSV="$2";       shift 2 ;;
    --pdk)             PDK="$2";             shift 2 ;;
    --top-k1)          TOP_K1="$2";          shift 2 ;;
    --drive-strengths) DRIVE_STRENGTHS="$2"; shift 2 ;;
    --netlist)         NETLIST="$2";         shift 2 ;;
    --out-dir)         OUT_DIR="$2";         shift 2 ;;
    --out-file)        OUT_FILE="$2";        shift 2 ;;
    --relation-cache)  RELATION_CACHE="$2";  shift 2 ;;
    --l0-cells)        L0_CELLS="$2";        shift 2 ;;
    --rel-workers)     REL_WORKERS="$2";     shift 2 ;;
    --rel-timeout)     REL_TIMEOUT="$2";     shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "ERROR: unknown argument: $1" >&2; exit 1 ;;
  esac
done

if [ -z "$MINING_DIR" ] && [ -z "$CELLS_CSV" ]; then
  echo "ERROR: one of --mining-dir or --cells-csv is required" >&2
  exit 1
fi
if [ -n "$MINING_DIR" ] && [ -n "$CELLS_CSV" ]; then
  echo "ERROR: --mining-dir and --cells-csv are mutually exclusive" >&2
  exit 1
fi

if [ -n "$MINING_DIR" ]; then
  [ -d "$MINING_DIR" ] || { echo "ERROR: --mining-dir does not exist: $MINING_DIR" >&2; exit 1; }
  FREQ_CSV="$MINING_DIR/pattern_frequency.csv"
  [ -f "$FREQ_CSV" ] || {
    echo "ERROR: pattern_frequency.csv not found in $MINING_DIR" >&2
    echo "       Run ./mine.sh first to produce mining output." >&2
    exit 1
  }
fi

if [ -n "$CELLS_CSV" ]; then
  [ -f "$CELLS_CSV" ] || { echo "ERROR: --cells-csv does not exist: $CELLS_CSV" >&2; exit 1; }
fi

ARGS=()
[ -n "$MINING_DIR" ] && ARGS+=(--mining-dir "$MINING_DIR" --top-k1 "$TOP_K1")
[ -n "$CELLS_CSV"  ] && ARGS+=(--cells-csv "$CELLS_CSV")
ARGS+=(--pdk "$PDK")
[ -n "$DRIVE_STRENGTHS" ] && ARGS+=(--drive-strengths "$DRIVE_STRENGTHS")
[ -n "$NETLIST"  ] && ARGS+=(--netlist "$NETLIST")
[ -n "$OUT_DIR"  ] && ARGS+=(--out-dir "$OUT_DIR")
[ -n "$OUT_FILE" ] && ARGS+=(--out-file "$OUT_FILE")

if [ -n "$MINING_DIR" ]; then
  echo "[extract.sh] mode=mining  PDK=$PDK  top-k1=$TOP_K1  mining-dir=$MINING_DIR"
else
  echo "[extract.sh] mode=cells-csv  PDK=$PDK  cells-csv=$CELLS_CSV"
fi
[ -n "$DRIVE_STRENGTHS" ] && echo "[extract.sh] drive-strengths=$DRIVE_STRENGTHS"
[ -n "$NETLIST"  ] && echo "[extract.sh] netlist=$NETLIST"

"$EXTRACT_BIN" "${ARGS[@]}"

if [ -n "$MINING_DIR" ] && [ -n "$RELATION_CACHE" ]; then
  L0_CELLS_PATH="${L0_CELLS:-$DATAGEN_DIR/cells.csv}"
  if [ ! -f "$L0_CELLS_PATH" ]; then
    echo "ERROR: L0 cells file not found: $L0_CELLS_PATH" >&2
    echo "       Provide --l0-cells FILE" >&2
    exit 1
  fi

  if [ -n "$OUT_DIR" ]; then
    FEAT_CSV="$OUT_DIR/${OUT_FILE:-features.csv}"
  else
    FEAT_CSV="$MINING_DIR/${OUT_FILE:-features.csv}"
  fi
  if [ ! -f "$FEAT_CSV" ]; then
    echo "ERROR: features.csv not found after extraction: $FEAT_CSV" >&2
    exit 1
  fi

  COMBINED_CELLS="${FEAT_CSV%/*}/relation_cells_tmp.csv"
  python3 - "$L0_CELLS_PATH" "$FEAT_CSV" "$COMBINED_CELLS" <<'PYEOF'
import csv, sys

l0_path, feat_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
seen = set()
rows = []

with open(l0_path, newline='') as f:
    for row in csv.DictReader(f):
        key = row['canonical_key'].strip()
        if key and key not in seen:
            seen.add(key)
            rows.append({'name': row['name'].strip(),
                         'n_inputs': row['n_inputs'].strip(),
                         'canonical_key': key})

with open(feat_path, newline='') as f:
    for row in csv.DictReader(f):
        key  = row['canonical'].strip()
        name = row['cell_name'].strip()
        if not key or key in seen or not name.endswith('_X1'):
            continue
        seen.add(key)
        rows.append({'name': name,
                     'n_inputs': row['n_inputs'].strip(),
                     'canonical_key': key})

with open(out_path, 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=['name', 'n_inputs', 'canonical_key'])
    w.writeheader()
    w.writerows(rows)

print(f"[extract.sh] relation: {len(rows)} unique canonicals "
      f"(L0 + candidates)", file=sys.stderr)
PYEOF

  REL_W="${REL_WORKERS:-$(nproc)}"
  echo "[extract.sh] updating relation cache: $RELATION_CACHE" \
       "(workers=$REL_W  timeout=${REL_TIMEOUT}s)"
  python3 "$SCRIPT_DIR/demand_predictor/build_relation.py" \
    --in          "$COMBINED_CELLS" \
    --cache       "$RELATION_CACHE" \
    --n-workers   "$REL_W" \
    --timeout-s   "$REL_TIMEOUT"
  rm -f "$COMBINED_CELLS"
  echo "[extract.sh] relation cache update complete."
fi
