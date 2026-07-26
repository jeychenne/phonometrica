#!/bin/sh
# Offscreen GUI smoke test for the settings round trip.
#
# Usage: test/gui/run_smoke_settings.sh <path-to-phonometrica-binary>
#
# The settings file is written by std/write_settings.phon when the app
# quits and read back by Settings::read at the next startup. It used to
# be *run* by the scripting engine, so a value containing a brace was
# read as an interpolation — a directory named "corpus {2024}" came back
# as "corpus 2024", and an unbalanced brace made the file unparsable, on
# which every preference was silently reset. It is now parsed as the
# JSON it is.
#
# The smoke launches the app twice against one throwaway profile: the
# first run stores such values, the second checks what came back. It
# also seeds a third run with a settings file in the pre-0.9 script
# format, which must still be read.

set -eu

BIN=${1:?usage: $0 <path-to-phonometrica-binary>}
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

SCRIPTS="$WORK/home/.config/phonometrica/Scripts"
SETTINGS="$WORK/home/.config/phonometrica/settings.phon"
mkdir -p "$SCRIPTS"

run_app() {
    # The timeout is a backstop: if a stray modal dialog ever blocks the
    # smoke quit, the result files below still decide pass/fail.
    if HOME="$WORK/home" QT_QPA_PLATFORM=offscreen PHON_GUI_SMOKE=6 \
           timeout 60 "$BIN" > "$WORK/app_output.txt" 2>&1; then
        :
    else
        status=$?
        if [ "$status" -eq 124 ]; then
            echo "WARNING: app did not quit within the smoke window (killed)" >&2
        else
            echo "FAIL: app exited with status $status" >&2
            cat "$WORK/app_output.txt" >&2
            exit 1
        fi
    fi
}

# --- Run 1: store the values, let the app write them out on exit ------
sed "s|__BASE__|$WORK|" "$HERE/smoke_settings_write.phon" > "$SCRIPTS/zz_smoke.phon"
run_app

if [ ! -f "$SETTINGS" ]; then
    echo "FAIL: no settings file was written" >&2
    exit 1
fi

# What was written must be JSON with the brace intact, and must no longer
# be a script that assigns to phon.settings.
grep -q 'corpus {2024}' "$SETTINGS" || {
    echo "FAIL: the settings file does not contain the stored path verbatim" >&2
    cat "$SETTINGS" >&2
    exit 1
}
if grep -q 'phon.settings *=' "$SETTINGS"; then
    echo "FAIL: the settings file still carries the executable prefix" >&2
    exit 1
fi

# --- Run 2: read them back -------------------------------------------
sed "s|__BASE__|$WORK|" "$HERE/smoke_settings_read.phon" > "$SCRIPTS/zz_smoke.phon"
run_app

if [ ! -f "$WORK/read_result.txt" ]; then
    echo "FAIL: probe produced no result file (startup script did not run?)" >&2
    exit 1
fi
cat "$WORK/read_result.txt"
grep -q "^ALL OK$" "$WORK/read_result.txt" || {
    echo "FAIL: see probe output above" >&2
    exit 1
}

# --- Run 3: a settings file in the old, executable format ------------
# Written by every version up to 0.8: the comment banner, the
# `phon.settings =` prefix, and JSON escaping — so a brace appears raw,
# which is exactly what used to make the file unreadable. The bare
# trailing decimal points are what the old engine's serializer produced
# for whole floats, and are repaired before parsing.
rm -f "$WORK/read_result.txt"
cat > "$SETTINGS" <<'LEGACY'
# This file was generated automatically.
# Do not edit it unless you know what you are doing.
phon.settings = {"last_directory": "__BASE__/corpus {2024}", "praat_path": "/opt/praat {unclosed", "smoke_backslash": "C:\\Users\\test", "console_ratio": 0., "project_ratio": 0.17, "geometry": [0., 0., 800., 600.]}
LEGACY
sed -i "s|__BASE__|$WORK|" "$SETTINGS"
run_app

if [ ! -f "$WORK/read_result.txt" ]; then
    echo "FAIL: probe produced no result file for the legacy settings file" >&2
    exit 1
fi
cat "$WORK/read_result.txt"
grep -q "^ALL OK$" "$WORK/read_result.txt" || {
    echo "FAIL: a settings file in the pre-0.9 format was not read correctly" >&2
    exit 1
}

# --- Run 4: an unreadable settings file ------------------------------
# Falling back to the defaults is the right answer, but it used to happen
# in complete silence. The app must start, and must say why.
rm -f "$WORK/read_result.txt"
printf '# banner\nthis is not a settings file {{{\n' > "$SETTINGS"
run_app

if [ ! -f "$WORK/read_result.txt" ]; then
    echo "FAIL: the app did not start on an unreadable settings file" >&2
    exit 1
fi
grep -q "Preferences have been reset" "$WORK/app_output.txt" || {
    echo "FAIL: an unreadable settings file was discarded without a word" >&2
    cat "$WORK/app_output.txt" >&2
    exit 1
}

echo "smoke_settings: OK"
