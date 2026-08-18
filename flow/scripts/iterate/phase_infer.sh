#!/usr/bin/env bash

set -uo pipefail

usage() {
    cat <<'USAGE'
phase_infer.sh — produce inference_results_x2_<TAG>.csv for one iteration.

Required:
    --design        <aes|jpeg|eth|sha256|keccak>
    --prev-dir      <absolute path to fc_D{m}/<design>_D{m}_<arity>/> (LOCAL)
    --prev-best-run <run name inside prev-dir, e.g. 430_2.0_3>
    --arity         <3in|4in>
    --next-label    <D{m+1} label (used only for AES/SHA256 D1 reuse short-circuit)>
    --model         <model.pt> (LOCAL) — or use --model-bundle
    --norm-stats    <model.norm_stats.json> (LOCAL) — or use --model-bundle
    --features-l0   <features_l0_with_x2_4in.csv> (LOCAL) — or use --model-bundle
    --p-to-np       <global_p_to_np.csv> (LOCAL) — or use --model-bundle
    --model-bundle  <dir holding all 4 model files; alternative to the 4 above>
                    Looks for: model_full.pt (or model.pt),
                               model_full.norm_stats.json (or model.norm_stats.json or norm_stats.json),
                               features_l0.csv,
                               global_p_to_np.csv (or p_to_np.csv).
                    Mutually exclusive with the 4 individual --model* flags.
    --inf-payload-root <directory where designs/<design>/<prev_run>/ lives> (LOCAL)

Optional:
    --server        <SP&R server>
                    If set, also SSH-check reachability + that prev_run exists
                    on that server. Omit to keep inference fully local
                    (option A: data + model live on the cad-local NFS tree).
    --k1            <int, default 10>
    --k2            <int, default 5>
    --dry-run       skip the actual run_cell_recommend invocation
USAGE
}

DESIGN=""
PREV_DIR=""
PREV_BEST_RUN=""
ARITY=""
NEXT_LABEL=""
SERVER=""
MODEL=""
NORM_STATS=""
FEATURES_L0=""
P_TO_NP=""
MODEL_BUNDLE=""
INF_PAYLOAD_ROOT=""
K1=10
K2=5
DRY_RUN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --design)            DESIGN="$2"; shift 2 ;;
        --prev-dir)          PREV_DIR="$2"; shift 2 ;;
        --prev-best-run)     PREV_BEST_RUN="$2"; shift 2 ;;
        --arity)             ARITY="$2"; shift 2 ;;
        --next-label)        NEXT_LABEL="$2"; shift 2 ;;
        --server)            SERVER="$2"; shift 2 ;;
        --model)             MODEL="$2"; shift 2 ;;
        --norm-stats)        NORM_STATS="$2"; shift 2 ;;
        --features-l0)       FEATURES_L0="$2"; shift 2 ;;
        --p-to-np)           P_TO_NP="$2"; shift 2 ;;
        --model-bundle)      MODEL_BUNDLE="$2"; shift 2 ;;
        --inf-payload-root)  INF_PAYLOAD_ROOT="$2"; shift 2 ;;
        --k1)                K1="$2"; shift 2 ;;
        --k2)                K2="$2"; shift 2 ;;
        --dry-run)           DRY_RUN=1; shift ;;
        -h|--help)           usage; exit 0 ;;
        *) echo "ERROR: unknown arg: $1" >&2; usage; exit 1 ;;
    esac
done

if [[ -n "$MODEL_BUNDLE" ]]; then
    if [[ ! -d "$MODEL_BUNDLE" ]]; then
        echo "ERROR: --model-bundle is not a directory: $MODEL_BUNDLE" >&2; exit 1
    fi
    if [[ -n "$MODEL" || -n "$NORM_STATS" || -n "$FEATURES_L0" || -n "$P_TO_NP" ]]; then
        echo "ERROR: --model-bundle is mutually exclusive with --model/--norm-stats/--features-l0/--p-to-np" >&2; exit 1
    fi
    _bundle_pick() {
        local out_var="$1"; shift
        local found=""
        for cand in "$@"; do
            if [[ -f "${MODEL_BUNDLE}/${cand}" ]]; then found="${MODEL_BUNDLE}/${cand}"; break; fi
        done
        if [[ -z "$found" ]]; then
            echo "ERROR: --model-bundle ${MODEL_BUNDLE} is missing all candidates: $*" >&2; return 1
        fi
        printf -v "$out_var" '%s' "$found"
    }
    if [[ -f "${MODEL_BUNDLE}/model_current.pt" ]]; then
        MODEL="${MODEL_BUNDLE}/model_current.pt"
        MODEL_PICK="canonical model_current.pt"
    else
        _bundle_pick MODEL   model_full.pt model.pt                                  || exit 1
        MODEL_PICK="legacy bundle candidate"
    fi
    _bundle_pick NORM_STATS  model_full.norm_stats.json model.norm_stats.json norm_stats.json || NORM_STATS=""
    _bundle_pick FEATURES_L0 features_l0.csv                                         || FEATURES_L0=""
    _bundle_pick P_TO_NP     global_p_to_np.csv p_to_np.csv                          || exit 1
    echo "[phase_infer] model bundle: $MODEL_BUNDLE"
    echo "[phase_infer]   model       = $MODEL (picked: ${MODEL_PICK})"
    echo "[phase_infer]   norm_stats  = ${NORM_STATS:-<embedded in checkpoint>}"
    echo "[phase_infer]   features_l0 = ${FEATURES_L0:-<embedded in checkpoint>}"
    echo "[phase_infer]   p_to_np     = $P_TO_NP"
