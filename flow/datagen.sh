#!/usr/bin/env bash
# Portions Copyright © 2022 Synopsys, Inc. All rights reserved. Portions of
# these TCL scripts are proprietary to and owned by Synopsys, Inc. and may only
# be used for internal use by educational institutions (including United States
# government labs, research institutes and federally funded research and
# development centers) on Synopsys tools for non-profit research, development,
# instruction, and other non-commercial uses or as otherwise specifically set forth
# by written agreement with Synopsys. All other use, reproduction, modification, or
# distribution of these TCL scripts is strictly prohibited.

set -uo pipefail

usage() {
    cat <<'EOF'
Usage: datagen.sh --config <conf> [--dry-run]

Runs the D0 sweep for every design in the config to build the GATv2 training
set, then mines each finished run.

  --config <file>   required, e.g. flow/config/datagen.conf
  --dry-run         run the preflight only, launch no Fusion Compiler job
  --help|-h

The config sets OUT_ROOT, PYTHON_BIN, STAGE, DESIGNS
("<design>|<host>|<n_runs>|<jobs>[|<clock>|<area>]" per line) and the MINE_*
mining bounds. PDK_ROOT, FC_BIN (or FC_SHELL_BIN) and FC_LICENSE must be set
in the environment or in the config.
EOF
}

CONFIG=""
DRY_RUN=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)  CONFIG="$2"; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ERROR: unknown arg: $1" >&2; usage; exit 1 ;;
    esac
done
[[ -z "$CONFIG" ]] && { echo "ERROR: --config required" >&2; exit 1; }
[[ ! -f "$CONFIG" ]] && { echo "ERROR: config not found: $CONFIG" >&2; exit 1; }
source "$CONFIG"

for ev in PDK_ROOT FC_FLOW_ROOT FC_SHELL_BIN FC_LICENSE; do
    printf -v "_conf_$ev" '%s' "${!ev:-}"
done
[[ -f "$REPO/.env.local" ]] && source "$REPO/.env.local"
for ev in PDK_ROOT FC_FLOW_ROOT FC_SHELL_BIN FC_LICENSE; do
    _cv="_conf_$ev"
    [[ -n "${!_cv:-}" ]] && printf -v "$ev" '%s' "${!_cv}"
done
[[ -z "${FC_SHELL_BIN:-}" && -n "${FC_BIN:-}" ]] && FC_SHELL_BIN="$FC_BIN/fc_shell"
for ev in PDK_ROOT FC_FLOW_ROOT FC_SHELL_BIN FC_LICENSE; do
    [[ -n "${!ev:-}" ]] && export "$ev"
done
if [[ -z "${PDK_ROOT:-}" ]]; then
    echo "ERROR: set PDK_ROOT in the config or $REPO/.env.local" >&2
    exit 1
fi
if [[ -z "${FC_LICENSE:-}" ]]; then
    echo "ERROR: FC_LICENSE unset. Set it in the config or $REPO/.env.local" >&2
    exit 1
fi

for v in REPO OUT_ROOT PYTHON_BIN STAGE; do
    [[ -z "${!v:-}" ]] && { echo "ERROR: config missing $v" >&2; exit 1; }
done
SEED="${SEED:-42}"
FC_JOB_GB="${FC_JOB_GB:-25}"

