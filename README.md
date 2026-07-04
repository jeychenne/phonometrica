# Phonometrica Engine

A high-performance, embeddable scripting engine in C++20, replacing the runtime in
[Phonometrica](https://github.com/jeychenne/phonometrica). Standalone library, no
Qt/GUI dependency, no third-party runtime dependencies.

See [`design/design.md`](design/design.md) (language semantics) and
[`design/architecture.md`](design/architecture.md) (C++ architecture and milestone
plan). Deviations from those documents are tracked in
[`DEVIATIONS.md`](DEVIATIONS.md).

## Status: M0 — Foundations

Implemented:

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

Acceptance (architecture §15, M0): container unit tests including fuzz-style
randomized ops against reference models (`std::vector`, `std::unordered_map`,
`std::set`), and an allocator stress test with pattern-fill overlap detection.

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
src/base/    definitions, bits, allocation, size-class heap
src/core/    Vector, SmallVector, FlatHashMap/Set, hash, relocate trait
test/unit/   C++ unit tests + minimal single-header harness
```
