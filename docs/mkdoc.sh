#!/bin/bash

# Build Phonometrica documentation.
# Usage: ./mkdoc.sh [html|qthelp|pdf|all]
#
# "html"    builds the website HTML (for phonometrica-ling.org).
# "qthelp"  builds the simplified HTML for the embedded help viewer.
#           (CMake runs this automatically; you only need it for testing.)
# "pdf"     builds the PDF manual.
# "all"     (or no argument) builds all three.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/_build}"

TARGET="${1:-all}"

cd "$SCRIPT_DIR"

if [ "$TARGET" = "html" ] || [ "$TARGET" = "all" ] ; then
    echo "==> Building website HTML..."
    sphinx-build -b html . "$BUILD_DIR/html"
    rm -rf "$BUILD_DIR/html/_sources"
    echo "    Output: $BUILD_DIR/html/"
fi

if [ "$TARGET" = "qthelp" ] || [ "$TARGET" = "all" ] ; then
    echo "==> Building Qt help HTML..."
    sphinx-build -b qthelp . "$BUILD_DIR/qthelp"
    echo "    Output: $BUILD_DIR/qthelp/"
fi

if [ "$TARGET" = "pdf" ] || [ "$TARGET" = "all" ] ; then
    echo "==> Building PDF manual..."
    sphinx-build -M latexpdf . "$BUILD_DIR"
    echo "    Output: $BUILD_DIR/latex/phonometrica_manual.pdf"
fi

echo "Done."
