#!/bin/sh
# Offscreen GUI smoke test for the get_formants natives.
#
# Usage: test/gui/run_smoke_formants.sh <path-to-phonometrica-binary>
#
# Sound formats are only registered during GUI initialization, so this
# has to drive the real app: it builds a throwaway profile whose
# Scripts directory contains smoke_formants.phon (startup scripts are
# auto-run), launches the app offscreen with PHON_GUI_SMOKE, and greps
# the probe's result file for "ALL OK".

set -eu

BIN=${1:?usage: $0 <path-to-phonometrica-binary>}
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/home/.config/phonometrica/Scripts"
cp "$HERE/../data/vowel_f500_1500_2500.wav" "$WORK/"
sed "s|__BASE__|$WORK|" "$HERE/smoke_formants.phon" \
    > "$WORK/home/.config/phonometrica/Scripts/zz_smoke_formants.phon"

# The timeout is a backstop: if a stray modal dialog ever blocks the
# smoke quit, the probe result below still decides pass/fail.
if HOME="$WORK/home" QT_QPA_PLATFORM=offscreen PHON_GUI_SMOKE=8 \
       timeout 60 "$BIN" > /dev/null 2>&1; then
    :
else
    status=$?
    if [ "$status" -eq 124 ]; then
        echo "WARNING: app did not quit within the smoke window (killed)" >&2
    else
        echo "FAIL: app exited with status $status" >&2
        exit 1
    fi
fi

if [ ! -f "$WORK/probe_result.txt" ]; then
    echo "FAIL: probe produced no result file (startup script did not run?)" >&2
    exit 1
fi

cat "$WORK/probe_result.txt"
grep -q "^ALL OK$" "$WORK/probe_result.txt" || {
    echo "FAIL: see probe output above" >&2
    exit 1
}
echo "smoke_formants: OK"
