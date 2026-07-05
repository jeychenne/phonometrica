// Phonometrica engine — Vector<T>, the workhorse growable buffer.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Contiguous, move-only-friendly, intptr_t-sized. Growth factor 1.5, initial
// capacity 8 (design/architecture.md §4). Grows with memcpy when T is trivially
// relocatable, else with move+destroy. Storage comes from raw_alloc so that
// over-aligned T are handled. List (§5.2) wraps Vector<Value>.

#ifndef PHON_CORE_VECTOR_HPP
#define PHON_CORE_VECTOR_HPP

#include <phon/base/alloc.hpp>
#include <phon/base/definitions.hpp>
#include <phon/core/relocate.hpp>
#include <initializer_list>
#include <new>
#include <type_traits>
#include <utility>

namespace phonometrica {

template<typename T>
class Vector final
{
public:
	using value_type = T;
	using iterator = T *;
	using const_iterator = const T *;

	static constexpr intptr_t INITIAL_CAPACITY = 8;

	Vector() = default;

	explicit Vector(intptr_t n) { resize(n); }

	Vector(intptr_t n, const T &value) { assign(n, value); }

	Vector(std::initializer_list<T> init)
	{
		reserve(static_cast<intptr_t>(init.size()));
		for (const T &x : init)
			push_back(x);
	}

	Vector(const Vector &other)
	{
		reserve(other.m_size);
		for (intptr_t i = 0; i < other.m_size; ++i)
			::new (static_cast<void *>(m_data + i)) T(other.m_data[i]);
		m_size = other.m_size;
	}

	Vector(Vector &&other) noexcept
	    : m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity)
	{
		other.m_data = nullptr;
		other.m_size = 0;
		other.m_capacity = 0;
	}

	Vector &operator=(const Vector &other)
	{
		if (this != &other)
		{
			clear();
			reserve(other.m_size);
			for (intptr_t i = 0; i < other.m_size; ++i)
				::new (static_cast<void *>(m_data + i)) T(other.m_data[i]);
			m_size = other.m_size;
		}
		return *this;
	}

	Vector &operator=(Vector &&other) noexcept
	{
		if (this != &other)
		{
			free_storage();
			m_data = other.m_data;
			m_size = other.m_size;
			m_capacity = other.m_capacity;
			other.m_data = nullptr;
			other.m_size = 0;
			other.m_capacity = 0;
		}
		return *this;
	}

	~Vector() { free_storage(); }

	// --- capacity ---

	intptr_t size() const noexcept { return m_size; }
	intptr_t capacity() const noexcept { return m_capacity; }
	bool empty() const noexcept { return m_size == 0; }

	void reserve(intptr_t n)
	{
		if (n > m_capacity)
			reallocate(n);
	}

	void shrink_to_fit()
	{
		if (m_size < m_capacity)
			reallocate(m_size);
	}

	// --- element access ---

	T &operator[](intptr_t i) noexcept
	{
		PHON_ASSERT(i >= 0 && i < m_size);
		return m_data[i];
	}
	const T &operator[](intptr_t i) const noexcept
	{
		PHON_ASSERT(i >= 0 && i < m_size);
		return m_data[i];
	}

	T &front() noexcept { PHON_ASSERT(m_size > 0); return m_data[0]; }
	const T &front() const noexcept { PHON_ASSERT(m_size > 0); return m_data[0]; }
	T &back() noexcept { PHON_ASSERT(m_size > 0); return m_data[m_size - 1]; }
	const T &back() const noexcept { PHON_ASSERT(m_size > 0); return m_data[m_size - 1]; }

	T *data() noexcept { return m_data; }
	const T *data() const noexcept { return m_data; }

	iterator begin() noexcept { return m_data; }
	iterator end() noexcept { return m_data + m_size; }
	const_iterator begin() const noexcept { return m_data; }
	const_iterator end() const noexcept { return m_data + m_size; }

	// --- modifiers ---

	void push_back(const T &value)
	{
		if (PHON_UNLIKELY(m_size == m_capacity))
			grow_one();
		::new (static_cast<void *>(m_data + m_size)) T(value);
		++m_size;
	}

