#!/usr/bin/env python3
# Phonometrica engine — Unicode table generator.
# Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
#
# Generates phon/base/unicode_tables.cpp from the vendored Unicode 16.0 UCD files
# in ./data/. The output is a single C++ source consumed by phon/base/unicode.cpp.
# Adapted from calao's generator; the engine vendors both the generator and its
# input data so regeneration for a new Unicode version needs nothing external.
#
# Grapheme property byte, one uint8_t per codepoint:
#   bits 0..3  Grapheme_Cluster_Break (see GCB_VALUES)
#   bits 4..5  Indic_Conjunct_Break    (see INCB_VALUES)
#   bit  6     Extended_Pictographic
#   bit  7     reserved (0)
#
# Tables are stored two-stage: stage 1 is one index per 256-codepoint block
# (4352 blocks over the 0x110000 codespace); stage 2 is the deduplicated set of
# distinct blocks, so unassigned space collapses onto the all-zero block.

from __future__ import annotations

import sys
from pathlib import Path

UNICODE_VERSION = "16.0.0"
HERE = Path(__file__).resolve().parent
DATA = HERE / "data"

# Output path (engine repo root is tools/unicode/../..).
REPO = HERE.parent.parent
OUT_C = REPO / "phon" / "base" / "unicode_tables.cpp"

# Symbol/macro naming for the engine (was calao_utf8_ / CALAO_UTF8_).
SYM = "phon_uni_"
MAC = "PHON_UNI_"

# Grapheme_Cluster_Break values; the index is the encoded GCB id. These MUST stay
# in sync with the GCB_* enum in phon/base/unicode.cpp. Reserve 0 for "Other" so
# memset-zero blocks decode as Other/None/!ExtPict.
GCB_VALUES = [
    "Other",  # 0 -- default for all unlisted codepoints
    "CR",
    "LF",
    "Control",
    "Extend",
    "Regional_Indicator",
    "Prepend",
    "SpacingMark",
    "L",
    "V",
    "T",
    "LV",
    "LVT",
    "ZWJ",
]
GCB_ID = {name: i for i, name in enumerate(GCB_VALUES)}
assert len(GCB_VALUES) <= 16, "GCB values exceed 4-bit field"

INCB_VALUES = ["None", "Consonant", "Extend", "Linker"]
INCB_ID = {name: i for i, name in enumerate(INCB_VALUES)}
assert len(INCB_VALUES) <= 4, "InCB values exceed 2-bit field"


def pack(gcb_id: int, incb_id: int, ext_pict: bool) -> int:
    return (gcb_id & 0x0F) | ((incb_id & 0x03) << 4) | ((1 << 6) if ext_pict else 0)


def parse_ucd_range(field: str) -> tuple[int, int]:
    field = field.strip()
    if ".." in field:
        a, b = field.split("..")
        return int(a, 16), int(b, 16)
    cp = int(field, 16)
    return cp, cp


def iter_ucd_lines(path: Path):
    with path.open("r", encoding="utf-8") as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            parts = [p.strip() for p in line.split(";")]
            lo, hi = parse_ucd_range(parts[0])
            yield lo, hi, parts[1:]


def load_grapheme_break(path: Path) -> dict[int, int]:
    out: dict[int, int] = {}
    for lo, hi, fields in iter_ucd_lines(path):
        name = fields[0]
        gid = GCB_ID.get(name)
        if gid is None:
            raise SystemExit(f"unknown GCB value: {name!r}")
        for cp in range(lo, hi + 1):
            out[cp] = gid
    return out


def load_extended_pictographic(path: Path) -> set[int]:
    out: set[int] = set()
    for lo, hi, fields in iter_ucd_lines(path):
        if fields[0] == "Extended_Pictographic":
            for cp in range(lo, hi + 1):
                out.add(cp)
    return out


def load_incb(path: Path) -> dict[int, int]:
    out: dict[int, int] = {}
    for lo, hi, fields in iter_ucd_lines(path):
        if len(fields) >= 2 and fields[0] == "InCB":
            iid = INCB_ID.get(fields[1])
            if iid is None:
                raise SystemExit(f"unknown InCB value: {fields[1]!r}")
            for cp in range(lo, hi + 1):
                out[cp] = iid
    return out


