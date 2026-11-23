#!/usr/bin/env bash
# =====================================================================
# run_export_r.sh -- self-test for "Export to R".
#
#     test/statistics/export_r/run_export_r.sh [path/to/phon_stats]
#
# 1. Runs test_export_r.phon under the headless statistics host, which
#    fits one model per case and exports each to out/<case>/ (and checks
#    that unsupported models are refused).
# 2. Runs every generated fit.R in R via check_export.R and fails if any
#    estimate disagrees beyond the script's own tolerance.
#
# Requirements: R with glmmTMB (>= 1.1.8) and mgcv (>= 1.9). phon_stats defaults to
# $PHON_STATS, then to phon_stats on the PATH.
# =====================================================================
set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
lib="$here/../lib"
phon_stats="${1:-${PHON_STATS:-phon_stats}}"

if ! command -v "$phon_stats" >/dev/null 2>&1; then
    echo "error: phon_stats not found (pass its path, or set PHON_STATS)" >&2
    exit 2
fi
if ! command -v Rscript >/dev/null 2>&1; then
    echo "error: Rscript not found" >&2
    exit 2
fi

rm -rf "$here/out"

echo "== Phonometrica: fit and export"
log="$(mktemp)"
trap 'rm -f "$log"' EXIT
"$phon_stats" -I "$lib" "$here/test_export_r.phon" | tee "$log"
phon_status=${PIPESTATUS[0]}
if [ "$phon_status" -ne 0 ] || ! grep -q "Results: .* 0 failed" "$log"; then
    echo "FAIL: the Phonometrica side of the self-test failed" >&2
    exit 1
fi

echo
echo "== R: run each generated fit.R and compare"
n=0
failed=0
for dir in "$here"/out/*/; do
    dir="${dir%/}"
    case "$(basename "$dir")" in blocked_*) continue ;; esac
    n=$((n + 1))
    # R's stderr (package start-up chatter, system warnings) is shown only on failure.
    if ! Rscript "$here/check_export.R" "$dir" 2>"$log"; then
        failed=$((failed + 1))
        sed 's/^/     | /' "$log"
    fi
done

echo
if [ "$n" -eq 0 ]; then
    echo "FAIL: no exports were produced" >&2
    exit 1
fi
echo "Export to R: $((n - failed)) of $n exports agree with R"
[ "$failed" -eq 0 ]
