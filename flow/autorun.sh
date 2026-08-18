#!/usr/bin/env bash

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
PIPELINE_DIR="$SCRIPT_DIR/scripts/iterate"

CONFIG=""
DRY_RUN=0
ONLY=""
FORCE=0

usage() {
    cat <<'EOF'
Usage: autorun.sh --config <conf> [--only <combo>] [--dry-run] [--force]

Drives every design/arity combination in the config from D0 to LOOP_UNTIL,
generating D0 with gen_d0.sh and then handing each combination to
scripts/iterate/loop.sh.

  --config <file>   required, e.g. flow/config/autorun.conf
  --only <combo>    run one combination only, named "<design>_<arity>"
  --dry-run         print what each stage would run, launch nothing
  --force           overwrite existing run directories
  --help|-h

The config sets COMBOS plus MODEL, NORM_STATS, P_TO_NP, FC_ROOT, LOOP_UNTIL,
CELLGEN_TR_MAX, CELLGEN_TIMEOUT, MAX_RANK_TRIES, REPS, SWEEP_PARALLEL and
CELLGEN_OUTPUT_DIR. PDK_ROOT must be set in the environment.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)  CONFIG="$2"; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        --only)    ONLY="$2"; shift 2 ;;
        --force)   FORCE=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ERROR: unknown arg: $1" >&2; usage; exit 1 ;;
    esac
done

[[ -z "$CONFIG" ]] && { echo "ERROR: --config required (see config/autorun.conf)" >&2; exit 1; }
[[ ! -f "$CONFIG" ]] && { echo "ERROR: config not found: $CONFIG" >&2; exit 1; }

[[ -f "$REPO/.env.local" ]] && source "$REPO/.env.local"
if [[ -z "${PDK_ROOT:-}" ]]; then
    echo "ERROR: PDK_ROOT unset and $REPO/.env.local did not provide it." >&2
    exit 1
fi
export PDK_ROOT

source "$CONFIG"

for v in P_TO_NP FC_ROOT K1 K2 LOOP_UNTIL \
         X2_RATIO PRED_X1_FLOOR CELLGEN_TR_MAX CELLGEN_TIMEOUT MAX_RANK_TRIES \
         REPS SWEEP_PARALLEL; do
    [[ -z "${!v:-}" ]] && { echo "ERROR: config missing required variable: $v" >&2; exit 1; }
done
[[ "${#COMBOS[@]}" -eq 0 ]] && { echo "ERROR: config COMBOS table is empty" >&2; exit 1; }

TS="$(date +%Y%m%d_%H%M%S)"
LOG_ROOT="$FC_ROOT/autorun_logs"
mkdir -p "$FC_ROOT" "$LOG_ROOT"
MASTER_LOG="$LOG_ROOT/autorun_${TS}.log"
say() { echo "[autorun] $*" | tee -a "$MASTER_LOG"; }
die() { echo "[autorun] ERROR: $*" | tee -a "$MASTER_LOG" >&2; exit 1; }

say "config=$CONFIG  dry_run=$DRY_RUN  combos=${#COMBOS[@]}  log=$MASTER_LOG"

PREFLIGHT_FAIL=0
pf() { say "PREFLIGHT FAIL: $*"; PREFLIGHT_FAIL=1; }

if [[ -z "$MODEL" ]]; then
    BUNDLE="$REPO/data/GATv2"
    _current="$BUNDLE/model_current.pt"
    _pretrained="$BUNDLE/model_pretrained.pt"
    if [[ -f "$_current" ]] && head -c 4 "$_current" | grep -q 'PK'; then
        MODEL="$_current"
        say "model resolved to the locally trained checkpoint: $MODEL"
    else
        MODEL="$_pretrained"
        say "no locally trained model_current.pt, using the committed model: $MODEL"
    fi
    [[ -f "$MODEL" ]] || pf "no usable GATv2 checkpoint: neither $_current nor $_pretrained resolves. Train via flow/train_gat.sh or set MODEL in the config"
fi
[[ -n "$MODEL" && ! -f "$MODEL" ]] && pf "MODEL not found: $MODEL"

