#!/usr/bin/env bash
set -euo pipefail
bin=$1
cfg=$2
mkdir -p /tmp/xdcam-phase0
tmp=$(mktemp)
"$bin" preflight --config "$cfg" >"$tmp"
grep -F -x 'start_allowed: true' "$tmp"
grep -F -x 'record_started: false' "$tmp"
grep -F -x 'requested_channels: 1' "$tmp"
grep -F -x 'channel_count_source: scan' "$tmp"
grep -F -x 'files_planned: 1' "$tmp"
grep -F -x 'files_written: 0' "$tmp"
grep -F -x 'single_input_record_possible: true' "$tmp"
grep -F -x 'mock: true' "$tmp"
grep -F -x 'hardware_pass: false' "$tmp"
if grep -F -x '4 INDEPENDENT INPUTS NOT AVAILABLE' "$tmp"; then
    echo '1-channel preflight must not print the 4-input refusal' >&2
    exit 1
fi
"$bin" scan --config "$cfg" >"$tmp"
grep -F -x 'start_allowed: true' "$tmp"
rm -f "$tmp"
