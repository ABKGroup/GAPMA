#!/usr/bin/env bash
set -uo pipefail

GAPMA_ROOT="${GAPMA_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}"
[ -f "$GAPMA_ROOT/.env.local" ] && source "$GAPMA_ROOT/.env.local"

if [[ -n "${FC_BIN:-}" ]]; then
    export PATH="${FC_BIN}:$PATH"
fi
if [[ -n "${FC_LICENSE:-}" ]]; then
    export SNPSLMD_LICENSE_FILE="$FC_LICENSE"
    export LM_LICENSE_FILE="$FC_LICENSE"
fi
if [[ -z "${PDK_ROOT:-}" ]]; then
    echo "ERROR: PDK_ROOT unset and $GAPMA_ROOT/.env.local did not provide it." >&2
    exit 1
fi
export PDK_ROOT

exec python3 "$GAPMA_ROOT/flow/scripts/iterate/gen_d0.py" "$@"