fi

miss=()
for v in DESIGN PREV_DIR PREV_BEST_RUN ARITY NEXT_LABEL \
         MODEL P_TO_NP INF_PAYLOAD_ROOT; do
    if [[ -z "${!v}" ]]; then flag="${v,,}"; miss+=("--${flag//_/-}"); fi
done
if (( ${#miss[@]} > 0 )); then
    echo "ERROR: missing required args: ${miss[*]}" >&2; usage; exit 1
fi

case "$ARITY" in
    3in) N_INPUTS_MIN=2; N_INPUTS_MAX=3 ;;
    4in) N_INPUTS_MIN=2; N_INPUTS_MAX=4 ;;
    *)   echo "ERROR: --arity must be 3in or 4in" >&2; exit 1 ;;
esac

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MINING_REPO_ROOT="${SCRIPT_DIR}/../../../tools/mining"
RUN_PIPELINE="${MINING_REPO_ROOT}/scripts/run_cell_recommend.sh"
NPN_ENUM_BIN="${MINING_REPO_ROOT}/src/logic_cluster_mining/build/npn_p_class_enum"

TAG="ni${N_INPUTS_MIN}${N_INPUTS_MAX}_k1${K1}_k2${K2}"
INF_PAYLOAD="${INF_PAYLOAD_ROOT}/designs/${DESIGN}/${PREV_BEST_RUN}"
INF_CSV="${INF_PAYLOAD}/inference_results_x2_${TAG}.csv"
INF_STAMP="${INF_CSV}.stamp"
PREV_RUN="${PREV_DIR%/}/${PREV_BEST_RUN}"

echo "[phase_infer] design=${DESIGN} arity=${ARITY} prev_run=${PREV_RUN}"
echo "[phase_infer] inf_csv=${INF_CSV}"

P_TO_NP_FULL="${INF_PAYLOAD_ROOT}/p_to_np_full.csv"
if (( ! DRY_RUN )); then
    if [[ ! -x "$NPN_ENUM_BIN" ]]; then
        echo "ERROR: npn_p_class_enum binary not executable: ${NPN_ENUM_BIN}" >&2
        exit 1
    fi
    rebuild=0
    if [[ ! -f "$P_TO_NP_FULL" ]]; then
        rebuild=1
    elif [[ "$NPN_ENUM_BIN" -nt "$P_TO_NP_FULL" ]]; then
        rebuild=1
    fi
    if (( rebuild )); then
        echo "[phase_infer] rebuilding full p_to_np map -> ${P_TO_NP_FULL}"
        mkdir -p "$INF_PAYLOAD_ROOT"
        TMP_P2NP="${P_TO_NP_FULL}.tmp.$$"
        echo "p_canonical,np_canonical,erased_input_inv,erased_output_inv" > "$TMP_P2NP"
        for N in 1 2 3 4; do
            "$NPN_ENUM_BIN" "$N" \
                || { echo "ERROR: npn_p_class_enum $N failed" >&2; rm -f "$TMP_P2NP"; exit 1; }
        done | awk -F, '
            /^p_canonical_bits,/ { next }
            {
                printf "0b%s,0b%s,%s,%s\n", $1, $2, $3, $4
            }
        ' >> "$TMP_P2NP" \
            || { echo "ERROR: p_to_np transform failed" >&2; rm -f "$TMP_P2NP"; exit 1; }
        mv -f "$TMP_P2NP" "$P_TO_NP_FULL"
        n_rows=$(tail -n +2 "$P_TO_NP_FULL" | wc -l)
        echo "[phase_infer]   rows = ${n_rows} (n=1+2+3+4)"
    else
        echo "[phase_infer] reuse cached p_to_np_full: ${P_TO_NP_FULL}"
    fi
    _GLOBAL_P2NP="${P_TO_NP}"
    ${PYTHON3:-python3} - <<PYEOF
import csv, sys
full_path   = '${P_TO_NP_FULL}'
global_path = '${_GLOBAL_P2NP}'
fieldnames  = ['p_canonical', 'np_canonical', 'erased_input_inv', 'erased_output_inv']
with open(full_path, newline='') as f:
    full_rows = list(csv.DictReader(f))
full_keys = {r['p_canonical'] for r in full_rows}
with open(global_path, newline='') as f:
    global_rows = list(csv.DictReader(f))
