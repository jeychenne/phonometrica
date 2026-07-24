// Phonometrica engine — allocation primitives implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/base/alloc.hpp>

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>

#if defined(__unix__) || defined(__APPLE__)
#	include <sys/mman.h>
#	include <unistd.h>
#	define PHON_HAVE_MMAP 1
#else
#	define PHON_HAVE_MMAP 0
#endif

namespace phonometrica {

void out_of_memory(intptr_t requested)
{
	std::fprintf(stderr, "Out of memory: failed to allocate %lld bytes\n",
	             static_cast<long long>(requested));
	std::fflush(stderr);
	std::abort();
}

// ---------------------------------------------------------------------------
// Byte allocation
// ---------------------------------------------------------------------------

void *sys_alloc(intptr_t size)
{
	PHON_ASSERT(size >= 0);
	// malloc(0) may return null legitimately; normalize to a 1-byte request so
	// callers can always distinguish success from OOM by the null check.
	void *p = std::malloc(size == 0 ? 1 : static_cast<size_t>(size));
	if (PHON_UNLIKELY(p == nullptr))
		out_of_memory(size);
	return p;
}

void *sys_realloc(void *ptr, intptr_t size)
{
	PHON_ASSERT(size >= 0);
	void *p = std::realloc(ptr, size == 0 ? 1 : static_cast<size_t>(size));
	if (PHON_UNLIKELY(p == nullptr))
		out_of_memory(size);
	return p;
}

void sys_free(void *ptr)
{
	std::free(ptr);
}

void *raw_alloc(intptr_t size, intptr_t align)
{
	PHON_ASSERT(size >= 0);
	PHON_ASSERT(is_power_of_two(static_cast<uintptr_t>(align)));
	if (align <= static_cast<intptr_t>(alignof(std::max_align_t)))
		return sys_alloc(size);

	// Over-aligned: std::aligned_alloc requires size to be a multiple of align.
	intptr_t rounded = align_up(size == 0 ? align : size, align);
	void *p = std::aligned_alloc(static_cast<size_t>(align), static_cast<size_t>(rounded));
	if (PHON_UNLIKELY(p == nullptr))
		out_of_memory(size);
	return p;
}

void raw_free(void *ptr, intptr_t align)
{
	PHON_UNUSED(align);
	// Both malloc and aligned_alloc results are released with std::free.
	std::free(ptr);
}

void *aligned_alloc64(intptr_t size)
{
	intptr_t rounded = align_up(size == 0 ? PHON_CACHELINE : size, PHON_CACHELINE);
	void *p = std::aligned_alloc(static_cast<size_t>(PHON_CACHELINE),
	                             static_cast<size_t>(rounded));
	if (PHON_UNLIKELY(p == nullptr))
		out_of_memory(size);
	return p;
}

void aligned_free64(void *ptr)
{
	std::free(ptr);
}

// ---------------------------------------------------------------------------
// Block allocator
// ---------------------------------------------------------------------------

namespace {

// Intrusive free-list node written into the first bytes of a pooled block.
struct PooledBlock
{
	PooledBlock *next;
};

std::mutex g_pool_mutex;
PooledBlock *g_pool_head = nullptr;
intptr_t g_pool_count = 0;

// Map one block from the OS with a low-address hint so cell pointers stay below
// 2^47 (design/architecture.md §3.3). If the hint is ignored we still return a
// usable block; the 48-bit assertion lives in value.hpp (M1), not here.
void *os_map_block()
{
#if PHON_HAVE_MMAP
	// Hint into the low canonical range; the kernel may relocate but usually
	// honors a free hint. MAP_32BIT is too small (4 GiB), so use an explicit hint.
	static uintptr_t hint = 0x0000'1000'0000'0000ull; // 16 TiB, well under 2^47
	void *p = ::mmap(reinterpret_cast<void *>(hint), static_cast<size_t>(PHON_BLOCK_SIZE),
	                 PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		out_of_memory(PHON_BLOCK_SIZE);
	// Advance the hint so successive maps stay contiguous and low.
	hint += static_cast<uintptr_t>(PHON_BLOCK_SIZE);
	return p;
#else
	// Portable fallback: block-aligned allocation.
	void *p = std::aligned_alloc(static_cast<size_t>(PHON_BLOCK_SIZE),
	                             static_cast<size_t>(PHON_BLOCK_SIZE));
	if (p == nullptr)
		out_of_memory(PHON_BLOCK_SIZE);
	return p;
#endif
}

void os_unmap_block(void *block)
{
#if PHON_HAVE_MMAP
	::munmap(block, static_cast<size_t>(PHON_BLOCK_SIZE));
#else
	std::free(block);
#endif
}

} // namespace

void *BlockAllocator::acquire_block()
{
	{
		std::lock_guard<std::mutex> lock(g_pool_mutex);
		if (g_pool_head != nullptr)
		{
			PooledBlock *b = g_pool_head;
			g_pool_head = b->next;
			--g_pool_count;
			return b;
		}
	}
	return os_map_block();
}

void BlockAllocator::release_block(void *block)
{
	PHON_ASSERT(block != nullptr);
	auto *b = static_cast<PooledBlock *>(block);
	std::lock_guard<std::mutex> lock(g_pool_mutex);
	b->next = g_pool_head;
	g_pool_head = b;
	++g_pool_count;
}

void BlockAllocator::trim_pool()
{
	PooledBlock *head;
	{
		std::lock_guard<std::mutex> lock(g_pool_mutex);
		head = g_pool_head;
		g_pool_head = nullptr;
		g_pool_count = 0;
	}
	while (head != nullptr)
	{
		PooledBlock *next = head->next;
		os_unmap_block(head);
		head = next;
	}
}

intptr_t BlockAllocator::pooled_block_count()
{
	std::lock_guard<std::mutex> lock(g_pool_mutex);
	return g_pool_count;
}

} // namespace phonometrica