def build_grapheme_table(gcb: dict[int, int], ext: set[int], incb: dict[int, int]) -> list[int]:
    table = [0] * 0x110000
    for cp, gid in gcb.items():
        table[cp] = pack(gid, incb.get(cp, 0), cp in ext)
    for cp, iid in incb.items():
        if cp not in gcb:
            table[cp] = pack(0, iid, cp in ext)
    for cp in ext:
        if cp not in gcb and cp not in incb:
            table[cp] = pack(0, 0, True)
    return table


def two_stage(table: list[int], block_bits: int = 8) -> tuple[list[int], list[int]]:
    block_size = 1 << block_bits
    if len(table) % block_size != 0:
        raise SystemExit("table size not divisible by block size")
    stage2: list[tuple[int, ...]] = []
    stage2_index: dict[tuple[int, ...], int] = {}
    stage1: list[int] = []
    for start in range(0, len(table), block_size):
        block = tuple(table[start:start + block_size])
        idx = stage2_index.get(block)
        if idx is None:
            idx = len(stage2)
            stage2.append(block)
            stage2_index[block] = idx
        stage1.append(idx)
    if len(stage2) > 0xFFFF:
        raise SystemExit("stage2 too large for uint16 stage1")
    flat2: list[int] = []
    for block in stage2:
        flat2.extend(block)
    return stage1, flat2


# --- case mappings ---------------------------------------------------------
#
# Per-codepoint uint32 encoding:
#   0                    -- no change (mapping is the source codepoint)
#   1..0x10FFFF          -- single-codepoint mapping
#   0xFFFF0000 | idx     -- multi-codepoint mapping at side[idx]
# Each side entry is a 4-tuple (count, cp1, cp2, cp3); count is 1..3.


def load_simple_case_mappings(path: Path) -> tuple[dict[int, int], dict[int, int]]:
    upper: dict[int, int] = {}
    lower: dict[int, int] = {}
    with path.open("r", encoding="utf-8") as f:
        for raw in f:
            raw = raw.strip()
            if not raw or raw.startswith("#"):
                continue
            fields = raw.split(";")
            if len(fields) < 15:
                continue
            cp = int(fields[0], 16)
            su = fields[12].strip()
            sl = fields[13].strip()
            if su:
                upper[cp] = int(su, 16)
            if sl:
                lower[cp] = int(sl, 16)
    return upper, lower


def load_special_casing(path: Path) -> tuple[dict[int, list[int]], dict[int, list[int]]]:
    upper: dict[int, list[int]] = {}
    lower: dict[int, list[int]] = {}
    with path.open("r", encoding="utf-8") as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            fields = [p.strip() for p in line.rstrip(";").split(";")]
            if len(fields) < 4:
                continue
            # 4 fields = unconditional; 5+ = conditional, skip.
            if len(fields) > 4:
                continue
            cp = int(fields[0], 16)
            lo_cps = [int(x, 16) for x in fields[1].split()] if fields[1] else []
            up_cps = [int(x, 16) for x in fields[3].split()] if fields[3] else []
            if lo_cps:
                lower[cp] = lo_cps
            if up_cps:
                upper[cp] = up_cps
    return upper, lower


def load_id_props(path: Path) -> tuple[set[int], set[int]]:
    start: set[int] = set()
    cont: set[int] = set()
    for lo, hi, fields in iter_ucd_lines(path):
        name = fields[0] if fields else ""
        if name == "ID_Start":
            for cp in range(lo, hi + 1):
                start.add(cp)
        elif name == "ID_Continue":
            for cp in range(lo, hi + 1):
                cont.add(cp)
    return start, cont


def load_white_space(path: Path) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    for lo, hi, fields in iter_ucd_lines(path):
        if fields and fields[0] == "White_Space":
            ranges.append((lo, hi))
    return sorted(ranges)


