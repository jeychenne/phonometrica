// Phonometrica engine — FlatHashSet, a thin wrapper over FlatHashMap.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// A set is a map whose value is empty (design/architecture.md §4). Iteration
// yields keys directly.

#ifndef PHON_CORE_FLAT_HASH_SET_HPP
#define PHON_CORE_FLAT_HASH_SET_HPP

#include <phon/engine/base/definitions.hpp>
#include <phon/engine/core/flat_hash_map.hpp>
#include <utility>

namespace phonometrica {

namespace detail {
struct EmptyValue
{
};
} // namespace detail

template<typename K, typename Hash = Hasher<K>, typename Eq = KeyEqual<K>>
class FlatHashSet final
{
	using Map = FlatHashMap<K, detail::EmptyValue, Hash, Eq>;

public:
	template<bool Const>
	class Iter
	{
	public:
		using map_iter = std::conditional_t<Const, typename Map::const_iterator,
		                                    typename Map::iterator>;

		Iter() = default;
		explicit Iter(map_iter it) noexcept : m_it(it) {}

		const K &operator*() const noexcept { return m_it->first; }
		const K *operator->() const noexcept { return &m_it->first; }

		Iter &operator++() noexcept
		{
			++m_it;
			return *this;
		}

		bool operator==(const Iter &o) const noexcept { return m_it == o.m_it; }
		bool operator!=(const Iter &o) const noexcept { return m_it != o.m_it; }

	private:
		map_iter m_it;
	};

	using iterator = Iter<false>;
	using const_iterator = Iter<true>;

	FlatHashSet() = default;

	intptr_t size() const noexcept { return m_map.size(); }
	bool empty() const noexcept { return m_map.empty(); }
	intptr_t capacity() const noexcept { return m_map.capacity(); }
	void reserve(intptr_t n) { m_map.reserve(n); }
	void clear() noexcept { m_map.clear(); }

	bool contains(const K &key) const noexcept { return m_map.contains(key); }

	// Insert key if absent; returns (iterator, inserted?).
	std::pair<iterator, bool> insert(const K &key)
	{
		auto res = m_map.emplace(key);
		return {iterator(res.first), res.second};
	}

	template<typename KK>
	std::pair<iterator, bool> emplace(KK &&key)
	{
		auto res = m_map.emplace(std::forward<KK>(key));
		return {iterator(res.first), res.second};
	}

	intptr_t erase(const K &key) { return m_map.erase(key); }

	iterator find(const K &key) noexcept { return iterator(m_map.find(key)); }
	const_iterator find(const K &key) const noexcept { return const_iterator(m_map.find(key)); }

	iterator begin() noexcept { return iterator(m_map.begin()); }
	iterator end() noexcept { return iterator(m_map.end()); }
	const_iterator begin() const noexcept { return const_iterator(m_map.begin()); }
	const_iterator end() const noexcept { return const_iterator(m_map.end()); }

private:
	Map m_map;
};

} // namespace phonometrica

#endif // PHON_CORE_FLAT_HASH_SET_HPP
