# Unicode tables

`src/base/unicode_tables.cpp` is generated, not hand-written. It holds the
Unicode 16.0 property data consumed by `src/base/unicode.cpp` (grapheme break /
InCB / Extended_Pictographic, case mappings, White_Space ranges, and UAX #31
ID_Start/ID_Continue), in a compact two-stage table layout.

This directory is **self-contained**: the generator and all of its input data are
vendored here, so regenerating the tables needs nothing external.

## Layout

```
tools/unicode/
  generate_tables.py   the generator (writes src/base/unicode_tables.cpp)
  data/                vendored Unicode 16.0.0 UCD files
    UnicodeData.txt              simple case mappings
    SpecialCasing.txt            full (multi-codepoint) case mappings
    GraphemeBreakProperty.txt    Grapheme_Cluster_Break
    emoji-data.txt               Extended_Pictographic
    DerivedCoreProperties.txt    Indic_Conjunct_Break, ID_Start, ID_Continue
    PropList.txt                 White_Space
    auxiliary/
      GraphemeBreakTest.txt      UAX #29 conformance suite (used by the test)
```

## Regenerating

```sh
python3 tools/unicode/generate_tables.py
```

Then rebuild and run the tests (`test_grapheme_conformance` checks the grapheme
iterator against `GraphemeBreakTest.txt`).

## Upgrading to a new Unicode version

1. Replace the files in `data/` (and `data/auxiliary/`) with the new UCD release
   and update `UNICODE_VERSION` in `generate_tables.py`.
2. Re-run the generator and the test suite.
3. If a stage-1 table's distinct-block count ever exceeds 255, the generator
   emits `uint16_t` for it and you must widen the matching declaration in
   `src/base/unicode_tables.hpp` (a compile error will flag this).

The runtime enum values in `src/base/unicode.cpp` (`GCB_*`, `InCB_*`) and the
pack masks (`GCB_MASK`, `INCB_SHIFT`, `EXTPICT_MASK`) must stay in sync with the
generator's `GCB_VALUES` / `INCB_VALUES` and `pack()`.

## Provenance

The generator and table layout were adapted from the calao project; the engine
now vendors its own copy so there is no ongoing dependency on calao.
