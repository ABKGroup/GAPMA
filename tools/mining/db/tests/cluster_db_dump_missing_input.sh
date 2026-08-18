#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <cluster_db_dump>" >&2
  exit 2
fi

bin="$1"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

set +e
NLCELL_RUNTIME_LOG="$tmpdir/logs" "$bin" "$tmpdir/missing.cdb" \
  >"$tmpdir/stdout" 2>"$tmpdir/stderr"
rc=$?
set -e

if [ "$rc" -ne 1 ]; then
  echo "expected exit code 1, got $rc" >&2
  cat "$tmpdir/stderr" >&2
  exit 1
fi

expected="ERROR: cannot open cluster_db: $tmpdir/missing.cdb"
if ! grep -Fqx "$expected" "$tmpdir/stderr"; then
  echo "missing error line: $expected" >&2
  cat "$tmpdir/stderr" >&2
  exit 1
fi

if grep -Fq "uncaught exception" "$tmpdir/stderr"; then
  echo "uncaught exception marker found" >&2
  cat "$tmpdir/stderr" >&2
  exit 1
fi
