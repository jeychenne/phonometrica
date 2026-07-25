# Phonometrica Engine

A high-performance, embeddable scripting engine in C++20 — the scripting runtime of
Phonometrica. No Qt/GUI dependency and no third-party runtime dependencies beyond
PCRE2 (vendored at `phon/third_party/pcre2`) and a thread library, so it still
configures and tests on its own.

Developed in a separate repository until roadmap step **A8**, which absorbed it here
with its history intact (see `MIGRATION_NOTES.md` at the repository root). The
directory is self-contained: sources, `CMakeLists.txt`, tests, golden corpora, Unicode
data, examples and benchmarks all live under it. Public headers are included as
`<phon/engine/...>`, resolved from the **repository root**, not from this directory.

See [`design/design.md`](design/design.md) (language semantics) and
[`design/architecture.md`](design/architecture.md) (C++ architecture and milestone
plan). Deviations from those documents are tracked in
[`DEVIATIONS.md`](DEVIATIONS.md) — the authoritative record of what the engine
actually does where it departs from the design docs.

## Milestone notes (historical — see DEVIATIONS.md for current behaviour)

The per-milestone summaries below were written as M0–M2 landed and were never
updated as the engine grew through modules, concurrency, arrays and the embedding
API. They describe real components but are **not** a current status report.

**M0 — Foundations**

- **`base/`** — `definitions.hpp` (assertions, compiler intrinsics), `bits.hpp`
  (alignment/bit utilities), `alloc.hpp`/`.cpp` (byte allocation, 64-byte aligned
  buffers, and a 64 KiB block allocator with a low-address `mmap` policy and a
  process-global block pool), `heap.hpp`/`.cpp` (per-thread size-class free-list
  heap: 16-byte steps to 256 B, powers of two to 8 KiB, large path via
  `aligned_alloc64`).
- **`core/`** — `Vector<T>`, `SmallVector<T, N>` (inline storage + heap spill),
  `FlatHashMap<K, V>` (Swiss-table: control-byte metadata, 16-wide SSE2/scalar
  group probing, 7/8 load factor, tombstone reuse), `FlatHashSet<K>`, plus the
  `TriviallyRelocatable` trait and hashing primitives.

**M1 — Values and core types**

- **Value / Cell / Handle** — 64-bit NaN-boxed `Value` (null/bool/int48/symbol/
  cell/ref tags) with a single choke-point header; 8-byte `Cell` object header;
  `Handle<T>` intrusive refcount; non-atomic `retain`/`release` with per-class
  finalizer dispatch. `Variant` is the public RAII wrapper.
- **`base/unicode`** — self-contained UTF-8 codec, UAX #29 grapheme segmentation,
  case mapping, White_Space, and UAX #31 identifier classification over prefetched
  Unicode 16.0 tables (adapted from calao; no third-party dependency).
- **`String`** — single-allocation inline UTF-8, copy-on-write, **grapheme-indexed**
  (see `DEVIATIONS.md`) with lazily built grapheme breadcrumbs for O(1)-amortized
  random access; API-compatible-in-spirit with Phonometrica's `String`. Plus the
  **atom table** (Symbol interning).
- **`List` / `Table` / `Set`** — value-semantic (CoW) containers. `Table` is the
  script dictionary (the design docs' "Map", named `Table` for Phonometrica
  compatibility). `Table`/`Set` are a stable cell wrapping the one engine hash table
  (`FlatHashMap`/`FlatHashSet`) keyed by structural `value_hash`/`value_equals`
  (unordered; see `DEVIATIONS.md`).
- **`object/`** — the M1 seed of the class registry + `value_hash`/`value_equals`.

**M2 — Type system + dispatch**

- **`object/class`** — the full class system: a stable registration id (in cell
  headers) plus a renumbered pre-order **subtype interval** (`is_a` = two compares),
  `class_of(Value)`, dynamic `add_class` with renumbering + `type_epoch`, and
  **metaclasses**/class-objects so a class can be a Value (`cast(x, Float)`).
