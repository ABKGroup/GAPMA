#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
JOBS="$(nproc 2>/dev/null || echo 4)"

usage() {
    cat <<'EOF'
Usage: build.sh [--jobs N] [--help]

Builds the mining binaries: the Design-Data-Compiler in db/, the mining engine,
the post-processing tools and the feature extractor.

  --jobs N   parallel build jobs (default: nproc)

Environment:
  YOSYS_ROOT         Yosys source tree (default: tools/third_party/yosys)
  YOSYS_TCL_INCLUDE  Tcl headers, if Yosys was built with Tcl support
  OPENDB_SRC_ROOT    OpenDB source tree, enables the OpenDB reader
  OPENDB_BUILD_ROOT  OpenDB build tree, required together with OPENDB_SRC_ROOT
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --jobs)     JOBS="$2"; shift 2 ;;
        --help|-h)  usage; exit 0 ;;
        *)          echo "ERROR: unknown argument: $1" >&2; usage >&2; exit 1 ;;
    esac
done

command -v cmake >/dev/null 2>&1 || { echo "ERROR: cmake not found"; exit 1; }
command -v g++ >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1 || { echo "ERROR: C++ compiler not found"; exit 1; }

echo "[info] Building mining engine..."
cmake -S "$SCRIPT_DIR/src/logic_cluster_mining" -B "$SCRIPT_DIR/src/logic_cluster_mining/build"
cmake --build "$SCRIPT_DIR/src/logic_cluster_mining/build" -j"$JOBS"

echo "[info] Building post-processing tools..."
cmake -S "$SCRIPT_DIR/src/cluster_postprocess" -B "$SCRIPT_DIR/src/cluster_postprocess/build"
cmake --build "$SCRIPT_DIR/src/cluster_postprocess/build" -j"$JOBS"

echo "[info] Building feature extractor..."
cmake -S "$SCRIPT_DIR/src/replacement_scoring" -B "$SCRIPT_DIR/src/replacement_scoring/build"
cmake --build "$SCRIPT_DIR/src/replacement_scoring/build" -j"$JOBS"

echo "[info] Building Design-Data-Compiler (db/)..."
: "${YOSYS_ROOT:=$SCRIPT_DIR/../third_party/yosys}"
if [ ! -f "$YOSYS_ROOT/Makefile" ]; then
  echo "ERROR: no Yosys at $YOSYS_ROOT, so lib2cdb/design2cdb cannot be built." >&2
  echo "       The mining engine, post-processing tools, and feature extractor were built." >&2
  echo "       Fetch and build Yosys with:" >&2
  echo "         git submodule update --init tools/third_party/yosys" >&2
  echo "         make -C tools/third_party/yosys -j\"\$(nproc)\"" >&2
  echo "       Or point YOSYS_ROOT at an existing Yosys source tree." >&2
  exit 1
fi
echo "[info] Yosys at $YOSYS_ROOT — lib2cdb/design2cdb will be built"
DB_CMAKE_ARGS="-DDDC_USE_YOSYS=ON -DYOSYS_ROOT=$YOSYS_ROOT"
[ -n "${YOSYS_TCL_INCLUDE:-}" ] && DB_CMAKE_ARGS="$DB_CMAKE_ARGS -DYOSYS_TCL_INCLUDE=$YOSYS_TCL_INCLUDE"
[ -n "${OPENDB_SRC_ROOT:-}" ] && [ -n "${OPENDB_BUILD_ROOT:-}" ] && \
  DB_CMAKE_ARGS="$DB_CMAKE_ARGS -DDDC_USE_OPENDB=ON -DOPENDB_SRC_ROOT=$OPENDB_SRC_ROOT -DOPENDB_BUILD_ROOT=$OPENDB_BUILD_ROOT"
cmake -S "$SCRIPT_DIR/db" -B "$SCRIPT_DIR/db/build" $DB_CMAKE_ARGS
cmake --build "$SCRIPT_DIR/db/build" -j"$JOBS"

echo ""
echo "=== Build complete ==="
echo "Binaries:"
echo "  $SCRIPT_DIR/src/logic_cluster_mining/build/logic_func_minor_cpp"
echo "  $SCRIPT_DIR/src/cluster_postprocess/build/merge_single_to_multi_cpp"
echo "  $SCRIPT_DIR/src/cluster_postprocess/build/select_overlap_cells"
echo "  $SCRIPT_DIR/src/replacement_scoring/build/extract_features_cpp"
echo "  $SCRIPT_DIR/src/replacement_scoring/build/canonical_to_cdl"
for bin in lib2cdb design2cdb cell_db_dump netlist_db_dump cluster_db_dump merge_db_dump; do
  [ -f "$SCRIPT_DIR/db/build/$bin" ] && echo "  $SCRIPT_DIR/db/build/$bin"
done
echo ""
echo "Use ./mine.sh to run mining (accepts both CDB and raw Liberty/Verilog input)."
echo "Use ./extract.sh to extract features from mining output."
