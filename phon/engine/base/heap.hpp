// Phonometrica engine — size-class free-list heap.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// A per-thread heap (design/architecture.md §8.1). It carves 64 KiB blocks from
// BlockAllocator into size-segregated free lists:
//
//   * 16-byte steps up to 256 bytes           -> classes 0..15
//   * powers of two 512, 1024, ... 8192       -> classes 16..20
//   * anything larger                         -> individual aligned_alloc64
//
// deallocate() is *sized*: the caller supplies the original request size, which
// is exactly what the runtime already has to hand (the size-class byte in a Cell
// header, §3.2). This avoids per-slot metadata and lets release() return memory
// to the correct free list in O(1).
//
// A Heap is owned by one thread and is NOT internally synchronized; only the
// underlying BlockAllocator pool is. On destruction the heap returns its blocks
// to the pool and frees any outstanding large allocations.

#ifndef PHON_BASE_HEAP_HPP
#define PHON_BASE_HEAP_HPP

#include <phon/engine/base/alloc.hpp>
#include <phon/engine/base/definitions.hpp>

namespace phonometrica {

// Largest request served from a size class; above this we go large.
inline constexpr intptr_t PHON_SMALL_MAX = 8192;
inline constexpr int PHON_NUM_SIZE_CLASSES = 21; // 16 stepped + 5 power-of-two

class Heap final
{
public:
	Heap() = default;
	~Heap();

	PHON_DISABLE_COPY(Heap)

	// Allocate at least `size` bytes, aligned to at least 16 (large allocations
	// are 64-byte aligned). Never returns null.
	void *allocate(intptr_t size);

	// Return a block obtained from allocate(); `size` must be the same value
	// passed to allocate() (see class comment).
	void deallocate(void *ptr, intptr_t size);

	// --- static size-class arithmetic (pure, no state) ---

	// Map a request size to its size-class index, or -1 for the large path.
	static int size_class_index(intptr_t size);

	// The number of usable bytes a given size class provides.
	static intptr_t size_class_bytes(int index);

	// --- introspection for tests ---

	// Count of free slots currently held on a given size class's free list.
	intptr_t free_list_length(int index) const;

	// Number of 64 KiB blocks this heap currently owns.
	intptr_t block_count() const { return m_block_count; }

	// Number of outstanding large allocations.
	intptr_t large_count() const { return m_large_count; }

private:
	struct FreeNode
	{
		FreeNode *next;
	};

	// Header written at the start of every 64 KiB block owned by this heap.
	struct BlockLink
	{
		BlockLink *next;
	};

	// Header placed before every large payload (keeps the payload 64-aligned).
	struct LargeLink
	{
		LargeLink *next;
		intptr_t size; // usable payload size (rounded)
	};
	static constexpr intptr_t LARGE_HEADER = 64; // preserves 64-byte alignment

	void refill(int index);

	FreeNode *m_free_lists[PHON_NUM_SIZE_CLASSES] = {};
	BlockLink *m_blocks = nullptr;
	LargeLink *m_large = nullptr;
	intptr_t m_block_count = 0;
	intptr_t m_large_count = 0;
};

} // namespace phonometrica

#endif // PHON_BASE_HEAP_HPP