if [[ "$NORM_STATS" == "auto" ]]; then
    [[ -f "${MODEL:-/nonexistent}" ]] || pf "MODEL not found for norm_stats check: $MODEL"
    if ! python3.9 - "$MODEL" <<'PY' 2>>"$MASTER_LOG"
import sys, torch
ck = torch.load(sys.argv[1], map_location="cpu", weights_only=False)
sys.exit(0 if (isinstance(ck, dict) and ck.get("norm_stats")) else 1)
PY
    then pf "checkpoint has no embedded norm_stats: $MODEL. Retrain with current code, or set NORM_STATS to a legacy JSON path"; fi
    NORM_STATS=""
fi
[[ -n "$NORM_STATS" && ! -f "$NORM_STATS" ]] && pf "NORM_STATS not found: $NORM_STATS"

[[ -n "$FEATURES_L0" && ! -f "$FEATURES_L0" ]] && pf "FEATURES_L0 not found: $FEATURES_L0"
[[ ! -f "$P_TO_NP" ]] && pf "P_TO_NP not found: $P_TO_NP"

MINING="$REPO/tools/mining"
for b in "$MINING/src/replacement_scoring/build/canonical_to_cdl" \
         "$MINING/db/build/lib2cdb" \
         "$MINING/db/build/design2cdb" \
         "$MINING/src/logic_cluster_mining/build/logic_func_minor_cpp"; do
    [[ ! -x "$b" ]] && pf "required binary missing (run tools/mining/build.sh): $b"
done

GEN_CDB="$MINING/scripts/gen_fc_cdb_macros.sh"
if [[ -x "$GEN_CDB" || -f "$GEN_CDB" ]]; then
    if (( DRY_RUN )); then
        say "dry-run: would regenerate fc_cdb_macros.txt via $GEN_CDB"
    else
        bash "$GEN_CDB" --pdk-lib-dir "$PDK_ROOT" \
            >>"$MASTER_LOG" 2>&1 || pf "gen_fc_cdb_macros.sh failed (see $MASTER_LOG)"
        say "fc_cdb_macros.txt regenerated ($(wc -l < "$REPO/flow/runs/fc_cdb_macros.txt") macros)"
    fi
else
    pf "producer missing: $GEN_CDB"
fi

if (( ! DRY_RUN )); then
    GRB_HOME="${GUROBI_HOME:-}"
    if [[ -x "$GRB_HOME/bin/gurobi_cl" ]]; then
        if ! LD_LIBRARY_PATH="$GRB_HOME/lib" timeout 40 "$GRB_HOME/bin/gurobi_cl" --license 2>&1 \
             | tee -a "$MASTER_LOG" | grep -q "Academic license\|Set parameter TokenServer"; then
            pf "Gurobi license probe failed — renew before an unattended run (cell-gen STEP1 needs it). GRB_LICENSE_FILE=${GRB_LICENSE_FILE:-unset}"
        elif LD_LIBRARY_PATH="$GRB_HOME/lib" timeout 40 "$GRB_HOME/bin/gurobi_cl" --license 2>&1 | grep -q "10009"; then
            pf "Gurobi license EXPIRED (Error 10009) — renew at portal.gurobi.com before an unattended run"
        else
            say "gurobi license OK"
        fi
    else
        say "WARN: gurobi_cl not found at $GRB_HOME — cannot pre-verify license (cell-gen may fail mid-run)"
    fi
fi

