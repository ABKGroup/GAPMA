#!/usr/bin/env bash

set -uo pipefail

GAPMA_ROOT="${GAPMA_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}"
[ -f "$GAPMA_ROOT/.env.local" ] && source "$GAPMA_ROOT/.env.local"

usage() {
    cat <<'USAGE'
phase_sweep.sh — generate + launch FC sweep on SP&R server.

Required:
    --design        <name>
    --next-label    <D{m+1} label>
    --arity         <3in|4in>
    --new-dir       <absolute path to new run dir>
    --sweep-areas   "a1 a2 a3 ..."
    --sweep-freq    <GHz>
Optional:
    --server        <SP&R server; default runs locally, no server needed>
    --reps          <int, default 3>
    --parallel      <int, default 6>
    --wait-sweep    block until "SWEEP DONE" appears in sweep.master.log
    --dry-run
Env overrides:
    FC_BIN_DIR             FC install bin dir baked into the generated script
    SNPS_LICENSE           license server for SNPSLMD/LM_LICENSE_FILE
    SWEEP_WAIT_TIMEOUT_S   overall --wait-sweep cap in seconds (default 43200)
USAGE
}

DESIGN=""
NEXT_LABEL=""
ARITY=""
SERVER=""
NEW_DIR=""
SWEEP_AREAS=""
SWEEP_FREQ=""
REPS=3
PARALLEL=6
WAIT_SWEEP=0
DRY_RUN=0
FC_BIN_DIR="${FC_BIN_DIR:-${FC_BIN:-}}"
SNPS_LICENSE="${SNPS_LICENSE:-${FC_LICENSE:-}}"
SWEEP_WAIT_TIMEOUT_S="${SWEEP_WAIT_TIMEOUT_S:-43200}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --design)        DESIGN="$2"; shift 2 ;;
        --next-label)    NEXT_LABEL="$2"; shift 2 ;;
        --arity)         ARITY="$2"; shift 2 ;;
        --server)        SERVER="$2"; shift 2 ;;
        --new-dir)       NEW_DIR="$2"; shift 2 ;;
        --sweep-areas)   SWEEP_AREAS="$2"; shift 2 ;;
        --sweep-freq)    SWEEP_FREQ="$2"; shift 2 ;;
        --reps)          REPS="$2"; shift 2 ;;
        --parallel)      PARALLEL="$2"; shift 2 ;;
        --wait-sweep)    WAIT_SWEEP=1; shift ;;
        --dry-run)       DRY_RUN=1; shift ;;
        -h|--help)       usage; exit 0 ;;
        *) echo "ERROR: unknown arg: $1" >&2; usage; exit 1 ;;
    esac
done

miss=()
for v in DESIGN NEXT_LABEL ARITY NEW_DIR SWEEP_AREAS SWEEP_FREQ; do
    [[ -z "${!v}" ]] && miss+=("--${v,,}")
