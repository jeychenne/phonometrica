#!/bin/sh
# Offscreen GUI smoke test for the "script_debug" preference.
#
# Usage: test/gui/run_smoke_script_debug.sh <path-to-phonometrica-binary>
#
# The `debug` statement is resolved at compile time from a flag the
# application sets on the Runtime (Runtime::set_debug), fed by the
# Preferences > General > "Run debug statements in scripts" checkbox.
# That wiring is only observable by running the real application: the
# engine tests cover the language side, but nothing in them proves the
# preference actually reaches the engine.
#
# So this drives the app twice with a throwaway profile whose saved
# settings set script_debug true and then false, running a startup
# script that records whether its `debug` line executed.

set -eu

BIN=${1:?usage: $0 <path-to-phonometrica-binary>}

# Run the app once with script_debug=$1; echoes RAN or STRIPPED.
probe() {
    want=$1
    work=$(mktemp -d)
    mkdir -p "$work/home/.config/phonometrica/Scripts"

    # What Settings::read() loads in place of the bundled defaults. Any key
    # this omits is filled in by Settings::post_initialize().
    printf 'phon.settings = { "script_debug": %s }\n' "$want" \
        > "$work/home/.config/phonometrica/settings.phon"

    # Startup scripts in the profile's Scripts directory are auto-run.
    cat > "$work/home/.config/phonometrica/Scripts/zz_probe_debug.phon" <<EOF
var f = open_file("$work/result.txt", "w")
write_line(f, "script ran")
debug write_line(f, "DEBUG RAN")
close(f)
EOF

    HOME="$work/home" QT_QPA_PLATFORM=offscreen PHON_GUI_SMOKE=5 \
        timeout 60 "$BIN" >/dev/null 2>&1 || true

    if [ ! -f "$work/result.txt" ]; then
        rm -rf "$work"
        echo "NOSCRIPT"
        return
    fi
    if grep -q "^DEBUG RAN$" "$work/result.txt"; then
        rm -rf "$work"; echo "RAN"
    else
        rm -rf "$work"; echo "STRIPPED"
    fi
}

on=$(probe true)
off=$(probe false)

if [ "$on" = "NOSCRIPT" ] || [ "$off" = "NOSCRIPT" ]; then
    echo "FAIL: the startup script did not run (profile setup wrong?)" >&2
    exit 1
fi

echo "script_debug=true  -> debug code $on"
echo "script_debug=false -> debug code $off"

if [ "$on" != "RAN" ]; then
    echo "FAIL: debug code should run when the preference is on" >&2
    exit 1
fi
if [ "$off" != "STRIPPED" ]; then
    echo "FAIL: debug code should be stripped when the preference is off" >&2
    exit 1
fi

echo "smoke_script_debug: OK"
