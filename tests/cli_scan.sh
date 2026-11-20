#!/usr/bin/env bash
set -euo pipefail
bin=$1
tmp=$(mktemp)
"$bin" scan >"$tmp"
grep -F -x 'mock: true' "$tmp"
grep -F -x 'backend: mock' "$tmp"
grep -F -x 'hardware_pass: false' "$tmp"
grep -F -x 'hardware_run: not claimed' "$tmp"
grep -F -x 'hardware_observed: false' "$tmp"
grep -F -x 'qualification: not a hardware pass' "$tmp"
grep -F -x 'independent_inputs: 1' "$tmp"
grep -F -x 'channel_count_from_scan: 1' "$tmp"
grep -F -x 'capture_started: false' "$tmp"
grep -F -x 'mxf_started: false' "$tmp"
grep -F -x 'capture_implemented: false' "$tmp"
grep -F -x 'mxf_implemented: false' "$tmp"
grep -F -x 'decklink_backend_built: false' "$tmp"
grep -F 'not a DeckLink 4K Extreme' "$tmp"
grep -F 'display_name: MOCK single-channel SDI I/O' "$tmp"
grep -F 'simultaneous_capture_openable: true' "$tmp"
grep -F 'audio_8ch_supported: true' "$tmp"
grep -F 'input_connectors: sdi' "$tmp"
grep -F 'left out of this build' "$tmp"
if grep -F -x '4 INDEPENDENT INPUTS NOT AVAILABLE' "$tmp"; then
    echo 'scan must not refuse a 1-input fixture' >&2
    exit 1
fi
set +e
"$bin" scan --backend decklink >"$tmp.out" 2>"$tmp.err"
code=$?
set -e
test "$code" -eq 1
grep -F 'not in this build' "$tmp.err"
rm -f "$tmp" "$tmp.out" "$tmp.err"