def build_case_table(
    simple: dict[int, int],
    special: dict[int, list[int]],
) -> tuple[list[int], list[tuple[int, int, int, int]]]:
    table: list[int] = [0] * 0x110000
    side: list[tuple[int, int, int, int]] = []
    side_index: dict[tuple[int, ...], int] = {}
    for cp, tgt in simple.items():
        if tgt != cp:
            table[cp] = tgt
    for cp, tgts in special.items():
        if len(tgts) == 1:
            tgt = tgts[0]
            table[cp] = 0 if tgt == cp else tgt
            continue
        if len(tgts) > 3:
            raise SystemExit(
                f"U+{cp:04X}: special-casing mapping of {len(tgts)} cps exceeds "
                f"the 3-cp side-table slot; expand the format"
            )
        cp1 = tgts[0]
        cp2 = tgts[1] if len(tgts) > 1 else 0
        cp3 = tgts[2] if len(tgts) > 2 else 0
        key = (len(tgts), cp1, cp2, cp3)
        idx = side_index.get(key)
        if idx is None:
            idx = len(side)
            side.append(key)
            side_index[key] = idx
        if idx > 0xFFFF:
            raise SystemExit("case side-table outgrew uint16 sentinel space")
        table[cp] = 0xFFFF0000 | idx
    return table, side


# --- C++ emission ----------------------------------------------------------


def emit_rows(lines: list[str], values, per_row: int, fmt) -> None:
    row: list[str] = []
    for v in values:
        row.append(fmt(v))
        if len(row) == per_row:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")


def emit_u8_two_stage(lines: list[str], name: str, stage1, stage2) -> None:
    stage1_t = "uint16_t" if max(stage1) > 0xFF else "uint8_t"
    lines.append(f"extern const {stage1_t} {SYM}{name}_stage1[{MAC}STAGE1_LEN] = {{")
    emit_rows(lines, stage1, 16, str)
    lines.append("};")
    lines.append("")
    lines.append(
        f"extern const uint8_t {SYM}{name}_stage2[{MAC}{name.upper()}_STAGE2_BLOCKS * {MAC}BLOCK_SIZE] = {{"
    )
    emit_rows(lines, stage2, 16, lambda v: f"0x{v:02x}")
    lines.append("};")
    lines.append("")


def emit_case_section(lines, name, stage1, stage2, side) -> None:
    n_blocks_distinct = len(stage2) // 256
    stage1_t = "uint16_t" if max(stage1) > 0xFF else "uint8_t"
    lines.append(f"#define {MAC}{name.upper()}_STAGE2_BLOCKS {n_blocks_distinct}")
    lines.append(f"#define {MAC}{name.upper()}_SIDE_LEN {len(side)}")
    lines.append("")
    lines.append(f"extern const {stage1_t} {SYM}{name}_stage1[{MAC}STAGE1_LEN] = {{")
    emit_rows(lines, stage1, 16, str)
    lines.append("};")
    lines.append("")
    lines.append(
        f"extern const uint32_t {SYM}{name}_stage2[{MAC}{name.upper()}_STAGE2_BLOCKS * {MAC}BLOCK_SIZE] = {{"
    )
    emit_rows(lines, stage2, 8, lambda v: f"0x{v:x}")
    lines.append("};")
    lines.append("")
    lines.append(f"extern const uint32_t {SYM}{name}_side[{MAC}{name.upper()}_SIDE_LEN * 4] = {{")
    for (cnt, c1, c2, c3) in side:
        lines.append(f"    {cnt}, 0x{c1:x}, 0x{c2:x}, 0x{c3:x},")
    lines.append("};")
    lines.append("")


def emit_white_space(lines, ranges) -> None:
    lines.append(f"extern const size_t {SYM}white_space_range_count = {len(ranges)};")
    lines.append("")
    lines.append(f"extern const uint32_t {SYM}white_space_ranges[{len(ranges)} * 2] = {{")
    for (lo, hi) in ranges:
        lines.append(f"    0x{lo:x}, 0x{hi:x},")
    lines.append("};")
    lines.append("")


def emit_id_props(lines, id_start, id_continue) -> int:
    table = [0] * 0x110000
    for cp in id_start:
        table[cp] |= 0x01
    for cp in id_continue:
        table[cp] |= 0x02
    stage1, stage2 = two_stage(table, block_bits=8)
    lines.append(f"#define {MAC}ID_STAGE2_BLOCKS {len(stage2) // 256}")
    lines.append("")
    emit_u8_two_stage(lines, "id", stage1, stage2)
    return len(stage1) * (2 if max(stage1) > 0xFF else 1) + len(stage2)