[[ -z "${FC_FLOW_ROOT:-}" ]] && FC_FLOW_ROOT="$REPO/flow/scripts/fc_reference"
export FC_FLOW_ROOT
(( ${#DESIGNS[@]} == 0 )) && { echo "ERROR: DESIGNS table is empty" >&2; exit 1; }

RUN_FC="$REPO/flow/scripts/datagen/run_fc.py"

MARKER_DIR="$OUT_ROOT/.datagen_markers"
LOG_DIR="$OUT_ROOT/.datagen_logs"
mkdir -p "$MARKER_DIR" "$LOG_DIR"

FAIL=0
[[ -f "$RUN_FC" ]] || { echo "PREFLIGHT FAIL: run_fc.py not found: $RUN_FC" >&2; FAIL=1; }

declare -A SEEN_SRV=()
for spec in "${DESIGNS[@]}"; do
    IFS='|' read -r design server n_runs jobs clock area <<< "$spec"
    for f in design server n_runs jobs; do
        [[ -z "${!f}" ]] && { echo "PREFLIGHT FAIL: DESIGNS line '$spec' missing field $f" >&2; FAIL=1; }
    done
    [[ "$n_runs" =~ ^[0-9]+$ && "$jobs" =~ ^[0-9]+$ ]] \
        || { echo "PREFLIGHT FAIL: n_runs/jobs not numeric in '$spec'" >&2; FAIL=1; }
    if [[ "$server" != "local" && -z "${SEEN_SRV[$server]:-}" ]]; then
        SEEN_SRV[$server]=1
        timeout 20 ssh -o BatchMode=yes "$server" true 2>/dev/null \
            || { echo "PREFLIGHT FAIL: ssh unreachable: $server" >&2; FAIL=1; }
    fi
done

declare -A SRV_JOBS=()
for spec in "${DESIGNS[@]}"; do
    IFS='|' read -r design server n_runs jobs clock area <<< "$spec"
    SRV_JOBS[$server]=$(( ${SRV_JOBS[$server]:-0} + jobs ))
done
for server in "${!SRV_JOBS[@]}"; do
    need_gb=$(( SRV_JOBS[$server] * FC_JOB_GB * 12 / 10 ))
    if [[ "$server" == "local" ]]; then
        avail_gb=$(awk '/MemAvailable/{printf "%d", $2/1048576}' /proc/meminfo)
    else
        avail_gb=$(timeout 20 ssh "$server" "awk '/MemAvailable/{printf \"%d\", \$2/1048576}' /proc/meminfo" 2>/dev/null)
    fi
    [[ -z "$avail_gb" ]] && { echo "PREFLIGHT FAIL: cannot read MemAvailable on $server" >&2; FAIL=1; continue; }
    if (( avail_gb < need_gb )); then
        echo "PREFLIGHT FAIL: $server MemAvailable ${avail_gb}GB < required ${need_gb}GB (${SRV_JOBS[$server]} jobs x ${FC_JOB_GB}GB x1.2)" >&2
        FAIL=1
    else
        echo "[preflight] $server: ${avail_gb}GB available >= ${need_gb}GB required"
    fi
done
(( FAIL )) && { echo "PREFLIGHT FAILED — fix the errors above." >&2; exit 1; }

MINE_ARGS="--mine-min-cells ${MINE_MIN_CELLS:-2} --mine-max-cells ${MINE_MAX_CELLS:-8} \
--mine-min-inputs ${MINE_MIN_INPUTS:-2} --mine-max-inputs ${MINE_MAX_INPUTS:-4} \
--mine-max-depth ${MINE_MAX_DEPTH:-3} --mine-max-outputs ${MINE_MAX_OUTPUTS:-1} \
--mine-num-cores ${MINE_NUM_CORES:-4}"
declare -A DESIGN_SRV=() DESIGN_LOG=()
LAUNCHED=()
for spec in "${DESIGNS[@]}"; do
    IFS='|' read -r design server n_runs jobs clock area <<< "$spec"
    marker="$MARKER_DIR/.datagen_done_${design}"
    if [[ -f "$marker" ]]; then
        echo "[datagen] $design: marker exists — skipping (rm $marker to redo)"
        continue
    fi
    log="$LOG_DIR/${design}.log"
    extra=""
    [[ -n "${clock:-}" ]] && extra+=" --clock $clock"
    [[ -n "${area:-}"  ]] && extra+=" --area $area"
    envp=""
    for ev in PDK_ROOT FC_FLOW_ROOT FC_SHELL_BIN FC_LICENSE; do
        [[ -n "${!ev:-}" ]] && envp+="$ev=${!ev} "
    done
    cmd="env $envp$PYTHON_BIN $RUN_FC \
--pdk SO3 --design $design --out-root $OUT_ROOT \
--n-runs $n_runs --jobs $jobs --stage $STAGE --seed $SEED \
$MINE_ARGS$extra"
    echo "[datagen] $design @ $server: $cmd"
    (( DRY_RUN )) && continue
    if [[ "$server" == "local" ]]; then
        nohup bash -c "$cmd" > "$log" 2>&1 &
    else
        ssh "$server" "nohup bash -lc '$cmd' > $log 2>&1 & echo launched" >/dev/null \
            || { echo "ERROR: launch failed on $server for $design" >&2; exit 1; }
    fi
    DESIGN_SRV[$design]="$server"
    DESIGN_LOG[$design]="$log"
    LAUNCHED+=("$design")
done
(( DRY_RUN )) && { echo "[datagen] dry-run complete."; exit 0; }
(( ${#LAUNCHED[@]} == 0 )) && { echo "[datagen] nothing to do."; exit 0; }

alive() {
    local design="$1" server="${DESIGN_SRV[$1]}"
    local pat="run_fc.py --pdk SO3 --design $design"
    if [[ "$server" == "local" ]]; then
        pgrep -f "$pat" >/dev/null
    else
        timeout 20 ssh "$server" "/usr/bin/pgrep -f \"$pat\" >/dev/null" 2>/dev/null
    fi
}
sleep 60
for design in "${LAUNCHED[@]}"; do
    if alive "$design"; then
        echo "[health] $design: alive on ${DESIGN_SRV[$design]}"
    else
        echo "[health] $design: NOT RUNNING after 60s — log tail:" >&2
        tail -5 "${DESIGN_LOG[$design]}" >&2
    fi
done

PASS=0; FAILED=()
declare -A DONE=()
while (( ${#DONE[@]} < ${#LAUNCHED[@]} )); do
    sleep 300
    for design in "${LAUNCHED[@]}"; do
        [[ -n "${DONE[$design]:-}" ]] && continue
        alive "$design" && continue
        DONE[$design]=1
        log="${DESIGN_LOG[$design]}"
        done_line=$(grep -oE "Done: [0-9]+ succeeded, [0-9]+ failed" "$log" | tail -1)
        ok=$(sed -nE 's/.*Done: ([0-9]+) succeeded.*/\1/p' <<< "$done_line")
        fc_failed=$(sed -nE 's/.*, ([0-9]+) failed.*/\1/p' <<< "$done_line")
        if grep -q "Traceback" "$log" || [[ -z "$done_line" ]] || (( fc_failed > 0 )); then
            echo "[datagen] $design: FAILED (${ok:-0} ok, ${fc_failed:-?} failed) — tail:" >&2
            tail -8 "$log" >&2
            FAILED+=("$design")
        else
            echo "[datagen] $design: DONE ($ok runs, 0 failed)"
            touch "$MARKER_DIR/.datagen_done_${design}"
            PASS=$((PASS+1))
            mef_log="$LOG_DIR/${design}.mining_edge_features.log"
            if ! "$PYTHON_BIN" "$REPO/flow/scripts/datagen/build_mining_edge_features.py" \
                    --root "$OUT_ROOT" --designs "$design" > "$mef_log" 2>&1; then
                echo "[datagen] $design: build_mining_edge_features FAILED — see $mef_log" >&2
                tail -8 "$mef_log" >&2
            fi
        fi
    done
done

echo "════ DATAGEN DONE: $PASS ok, ${#FAILED[@]} failed ════"
if (( ${#FAILED[@]} > 0 )); then
    printf 'FAILED: %s\n' "${FAILED[@]}"
    exit 1
fi
