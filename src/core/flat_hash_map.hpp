// Phonometrica engine — FlatHashMap, a Swiss-table open-addressing hash map.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// One implementation backs the script Map, atom-table shards, module namespaces,
// dispatch memo tables, and compiler scopes (design/architecture.md §4). Design:
//
//   * A control byte per slot: kEmpty (0x80), kDeleted (0xFE), or a 7-bit hash
//     tag (0x00..0x7F) for full slots.
//   * Slots probed 16 at a time as a "group" (SSE2 where available, portable
//     scalar fallback), using triangular probing over power-of-two capacities.
//   * Max load factor 7/8. Erase leaves a tombstone; tombstones are reused on
//     insert and cleared on rehash. (The Abseil "reclaim tombstone to empty on
//     erase" optimization is deferred — an M8 tuning knob; correctness here does
//     not depend on it.)
//
// Iteration order is unspecified (this is the general table; the ordered script
// Map of §5.2 is a separate type).

#ifndef PHON_CORE_FLAT_HASH_MAP_HPP
#define PHON_CORE_FLAT_HASH_MAP_HPP

#include "base/alloc.hpp"
#include "base/bits.hpp"
#include "base/definitions.hpp"
#include "core/hash.hpp"
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

// Define PHON_NO_SSE2 to force the portable scalar group path (used to test the
// fallback on x86, and taken automatically on non-SSE2 targets).
#if defined(__SSE2__) && !defined(PHON_NO_SSE2)
#	include <emmintrin.h>
#	define PHON_SWISS_SSE2 1
#else
#	define PHON_SWISS_SSE2 0
#endif

namespace phonometrica {

namespace swiss {

using ctrl_t = int8_t;
inline constexpr ctrl_t kEmpty = -128;   // 0x80
inline constexpr ctrl_t kDeleted = -2;   // 0xFE
inline constexpr int kWidth = 16;

PHON_FORCE_INLINE bool is_full(ctrl_t c) noexcept { return c >= 0; }
PHON_FORCE_INLINE bool is_empty(ctrl_t c) noexcept { return c == kEmpty; }

PHON_FORCE_INLINE uint64_t h1(uint64_t hash) noexcept { return hash >> 7; }
PHON_FORCE_INLINE ctrl_t h2(uint64_t hash) noexcept { return static_cast<ctrl_t>(hash & 0x7F); }

// Iterate the set bits (matching group lanes) of a 16-bit mask, low to high.
class BitMask
{
public:
	explicit BitMask(uint32_t mask) noexcept : m_mask(mask) {}
	bool any() const noexcept { return m_mask != 0; }
	int lowest() const noexcept { return count_trailing_zeros(m_mask); }
	void clear_lowest() noexcept { m_mask &= (m_mask - 1); }
	explicit operator bool() const noexcept { return m_mask != 0; }

private:
	uint32_t m_mask;
};

// A 16-lane view over control bytes.
class Group
{
public:
	explicit Group(const ctrl_t *p) noexcept
	{
#if PHON_SWISS_SSE2
		m_ctrl = _mm_loadu_si128(reinterpret_cast<const __m128i *>(p));
#else
		std::memcpy(m_bytes, p, kWidth);
#endif
	}

	BitMask match(ctrl_t tag) const noexcept
	{
#if PHON_SWISS_SSE2
		auto match = _mm_set1_epi8(tag);
		return BitMask(static_cast<uint32_t>(
		    _mm_movemask_epi8(_mm_cmpeq_epi8(match, m_ctrl))));
#else
		uint32_t mask = 0;
		for (int i = 0; i < kWidth; ++i)
			if (m_bytes[i] == tag)
				mask |= (1u << i);
		return BitMask(mask);
#endif
	}

	BitMask match_empty() const noexcept { return match(kEmpty); }