done
if (( ${#miss[@]} > 0 )); then
    echo "ERROR: phase_sweep missing required args: ${miss[*]}" >&2; exit 1
fi

read -ra _AREAS_CHK <<< "$SWEEP_AREAS"
read -ra _FREQS_CHK <<< "$SWEEP_FREQ"
if (( ${#_FREQS_CHK[@]} == 1 && ${#_AREAS_CHK[@]} > 1 )); then
    echo "[phase_sweep] broadcasting single --sweep-freq ${_FREQS_CHK[0]} to all ${#_AREAS_CHK[@]} area points"
    SWEEP_FREQ=""
    for _a in "${_AREAS_CHK[@]}"; do SWEEP_FREQ+="${_FREQS_CHK[0]} "; done
    SWEEP_FREQ="${SWEEP_FREQ% }"
    read -ra _FREQS_CHK <<< "$SWEEP_FREQ"
fi
if (( ${#_AREAS_CHK[@]} != ${#_FREQS_CHK[@]} )); then
    echo "ERROR: phase_sweep --sweep-areas (${#_AREAS_CHK[@]} pts) and --sweep-freq (${#_FREQS_CHK[@]} pts) length mismatch; area[i] is paired with freq[i], lengths must be equal (a single freq broadcasts automatically)" >&2
    exit 1
fi

SWEEP_NAME="run_${NEXT_LABEL,,}_${ARITY}_sweep.sh"
SWEEP_LOCAL="$(mktemp -p "${HOME}" sweep.XXXXXX.sh)"
trap 'rm -f "${SWEEP_LOCAL}"' EXIT

cat > "${SWEEP_LOCAL}" <<EOF
#!/usr/bin/env bash
set -uo pipefail
SCRIPT_DIR="\$(cd "\$(dirname "\$0")" && pwd)"
cd "\$SCRIPT_DIR"
FC_BIN_DIR="${FC_BIN_DIR}"
SNPS_LICENSE="${SNPS_LICENSE}"
export PATH="\${FC_BIN_DIR}:\$PATH"
export SNPSLMD_LICENSE_FILE="\${SNPS_LICENSE}"
export LM_LICENSE_FILE="\${SNPS_LICENSE}"
export RTL_ROOT="${RTL_ROOT:-$GAPMA_ROOT/rtl}"
export PDK_DIR="${PDK_DIR:-$(dirname "${PDK_ROOT:-$GAPMA_ROOT/pdk/SO3}")}"
PARALLEL=\${1:-${PARALLEL}}
echo "[sweep] host=\$(hostname) parallel=\$PARALLEL start=\$(date)"
mkdir -p sweep_logs
: > sweep_logs/ok.list
: > sweep_logs/fail.list
: > sweep_logs/skipped.list
run_one() {
    local area=\$1 freq=\$2 rep=\$3
    local run_dir="\${area}_\${freq}_\${rep}"
    local log="\${SCRIPT_DIR}/sweep_logs/\${run_dir}.log"
    if ! mkdir "\${SCRIPT_DIR}/\${run_dir}"; then
        echo "[SKIP EXISTING] \${run_dir} already exists, not re-running (mkdir error above says why)"
        echo "\${run_dir}" >> "\${SCRIPT_DIR}/sweep_logs/skipped.list"
        return 0
    fi
    echo "[START] \${run_dir} on \$(hostname) at \$(date +%T)" | tee "\${log}"
    cp -rf "\${SCRIPT_DIR}/initial/." "\${SCRIPT_DIR}/\${run_dir}/"
    cd "\${SCRIPT_DIR}/\${run_dir}"
    if [[ -f data/cell_names.txt ]]; then
        CELL_NAMES="\$(tr '\n' ' ' < data/cell_names.txt)"
        export CELL_NAMES
    else
        echo "[WARN] \${run_dir}: data/cell_names.txt not found, CELL_NAMES unset -- make setup will load the full PDK library" | tee -a "\${log}"
    fi
    if [[ -f data/dont_use.tcl && ! -f filters/dont_use.tcl ]]; then
        mkdir -p filters
        cp data/dont_use.tcl filters/dont_use.tcl
    fi
    if [[ ! -f filters/dont_use.tcl ]]; then
        echo "[DONE fail setup rc=90] \${run_dir} \$(date +%T): no dont_use.tcl to stage at filters/ -- refusing to run unfiltered" | tee -a "\${log}"
        echo "stage=setup rc=90 no-dont_use \$(date +%T)" > "\${SCRIPT_DIR}/\${run_dir}/FAILED"
        echo "\${run_dir}" >> "\${SCRIPT_DIR}/sweep_logs/fail.list"
        return 0
    fi
    RUN_AREA="\${area}" RUN_FREQ_GHZ="\${freq}" make setup     > nohup_setup.log 2>&1
    rc=\$?
    if [[ \$rc -ne 0 ]]; then
        echo "[DONE fail setup rc=\$rc] \${run_dir} \$(date +%T)" | tee -a "\${log}"
        echo "stage=setup rc=\$rc \$(date +%T)" > "\${SCRIPT_DIR}/\${run_dir}/FAILED"
        echo "\${run_dir}" >> "\${SCRIPT_DIR}/sweep_logs/fail.list"
        return 0
    fi
    RUN_AREA="\${area}" RUN_FREQ_GHZ="\${freq}" make route_opt > nohup_route_opt.log 2>&1
    rc=\$?
    if [[ \$rc -eq 0 ]]; then
        echo "[DONE ok]   \${run_dir} \$(date +%T)" | tee -a "\${log}"
        echo "\${run_dir}" >> "\${SCRIPT_DIR}/sweep_logs/ok.list"
    else
        echo "[DONE fail route_opt rc=\$rc] \${run_dir} \$(date +%T)" | tee -a "\${log}"
        echo "stage=route_opt rc=\$rc \$(date +%T)" > "\${SCRIPT_DIR}/\${run_dir}/FAILED"
        echo "\${run_dir}" >> "\${SCRIPT_DIR}/sweep_logs/fail.list"
    fi
}
export SCRIPT_DIR
export -f run_one

AREAS_ARR=(${SWEEP_AREAS})
FREQS_ARR=(${SWEEP_FREQ})
PAIRS=\$(for idx in \$(seq 0 \$(( \${#AREAS_ARR[@]} - 1 )) ); do
    for rep in \$(seq 1 ${REPS}); do
        echo "\${AREAS_ARR[\$idx]} \${FREQS_ARR[\$idx]} \${rep}"
    done
done)
echo "\$PAIRS" | xargs -P "\${PARALLEL}" -L 1 bash -c 'run_one "\$@"' _
n_ok=\$(wc -l < "\${SCRIPT_DIR}/sweep_logs/ok.list")
n_fail=\$(wc -l < "\${SCRIPT_DIR}/sweep_logs/fail.list")
n_skip=\$(wc -l < "\${SCRIPT_DIR}/sweep_logs/skipped.list")
echo "SWEEP DONE: \${n_ok} ok, \${n_fail} failed, \${n_skip} skipped"
echo "=== SWEEP DONE \$(hostname) \$(date +%T) ==="
if (( n_fail > 0 )); then
    echo "=== SWEEP FAILURES: \${n_fail} ==="
fi
EOF
chmod +x "${SWEEP_LOCAL}"

if [[ -n "$SERVER" ]]; then
    if (( DRY_RUN )); then
        echo "[phase_sweep] [DRY] would scp ${SWEEP_NAME} to ${SERVER}:${NEW_DIR} and launch"
        exit 0
    fi

    scp -q "${SWEEP_LOCAL}" "${SERVER}:${NEW_DIR}/${SWEEP_NAME}" \
        || { echo "ERROR: scp sweep script failed" >&2; exit 1; }
    ssh "$SERVER" "chmod +x '${NEW_DIR}/${SWEEP_NAME}'" \
        || { echo "ERROR: chmod on remote failed" >&2; exit 1; }
    echo "[phase_sweep] sweep script pushed: ${SERVER}:${NEW_DIR}/${SWEEP_NAME}"

    echo "[phase_sweep] launching sweep on ${SERVER} (parallel=${PARALLEL})"
    ssh "$SERVER" "
        cd '${NEW_DIR}' || exit 1
        nohup bash '${SWEEP_NAME}' ${PARALLEL} > sweep.master.log 2>&1 &
        echo \$! > '${NEW_DIR}/sweep.master.pid'
        sleep 4
    " || { echo "ERROR: sweep launch failed" >&2; exit 1; }

    sleep 2
    ssh "$SERVER" "
        echo '===master==='
        pgrep -af '${SWEEP_NAME}' | head -3
        echo '===master log==='
        head -8 '${NEW_DIR}/sweep.master.log'
        echo '===dirs==='
        ls '${NEW_DIR}' | grep -E '^[0-9]+_' | head
    "

    if (( WAIT_SWEEP )); then
        echo "[phase_sweep] waiting until SWEEP DONE marker appears (cap ${SWEEP_WAIT_TIMEOUT_S}s, liveness via sweep.master.pid)"
        ssh "$SERVER" "
            deadline=\$(( \$(date +%s) + ${SWEEP_WAIT_TIMEOUT_S} ))
            until grep -q 'SWEEP DONE' '${NEW_DIR}/sweep.master.log'; do
                if [ \$(date +%s) -ge \$deadline ]; then
                    echo 'ERROR: SWEEP DONE marker not seen within ${SWEEP_WAIT_TIMEOUT_S}s in ${NEW_DIR}/sweep.master.log' >&2
                    exit 2
                fi
                pid=\$(cat '${NEW_DIR}/sweep.master.pid' 2>/dev/null)
                if [ -z \"\$pid\" ] || ! kill -0 \"\$pid\" 2>/dev/null; then
                    sleep 2
                    grep -q 'SWEEP DONE' '${NEW_DIR}/sweep.master.log' && break
                    echo \"ERROR: sweep master (pid=\${pid:-unknown from sweep.master.pid}) is gone and SWEEP DONE marker absent\" >&2
                    exit 3
                fi
                sleep 30
            done
            tail -5 '${NEW_DIR}/sweep.master.log'
        " || { echo "ERROR: --wait-sweep failed on ${SERVER}:${NEW_DIR} (timeout or dead master, see message above)" >&2; exit 1; }
    fi

    echo "[phase_sweep] complete: ${SERVER}:${NEW_DIR}"
else
    if (( DRY_RUN )); then
        echo "[phase_sweep] [DRY] would run ${SWEEP_NAME} locally in ${NEW_DIR}"
        exit 0
    fi

    mkdir -p "${NEW_DIR}"
    cp "${SWEEP_LOCAL}" "${NEW_DIR}/${SWEEP_NAME}"
    chmod +x "${NEW_DIR}/${SWEEP_NAME}"
    echo "[phase_sweep] sweep script staged locally: ${NEW_DIR}/${SWEEP_NAME}"

    echo "[phase_sweep] launching sweep locally (parallel=${PARALLEL})"
    ( cd "${NEW_DIR}" && \
      nohup bash "${SWEEP_NAME}" "${PARALLEL}" > sweep.master.log 2>&1 &
      echo $! > "${NEW_DIR}/sweep.master.pid" )
    sleep 4

    sleep 2
    echo "===master==="
    pgrep -af "${SWEEP_NAME}" | head -3
    echo "===master log==="
    head -8 "${NEW_DIR}/sweep.master.log" 2>/dev/null
    echo "===dirs==="
    ls "${NEW_DIR}" | grep -E '^[0-9]+_' | head

    if (( WAIT_SWEEP )); then
        echo "[phase_sweep] waiting until SWEEP DONE marker appears (cap ${SWEEP_WAIT_TIMEOUT_S}s, liveness via sweep.master.pid)"
        deadline=$(( $(date +%s) + SWEEP_WAIT_TIMEOUT_S ))
        until grep -q 'SWEEP DONE' "${NEW_DIR}/sweep.master.log" 2>/dev/null; do
            if [ "$(date +%s)" -ge "$deadline" ]; then
                echo "ERROR: SWEEP DONE marker not seen within ${SWEEP_WAIT_TIMEOUT_S}s in ${NEW_DIR}/sweep.master.log" >&2
                exit 2
            fi
            pid=$(cat "${NEW_DIR}/sweep.master.pid" 2>/dev/null)
            if [ -z "$pid" ] || ! kill -0 "$pid" 2>/dev/null; then
                sleep 2
                grep -q 'SWEEP DONE' "${NEW_DIR}/sweep.master.log" 2>/dev/null && break
                echo "ERROR: sweep master (pid=${pid:-unknown}) is gone and SWEEP DONE marker absent" >&2
                exit 3
            fi
            sleep 30
        done
        tail -5 "${NEW_DIR}/sweep.master.log"
    fi

    echo "[phase_sweep] complete: ${NEW_DIR}"
fi
