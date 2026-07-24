// Phonometrica engine — size-class free-list heap implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/base/heap.hpp>

namespace phonometrica {

// Size class layout:
//   index 0..15  -> (index + 1) * 16   =  16, 32, ..., 256
//   index 16..20 -> 512 << (index - 16) = 512, 1024, 2048, 4096, 8192
int Heap::size_class_index(intptr_t size)
{
	if (PHON_UNLIKELY(size > PHON_SMALL_MAX))
		return -1;
	if (size <= 256)
	{
		if (size < 1)
			size = 1;
		return static_cast<int>((size + 15) >> 4) - 1; // ceil(size/16) - 1
	}
	// size in 257..8192: smallest power of two >= size, floored at 512.
	uintptr_t p = next_power_of_two(static_cast<uintptr_t>(size));
	if (p < 512)
		p = 512;
	return 16 + (count_trailing_zeros(static_cast<uint64_t>(p)) - 9); // 512 == 2^9
}

intptr_t Heap::size_class_bytes(int index)
{
	PHON_ASSERT(index >= 0 && index < PHON_NUM_SIZE_CLASSES);
	if (index < 16)
		return static_cast<intptr_t>(index + 1) * 16;
	return static_cast<intptr_t>(512) << (index - 16);
}

Heap::~Heap()
{
	// Return blocks to the shared pool.
	BlockLink *b = m_blocks;
	while (b != nullptr)
	{
		BlockLink *next = b->next;
		BlockAllocator::release_block(b);
		b = next;
	}
	// Free outstanding large allocations.
	LargeLink *l = m_large;
	while (l != nullptr)
	{
		LargeLink *next = l->next;
		aligned_free64(l);
		l = next;
	}
}

void Heap::refill(int index)
{
	const intptr_t slot = size_class_bytes(index);

	void *raw = BlockAllocator::acquire_block();

	// Reserve an aligned header region at the block start for the block link.
	// PHON_CACHELINE (64) keeps every slot at least 16-byte aligned and the
	// first slot cache-line aligned.
	auto *link = static_cast<BlockLink *>(raw);
	link->next = m_blocks;
	m_blocks = link;
	++m_block_count;

	auto base = reinterpret_cast<uintptr_t>(raw) + PHON_CACHELINE;
	auto end = reinterpret_cast<uintptr_t>(raw) + PHON_BLOCK_SIZE;
	const intptr_t count = static_cast<intptr_t>((end - base) / static_cast<uintptr_t>(slot));
	PHON_ASSERT(count > 0);

	// Thread the slots onto the free list. Build front-to-back so the list head
	// ends up at the lowest address (nicer locality on first burst of allocs).
	FreeNode *head = m_free_lists[index];
	for (intptr_t i = count - 1; i >= 0; --i)
	{
		auto *node = reinterpret_cast<FreeNode *>(base + static_cast<uintptr_t>(i * slot));
		node->next = head;
		head = node;
	}
	m_free_lists[index] = head;
}

void *Heap::allocate(intptr_t size)
{
	PHON_ASSERT(size >= 0);
	const int index = size_class_index(size);
	if (PHON_UNLIKELY(index < 0))
	{
		// Large path: header + payload in one 64-aligned allocation.
		intptr_t rounded = align_up(size, PHON_CACHELINE);
		void *raw = aligned_alloc64(LARGE_HEADER + rounded);
		auto *link = static_cast<LargeLink *>(raw);
		link->next = m_large;
		link->size = rounded;
		m_large = link;
		++m_large_count;
		return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(raw) + LARGE_HEADER);
	}

	FreeNode *node = m_free_lists[index];
	if (PHON_UNLIKELY(node == nullptr))
	{
		refill(index);
		node = m_free_lists[index];
	}
	m_free_lists[index] = node->next;
	return node;
}

void Heap::deallocate(void *ptr, intptr_t size)
{
	if (ptr == nullptr)
		return;
	const int index = size_class_index(size);
	if (PHON_UNLIKELY(index < 0))
	{
		// Large path: recover the header and unlink it.
		auto *link = reinterpret_cast<LargeLink *>(reinterpret_cast<uintptr_t>(ptr) - LARGE_HEADER);
		LargeLink **pp = &m_large;
		while (*pp != nullptr && *pp != link)
			pp = &(*pp)->next;
		PHON_ASSERT_MSG(*pp == link, "deallocate: large pointer not owned by this heap");
		*pp = link->next;
		--m_large_count;
		aligned_free64(link);
		return;
	}

	auto *node = static_cast<FreeNode *>(ptr);
	node->next = m_free_lists[index];
	m_free_lists[index] = node;
}

intptr_t Heap::free_list_length(int index) const
{
	PHON_ASSERT(index >= 0 && index < PHON_NUM_SIZE_CLASSES);
	intptr_t n = 0;
	for (FreeNode *p = m_free_lists[index]; p != nullptr; p = p->next)
		++n;
	return n;
}

} // namespace phonometrica
