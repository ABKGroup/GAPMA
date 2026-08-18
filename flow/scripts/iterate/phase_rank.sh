#!/usr/bin/env bash

set -uo pipefail

usage() {
    cat <<'USAGE'
phase_rank.sh --csv <inference.csv> --rank <N>

    --csv   inference_results_x2_*.csv produced by phase_infer.sh
    --rank  1-indexed rank to extract; rank=1 is the first data row.

Exit codes:
    0  success — 3 lines printed on stdout
    2  header malformed (missing canon_i / pred_cell_i / pred_cell_i_x2)
    3  rank row has an empty field
    4  rank row does not exist (CSV has fewer data rows)
USAGE
}

CSV=""
RANK=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --csv)   CSV="$2"; shift 2 ;;
        --rank)  RANK="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ERROR: unknown arg: $1" >&2; usage; exit 1 ;;
    esac
done
[[ -z "$CSV"  ]] && { echo "ERROR: --csv required"  >&2; exit 1; }
[[ -z "$RANK" ]] && { echo "ERROR: --rank required" >&2; exit 1; }
[[ -f "$CSV"  ]] || { echo "ERROR: csv not found: $CSV" >&2; exit 1; }
[[ "$RANK" =~ ^[0-9]+$ ]] || { echo "ERROR: --rank must be a positive integer (got $RANK)" >&2; exit 1; }

awk -F, -v rk="$RANK" '
    function trim(s) { gsub(/[ \r]/,"",s); return s }
    NR==1 {
        for (i=1;i<=NF;i++) {
            c = trim($i)
            if (c ~ /^canon_[1-5]$/)             ci[c]=i
            else if (c ~ /^pred_cell_[1-5]$/)    p1[c]=i
            else if (c ~ /^pred_cell_[1-5]_x2$/) p2[c]=i
        }
        bad=0
        for (k=1;k<=5;k++) {
            if (!("canon_" k in ci))             { print "ERROR: header missing canon_" k > "/dev/stderr"; bad=1 }
            if (!("pred_cell_" k in p1))         { print "ERROR: header missing pred_cell_" k > "/dev/stderr"; bad=1 }
            if (!("pred_cell_" k "_x2" in p2))   { print "ERROR: header missing pred_cell_" k "_x2" > "/dev/stderr"; bad=1 }
        }
        if (bad) exit 2
        next
    }
    NR==rk+1 {
        line1=""; line2=""; line3=""
        for (k=1;k<=5;k++) {
            v1 = trim($(ci["canon_" k]))
            v2 = trim($(p1["pred_cell_" k]))
            v3 = trim($(p2["pred_cell_" k "_x2"]))
            if (v1=="" || v2=="" || v3=="") {
                print "ERROR: empty field in rank=" rk " slot=" k " (canon=" v1 " px1=" v2 " px2=" v3 ")" > "/dev/stderr"
                row_error=1
                exit 3
            }
            line1 = (k==1 ? v1 : line1 " " v1)
            line2 = (k==1 ? v2 : line2 " " v2)
            line3 = (k==1 ? v3 : line3 " " v3)
        }
        print line1; print line2; print line3
        found=1
        exit 0
    }
    END {
        if (!found && !bad && !row_error) {
            print "ERROR: rank=" rk " row not found in CSV (only " (NR-1) " data rows)" > "/dev/stderr"
            exit 4
        }
    }
' "$CSV"
