#!/usr/bin/env bash

set -uo pipefail

usage() {
    cat <<'USAGE'
phase_setup.sh — dont_use.tcl build + run dir staging.

Cell list comes from STDIN (one cell per line).

Required:
    --design        <name; cosmetic, for header text>
    --prev-dir      <path to fc_D{m}/<design>_D{m}_<arity>/>
    --next-label    <D{m+1} label; cosmetic, for header text>
    --arity         <3in|4in; cosmetic, for header text>
    --new-dir       <path for the new run dir>
    --x2-ratio      <float; cosmetic, for header text>
Optional:
    --server        <SP&R server; default runs locally, no server needed>
    --force         overwrite existing new-dir setup
    --dry-run
USAGE
}

DESIGN=""
PREV_DIR=""
NEXT_LABEL=""
ARITY=""
SERVER=""
NEW_DIR=""
X2_RATIO="0.10"
FORCE=0
DRY_RUN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --design)      DESIGN="$2"; shift 2 ;;
        --prev-dir)    PREV_DIR="$2"; shift 2 ;;
        --next-label)  NEXT_LABEL="$2"; shift 2 ;;
        --arity)       ARITY="$2"; shift 2 ;;
        --server)      SERVER="$2"; shift 2 ;;
        --new-dir)     NEW_DIR="$2"; shift 2 ;;
        --x2-ratio)    X2_RATIO="$2"; shift 2 ;;
        --force)       FORCE=1; shift ;;
        --dry-run)     DRY_RUN=1; shift ;;
        -h|--help)     usage; exit 0 ;;
        *) echo "ERROR: unknown arg: $1" >&2; usage; exit 1 ;;
    esac
done

miss=()
for v in DESIGN PREV_DIR NEXT_LABEL ARITY NEW_DIR; do
    [[ -z "${!v}" ]] && miss+=("--${v,,}")
