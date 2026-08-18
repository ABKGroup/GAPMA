#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MINER="$SCRIPT_DIR/src/logic_cluster_mining/build/logic_func_minor_cpp"
LIB2CDB="$SCRIPT_DIR/db/build/lib2cdb"
DESIGN2CDB="$SCRIPT_DIR/db/build/design2cdb"

CELL_DB=""
NETLIST_DB=""
NETLIST=""
DEF_FILE=""
LIBS=()
LIB_DIRS=()
LEF_FILES=()
CDL_FILES=()
OUT_DIR=""
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --cell-db)     CELL_DB="$2"; shift 2 ;;
    --netlist-db)  NETLIST_DB="$2"; shift 2 ;;
    --netlist)     NETLIST="$2"; shift 2 ;;
    --def)         DEF_FILE="$2"; shift 2 ;;
    --libs)        shift; while [[ $# -gt 0 && "$1" != --* ]]; do LIBS+=("$1"); shift; done ;;
    --lib-dir)     LIB_DIRS+=("$2"); shift 2 ;;
    --lef)         shift; while [[ $# -gt 0 && "$1" != --* ]]; do LEF_FILES+=("$1"); shift; done ;;
    --cdl)         CDL_FILES+=("$2"); shift 2 ;;
    --out-dir)     OUT_DIR="$2"; shift 2 ;;
    --help|-h)
      echo "Usage: mine.sh [--cell-db <cell.cdb>] [--netlist-db <netlist.cdb>]"
      echo "               [--libs <*.lib>] [--lib-dir <dir>] [--netlist <file.v>]"
      echo "               [--def <file.def>] [--lef <*.lef>] [--cdl <file.cdl>]"
      echo "               --out-dir <dir> [mining flags...]"
      echo ""
      echo "Accepts pre-built CDB files or raw Liberty/Verilog/DEF."
      echo "When raw files are given, converts to CDB automatically."
      echo "Extra flags are forwarded to logic_func_minor_cpp."
      exit 0 ;;
    --*)
      # Mining flags belong to logic_func_minor_cpp, which rejects the ones it
      # does not know, so forward them together with any values that follow.
      EXTRA_ARGS+=("$1"); shift
      while [[ $# -gt 0 && "$1" != --* ]]; do EXTRA_ARGS+=("$1"); shift; done ;;
    *)             echo "ERROR: unknown argument: $1" >&2; exit 1 ;;
  esac
done

if [ -z "$OUT_DIR" ]; then
  echo "ERROR: --out-dir is required" >&2; exit 1
fi

mkdir -p "$OUT_DIR"
TMPDIR="$OUT_DIR/.cdb_tmp"

if [ -z "$CELL_DB" ]; then
  if [ ${#LIBS[@]} -eq 0 ] && [ ${#LIB_DIRS[@]} -eq 0 ]; then
    echo "ERROR: provide --cell-db or --libs/--lib-dir" >&2; exit 1
  fi
  if [ ! -x "$LIB2CDB" ]; then
    echo "ERROR: lib2cdb not found at $LIB2CDB" >&2
    echo "       Run ./build.sh with YOSYS_ROOT set first." >&2
    exit 1
  fi

  mkdir -p "$TMPDIR"
  CELL_DB="$TMPDIR/cell.cdb"

  LIB2CDB_ARGS=("--out" "$CELL_DB")
  for l in "${LIBS[@]+"${LIBS[@]}"}"; do LIB2CDB_ARGS+=("--libs" "$l"); done
  for d in "${LIB_DIRS[@]+"${LIB_DIRS[@]}"}"; do LIB2CDB_ARGS+=("--lib-dir" "$d"); done
  for f in "${LEF_FILES[@]+"${LEF_FILES[@]}"}"; do LIB2CDB_ARGS+=("--lef" "$f"); done
  for f in "${CDL_FILES[@]+"${CDL_FILES[@]}"}"; do LIB2CDB_ARGS+=("--cdl" "$f"); done

  echo "[mine.sh] Building cell.cdb from Liberty..."
  "$LIB2CDB" "${LIB2CDB_ARGS[@]}"
  echo "[mine.sh] cell.cdb → $CELL_DB"
fi

if [ -z "$NETLIST_DB" ]; then
  if [ -z "$NETLIST" ]; then
    echo "ERROR: provide --netlist-db or --netlist" >&2; exit 1
  fi
  if [ ${#LIBS[@]} -eq 0 ] && [ ${#LIB_DIRS[@]} -eq 0 ]; then
    echo "ERROR: --libs or --lib-dir required for Verilog → netlist.cdb conversion" >&2; exit 1
  fi
  if [ ! -x "$DESIGN2CDB" ]; then
    echo "ERROR: design2cdb not found at $DESIGN2CDB" >&2
    echo "       Run ./build.sh with YOSYS_ROOT set first." >&2
    exit 1
  fi

  mkdir -p "$TMPDIR"
  NETLIST_DB="$TMPDIR/netlist.cdb"

  D2CDB_ARGS=("--netlist" "$NETLIST" "--out" "$NETLIST_DB" "--flatten")
  for l in "${LIBS[@]+"${LIBS[@]}"}"; do D2CDB_ARGS+=("--libs" "$l"); done
  for d in "${LIB_DIRS[@]+"${LIB_DIRS[@]}"}"; do D2CDB_ARGS+=("--lib-dir" "$d"); done
  [ -n "$DEF_FILE" ] && D2CDB_ARGS+=("--def" "$DEF_FILE")

  echo "[mine.sh] Building netlist.cdb from Verilog..."
  "$DESIGN2CDB" "${D2CDB_ARGS[@]}"
  echo "[mine.sh] netlist.cdb → $NETLIST_DB"
fi

if [ ! -x "$MINER" ]; then
  echo "ERROR: logic_func_minor_cpp not found at $MINER" >&2
  echo "       Run ./build.sh first." >&2
  exit 1
fi

echo "[mine.sh] Running mining..."
"$MINER" \
  --cell-db "$CELL_DB" \
  --netlist-db "$NETLIST_DB" \
  --out-dir "$OUT_DIR" \
  "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}"

echo "[mine.sh] Done. Results in $OUT_DIR"
