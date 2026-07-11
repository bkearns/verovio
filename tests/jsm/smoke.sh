#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 /path/to/verovio" >&2
    exit 2
fi

verovio=$1
repo=$(cd "$(dirname "$0")/../.." && pwd)
fixture="$repo/tests/jsm/key-signature-accidentals.jsm"
tmp=$(mktemp -d)
output="$tmp/output.svg"
trap 'rm -rf "$tmp"' EXIT

"$verovio" -r "$repo/data" -f auto -o "$output" "$fixture"

rg -q 'id="event-b-flat-1"' "$output"
rg -q 'data-jsm-tone-id="tone-b-flat-1"' "$output"
rg -q 'data-jsm-measure-id="part-measure-1"' "$output"

written_accidentals=$(rg -U -o 'class="accid">[[:space:]]*<use' "$output" | rg -c '^class=')
if [[ $written_accidentals -ne 3 ]]; then
    echo "expected 3 written accidentals, found $written_accidentals" >&2
    exit 1
fi

echo "JSM native smoke passed: stable IDs present; written accidentals=$written_accidentals"

jq '.score.parts[0].measures[0].staves[0].voices[0].events[0].id = "unsafe id"' "$fixture" >"$tmp/unsafe.jsm"
if "$verovio" -r "$repo/data" -f jsm -o "$tmp/unsafe.svg" "$tmp/unsafe.jsm" >"$tmp/unsafe.log" 2>&1; then
    echo "unsafe ID was accepted" >&2
    exit 1
fi
rg -q 'JSM_UNSAFE_ID' "$tmp/unsafe.log"

jq '.score.parts[0].measures[0].staves[0].voices[0].events[1].id = "event-b-flat-1"' "$fixture" >"$tmp/duplicate.jsm"
if "$verovio" -r "$repo/data" -f jsm -o "$tmp/duplicate.svg" "$tmp/duplicate.jsm" >"$tmp/duplicate.log" 2>&1; then
    echo "duplicate ID was accepted" >&2
    exit 1
fi
rg -q 'JSM_DUPLICATE_ID' "$tmp/duplicate.log"

printf '[ "JSM", "0.1.0", "c", {}, [] ]\n' >"$tmp/compact.jsm"
if "$verovio" -r "$repo/data" -f auto -o "$tmp/compact.svg" "$tmp/compact.jsm" >"$tmp/compact.log" 2>&1; then
    echo "unsupported compact profile was accepted" >&2
    exit 1
fi
rg -q 'JSM_PROFILE' "$tmp/compact.log"

echo "JSM rejection smoke passed: unsafe IDs, duplicate IDs, and unsupported compact profile"
