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