	void push_back(T &&value)
	{
		if (PHON_UNLIKELY(m_size == m_capacity))
			grow_one();
		::new (static_cast<void *>(m_data + m_size)) T(std::move(value));
		++m_size;
	}

	template<typename... Args>
	T &emplace_back(Args &&...args)
	{
		if (PHON_UNLIKELY(m_size == m_capacity))
			grow_one();
		T *slot = m_data + m_size;
		::new (static_cast<void *>(slot)) T(std::forward<Args>(args)...);
		++m_size;
		return *slot;
	}

	void pop_back() noexcept
	{
		PHON_ASSERT(m_size > 0);
		--m_size;
		m_data[m_size].~T();
	}

	// Stable erase: shifts the tail down by one (like std::vector::erase).
	void erase(intptr_t i)
	{
		PHON_ASSERT(i >= 0 && i < m_size);
		for (intptr_t j = i; j + 1 < m_size; ++j)
			m_data[j] = std::move(m_data[j + 1]);
		pop_back();
	}

	// Unstable erase: O(1), moves the last element into the hole.
	void erase_unordered(intptr_t i)
	{
		PHON_ASSERT(i >= 0 && i < m_size);
		if (i != m_size - 1)
			m_data[i] = std::move(m_data[m_size - 1]);
		pop_back();
	}

	// Stable insert before index i (i == size() appends).
	void insert(intptr_t i, const T &value)
	{
		PHON_ASSERT(i >= 0 && i <= m_size);
		if (PHON_UNLIKELY(m_size == m_capacity))
			grow_one();
		if (i == m_size)
		{
			::new (static_cast<void *>(m_data + m_size)) T(value);
		}
		else
		{
			// Open a hole at the end, then shift up.
			::new (static_cast<void *>(m_data + m_size)) T(std::move(m_data[m_size - 1]));
			for (intptr_t j = m_size - 1; j > i; --j)
				m_data[j] = std::move(m_data[j - 1]);
			m_data[i] = value;
		}
		++m_size;
	}

	void resize(intptr_t n)
	{
		PHON_ASSERT(n >= 0);
		if (n < m_size)
		{
			destroy_range(m_data + n, m_size - n);
		}
		else if (n > m_size)
		{
			reserve(n);
			for (intptr_t i = m_size; i < n; ++i)
				::new (static_cast<void *>(m_data + i)) T();
		}
		m_size = n;
	}

	void assign(intptr_t n, const T &value)
	{
		clear();
		reserve(n);
		for (intptr_t i = 0; i < n; ++i)
			::new (static_cast<void *>(m_data + i)) T(value);
		m_size = n;
	}

	void clear() noexcept
	{
		destroy_range(m_data, m_size);
		m_size = 0;
	}

private:
	void grow_one()
	{
		intptr_t next = m_capacity == 0 ? INITIAL_CAPACITY : m_capacity + m_capacity / 2;
		if (next <= m_capacity) // guard tiny/degenerate growth
			next = m_capacity + 1;
		reallocate(next);
	}

	void reallocate(intptr_t new_capacity)
	{
		PHON_ASSERT(new_capacity >= m_size);
		if (new_capacity == 0)
		{
			free_storage();
			return;
		}
		T *new_data = static_cast<T *>(raw_alloc(new_capacity * static_cast<intptr_t>(sizeof(T)),
		                                         static_cast<intptr_t>(alignof(T))));
		relocate_range(new_data, m_data, m_size);
		if (m_data != nullptr)
			raw_free(m_data, static_cast<intptr_t>(alignof(T)));
		m_data = new_data;
		m_capacity = new_capacity;
	}

	void free_storage() noexcept
	{
		destroy_range(m_data, m_size);
		if (m_data != nullptr)
			raw_free(m_data, static_cast<intptr_t>(alignof(T)));
		m_data = nullptr;
		m_size = 0;
		m_capacity = 0;
	}

	T *m_data = nullptr;
	intptr_t m_size = 0;
	intptr_t m_capacity = 0;
};

} // namespace phonometrica

#endif // PHON_CORE_VECTOR_HPP
