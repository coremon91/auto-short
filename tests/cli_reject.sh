#!/usr/bin/env bash
set -euo pipefail
bin=$1
tmp=$(mktemp)
cat >"$tmp" <<'EOF'
{ "backend": "mock", "video": { "width": 1280 } }
EOF
set +e
"$bin" preflight --config "$tmp" >"$tmp.out" 2>"$tmp.err"
code=$?
set -e
test "$code" -eq 1
grep -F 'CONFIG_REJECTED:' "$tmp.err"
rm -f "$tmp" "$tmp.out" "$tmp.err"
