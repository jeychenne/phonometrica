#!/bin/bash
# Four-way comparison on the synthetic set, per SNR condition:
#   fixed baseline, raw Weenink, our intrinsic ladder, and Fast Track (external).
# Usage:
#   ./run_compare.sh  manifest.tsv  ./build-cal/calibrate_formants  fasttrack.tsv
# where fasttrack.tsv has one row per token:  <filename> <F1> <F2> <F3>  (midpoint formants; header ok; path or bare name)
set -e
MANIFEST="${1:-manifest.tsv}"
CAL="${2:-./build-cal/calibrate_formants}"
FT="${3:-}"

awk -F'\t' '!/^#/ {print > ("manifest_" $NF ".tsv")}' "$MANIFEST"
all=$(awk -F'\t' '!/^#/ {print $NF}' "$MANIFEST" | sort -u)
conds=""
echo "$all" | grep -qx clean && conds="clean"
for n in $(echo "$all" | grep -vx clean | sort -rn); do conds="$conds $n"; done
echo "conditions: $conds"

ext_args=()
[ -n "$FT" ] && ext_args=(--external "$FT" --label FastTrack)

for cond in $conds; do
    f="manifest_${cond}.tsv"; [ -s "$f" ] || continue
    echo; echo "######################  SNR = ${cond}  ######################"
    "$CAL" "$f" "${ext_args[@]}" | tee "cmp_${cond}.txt"
done

echo
echo "==================  COMPARISON (overall MAE, Hz)  =================="
printf "%-8s %10s %10s %10s %10s\n" "SNR" "baseline" "Weenink" "ours" "FastTrack"
for cond in $conds; do
    r="cmp_${cond}.txt"; [ -f "$r" ] || continue
    base=$(awk '/Fixed baseline/{getline; print $3; exit}' "$r")
    ween=$(awk '/Weenink W/{getline; print $3; exit}' "$r")
    ours=$(awk '/default weights = 1, Cln off/{getline; print $3; exit}' "$r")
    ft=$(awk '/External: FastTrack/{f=1} f&&/Overall/{print $3; exit}' "$r")
    printf "%-8s %10s %10s %10s %10s\n" "$cond" "$base" "$ween" "$ours" "${ft:-n/a}"
done

echo
echo "==================  F3 MAE by hard vowel (er / oa / uw / iy)  =================="
printf "%-8s %-10s %8s %8s %8s\n" "SNR" "method" "er" "oa" "uw"
for cond in $conds; do
    r="cmp_${cond}.txt"; [ -f "$r" ] || continue
    for m in "Weenink W|Weenink" "default weights = 1, Cln off|ours" "External: FastTrack|FastTrack"; do
        tag="${m%%|*}"; name="${m##*|}"
        vals=$(awk -v t="$tag" '$0 ~ t {f=1} f&&/^    er /{er=$NF} f&&/^    oa /{oa=$NF} f&&/^    uw /{uw=$NF; print er, oa, uw; exit}' "$r")
        [ -n "$vals" ] && printf "%-8s %-10s %8s %8s %8s\n" "$cond" "$name" $vals
    done
done
