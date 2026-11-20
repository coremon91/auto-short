#!/usr/bin/env bash
set -euo pipefail
bin=$1
cfg=$2
mkdir -p /tmp/xdcam-phase0
tmp=$(mktemp)
set +e
"$bin" preflight --config "$cfg" >"$tmp"
code=$?
set -e
test "$code" -eq 2
grep -F -x '4 INDEPENDENT INPUTS NOT AVAILABLE' "$tmp"
grep -F -x 'start_allowed: false' "$tmp"
grep -F -x 'record_started: false' "$tmp"
grep -F -x 'files_planned: 0' "$tmp"
grep -F -x 'files_written: 0' "$tmp"
grep -F -x 'requested_channels: 4' "$tmp"
grep -F -x 'single_input_record_possible: true' "$tmp"
grep -F -x 'capture_started: false' "$tmp"
set +e
"$bin" scan --config "$cfg" >"$tmp"
code=$?
set -e
test "$code" -eq 2
grep -F -x '4 INDEPENDENT INPUTS NOT AVAILABLE' "$tmp"
rm -f "$tmp"
