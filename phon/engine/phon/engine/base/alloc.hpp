// Phonometrica engine — raw allocation primitives and the page/block allocator.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Three tiers live here:
//   * sys_alloc/sys_free/sys_realloc — malloc-backed, OOM-checked bytes.
//   * raw_alloc/raw_free            — over-alignment-aware bytes (container store).
//   * aligned_alloc64               — 64-byte aligned buffers (NumArray data, §5.3).
//   * BlockAllocator                — 64 KiB blocks mapped low (< 2^47, §3.3),
//                                     recycled through a process-global pool.
// The size-class free-list heap that consumes blocks lives in base/heap.hpp.

#ifndef PHON_BASE_ALLOC_HPP
#define PHON_BASE_ALLOC_HPP

#include <phon/engine/base/bits.hpp>
#include <phon/engine/base/definitions.hpp>

namespace phonometrica {

// 64 KiB blocks, per §8.1.
inline constexpr intptr_t PHON_BLOCK_SIZE = 64 * 1024;

// NumArray buffers and other SIMD payloads are aligned to 64 bytes (AVX-512).
inline constexpr intptr_t PHON_CACHELINE = 64;

[[noreturn]] void out_of_memory(intptr_t requested);

// ---------------------------------------------------------------------------
// Byte allocation
// ---------------------------------------------------------------------------

// malloc-backed; aborts through out_of_memory on failure. Alignment is
// alignof(max_align_t) (16 on LP64), enough for Value, Handle, Cell.
void *sys_alloc(intptr_t size);
void *sys_realloc(void *ptr, intptr_t size);
void sys_free(void *ptr);

// Over-alignment-aware allocation for container storage. When align is no
// greater than max_align_t this is exactly sys_alloc; larger alignments use the
// aligned path. Free the result with raw_free.
void *raw_alloc(intptr_t size, intptr_t align);
void raw_free(void *ptr, intptr_t align);

// 64-byte aligned buffer (free with aligned_free64).
void *aligned_alloc64(intptr_t size);
void aligned_free64(void *ptr);

// ---------------------------------------------------------------------------
// Block allocator: 64 KiB blocks for the size-class heap
// ---------------------------------------------------------------------------
//
// Blocks are mapped with a low-address hint so cell pointers fit in 48 bits
// (design/architecture.md §3.3). Freed blocks are cached in a process-global,
// mutex-protected pool so the common case (thread birth/death) never calls the
// OS. The pool is a leaf utility with no dependency on higher layers.

class BlockAllocator final
{
public:
	// Obtain one PHON_BLOCK_SIZE block (from the pool if available, else the OS).
	// The block is PHON_BLOCK_SIZE-aligned. Never returns null (aborts on OOM).
	static void *acquire_block();

	// Return a block to the pool for reuse.
	static void release_block(void *block);

	// Drain the pool back to the OS. Intended for shutdown / leak-checking.
	static void trim_pool();

	// Number of blocks currently cached in the pool (test/introspection).
	static intptr_t pooled_block_count();
};

} // namespace phonometrica

#endif // PHON_BASE_ALLOC_HPP
