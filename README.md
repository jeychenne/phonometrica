# Phonometrica Engine

A high-performance, embeddable scripting engine in C++20, replacing the runtime in
[Phonometrica](https://github.com/jeychenne/phonometrica). Standalone library, no
Qt/GUI dependency, no third-party runtime dependencies.

See [`design/design.md`](design/design.md) (language semantics) and
[`design/architecture.md`](design/architecture.md) (C++ architecture and milestone
plan). Deviations from those documents are tracked in
[`DEVIATIONS.md`](DEVIATIONS.md).

## Status: M2 — Type system + dispatch (complete)

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
- **`List` / `Map` / `Set`** — value-semantic (CoW) containers. `Map`/`Set` are a
  stable cell wrapping the one engine hash table (`FlatHashMap`/`FlatHashSet`) keyed
  by structural `value_hash`/`value_equals` (unordered; see `DEVIATIONS.md`).
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
uniqueness across String/List/Map/Set, grapheme breadcrumb correctness on IPA
samples, Map/Set fuzz vs `std::unordered_map`/`std::set`, and a UAX #29 grapheme
conformance suite; M2 subtype intervals under renumbering, ref-mask applicability,
metaclass dispatch, ambiguity-at-definition, and epoch invalidation. The suite is
warning-clean (`-Wall -Wextra -Werror`) and leak-clean under ASan+UBSan.

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Options:

- `-DPHON_SANITIZE=ON` — AddressSanitizer + UndefinedBehaviorSanitizer (the CI bar
  for unit and script tests, §16.3). The suite is leak-clean under ASan.
- `-DPHON_WERROR=ON` — treat warnings as errors. The tree builds warning-clean
  under `-Wall -Wextra`.

## Layout

```
src/base/     definitions, bits, allocation, size-class heap, Unicode
src/core/     Value, Cell, Handle, Variant, Symbol, containers, hash
src/object/   class registry + subtype intervals, value hash/equality
src/dispatch/ generic functions + multiple dispatch
src/memory/   cell allocation + finalizer dispatch
src/types/    String, atom table, List, Map, Set
src/runtime/  bootstrap (builtin class registration)
test/unit/    C++ unit tests + minimal single-header harness
tools/unicode/ Unicode table provenance
```