	// Empty or deleted lanes: exactly the bytes with the sign bit set (full
	// slots are 0..127, non-negative).
	BitMask match_empty_or_deleted() const noexcept
	{
#if PHON_SWISS_SSE2
		return BitMask(static_cast<uint32_t>(_mm_movemask_epi8(m_ctrl)));
#else
		uint32_t mask = 0;
		for (int i = 0; i < kWidth; ++i)
			if (m_bytes[i] < 0)
				mask |= (1u << i);
		return BitMask(mask);
#endif
	}

private:
#if PHON_SWISS_SSE2
	__m128i m_ctrl;
#else
	ctrl_t m_bytes[kWidth];
#endif
};

// max_load(cap) = 7/8 cap (floor). cap is a power of two >= kWidth.
PHON_FORCE_INLINE intptr_t max_load(intptr_t cap) noexcept { return cap - cap / 8; }

} // namespace swiss

template<typename K, typename V, typename Hash = Hasher<K>, typename Eq = KeyEqual<K>>
class FlatHashMap final
{
public:
	struct Entry
	{
		K first;
		V second;
	};

private:
	using ctrl_t = swiss::ctrl_t;
	static constexpr int kWidth = swiss::kWidth;
	static constexpr intptr_t INITIAL_CAPACITY = 16;

public:
	template<bool Const>
	class Iter
	{
	public:
		using entry_t = std::conditional_t<Const, const Entry, Entry>;

		Iter() = default;
		Iter(ctrl_t *ctrl, entry_t *slot, ctrl_t *ctrl_end) noexcept
		    : m_ctrl(ctrl), m_slot(slot), m_end(ctrl_end)
		{
			skip_to_full();
		}

		entry_t &operator*() const noexcept { return *m_slot; }
		entry_t *operator->() const noexcept { return m_slot; }

		Iter &operator++() noexcept
		{
			++m_ctrl;
			++m_slot;
			skip_to_full();
			return *this;
		}

		bool operator==(const Iter &o) const noexcept { return m_ctrl == o.m_ctrl; }
		bool operator!=(const Iter &o) const noexcept { return m_ctrl != o.m_ctrl; }

	private:
		void skip_to_full() noexcept
		{
			while (m_ctrl != m_end && !swiss::is_full(*m_ctrl))
			{
				++m_ctrl;
				++m_slot;
			}
		}

		ctrl_t *m_ctrl = nullptr;
		entry_t *m_slot = nullptr;
		ctrl_t *m_end = nullptr;
	};

	using iterator = Iter<false>;
	using const_iterator = Iter<true>;

	FlatHashMap() = default;

	FlatHashMap(const FlatHashMap &other)
	{
		reserve(other.m_size);
		for (const Entry &e : other)
			emplace(e.first, e.second);
	}

	FlatHashMap(FlatHashMap &&other) noexcept { swap_members(other); }

	FlatHashMap &operator=(const FlatHashMap &other)
	{
		if (this != &other)
		{
			clear();
			reserve(other.m_size);
			for (const Entry &e : other)
				emplace(e.first, e.second);
		}
		return *this;
	}

	FlatHashMap &operator=(FlatHashMap &&other) noexcept
	{
		if (this != &other)
		{
			destroy_all();
			m_ctrl = nullptr;
			m_slots = nullptr;
			m_capacity = 0;
			m_size = 0;
			m_deleted = 0;
			m_growth_left = 0;
			swap_members(other);
		}
		return *this;
	}

	~FlatHashMap() { destroy_all(); }

	// --- capacity ---

	intptr_t size() const noexcept { return m_size; }
	bool empty() const noexcept { return m_size == 0; }
	intptr_t capacity() const noexcept { return m_capacity; }

	void reserve(intptr_t n)
	{
		if (n <= 0)
			return;
		// Smallest power-of-two capacity whose 7/8 load holds n elements.
		intptr_t cap = m_capacity < INITIAL_CAPACITY ? INITIAL_CAPACITY : m_capacity;
		while (swiss::max_load(cap) < n)
			cap *= 2;
		if (cap > m_capacity)
			rehash(cap);
	}