declare -A SERVER_SEEN=()
declare -a VALID_COMBOS=()
for line in "${COMBOS[@]}"; do
    IFS='|' read -r design arity d0_dir d0_best spr_server areas freq clock_guess <<< "$line"
    key="${design}_${arity}"
    if [[ -n "$ONLY" ]] && ! echo ",$ONLY," | grep -qF ",$key,"; then
        say "skip (not in --only): $key"; continue
    fi
    for f in design arity d0_dir; do
        [[ -z "${!f}" ]] && pf "$key: combo field '$f' empty in config line: $line"
    done
    [[ -z "$d0_best" && -z "$clock_guess" ]] && \
        pf "$key: combo gives neither d0_best_run nor clock_guess, so D0 can be neither located nor generated: $line"
    if [[ -z "$d0_best" || ! -d "$d0_dir/$d0_best" ]]; then
        if [[ -n "$clock_guess" ]]; then
            if (( DRY_RUN )); then
                say "$key: dry-run — would generate D0 via gen_d0.sh (clock_guess=${clock_guess}ns, out_root=${d0_dir})"
            else
                say "$key: D0 missing, generating via gen_d0.sh (this runs real FC SP&R, see $LOG_ROOT/${key}_d0gen_${TS}.log)"
                if bash "$PIPELINE_DIR/gen_d0.sh" \
                        --design "$design" \
                        --clock-guess "$clock_guess" \
                        --out-root "$d0_dir" \
                        --reps "$REPS" --parallel "$SWEEP_PARALLEL" \
                        > "$LOG_ROOT/${key}_d0gen_${TS}.log" 2>&1; then
                    d0_best="$(grep '^\[gen_d0\] autorun.conf COMBOS entry:' "$LOG_ROOT/${key}_d0gen_${TS}.log" | sed -E 's/.*d0_best_run=//')"
                    [[ -z "$d0_best" ]] && pf "$key: gen_d0.sh reported success but no d0_best_run could be parsed from $LOG_ROOT/${key}_d0gen_${TS}.log"
                    say "$key: D0 generated: $d0_dir/$d0_best"
                else
                    pf "$key: gen_d0.sh failed — see $LOG_ROOT/${key}_d0gen_${TS}.log"
                fi
            fi
        else
            pf "$key: D0 run missing: $d0_dir/$d0_best, and combo line has no clock_guess to auto-generate it (see config/autorun.conf COMBOS format)"
        fi
    fi
    [[ -d "$d0_dir/$d0_best" && ! -f "$d0_dir/$d0_best/qor.csv" ]] && pf "$key: D0 run has no qor.csv: $d0_dir/$d0_best"

    if [[ -n "$d0_best" ]]; then
        d0_area="${d0_best%%_*}"
        d0_freq="${d0_best#*_}"; d0_freq="${d0_freq%_*}"
        if [[ -z "$freq" ]]; then
            freq="$d0_freq"
            say "$key: sweep_freq not given, derived from D0: ${freq} GHz"
        fi
        if [[ -z "$areas" ]]; then
            if [[ "$d0_area" =~ ^[0-9]+$ ]]; then
                areas="$(awk -v a="$d0_area" 'BEGIN{
                    for (i = 0; i < 5; i++) {
                        v = int(a * (1.05 - 0.05 * i) + 0.5)
                        printf (i ? " %d" : "%d"), v
                    }
                }')"
                say "$key: sweep_areas not given, derived from D0 area ${d0_area}um^2: $areas"
            else
                pf "$key: cannot derive sweep_areas, D0 run name '$d0_best' does not start with an integer area"
            fi
        fi
    fi
    if (( DRY_RUN )) && [[ -z "$d0_best" ]]; then
        say "$key: dry-run — d0_best_run/sweep_areas/sweep_freq will be derived from the generated D0 on a real run"
    else
        for f in d0_best areas freq; do
            [[ -z "${!f}" ]] && pf "$key: '$f' still empty after D0 resolution: $line"
        done
    fi
    if [[ -n "$spr_server" && -z "${SERVER_SEEN[$spr_server]:-}" ]]; then
        SERVER_SEEN[$spr_server]=1
        if (( DRY_RUN )); then
            say "dry-run: would probe ssh reachability of '$spr_server'"
        elif ! timeout 25 ssh -o ConnectTimeout=10 -o BatchMode=yes "$spr_server" true 2>>"$MASTER_LOG"; then
            pf "SP&R server unreachable via ssh: $spr_server"
        fi
    fi
    VALID_COMBOS+=("${design}|${arity}|${d0_dir}|${d0_best}|${spr_server}|${areas}|${freq}")
done

