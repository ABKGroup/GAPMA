#!/usr/bin/env bash

set -uo pipefail

GAPMA_ROOT="${GAPMA_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}"
[ -f "$GAPMA_ROOT/.env.local" ] && source "$GAPMA_ROOT/.env.local"

usage() {
    cat <<'USAGE'
phase_resolve.sh — produce final cell list for one rank attempt.

Required:
    --canonicals "c1 c2 c3 c4 c5"
    --pred-x1    "p1 p2 p3 p4 p5"
    --pred-x2    "p1 p2 p3 p4 p5"
    --pdk-lib-dir <SO3 lib dir>
    --verify-helper <path to find_lib_cell_by_canonical.py>
    --canonical-to-cdl-bin <path>
    --mining-repo-root <root of tools/mining>

X2/cell-gen options:
    --x2-ratio        <float, default 0.10>
    --pred-x1-floor   <float, default 0.001>
    --cellgen-output-dir <local dir; default $PDK_ROOT/pipeline/runs>
        (cell-gen always runs locally against PDK_ROOT, no server needed)
    --cellgen-tr-max  <int, default 20>
    --cellgen-timeout <seconds, default 3600>
    --lib2cdb-bin     <path>
    --pdk-root    <default $PDK_ROOT>
    --rank-tag        <log label, e.g. rank1>
    --dry-run
USAGE
}

CANONICALS_STR=""
PRED_X1_STR=""
PRED_X2_STR=""
PDK_LIB_DIR=""
VERIFY_HELPER=""
CANONICAL_TO_CDL_BIN=""
MINING_REPO_ROOT=""
X2_RATIO=0.10
PRED_X1_FLOOR=0.001
CELLGEN_OUTPUT_DIR=""
CELLGEN_TR_MAX=20
CELLGEN_TIMEOUT=3600
LIB2CDB_BIN=""
PDK_ROOT="${PDK_ROOT:-}"
RANK_TAG="rank1"
DRY_RUN=0
AREA_GAIN_CSV=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --canonicals)        CANONICALS_STR="$2"; shift 2 ;;
        --pred-x1)           PRED_X1_STR="$2"; shift 2 ;;
        --pred-x2)           PRED_X2_STR="$2"; shift 2 ;;
        --pdk-lib-dir)       PDK_LIB_DIR="$2"; shift 2 ;;
        --verify-helper)     VERIFY_HELPER="$2"; shift 2 ;;
        --canonical-to-cdl-bin) CANONICAL_TO_CDL_BIN="$2"; shift 2 ;;
        --mining-repo-root)  MINING_REPO_ROOT="$2"; shift 2 ;;
        --x2-ratio)          X2_RATIO="$2"; shift 2 ;;
        --pred-x1-floor)     PRED_X1_FLOOR="$2"; shift 2 ;;
        --cellgen-output-dir) CELLGEN_OUTPUT_DIR="$2"; shift 2 ;;
        --cellgen-tr-max)    CELLGEN_TR_MAX="$2"; shift 2 ;;
        --cellgen-timeout)   CELLGEN_TIMEOUT="$2"; shift 2 ;;
        --lib2cdb-bin)       LIB2CDB_BIN="$2"; shift 2 ;;
        --pdk-root)      PDK_ROOT="$2"; shift 2 ;;
        --rank-tag)          RANK_TAG="$2"; shift 2 ;;
        --area-gain-csv)     AREA_GAIN_CSV="$2"; shift 2 ;;
        --dry-run)           DRY_RUN=1; shift ;;
        -h|--help)           usage; exit 0 ;;
        *) echo "ERROR: unknown arg: $1" >&2; usage; exit 1 ;;
    esac
done

CELLGEN_OUTPUT_DIR="${CELLGEN_OUTPUT_DIR:-${PDK_ROOT}/cellgen_runs}"
CELLGEN_DRIVER="${CELLGEN_DRIVER:-${GAPMA_ROOT}/tools/cell_generation/single_cell.sh}"