	// --- lookup ---

	iterator find(const K &key) noexcept
	{
		intptr_t idx = find_index(key);
		if (idx < 0)
			return end();
		return iterator(m_ctrl + idx, m_slots + idx, m_ctrl + m_capacity);
	}

	const_iterator find(const K &key) const noexcept
	{
		intptr_t idx = find_index(key);
		if (idx < 0)
			return end();
		return const_iterator(m_ctrl + idx, m_slots + idx, m_ctrl + m_capacity);
	}

	bool contains(const K &key) const noexcept { return find_index(key) >= 0; }

	// --- modifiers ---

	V &operator[](const K &key)
	{
		auto res = find_or_prepare_insert(key);
		if (res.second)
		{
			::new (static_cast<void *>(&m_slots[res.first].first)) K(key);
			::new (static_cast<void *>(&m_slots[res.first].second)) V();
		}
		return m_slots[res.first].second;
	}

	// Insert if absent; returns (iterator, inserted?).
	std::pair<iterator, bool> insert(const K &key, const V &value)
	{
		return emplace(key, value);
	}

	template<typename KK, typename... Args>
	std::pair<iterator, bool> emplace(KK &&key, Args &&...args)
	{
		auto res = find_or_prepare_insert(key);
		if (res.second)
		{
			::new (static_cast<void *>(&m_slots[res.first].first)) K(std::forward<KK>(key));
			::new (static_cast<void *>(&m_slots[res.first].second)) V(std::forward<Args>(args)...);
		}
		return {iterator(m_ctrl + res.first, m_slots + res.first, m_ctrl + m_capacity),
		        res.second};
	}

	// Insert or overwrite the existing value.
	std::pair<iterator, bool> insert_or_assign(const K &key, const V &value)
	{
		auto res = find_or_prepare_insert(key);
		if (res.second)
		{
			::new (static_cast<void *>(&m_slots[res.first].first)) K(key);
			::new (static_cast<void *>(&m_slots[res.first].second)) V(value);
		}
		else
		{
			m_slots[res.first].second = value;
		}
		return {iterator(m_ctrl + res.first, m_slots + res.first, m_ctrl + m_capacity),
		        res.second};
	}

	// Remove key if present; returns the number erased (0 or 1).
	intptr_t erase(const K &key)
	{
		intptr_t idx = find_index(key);
		if (idx < 0)
			return 0;
		erase_at(idx);
		return 1;
	}

	void clear() noexcept
	{
		if (m_capacity == 0)
			return;
		for (intptr_t i = 0; i < m_capacity; ++i)
		{
			if (swiss::is_full(m_ctrl[i]))
				m_slots[i].~Entry();
		}
		std::memset(m_ctrl, static_cast<int>(swiss::kEmpty),
		            static_cast<size_t>(m_capacity + kWidth));
		m_size = 0;
		m_deleted = 0;
		m_growth_left = swiss::max_load(m_capacity);
	}

	// --- iteration ---

	iterator begin() noexcept
	{
		return iterator(m_ctrl, m_slots, m_ctrl + m_capacity);
	}
	iterator end() noexcept
	{
		return iterator(m_ctrl + m_capacity, m_slots + m_capacity, m_ctrl + m_capacity);
	}
	const_iterator begin() const noexcept
	{
		return const_iterator(m_ctrl, m_slots, m_ctrl + m_capacity);
	}
	const_iterator end() const noexcept
	{
		return const_iterator(m_ctrl + m_capacity, m_slots + m_capacity, m_ctrl + m_capacity);
	}

private:
	// Locate an existing key; returns its slot index or -1.
	intptr_t find_index(const K &key) const noexcept
	{
		if (m_capacity == 0)
			return -1;
		const uint64_t hash = Hash{}(key);
		const ctrl_t tag = swiss::h2(hash);
		const uintptr_t mask = static_cast<uintptr_t>(m_capacity - 1);
		uintptr_t pos = static_cast<uintptr_t>(swiss::h1(hash)) & mask;
		uintptr_t stride = 0;
		for (;;)
		{
			swiss::Group g(m_ctrl + pos);
			for (auto bits = g.match(tag); bits.any(); bits.clear_lowest())
			{
				uintptr_t idx = (pos + static_cast<uintptr_t>(bits.lowest())) & mask;
				if (PHON_LIKELY(Eq{}(m_slots[idx].first, key)))
					return static_cast<intptr_t>(idx);
			}
			if (g.match_empty().any())
				return -1;
			stride += kWidth;
			pos = (pos + stride) & mask;
			PHON_ASSERT(stride <= static_cast<uintptr_t>(m_capacity));
		}
	}