def main() -> int:
    if not DATA.exists():
        raise SystemExit(f"no data directory at {DATA}")

    gcb = load_grapheme_break(DATA / "GraphemeBreakProperty.txt")
    ext = load_extended_pictographic(DATA / "emoji-data.txt")
    incb = load_incb(DATA / "DerivedCoreProperties.txt")
    simple_upper, simple_lower = load_simple_case_mappings(DATA / "UnicodeData.txt")
    spec_upper, spec_lower = load_special_casing(DATA / "SpecialCasing.txt")
    ws_ranges = load_white_space(DATA / "PropList.txt")
    id_start, id_continue = load_id_props(DATA / "DerivedCoreProperties.txt")

    g_table = build_grapheme_table(gcb, ext, incb)
    stage1, stage2 = two_stage(g_table, block_bits=8)

    lower_table, lower_side = build_case_table(simple_lower, spec_lower)
    upper_table, upper_side = build_case_table(simple_upper, spec_upper)
    lower_s1, lower_s2 = two_stage(lower_table)
    upper_s1, upper_s2 = two_stage(upper_table)

    lines: list[str] = []
    lines.append("// Phonometrica engine — Unicode 16.0 property tables.")
    lines.append("// Auto-generated by tools/unicode/generate_tables.py. DO NOT EDIT BY HAND.")
    lines.append("// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).")
    lines.append("//")
    lines.append("// Consumed by phon/base/unicode.cpp; declarations in base/unicode_tables.hpp:")
    lines.append(f"//   {SYM}stage1/stage2     -- grapheme / InCB / ExtPict")
    lines.append(f"//   {SYM}lower_*           -- lowercase mappings")
    lines.append(f"//   {SYM}upper_*           -- uppercase mappings")
    lines.append(f"//   {SYM}white_space_*     -- White_Space property ranges")
    lines.append(f"//   {SYM}id_stage1/stage2  -- UAX #31 ID_Start / ID_Continue")
    lines.append("//                             (bit 0 = ID_Start, bit 1 = ID_Continue)")
    lines.append("")
    lines.append('#include <phon/base/unicode_tables.hpp>')
    lines.append("")
    lines.append("#include <cstddef>")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append("namespace phonometrica {")
    lines.append("namespace unicode {")
    lines.append("")
    lines.append(f"#define {MAC}BLOCK_BITS 8")
    lines.append(f"#define {MAC}BLOCK_SIZE 256")
    lines.append(f"#define {MAC}STAGE1_LEN {len(stage1)}")
    lines.append(f"#define {MAC}STAGE2_BLOCKS {len(stage2) // 256}")
    lines.append("")

    fmt_t = "uint16_t" if max(stage1) > 0xFF else "uint8_t"
    lines.append(f"extern const {fmt_t} {SYM}stage1[{MAC}STAGE1_LEN] = {{")
    emit_rows(lines, stage1, 16, str)
    lines.append("};")
    lines.append("")
    lines.append(f"extern const uint8_t {SYM}stage2[{MAC}STAGE2_BLOCKS * {MAC}BLOCK_SIZE] = {{")
    emit_rows(lines, stage2, 16, lambda v: f"0x{v:02x}")
    lines.append("};")
    lines.append("")

    emit_case_section(lines, "lower", lower_s1, lower_s2, lower_side)
    emit_case_section(lines, "upper", upper_s1, upper_s2, upper_side)
    emit_white_space(lines, ws_ranges)
    id_bytes = emit_id_props(lines, id_start, id_continue)

    lines.append("} // namespace unicode")
    lines.append("} // namespace phonometrica")

    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    OUT_C.write_text("\n".join(lines) + "\n", encoding="utf-8")

    bytes_total = (
        len(stage1) * (2 if max(stage1) > 0xFF else 1)
        + len(stage2)
        + len(lower_s1) * (2 if max(lower_s1) > 0xFF else 1)
        + len(lower_s2) * 4
        + len(lower_side) * 16
        + len(upper_s1) * (2 if max(upper_s1) > 0xFF else 1)
        + len(upper_s2) * 4
        + len(upper_side) * 16
        + len(ws_ranges) * 8
        + id_bytes
    )
    print(
        f"wrote {OUT_C.relative_to(REPO)} (Unicode {UNICODE_VERSION}): "
        f"grapheme stage2={len(stage2) // 256} blocks, "
        f"lower={len(lower_s2) // 256} blocks/{len(lower_side)} side, "
        f"upper={len(upper_s2) // 256} blocks/{len(upper_side)} side, "
        f"ws={len(ws_ranges)} ranges, "
        f"id_start={len(id_start)}, id_continue={len(id_continue)}; "
        f"{bytes_total} bytes total"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