miss=()
for v in CANONICALS_STR PRED_X1_STR PRED_X2_STR PDK_LIB_DIR VERIFY_HELPER \
         CANONICAL_TO_CDL_BIN MINING_REPO_ROOT; do
    if [[ -z "${!v}" ]]; then flag="${v,,}"; miss+=("--${flag//_/-}"); fi
done
if (( ${#miss[@]} > 0 )); then
    echo "ERROR: phase_resolve missing required args: ${miss[*]}" >&2; exit 1
fi
if [[ -z "$PDK_ROOT" ]]; then
    echo "ERROR: phase_resolve: PDK root not set. Pass --pdk-root or export PDK_ROOT" >&2; exit 1
fi

read -ra CANON5  <<< "$CANONICALS_STR"
read -ra PRED_X1 <<< "$PRED_X1_STR"
read -ra PRED_X2 <<< "$PRED_X2_STR"
if (( ${#CANON5[@]} != 5 )) || (( ${#PRED_X1[@]} != 5 )) || (( ${#PRED_X2[@]} != 5 )); then
    echo "ERROR: phase_resolve expects 5/5/5 (got ${#CANON5[@]}/${#PRED_X1[@]}/${#PRED_X2[@]})" >&2
    exit 1
fi

if [[ ! -r "${VERIFY_HELPER}" ]]; then
    echo "ERROR: verify helper not readable: ${VERIFY_HELPER}" >&2; exit 1
fi
if [[ ! -x "${CANONICAL_TO_CDL_BIN}" ]]; then
    echo "ERROR: canonical_to_cdl binary not executable: ${CANONICAL_TO_CDL_BIN}" >&2; exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SUBSTITUTE_HELPER="${SCRIPT_DIR}/rank_area_gain.py"
FC_CDB_LIST="${GAPMA_ROOT}/flow/runs/fc_cdb_macros.txt"

MOCK_PREFIX=""
if (( DRY_RUN )); then
    MOCK_PREFIX="[DRY-RUN MOCK — NOT REAL RESOLUTION] "
fi

declare -a TOP5_X1
if (( DRY_RUN )); then
    TOP5_X1=("NAND2_X1" "NOR2_X1" "XOR2_X1" "AND2_X1" "INV_X1")
else
    mapfile -t TOP5_X1 < <(
        python3 "${VERIFY_HELPER}" \
            --lib-dir     "${PDK_LIB_DIR}" \
            --lef-dir     "${PDK_ROOT}/lef" \
            --cdb-list    "${FC_CDB_LIST}" \
            --canonicals  "${CANON5[@]}" \
            --prefer-suffix _X1 \
            --require-suffix _X1 \
            --single
    )
fi
if (( ${#TOP5_X1[@]} != 5 )); then
    echo "ERROR: verify helper returned ${#TOP5_X1[@]} X1 cells (expected 5) for ${RANK_TAG}" >&2
    exit 1
fi
declare -A SUBSTITUTED=()
if [[ -n "$AREA_GAIN_CSV" ]]; then
    need_sub=0
    for i in 0 1 2 3 4; do [[ -z "${TOP5_X1[$i]}" ]] && need_sub=1; done
    if (( need_sub )); then
        if [[ ! -f "$AREA_GAIN_CSV" ]]; then
            echo "ERROR: --area-gain-csv given but file not found: ${AREA_GAIN_CSV}" >&2; exit 1
        fi
        mapfile -t SUBS < <(python3 "$SUBSTITUTE_HELPER" \
            --area-gain-csv "$AREA_GAIN_CSV" \
            --lib-dir "$PDK_LIB_DIR" \
            --lef-dir "${PDK_ROOT}/lef" \
            --verify-helper "$VERIFY_HELPER" \
            --cdb-list    "${FC_CDB_LIST}" \
            --exclude "${CANON5[@]}")
        sub_idx=0
        for i in 0 1 2 3 4; do
            [[ -n "${TOP5_X1[$i]}" ]] && continue
            while (( sub_idx < ${#SUBS[@]} )); do
                line="${SUBS[$sub_idx]}"; sub_idx=$((sub_idx+1))
                sub_canon="${line%% *}"; sub_cell="${line#* }"
                [[ -z "$sub_canon" || -z "$sub_cell" || "$sub_canon" == "$sub_cell" ]] && continue
                echo "[phase_resolve] slot $((i+1)) (${CANON5[$i]}): X1 unresolvable (would need cell-gen) -> substituting next area-gain candidate ${sub_canon} (${sub_cell})" >&2
                CANON5[$i]="$sub_canon"; TOP5_X1[$i]="$sub_cell"; SUBSTITUTED[$i]=1
                break
            done
        done
    fi
fi
for i in "${!TOP5_X1[@]}"; do
    if [[ -z "${TOP5_X1[$i]}" ]]; then
        echo "ERROR: no X1 lib cell matches canonical ${CANON5[$i]} (slot $((i+1)), ${RANK_TAG}) and no area-gain substitute available" >&2
        exit 1
    fi
done
echo "${MOCK_PREFIX}[phase_resolve] ${RANK_TAG} X1 cells: ${TOP5_X1[*]}" >&2

TOP5_X2=()
CELLGEN_LAUNCHED=()
for i in 0 1 2 3 4; do
    if [[ -n "${SUBSTITUTED[$i]:-}" ]]; then
        TOP5_X2[$i]=""
        continue
    fi
    x1=${PRED_X1[$i]}
    x2=${PRED_X2[$i]}
    canon=${CANON5[$i]}

    x1_above_floor=$(awk -v a="$x1" -v f="$PRED_X1_FLOOR" 'BEGIN { print (a >= f) }')
    if (( ! x1_above_floor )); then
        TOP5_X2[$i]=""
        continue
    fi
    ratio_ok=$(awk -v a="$x2" -v b="$x1" -v t="$X2_RATIO" 'BEGIN { if (b > 0 && a/b >= t) print 1; else print 0 }')
    if [[ "$ratio_ok" != "1" ]]; then
        TOP5_X2[$i]=""
        continue
    fi

    if (( DRY_RUN )); then
        x2_cell="MOCK_X2"
    else
        x2_cell=$(python3 "${VERIFY_HELPER}" \
            --lib-dir     "${PDK_LIB_DIR}" \
            --lef-dir     "${PDK_ROOT}/lef" \
            --cdb-list    "${FC_CDB_LIST}" \
            --canonicals  "$canon" \
            --prefer-suffix _X2 \
            --require-suffix _X2 \
            --single | head -1)
    fi

    if [[ -n "$x2_cell" ]]; then
        TOP5_X2[$i]="$x2_cell"
        ratio=$(awk -v a=$x2 -v b=$x1 'BEGIN { printf "%.3f", a/b }')
        echo "${MOCK_PREFIX}[phase_resolve] slot $((i+1)): X2 ratio=${ratio} -> X2=${x2_cell} (lib)" >&2
        continue
    fi

    x1_sibling="${TOP5_X1[$i]}"
    if [[ "$x1_sibling" != *_X1* ]]; then
        echo "ERROR: slot $((i+1)) (${canon}): X1 sibling '${x1_sibling}' has no _X1 token — cannot derive the X2 family name." >&2
        exit 1
    fi
    x2_gen_name="${x1_sibling/_X1/_X2}"

    cdl_x1="${HOME}/.phase_resolve_${x1_sibling}_$$.cdl"
    rm -f "$cdl_x1"
    if ! "${CANONICAL_TO_CDL_BIN}" "${canon}" --name "${x1_sibling}" > "$cdl_x1"; then
        rm -f "$cdl_x1"
        echo "ERROR: canonical_to_cdl failed for ${canon} (slot $((i+1))). Aborting (data loss not allowed)." >&2
        exit 1
    fi
    tr_n=$(grep -oE 'Transistors:[[:space:]]*[0-9]+' "$cdl_x1" | awk '{print $2}' | head -1)
    if [[ -z "$tr_n" ]]; then
        rm -f "$cdl_x1"
        echo "ERROR: no 'Transistors:' line for ${canon} (slot $((i+1))). Aborting." >&2
        exit 1
    fi

    if (( tr_n > CELLGEN_TR_MAX )); then
        echo "WARNING: slot $((i+1)) (${canon}): X2 absent, TR=${tr_n} > ${CELLGEN_TR_MAX}; NOT generating X2. X1 only." >&2
        TOP5_X2[$i]=""
        rm -f "$cdl_x1"
        continue
    fi
    if [[ ! -f "${CELLGEN_DRIVER}" ]]; then
        echo "ERROR: slot $((i+1)) (${canon}): X2 absent + TR=${tr_n} <= ${CELLGEN_TR_MAX} requires cell-gen, but ${CELLGEN_DRIVER} is missing." >&2
        rm -f "$cdl_x1"
        exit 1
    fi
    for _occupant in "${PDK_ROOT}/gds/${x2_gen_name}.gds" \
                     "${PDK_ROOT}/cdl/${x2_gen_name}.cdl" \
                     "${PDK_LIB_DIR}/${x2_gen_name}_tt_0.7_25_nldm.lib"; do
        if [[ -e "$_occupant" ]]; then
            echo "ERROR: slot $((i+1)) (${canon}): target name ${x2_gen_name} already occupied by ${_occupant} whose function did NOT match this canonical (verify helper found no X2). Refusing to overwrite a different-function cell." >&2
            exit 1
        fi
    done
    echo "[phase_resolve] slot $((i+1)) (${canon}): X2 missing, TR=${tr_n}, launching cell-gen locally for ${x2_gen_name} (family of ${x1_sibling})" >&2

    if (( ! DRY_RUN )); then
        if [[ "$x2_gen_name" != *_X2 ]]; then
            echo "ERROR: cell-gen path only supports _X2 targets (got ${x2_gen_name})." >&2
            exit 1
        fi
        SCALER="${GAPMA_ROOT}/tools/cell_generation/scripts/x1_to_x2_scaler.py"
        if [[ ! -f "$SCALER" ]]; then
            echo "ERROR: x1_to_x2_scaler.py not found at ${SCALER}. Aborting." >&2
            exit 1
        fi
        cdl_gen="${HOME}/.phase_resolve_${x2_gen_name}_$$.cdl"
        rm -f "$cdl_gen"
        if [[ -f "${PDK_ROOT}/cdl/${x1_sibling}.cdl" ]]; then
            rm -f "$cdl_x1"
            cp "${PDK_ROOT}/cdl/${x1_sibling}.cdl" "$cdl_x1"
        fi
        if ! python3 "$SCALER" --src "$cdl_x1" --out "$cdl_gen"; then
            rm -f "$cdl_x1" "$cdl_gen"
            echo "ERROR: x1_to_x2_scaler failed for ${x2_gen_name}. Aborting." >&2
            exit 1
        fi
        rm -f "$cdl_x1"
        if ! cp "$cdl_gen" "${PDK_ROOT}/cdl/${x2_gen_name}.cdl"; then
            rm -f "$cdl_gen"
            echo "ERROR: copy of ${x2_gen_name}.cdl to ${PDK_ROOT}/cdl/ failed. Aborting." >&2
            exit 1
        fi
        rm -f "$cdl_gen"
        mkdir -p "${CELLGEN_OUTPUT_DIR}" \
          || { echo "ERROR: mkdir -p ${CELLGEN_OUTPUT_DIR} failed" >&2; exit 1; }
        ( cd "${CELLGEN_OUTPUT_DIR}" || exit 1
          nohup bash "${CELLGEN_DRIVER}" "${x2_gen_name}" \
              > "cellgen_${x2_gen_name}.log" 2>&1 & ) \
          || { echo "ERROR: failed to launch cell-gen for ${x2_gen_name}" >&2; exit 1; }
    fi
    rm -f "${cdl_x1:-}"
    TOP5_X2[$i]="$x2_gen_name"
    CELLGEN_LAUNCHED+=("$x2_gen_name")
done

if (( ${#CELLGEN_LAUNCHED[@]} > 0 )) && (( ! DRY_RUN )); then
    echo "[phase_resolve] waiting up to ${CELLGEN_TIMEOUT}s for ${#CELLGEN_LAUNCHED[@]} async cell-gens" >&2
    deadline=$(( $(date +%s) + CELLGEN_TIMEOUT ))
    for name in "${CELLGEN_LAUNCHED[@]}"; do
        gen_log="${CELLGEN_OUTPUT_DIR}/cellgen_${name}.log"
        while (( $(date +%s) < deadline )); do
            if ls "${PDK_ROOT}/lib/${name}_tt_"*.lib >/dev/null 2>&1; then
                echo "[phase_resolve]   ${name}: .lib ready" >&2
                break
            fi
            err_line=""
            [[ -f "$gen_log" ]] && err_line=$(grep -m1 -E 'ERROR|FATAL|Error:|No such file|command not found' "$gen_log")
            if [[ -n "$err_line" ]]; then
                echo "ERROR: cell-gen for ${name} failed — ${gen_log} contains: \"${err_line}\". Aborting." >&2
                exit 1
            fi
            sleep 30
        done
        if (( $(date +%s) >= deadline )); then
            echo "ERROR: cell-gen timeout (${CELLGEN_TIMEOUT}s) waiting for ${name} .lib. Aborting." >&2
            exit 1
        fi
    done

    if [[ -z "$LIB2CDB_BIN" ]]; then
        LIB2CDB_BIN="${MINING_REPO_ROOT}/db/build/lib2cdb"
    fi
    if [[ ! -x "$LIB2CDB_BIN" ]]; then
        echo "ERROR: lib2cdb binary not found at $LIB2CDB_BIN; needed to compile new .lib into db/." >&2
        exit 1
    fi
    SO3_DB_DIR="${PDK_ROOT}/db"
    for name in "${CELLGEN_LAUNCHED[@]}"; do
        lib_local=$(ls "${PDK_ROOT}/lib/${name}_tt_"*.lib 2>/dev/null | head -1)
        if [[ -z "$lib_local" ]]; then
            echo "ERROR: ${name} .lib not found at ${PDK_ROOT}/lib/ after cell-gen wait. Aborting." >&2
            exit 1
        fi
        if ! mkdir -p "${SO3_DB_DIR}" || ! "${LIB2CDB_BIN}" --libs "${lib_local}" --out "${SO3_DB_DIR}/${name}.cdb"; then
            echo "ERROR: lib2cdb failed for ${name}. Aborting (data loss not allowed)." >&2
            exit 1
        fi
        echo "[phase_resolve]   ${name}: lib2cdb -> ${SO3_DB_DIR}/${name}.cdb" >&2
        if [[ ! -f "$FC_CDB_LIST" ]]; then
            echo "ERROR: FC CDB list not found: ${FC_CDB_LIST}; cannot register ${name}. Aborting." >&2
            exit 1
        fi
        if ! grep -qxF "$name" "$FC_CDB_LIST"; then
            echo "$name" >> "$FC_CDB_LIST"
            echo "[phase_resolve]   ${name}: appended to ${FC_CDB_LIST}" >&2
        fi
    done
fi

for i in 0 1 2 3 4; do
    echo "${MOCK_PREFIX}${TOP5_X1[$i]}"
    if [[ -n "${TOP5_X2[$i]}" ]]; then
        echo "${MOCK_PREFIX}${TOP5_X2[$i]}"
    fi
done
exit 0