	// Find the first empty-or-deleted slot along key's probe path.
	intptr_t find_first_non_full(uint64_t hash) const noexcept
	{
		const uintptr_t mask = static_cast<uintptr_t>(m_capacity - 1);
		uintptr_t pos = static_cast<uintptr_t>(swiss::h1(hash)) & mask;
		uintptr_t stride = 0;
		for (;;)
		{
			swiss::Group g(m_ctrl + pos);
			auto bits = g.match_empty_or_deleted();
			if (bits.any())
				return static_cast<intptr_t>((pos + static_cast<uintptr_t>(bits.lowest())) & mask);
			stride += kWidth;
			pos = (pos + stride) & mask;
			PHON_ASSERT(stride <= static_cast<uintptr_t>(m_capacity));
		}
	}

	// Returns (slot_index, inserted). When inserted, the slot's ctrl byte is set
	// and size accounted, but the Entry is NOT yet constructed (caller does it).
	std::pair<intptr_t, bool> find_or_prepare_insert(const K &key)
	{
		if (m_capacity == 0)
			rehash(INITIAL_CAPACITY);

		const uint64_t hash = Hash{}(key);
		const ctrl_t tag = swiss::h2(hash);
		const uintptr_t mask = static_cast<uintptr_t>(m_capacity - 1);
		uintptr_t pos = static_cast<uintptr_t>(swiss::h1(hash)) & mask;
		uintptr_t stride = 0;
		for (;;)
		{
			swiss::Group g(m_ctrl + pos);
			for (auto bits = g.match(tag); bits.any(); bits.clear_lowest())
			{
				uintptr_t idx = (pos + static_cast<uintptr_t>(bits.lowest())) & mask;
				if (PHON_LIKELY(Eq{}(m_slots[idx].first, key)))
					return {static_cast<intptr_t>(idx), false};
			}
			if (g.match_empty().any())
				break; // key is absent
			stride += kWidth;
			pos = (pos + stride) & mask;
			PHON_ASSERT(stride <= static_cast<uintptr_t>(m_capacity));
		}

		intptr_t target = find_first_non_full(hash);
		if (swiss::is_empty(m_ctrl[target]) && m_growth_left == 0)
		{
			rehash_grow();
			target = find_first_non_full(hash); // now guaranteed empty
		}
		const bool was_empty = swiss::is_empty(m_ctrl[target]);
		set_ctrl(target, tag);
		if (was_empty)
			--m_growth_left;
		else
			--m_deleted;
		++m_size;
		return {target, true};
	}

	void erase_at(intptr_t idx) noexcept
	{
		m_slots[idx].~Entry();
		set_ctrl(idx, swiss::kDeleted);
		++m_deleted;
		--m_size;
	}

	void set_ctrl(intptr_t i, ctrl_t c) noexcept
	{
		m_ctrl[i] = c;
		// Mirror the first kWidth control bytes into the wrap-around tail so a
		// group load starting near the end reads a coherent group.
		if (i < kWidth)
			m_ctrl[m_capacity + i] = c;
	}

