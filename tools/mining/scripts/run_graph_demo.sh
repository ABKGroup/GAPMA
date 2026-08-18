#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../../.." && pwd)"

exec python3 "$HERE/demand_predictor/graph_builder.py" \
  --l0 "$HERE/demand_predictor/features_l0.csv" \
  --features "$REPO_ROOT/examples/graph_demo/features.csv" \
  --mining-edge-features "$REPO_ROOT/examples/graph_demo/mining_edge_features.csv" \
  --mining-overlap-ratio "$REPO_ROOT/examples/graph_demo/mining_overlap_ratio.csv"