- **`dispatch/generic`** — `GenericFunction`/`Method` with multiple dispatch:
  applicable-method filtering (interval subtype + exact `ref`-mask match),
  most-specific selection, **ambiguity detected at definition time**, and memo
  tables (arity 1–2 exact keys) that self-invalidate on method-set or type-epoch
  change. Inspired by calao's `class_id`-indexed applicability, following the
  spec's memo + definition-time-ambiguity structure.

Acceptance: M0 container fuzz tests + allocator stress; M1 `Value` encoding
round-trips (every tag, ±2^47 integer boundary, NaN payloads survive), CoW
uniqueness across String/List/Table/Set, grapheme breadcrumb correctness on IPA
samples, Table/Set fuzz vs `std::unordered_map`/`std::set`, and a UAX #29 grapheme
conformance suite; M2 subtype intervals under renumbering, ref-mask applicability,
metaclass dispatch, ambiguity-at-definition, and epoch invalidation. The suite is
warning-clean (`-Wall -Wextra -Werror`) and leak-clean under ASan+UBSan.

## Conventions

Modern C++ with RAII: a raw pointer is used only when the pointee is not owned *and*
may be null. Otherwise use a reference or a `unique_ptr`.

## Building and testing

All commands run from the **repository root**. The engine is a subdirectory of the
application build, so building the application builds `phon_engine` automatically.
Its other targets are `EXCLUDE_FROM_ALL` and must be named explicitly — a plain
build will NOT reveal breakage in `phon_repl` or `phon_bench`:

```sh
cmake --build <build-dir> --target phon_unit_tests phon_repl phon_bench
<build-dir>/phon/engine/phon_unit_tests
ctest --test-dir <build-dir> --output-on-failure   # unit + the two phon_repl acceptance runs
```

To work on the engine alone, configure without the application layer — this needs
neither Qt nor libsndfile, which is what keeps the sanitizer runs affordable:

```sh
cmake -S . -B build-engine -DWITH_APPLICATION=OFF -DWITH_GUI=OFF -DPHON_BUILD_DOCS=OFF
cmake --build build-engine --target phon_unit_tests
```

Options (defined in this directory's `CMakeLists.txt`, so they apply to any of the
configurations above):

- `-DPHON_SANITIZE=ON` — AddressSanitizer + UndefinedBehaviorSanitizer (the CI bar
  for unit and script tests, §16.3). The suite is leak-clean under ASan.
- `-DPHON_TSAN=ON` — ThreadSanitizer (the concurrency acceptance bar). Mutually
  exclusive with `PHON_SANITIZE`.
- `-DPHON_WERROR=ON` — treat warnings as errors. The tree builds warning-clean
  under `-Wall -Wextra`.

The standing acceptance bar is the unit suite green in all three configurations
(normal, ASan, TSan).

## Layout

The engine implementation lives under `phon/engine/`; a small set of thin public
forwarding headers sit directly under `phon/` and form the C++ embedding surface
(repo root is the include path). This keeps `phon/` uncluttered when the engine is
dropped into Phonometrica alongside its own `application/`, `gui/`, etc. `.hpp` and
`.cpp` sit together per subsystem.

```
phon/*.hpp            public forwarders — String, Array, List, Table, Set, File,
                      Regex, error, runtime, plus Hashmap/Dictionary aliases
phon/engine/base/         definitions, bits, allocation, size-class heap, Unicode,
                          Unicode-correct file-system layer
phon/engine/core/         Value, Cell, Handle, Variant, Symbol, containers, hash,
                          cell allocation + cycle collector (GC)
phon/engine/object/       class registry + subtype intervals, value hash/equality,
                          generic functions + multiple dispatch
phon/engine/types/        String, atom table, List, Table, Set, Array, File, Regex
phon/engine/compile/      scanner, parser, AST, lowering, disassembler
phon/engine/vm/           opcodes, functions, isolate, interpreter
phon/engine/concurrency/  channels, spawn, transfer, thread pool, parallel
phon/engine/lib/          standard library (math, string, list, array, system, file)
phon/engine/runtime/      Runtime front end + bootstrap (builtin class registration)
test/unit/                C++ unit tests + minimal single-header harness
tools/unicode/            Unicode table generator + vendored UCD data
```
