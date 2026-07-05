# Deviations

Per architecture §16.6, any deviation from the design/architecture documents is
recorded here with rationale, for the project owner's review.

## M0 — Foundations

1. **Size-class heap lives in `base/`, not `memory/`.**
   The architecture (§8.1) discusses the per-thread heap under `memory/`, but the
   directory-layout note (§0) lists "allocator, arenas" under `base/`. The heap
   (`base/heap.hpp`) and the 64 KiB block allocator (`base/alloc.hpp`) depend only
   on layer 1 primitives and are consumed by every layer above, so they are placed
   in `base/`. `memory/` remains reserved for refcount ops, the cycle collector,
   and freeze/transfer (M1+).

2. **Custom minimal test harness instead of doctest/Catch2.**
   §0/§14 permit "doctest or Catch2 (single-header, no gtest dep)". Rather than
   vendor a large third-party header, `test/unit/test_framework.hpp` provides a
   ~150-line harness (`TEST_CASE`/`CHECK`/`REQUIRE`, an intrusive registry, and a
   runner). Zero third-party dependencies; swappable for doctest later if desired.

3. **Erase always writes a tombstone (`kDeleted`) in `FlatHashMap`.**
   Abseil reclaims a deleted slot straight to `kEmpty` when its probe group still
   contains an empty slot, sparing a rehash. That optimization is deferred (an M8
   tuning knob). Correctness does not depend on it: tombstones are reused on
   insert and cleared on rehash, so capacity stays bounded under churn (covered by
   `test_flat_hash_map`'s 50k-cycle churn test).

4. **`raw_alloc`/`aligned_alloc64` use `std::aligned_alloc` + `std::free`.**
   Correct on POSIX/glibc (the current target). Windows lacks a `free`-compatible
   `aligned_alloc`; a `_aligned_malloc`/`_aligned_free` path is a TODO for the
   Windows port. The block allocator already has an mmap/`aligned_alloc` split
   behind `PHON_HAVE_MMAP`.

5. **Low-address `mmap` hint is best-effort, not asserted.**
   `BlockAllocator` maps blocks with a low-address hint (§3.3) but does not yet
   assert the result fits in 48 bits — there are no `Cell` pointers to box in M0.
   The 48-bit assertion belongs in `value.hpp` (M1), where `make_cell` is defined.

## M1 — Values and core types

1. **String indexing is by grapheme cluster, reversing design §8.**
   Design §8 specified codepoint indexing; the project owner reversed this to
   grapheme clusters (UAX #29 "user-perceived characters") on 2026-07-04. For the
   phonetics/IPA audience a "character" is the grapheme, and this keeps behavioral
   parity with Phonometrica's String. Breadcrumbs index grapheme boundaries; a
   codepoint/byte-level API (`next_codepoint`, byte iterators, `utf8_length`)
   remains underneath. Positions in `s[i]`/`at`/`find`/`index_to_iter`/`advance`/
   `distance`/`mid`/`left`/`right`/`length` are graphemes.

2. **All M1 cells use the FOREIGN allocation path (`sys_alloc`).**
   `cell_alloc`/`cell_realloc`/`cell_free` (memory/cell_memory.cpp) allocate every
   cell via `sys_alloc` with size-class byte `SC_FOREIGN` (§11.5). Single-allocation
   growth is therefore a `sys_realloc`. The Isolate-arena path (real size classes,
   arena free lists, copy-on-grow) arrives in M4 and only that file changes.

3. **retain/release are non-atomic; the SHARED_BUFFER atomic regime is stubbed.**
   The `FLAG_SHARED_BUFFER` bit is reserved but unused in M1 (single-threaded).
   Atomic refcounting for frozen/shared buffers is wired in M7 (§8.3).

4. **Class registry is the M1 seed, not the full §6 type system.**
   `object/class.hpp` provides a flat id→Class table with finalize/clone/hash/
   equals hooks — exactly what retain/release, CoW, and hashing need. Subtype
   intervals, renumbering, epochs, metaclasses, and generic dispatch are M2.

5. **Atom table uses a single mutex, not 16 shards.**
   §5.1 specifies 16 sharded `{mutex, FlatHashMap}` interning tables; M1 uses one
   mutex + one map + one append-only name vector (single-threaded is the only
   client so far). Sharding is a concurrency optimization deferred to M7.

6. **String API is a dependency-free subset (per owner sign-off).**
   Implemented: construction, byte/codepoint/grapheme access + breadcrumbs, CoW,
   comparison/hash, append/prepend/insert/concat, clear/reserve/chop/shrink,
   replace/remove/trim, to_upper/to_lower/reverse/repeat/left/right/mid, find/
   rfind/contains/starts_with/ends_with/count, convert/to_int/to_float/to_bool/
   format, and UTF-16/UTF-32 conversion. Deferred to the milestone that supplies
   the dependency: `split`/`join` (Array<String>, M6), `replace(Regex,…)` (M5),
   positional `arg`, Qt/wxWidgets conversions, and `wstring`/`wchar_t` (Windows).

7. **Script Map/Set are unordered, reusing the one hash table (FlatHashMap).**
   The script `Map`/`Set` are a stable cell wrapping `FlatHashMap<Value,Value>` /
   `FlatHashSet<Value>` (keyed by `value_hash`/`value_equals`), with copy-on-write
   in the wrapper. This follows architecture §4 ("this single implementation backs
   … the script Map type") and drops the **insertion-ordered iteration** that
   §5.2 called for — a decision the owner made on 2026-07-04 after confirming that
   neither Phonometrica (`Table` wraps one unordered `Hashmap`; `Set` is a sorted
   `std::set`) nor calao (one cell-headed open-addressing table) provides ordered
   iteration. One hash algorithm serves both internal use (atom table, future
   dispatch memo/compiler scopes) and the script types; the cell stays put on
   growth (only the table's buffer moves), so no slot rewriting is needed (§5.0).
   `keys()`/`values()`/`to_list()` iterate in unspecified order.
