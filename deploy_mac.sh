#!/usr/bin/env zsh

# Adapt to your system
# NB: use Qt 6.11.2 or later. Qt 6.11.0/6.11.1 have a macOS regression
# (QTBUG-147602) that crashes in QImage::toCGImage() whenever a busy cursor
# is set via setOverrideCursor(), e.g. when opening an annotation.
VERSION="6.11.3"
OUTDIR="$HOME/Temp/build/phon-release"

# This assumes a standard install in the user's home directory
$HOME/Qt/$VERSION/macos/bin/macdeployqt $OUTDIR/Phonometrica.app -dmg

