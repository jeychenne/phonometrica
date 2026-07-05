// Phonometrica engine — heap / size-class allocator tests, incl. stress.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/base/heap.hpp>
#include "test_framework.hpp"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

using namespace phonometrica;

TEST_CASE("size_class_index maps the 16-byte-step region")
{
	CHECK(Heap::size_class_index(1) == 0);
	CHECK(Heap::size_class_index(16) == 0);
	CHECK(Heap::size_class_index(17) == 1);
	CHECK(Heap::size_class_index(32) == 1);
	CHECK(Heap::size_class_index(255) == 15);
	CHECK(Heap::size_class_index(256) == 15);
	CHECK(Heap::size_class_bytes(0) == 16);
	CHECK(Heap::size_class_bytes(15) == 256);
}

TEST_CASE("size_class_index maps the power-of-two region")
{
	CHECK(Heap::size_class_index(257) == 16);
	CHECK(Heap::size_class_index(512) == 16);
	CHECK(Heap::size_class_index(513) == 17);
	CHECK(Heap::size_class_index(1024) == 17);
	CHECK(Heap::size_class_index(8192) == 20);
	CHECK(Heap::size_class_bytes(16) == 512);
	CHECK(Heap::size_class_bytes(20) == 8192);
	// Above the largest size class -> large path.
	CHECK(Heap::size_class_index(8193) == -1);
	CHECK(Heap::size_class_index(1 << 20) == -1);
}

TEST_CASE("class bytes cover their request range")
{
	for (intptr_t n = 1; n <= PHON_SMALL_MAX; ++n)
	{
		int idx = Heap::size_class_index(n);
		REQUIRE(idx >= 0 && idx < PHON_NUM_SIZE_CLASSES);
		CHECK(Heap::size_class_bytes(idx) >= n);
		// Should be the *smallest* class that fits.
		if (idx > 0)
			CHECK(Heap::size_class_bytes(idx - 1) < n);
	}
}

TEST_CASE("small allocations are reused from the free list")
{
	Heap heap;
	int idx = Heap::size_class_index(24);
	void *a = heap.allocate(24); // class 1 (32 bytes)
	intptr_t after_alloc = heap.free_list_length(idx);
	heap.deallocate(a, 24);
	CHECK(heap.free_list_length(idx) == after_alloc + 1); // freed slot goes back on the list
	void *b = heap.allocate(24);
	CHECK(a == b); // most-recently-freed slot handed straight back
	heap.deallocate(b, 24);
}

TEST_CASE("one block satisfies many same-class allocations")
{
	Heap heap;
	std::vector<void *> ptrs;
	for (int i = 0; i < 100; ++i)
		ptrs.push_back(heap.allocate(64));
	// 100 * 64B slots fit comfortably in a single 64 KiB block.
	CHECK(heap.block_count() == 1);
	for (void *p : ptrs)
		heap.deallocate(p, 64);
}

TEST_CASE("allocations meet alignment requirements")
{
	Heap heap;
	for (intptr_t n : {1, 16, 24, 100, 256, 512, 4096, 8192})
	{
		void *p = heap.allocate(n);
		CHECK((reinterpret_cast<uintptr_t>(p) % 16) == 0);
		heap.deallocate(p, n);
	}
	// Large allocations are 64-byte aligned.
	for (intptr_t n : {8193, 20000, 100000})
	{
		void *p = heap.allocate(n);
		CHECK((reinterpret_cast<uintptr_t>(p) % 64) == 0);
		heap.deallocate(p, n);
	}
}

TEST_CASE("large allocations are tracked and freed")
{
	Heap heap;
	void *a = heap.allocate(50000);
	void *b = heap.allocate(70000);
	CHECK(heap.large_count() == 2);
	heap.deallocate(a, 50000);
	CHECK(heap.large_count() == 1);
	heap.deallocate(b, 70000);
	CHECK(heap.large_count() == 0);
}

TEST_CASE("blocks return to the shared pool on heap destruction")
{
	BlockAllocator::trim_pool();
	{
		Heap heap;
		(void) heap.allocate(64);
		CHECK(heap.block_count() == 1);
	}
	// Block should now be cached for reuse rather than returned to the OS.
	CHECK(BlockAllocator::pooled_block_count() >= 1);
	BlockAllocator::trim_pool();
	CHECK(BlockAllocator::pooled_block_count() == 0);
}

// --- stress test: randomized alloc/free with pattern-fill overlap detection ---

TEST_CASE("allocator stress: no overlap, no corruption, sizes preserved")
{
	Heap heap;
	std::mt19937_64 rng(0xC0FFEEu);

	struct Live
	{
		void *ptr;
		intptr_t size;
		uint8_t tag;
	};
	std::vector<Live> live;

	auto fill = [](const Live &l) {
		std::memset(l.ptr, l.tag, static_cast<size_t>(l.size));
	};
	auto verify = [](const Live &l) -> bool {
		const auto *p = static_cast<const uint8_t *>(l.ptr);
		for (intptr_t i = 0; i < l.size; ++i)
			if (p[i] != l.tag)
				return false;
		return true;
	};

	// Bias toward small sizes but include the large path.
	auto random_size = [&rng]() -> intptr_t {
		int bucket = static_cast<int>(rng() % 10);
		if (bucket < 6)
			return 1 + static_cast<intptr_t>(rng() % 256);
		if (bucket < 9)
			return 257 + static_cast<intptr_t>(rng() % (8192 - 257));
		return 8193 + static_cast<intptr_t>(rng() % 40000);
	};

	uint8_t next_tag = 1;
	const int ITERS = 40000;
	for (int it = 0; it < ITERS; ++it)
	{
		bool do_alloc = live.empty() || (rng() % 100) < 60;
		if (do_alloc)
		{
			Live l;
			l.size = random_size();
			l.ptr = heap.allocate(l.size);
			l.tag = next_tag++;
			if (next_tag == 0)
				next_tag = 1;
			REQUIRE(l.ptr != nullptr);
			fill(l);
			live.push_back(l);
		}
		else
		{
			auto i = static_cast<size_t>(rng() % live.size());
			REQUIRE(verify(live[i])); // pattern intact -> no overlap/corruption
			heap.deallocate(live[i].ptr, live[i].size);
			live[i] = live.back();
			live.pop_back();
		}

		// Periodically verify every live block's pattern is still intact.
		if ((it % 2000) == 0)
		{
			for (const Live &l : live)
				REQUIRE(verify(l));
		}
	}

	// Final sweep, then drain.
	for (const Live &l : live)
	{
		CHECK(verify(l));
		heap.deallocate(l.ptr, l.size);
	}
}