(( ${#VALID_COMBOS[@]} == 0 )) && pf "no combos selected (check --only / config COMBOS)"
(( PREFLIGHT_FAIL )) && die "preflight failed — fix every item above, then re-run. Nothing was launched."
say "PREFLIGHT OK: ${#VALID_COMBOS[@]} combo(s), model=$MODEL"

run_combo() {
    local line="$1"
    local design arity d0_dir d0_best spr_server areas freq
    IFS='|' read -r design arity d0_dir d0_best spr_server areas freq <<< "$line"
    local key="${design}_${arity}"
    local marker="$FC_ROOT/.autorun_done_${key}_${LOOP_UNTIL}"
    local clog="$LOG_ROOT/${key}_${TS}.log"

    if [[ -f "$marker" && $FORCE -eq 0 ]]; then
        say "$key: DONE marker exists ($marker) — skip (--force to redo)"
        return 0
    fi

    local cmd=( bash "$PIPELINE_DIR/loop.sh"
        --design "$design" --arity "$arity"
        --d0-dir "$d0_dir" --d0-best-run "$d0_best"
        --loop-until "$LOOP_UNTIL" --fc-root "$FC_ROOT"
        --server "$spr_server"
        --sweep-areas "$areas" --sweep-freq "$freq"
        --model "$MODEL" --norm-stats "$NORM_STATS"
        --features-l0 "$FEATURES_L0" --p-to-np "$P_TO_NP"
        --k1 "$K1" --k2 "$K2" --x2-ratio "$X2_RATIO"
        --pred-x1-floor "$PRED_X1_FLOOR"
        --cellgen-tr-max "$CELLGEN_TR_MAX" --cellgen-timeout "$CELLGEN_TIMEOUT"
        --max-rank-tries "$MAX_RANK_TRIES"
        --reps "$REPS" --parallel "$SWEEP_PARALLEL"
    )
    [[ -n "${CELLGEN_OUTPUT_DIR:-}" ]] && cmd+=( --cellgen-output-dir "$CELLGEN_OUTPUT_DIR" )
    (( FORCE )) && cmd+=( --force )

    if (( DRY_RUN )); then
        say "$key: DRY-RUN — would execute:"
        printf '    %q ' "${cmd[@]}" | tee -a "$MASTER_LOG"; echo | tee -a "$MASTER_LOG"
        return 0
    fi

    say "$key: launching loop -> $LOOP_UNTIL (log: $clog)"
    if "${cmd[@]}" >"$clog" 2>&1; then
        touch "$marker"
        say "$key: COMPLETE ($LOOP_UNTIL reached)"
        return 0
    else
        say "$key: FAILED — tail of $clog:"
        tail -5 "$clog" | sed 's/^/    /' | tee -a "$MASTER_LOG"
        return 1
    fi
}

FAILED=(); DONE=()
if (( ${SERVER_PARALLEL:-0} )); then
    declare -A SRV_PIDS=()
    declare -A GROUPS=()
    for line in "${VALID_COMBOS[@]}"; do
        srv="$(echo "$line" | cut -d'|' -f5)"
        GROUPS[$srv]+="${line}"$'\n'
    done
    for srv in "${!GROUPS[@]}"; do
        (
            while IFS= read -r l; do [[ -n "$l" ]] && run_combo "$l"; done <<< "${GROUPS[$srv]}"
        ) &
        SRV_PIDS[$srv]=$!
    done
    for srv in "${!SRV_PIDS[@]}"; do
        wait "${SRV_PIDS[$srv]}" || FAILED+=("server-group:$srv")
    done
else
    for line in "${VALID_COMBOS[@]}"; do
        key="$(echo "$line" | cut -d'|' -f1)_$(echo "$line" | cut -d'|' -f2)"
        if run_combo "$line"; then DONE+=("$key"); else FAILED+=("$key"); fi
    done
fi

say "================ SUMMARY ================"
say "completed: ${#DONE[@]} — ${DONE[*]:-none}"
say "failed   : ${#FAILED[@]} — ${FAILED[*]:-none}"
say "master log: $MASTER_LOG"
(( ${#FAILED[@]} > 0 )) && exit 1
exit 0