added = [r for r in global_rows if r['p_canonical'] not in full_keys]
if added:
    with open(full_path, 'a', newline='') as f:
        w = csv.DictWriter(f, fieldnames=fieldnames, extrasaction='ignore')
        w.writerows(added)
    print(f"[phase_infer] merged {len(added)} global_p_to_np entries (non-P-minimal canonicals)")
else:
    print("[phase_infer] global_p_to_np: no new entries to merge")
PYEOF
    merge_rc=$?
    if (( merge_rc != 0 )); then
        echo "ERROR: global_p_to_np merge into ${P_TO_NP_FULL} failed (rc=${merge_rc})" >&2
        exit 1
    fi
    P_TO_NP="$P_TO_NP_FULL"
    echo "[phase_infer]   using P_TO_NP = ${P_TO_NP} (overrides bundle)"
fi

if (( ! DRY_RUN )) && [[ -n "$SERVER" ]]; then
    ssh -o ConnectTimeout=6 "$SERVER" \
        "hostname; grep MemAvailable /proc/meminfo; nproc" >/dev/null \
        || { echo "ERROR: cannot reach server ${SERVER}" >&2; exit 1; }
    ssh "$SERVER" "test -d ${PREV_RUN}" \
        || { echo "ERROR: prev run dir not found on ${SERVER}: ${PREV_RUN}" >&2; exit 1; }
fi
if (( ! DRY_RUN )); then
    if [[ ! -d "${PREV_RUN}" ]]; then
        echo "ERROR: prev run dir not found locally: ${PREV_RUN}" >&2; exit 1
    fi
fi

if [[ ! -x "${RUN_PIPELINE}" ]]; then
    echo "ERROR: pipeline script not found or not executable: ${RUN_PIPELINE}" >&2
    exit 1
fi

_stamp_part() {
    if [[ -z "$1" ]]; then
        md5sum </dev/null | awk '{print $1}'
    elif [[ -f "$1" ]]; then
        md5sum "$1" | awk '{print $1}'
    else
        echo "missing"
    fi
}
cur_stamp="$(_stamp_part "${MODEL}")_$(_stamp_part "${FEATURES_L0}")_$(_stamp_part "${NORM_STATS}")_$(_stamp_part "${P_TO_NP}")"
if [[ -f "${INF_CSV}" && -f "${INF_STAMP}" ]]; then
    saved_stamp="$(cat "${INF_STAMP}")"
    if [[ "$cur_stamp" != "$saved_stamp" ]]; then
        echo "[phase_infer] model/features/norm_stats/p_to_np changed; invalidating cached CSV"
        rm -f "${INF_CSV}" "${INF_STAMP}"
    fi
elif [[ -f "${INF_CSV}" ]]; then
    echo "[phase_infer] cached CSV has no stamp; treating as STALE: ${INF_CSV}"
    rm -f "${INF_CSV}"
fi

if [[ "$NEXT_LABEL" == "D1" && "$ARITY" == "4in" && ( "$DESIGN" == "aes" || "$DESIGN" == "sha256" ) ]]; then
    SISTER_3IN_CSV="${INF_PAYLOAD_ROOT}/designs/${DESIGN}/${PREV_BEST_RUN}/inference_results_x2_ni23_k1${K1}_k2${K2}.csv"
    if [[ -f "$SISTER_3IN_CSV" ]]; then
        echo "[phase_infer] reuse: ${DESIGN} D1-4in == D1-3in; copy ${SISTER_3IN_CSV} -> ${INF_CSV}"
        mkdir -p "$INF_PAYLOAD"
        cp -f "$SISTER_3IN_CSV" "${INF_CSV}" \
            || { echo "ERROR: failed to copy sister 3in CSV" >&2; exit 1; }
    fi
fi

if [[ -f "${INF_CSV}" ]]; then
    echo "[phase_infer] cached CSV reused: ${INF_CSV}"
else
    echo "[phase_infer] running run_cell_recommend.sh (mine + infer)"
    if (( DRY_RUN )); then
        echo "  [DRY] would run: ${RUN_PIPELINE} --src-run ${PREV_RUN} --design ${DESIGN} ..."
    else
        mkdir -p "$INF_PAYLOAD"
        bash "${RUN_PIPELINE}" \
            --src-run     "${PREV_RUN}" \
            --design      "${DESIGN}" \
            --dest-root   "${INF_PAYLOAD_ROOT}" \
            --model       "${MODEL}" \
            --features-l0 "${FEATURES_L0}" \
            --p-to-np     "${P_TO_NP}" \
            --max-inputs  "${N_INPUTS_MAX}" \
            --k1          "${K1}" \
            --k2          "${K2}" \
            || { echo "ERROR: run_cell_recommend.sh failed" >&2; exit 1; }
        echo "$cur_stamp" > "${INF_STAMP}"
    fi
fi

if [[ ! -f "${INF_CSV}" ]] && (( ! DRY_RUN )); then
    echo "ERROR: inference CSV not produced: ${INF_CSV}" >&2
    exit 1
fi

echo "${INF_CSV}"
