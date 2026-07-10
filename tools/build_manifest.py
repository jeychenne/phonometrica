#!/usr/bin/env python3
# Build a calibration manifest for calibrate_formants from the Hillenbrand et al. (1995) distribution.
#
#   python3 tools/build_manifest.py  /path/to/h95-alldata  manifest.tsv  [--whole-file]
#
# Output (one token per line, TAB-separated), consumed by calibrate_formants:
#
#   wav_path   t_start   t_end   goldF1   goldF2   goldF3   vowel   class
#
# t_start / t_end are the vowel boundaries in SECONDS within the file. Gold formants are the 50%-of-duration values
# from vowdata.dat (0 => not measurable; the C++ side skips a formant whose gold is 0). vowel is the 2-letter H95 code
# (iy, ih, ...); class is man/woman/boy/girl, inferred from the filename prefix (m/w/b/g).
#
# !!! VERIFY THE COLUMN MAP BELOW against the distribution's readme.txt before trusting the numbers. The script prints
# !!! the first few assembled rows so you can eyeball them; if F1/F2/F3 look wrong, fix VOWDATA_COLS / TIMEDATA_COLS.

import os, sys, wave, glob

# ---------------------------------------------------------------------------------------------------------------------
# COLUMN MAP  --  0-based indices into the whitespace-split fields of each data line. Defaults follow the widely
# documented vowdata.dat layout:  filename dur f0  F1s F2s F3s F4s  F1_20 F2_20 F3_20  F1_50 F2_50 F3_50  F1_80 F2_80 F3_80
# ---------------------------------------------------------------------------------------------------------------------
VOWDATA_COLS = {
    "filename": 0,
    "dur_ms":   1,     # vowel duration in ms
    "F1_50":    10,    # 50%-of-duration formants (the gold values we compare against)
    "F2_50":    11,
    "F3_50":    12,
}

# Vowel boundaries. Two possible sources:
#   (A) --whole-file : the wav IS the excised vowel  ->  t_start = 0, t_end = wav duration (read from the wav header).
#   (B) default      : boundaries come from timedata.dat. Set the column indices of the vowel begin/end times here and
#                      their unit. If the H95 wavs are full /hVd/ words, you MUST use (B) with correct columns.
TIMEDATA_COLS = {
    "filename":  0,
    "begin":     1,    # vowel begin time
    "end":       2,    # vowel end time
    "unit":      "ms", # "ms" or "s"
}

CLASS_OF = {"m": "man", "w": "woman", "b": "boy", "g": "girl"}


def read_table(path):
    rows = {}
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith(("#", ";", "%")):
                continue
            parts = line.split()
            # skip a header row (first field not a known filename pattern)
            if not parts or parts[0][:1] not in CLASS_OF:
                continue
            rows[parts[0]] = parts
    return rows


def wav_duration_s(path):
    with wave.open(path, "rb") as w:
        return w.getnframes() / float(w.getframerate())


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: build_manifest.py  H95_DIR  OUT.tsv  [--whole-file]")
    root, out = sys.argv[1], sys.argv[2]
    whole_file = "--whole-file" in sys.argv[3:]

    vow = read_table(os.path.join(root, "vowdata.dat"))
    tim = {} if whole_file else read_table(os.path.join(root, "timedata.dat"))

    # index wav files by their basename (without extension), across the men/women/kids subfolders
    wavs = {}
    for p in glob.glob(os.path.join(root, "**", "*.wav"), recursive=True):
        wavs[os.path.splitext(os.path.basename(p))[0]] = p

    written, skipped, preview = 0, 0, []
    with open(out, "w") as fo:
        fo.write("# wav_path\tt_start\tt_end\tgoldF1\tgoldF2\tgoldF3\tvowel\tclass\n")
        for name, parts in sorted(vow.items()):
            wav = wavs.get(name)
            if wav is None:
                skipped += 1
                continue
            try:
                f1 = float(parts[VOWDATA_COLS["F1_50"]])
                f2 = float(parts[VOWDATA_COLS["F2_50"]])
                f3 = float(parts[VOWDATA_COLS["F3_50"]])
            except (IndexError, ValueError):
                skipped += 1
                continue

            if whole_file:
                t1, t2 = 0.0, wav_duration_s(wav)
            else:
                tp = tim.get(name)
                if tp is None:
                    skipped += 1
                    continue
                scale = 0.001 if TIMEDATA_COLS["unit"] == "ms" else 1.0
                try:
                    t1 = float(tp[TIMEDATA_COLS["begin"]]) * scale
                    t2 = float(tp[TIMEDATA_COLS["end"]]) * scale
                except (IndexError, ValueError):
                    skipped += 1
                    continue

            cls = CLASS_OF[name[0]]
            vowel = name[3:5]  # e.g. m01ae -> "ae"
            row = f"{wav}\t{t1:.4f}\t{t2:.4f}\t{f1:.0f}\t{f2:.0f}\t{f3:.0f}\t{vowel}\t{cls}\n"
            fo.write(row)
            written += 1
            if len(preview) < 6:
                preview.append(row.rstrip())

    print(f"wrote {written} tokens to {out}  (skipped {skipped})")
    print("\n--- VERIFY these first rows (wav  t_start  t_end  F1  F2  F3  vowel  class) ---")
    for r in preview:
        print("  " + r)
    print("\nIf F1/F2/F3 or the times look wrong, fix VOWDATA_COLS / TIMEDATA_COLS against readme.txt and re-run.")


if __name__ == "__main__":
    main()