	void rehash_grow()
	{
		intptr_t new_cap;
		if (m_size + 1 > swiss::max_load(m_capacity))
			new_cap = m_capacity * 2; // genuinely full
		else
			new_cap = m_capacity; // dominated by tombstones; same-size rehash clears them
		rehash(new_cap);
	}

	// Allocate a fresh table of new_cap slots and reinsert all live entries.
	void rehash(intptr_t new_cap)
	{
		PHON_ASSERT(is_power_of_two(static_cast<uintptr_t>(new_cap)) && new_cap >= kWidth);

		ctrl_t *old_ctrl = m_ctrl;
		Entry *old_slots = m_slots;
		intptr_t old_cap = m_capacity;

		allocate(new_cap);

		if (old_cap != 0)
		{
			for (intptr_t i = 0; i < old_cap; ++i)
			{
				if (swiss::is_full(old_ctrl[i]))
				{
					insert_unique_moving(std::move(old_slots[i]));
					old_slots[i].~Entry();
				}
			}
			free_table(old_ctrl, old_cap);
		}
	}

	// Insert an entry known to be absent, into a table known to have room.
	void insert_unique_moving(Entry &&e)
	{
		const uint64_t hash = Hash{}(e.first);
		intptr_t target = find_first_non_full(hash);
		set_ctrl(target, swiss::h2(hash));
		::new (static_cast<void *>(&m_slots[target])) Entry(std::move(e));
		--m_growth_left;
		++m_size;
	}

	// Layout: [ctrl bytes (cap + kWidth)] [pad to alignof(Entry)] [Entry slots].
	static intptr_t slots_offset(intptr_t cap) noexcept
	{
		return align_up(cap + kWidth, static_cast<intptr_t>(alignof(Entry)));
	}

	void allocate(intptr_t cap)
	{
		const intptr_t bytes = slots_offset(cap) + cap * static_cast<intptr_t>(sizeof(Entry));
		void *raw = raw_alloc(bytes, static_cast<intptr_t>(alignof(Entry)) > 16
		                                 ? static_cast<intptr_t>(alignof(Entry))
		                                 : 16);
		m_ctrl = static_cast<ctrl_t *>(raw);
		m_slots = reinterpret_cast<Entry *>(static_cast<char *>(raw) + slots_offset(cap));
		std::memset(m_ctrl, static_cast<int>(swiss::kEmpty),
		            static_cast<size_t>(cap + kWidth));
		m_capacity = cap;
		m_size = 0;
		m_deleted = 0;
		m_growth_left = swiss::max_load(cap);
	}

	void free_table(ctrl_t *ctrl, intptr_t cap) noexcept
	{
		PHON_UNUSED(cap);
		intptr_t align = static_cast<intptr_t>(alignof(Entry)) > 16
		                     ? static_cast<intptr_t>(alignof(Entry))
		                     : 16;
		raw_free(ctrl, align);
	}

	void destroy_all() noexcept
	{
		if (m_capacity == 0)
			return;
		for (intptr_t i = 0; i < m_capacity; ++i)
		{
			if (swiss::is_full(m_ctrl[i]))
				m_slots[i].~Entry();
		}
		free_table(m_ctrl, m_capacity);
	}

	void swap_members(FlatHashMap &o) noexcept
	{
		std::swap(m_ctrl, o.m_ctrl);
		std::swap(m_slots, o.m_slots);
		std::swap(m_capacity, o.m_capacity);
		std::swap(m_size, o.m_size);
		std::swap(m_deleted, o.m_deleted);
		std::swap(m_growth_left, o.m_growth_left);
	}

	ctrl_t *m_ctrl = nullptr;   // capacity + kWidth control bytes
	Entry *m_slots = nullptr;   // capacity entries
	intptr_t m_capacity = 0;    // power of two, or 0
	intptr_t m_size = 0;        // live entries
	intptr_t m_deleted = 0;     // tombstones
	intptr_t m_growth_left = 0; // empty slots consumable before rehash
};

} // namespace phonometrica

#endif // PHON_CORE_FLAT_HASH_MAP_HPP