done
if (( ${#miss[@]} > 0 )); then
    echo "ERROR: phase_setup missing required args: ${miss[*]}" >&2; exit 1
fi

mapfile -t CELL_LIST
if (( ${#CELL_LIST[@]} == 0 )); then
    echo "ERROR: phase_setup got an empty cell list on stdin" >&2; exit 1
fi
for c in "${CELL_LIST[@]}"; do
    if [[ -z "$c" ]]; then
        echo "ERROR: empty cell name in stdin list" >&2; exit 1
    fi
done

if [[ -z "$SERVER" && ! -e "${PREV_DIR%/}/initial" && -f "${PREV_DIR%/}/filters/dont_use.tcl" ]]; then
    mkdir -p "${PREV_DIR%/}/initial/data"
    cp "${PREV_DIR%/}/filters/dont_use.tcl" "${PREV_DIR%/}/initial/data/dont_use.tcl"
    for f in "${PREV_DIR%/}"/sdc/*.sdc; do
        [[ -f "$f" ]] && cp "$f" "${PREV_DIR%/}/initial/data/$(basename "$f")"
    done
    for src in Makefile scripts configs; do
        if [[ ! -e "${PREV_DIR%/}/${src}" ]]; then
            echo "ERROR: cannot synthesize initial/ from ${PREV_DIR%/}: '${src}' is missing." >&2
            echo "       Without it the staged run dir has no FC flow template and" >&2
            echo "       'make setup' fails with \"No rule to make target 'setup'\"." >&2
            exit 1
        fi
        cp -r "${PREV_DIR%/}/${src}" "${PREV_DIR%/}/initial/${src}"
    done
    echo "[phase_setup] synthesized ${PREV_DIR%/}/initial/{data,Makefile,scripts,configs} from D0 baseline layout"
fi

PREV_DU_REL="initial/data/dont_use.tcl"
NEW_DU_LOCAL="$(mktemp -p "${HOME}" du.XXXXXX)"
REMOTE_DU_LOCAL="$(mktemp -p "${HOME}" du_remote.XXXXXX)"
trap 'rm -f "${NEW_DU_LOCAL}" "${REMOTE_DU_LOCAL}"' EXIT

if (( DRY_RUN )); then
    echo "[phase_setup] [DRY] would scp prev dont_use.tcl, append ${#CELL_LIST[@]} cells, push to ${NEW_DIR}"
    printf '%s\n' "${CELL_LIST[@]}"
    exit 0
fi

if [[ -n "$SERVER" ]]; then
    ssh "$SERVER" "cat ${PREV_DIR%/}/${PREV_DU_REL}" > "${NEW_DU_LOCAL}" \
        || { echo "ERROR: cannot read prev dont_use.tcl from server" >&2; exit 1; }
else
    cat "${PREV_DIR%/}/${PREV_DU_REL}" > "${NEW_DU_LOCAL}" \
        || { echo "ERROR: cannot read prev dont_use.tcl at ${PREV_DIR%/}/${PREV_DU_REL}" >&2; exit 1; }
fi

{
    echo ""
    echo "# L${NEXT_LABEL#D}-${ARITY} cells: ${DESIGN} ${NEXT_LABEL}-${ARITY} (phase_setup)"
    echo "# X1 always; X2 included only when pred_x2/pred_x1 > ${X2_RATIO}"
    for c in "${CELL_LIST[@]}"; do
        echo "set_lib_cell_purpose */${c} -include all"
    done

    echo ""
    echo "# --- explicit blacklist + library census (phase_setup) ---"
    echo "set _allow [get_lib_cells -quiet {*/__none__}]"
    echo "foreach _p [list \\"
    for c in "${CELL_LIST[@]}"; do echo "    ${c} \\"; done
    echo "  ] {"
    echo "    set _allow [add_to_collection \$_allow [get_lib_cells -quiet */\$_p]]"
    echo "}"
    echo "set _all  [get_lib_cells */*]"
    echo "set _deny [remove_from_collection \$_all \$_allow]"
    echo "if {[sizeof_collection \$_deny] > 0} { set_lib_cell_purpose -exclude all \$_deny }"
    echo 'puts "RM-census: loaded=[sizeof_collection $_all] allowed=[sizeof_collection $_allow] denied=[sizeof_collection $_deny]"'
    echo "set _nolog {}"
    echo "foreach_in_collection _c \$_all {"
    echo "    set _nm [get_object_name \$_c]"
    echo "    if {[get_attribute -quiet \$_c design_type] eq \"\"} { lappend _nolog \$_nm }"
    echo "}"
    echo 'puts "RM-census-nolog: [llength $_nolog]"'
    echo "if {[llength \$_nolog] > 0} {"
    echo "    foreach _nm \$_nolog {"
    echo '        puts "RM-error: physical-only lib cell with no logical model: $_nm"'
    echo "    }"
    echo '    error "RM-error: [llength $_nolog] lib cell(s) loaded with no logical model -- lef/db set mismatch, aborting"'
    echo "}"
} >> "${NEW_DU_LOCAL}"

n_inc=$(grep -c -- "-include" "${NEW_DU_LOCAL}")
echo "[phase_setup] built dont_use.tcl: ${n_inc} -include lines (added ${#CELL_LIST[@]} new cells)"
echo "[phase_setup] + explicit blacklist of the complement + library census gate"

CELL_NAMES_LOCAL="$(mktemp -p "${HOME}" cellnames.XXXXXX)"
trap 'rm -f "${NEW_DU_LOCAL}" "${REMOTE_DU_LOCAL}" "${CELL_NAMES_LOCAL}"' EXIT
{
    grep -oE '^set_lib_cell_purpose \*/[A-Za-z0-9_]+ -include all' "${NEW_DU_LOCAL}" \
        | sed -E 's#^set_lib_cell_purpose \*/([A-Za-z0-9_]+) -include all#\1#'
    grep -oE '^set _luc \[get_lib_cells -quiet \{\*/[A-Za-z0-9_]+\}\]' "${NEW_DU_LOCAL}" \
        | sed -E 's#.*\{\*/([A-Za-z0-9_]+)\}.*#\1#'
} | grep -vx '__none__' | sort -u > "${CELL_NAMES_LOCAL}"
n_cn=$(wc -l < "${CELL_NAMES_LOCAL}")
if (( n_cn < ${#CELL_LIST[@]} )); then
    echo "ERROR: cell_names.txt has ${n_cn} cells, fewer than the ${#CELL_LIST[@]} just added; dont_use.tcl parse is wrong" >&2
    exit 1
fi
echo "[phase_setup] cell_names.txt: ${n_cn} cells (baseline + candidates)"

PREV_INITIAL_DIR="${PREV_DIR%/}/initial"
STAGE_SCRIPT="$(cat <<EOF
    set -e
    mkdir -p '${NEW_DIR}'
    if [ ! -d '${NEW_DIR}/initial' ]; then
        cp -r '${PREV_INITIAL_DIR}' '${NEW_DIR}/initial'
    fi
    if [ -L '${NEW_DIR}/initial/data' ] && [ ! -d '${NEW_DIR}/initial/data/' ]; then
        rm '${NEW_DIR}/initial/data'
        cp -rL '${PREV_INITIAL_DIR}/data' '${NEW_DIR}/initial/data'
    fi
    if [ ! -e '${NEW_DIR}/data' ] && [ -d '${NEW_DIR}/initial/data' ]; then
        ln -sfn initial/data '${NEW_DIR}/data'
    fi
EOF
)"

SKIP_PUSH=0
if [[ -n "$SERVER" ]]; then
    if (( ! FORCE )); then
        if ssh -o ConnectTimeout=6 "$SERVER" "test -e '${NEW_DIR}/initial/data/dont_use.tcl'"; then
            ssh -o ConnectTimeout=6 "$SERVER" "cat '${NEW_DIR}/initial/data/dont_use.tcl'" > "${REMOTE_DU_LOCAL}" \
                || { echo "ERROR: cannot read existing dont_use.tcl from ${SERVER}:${NEW_DIR} for identity check" >&2; exit 1; }
            if diff -q "${NEW_DU_LOCAL}" "${REMOTE_DU_LOCAL}" >/dev/null; then
                echo "[SKIP] identical setup already present"
                SKIP_PUSH=1
            else
                echo "ERROR: target run dir already has a different dont_use.tcl: ${NEW_DIR}. Re-run with --force." >&2
                exit 1
            fi
        fi
    fi

    ssh "$SERVER" "$STAGE_SCRIPT" || { echo "ERROR: failed to stage new run dir" >&2; exit 1; }

    if (( ! SKIP_PUSH )); then
        scp -q "${NEW_DU_LOCAL}" "${SERVER}:${NEW_DIR}/initial/data/dont_use.tcl" \
            || { echo "ERROR: failed to scp new dont_use.tcl" >&2; exit 1; }
        scp -q "${CELL_NAMES_LOCAL}" "${SERVER}:${NEW_DIR}/initial/data/cell_names.txt" \
            || { echo "ERROR: failed to scp cell_names.txt" >&2; exit 1; }
    fi

    echo "[phase_setup] staged ${SERVER}:${NEW_DIR}/initial/"
else
    if (( ! FORCE )); then
        if [[ -e "${NEW_DIR}/initial/data/dont_use.tcl" ]]; then
            if diff -q "${NEW_DU_LOCAL}" "${NEW_DIR}/initial/data/dont_use.tcl" >/dev/null; then
                echo "[SKIP] identical setup already present"
                SKIP_PUSH=1
            else
                echo "ERROR: target run dir already has a different dont_use.tcl: ${NEW_DIR}. Re-run with --force." >&2
                exit 1
            fi
        fi
    fi

    bash -c "$STAGE_SCRIPT" || { echo "ERROR: failed to stage new run dir" >&2; exit 1; }

    if (( ! SKIP_PUSH )); then
        cp "${NEW_DU_LOCAL}" "${NEW_DIR}/initial/data/dont_use.tcl" \
            || { echo "ERROR: failed to cp new dont_use.tcl" >&2; exit 1; }
        cp "${CELL_NAMES_LOCAL}" "${NEW_DIR}/initial/data/cell_names.txt" \
            || { echo "ERROR: failed to cp cell_names.txt" >&2; exit 1; }
    fi

    echo "[phase_setup] staged ${NEW_DIR}/initial/"
fi
