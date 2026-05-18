#!/usr/bin/env zsh

# Adapt to your system
VERSION="6.11.0"
OUTDIR="$HOME/Temp/build/phon-release"

# This assumes a standard install in the user's home directory
$HOME/Qt/$VERSION/macos/bin/macdeployqt $OUTDIR/Phonometrica.app -dmg

