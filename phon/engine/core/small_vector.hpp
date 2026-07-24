// Phonometrica engine — SmallVector<T, N>, inline storage with heap spill.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// N elements live inline; the (N+1)-th spills to a heap buffer. Used pervasively
// in the compiler (child lists, register lists) and dispatch (argument type
// tuples) where most instances stay tiny (design/architecture.md §4). Same growth
// policy as Vector once spilled.

#ifndef PHON_CORE_SMALL_VECTOR_HPP
#define PHON_CORE_SMALL_VECTOR_HPP

#include <phon/engine/base/alloc.hpp>
#include <phon/engine/base/definitions.hpp>
#include <phon/engine/core/relocate.hpp>
#include <initializer_list>
#include <new>
#include <type_traits>
#include <utility>

namespace phonometrica {

template<typename T, intptr_t N>
class SmallVector final
{
	static_assert(N > 0, "SmallVector needs at least one inline slot");

public:
	using value_type = T;
	using iterator = T *;
	using const_iterator = const T *;

	SmallVector() : m_data(inline_storage()), m_capacity(N) {}

	SmallVector(std::initializer_list<T> init) : m_data(inline_storage()), m_capacity(N)
	{
		reserve(static_cast<intptr_t>(init.size()));
		for (const T &x : init)
			push_back(x);
	}

	SmallVector(const SmallVector &other) : m_data(inline_storage()), m_capacity(N)
	{
		reserve(other.m_size);
		for (intptr_t i = 0; i < other.m_size; ++i)
			::new (static_cast<void *>(m_data + i)) T(other.m_data[i]);
		m_size = other.m_size;
	}

	SmallVector(SmallVector &&other) : m_data(inline_storage()), m_capacity(N)
	{
		move_from(other);
	}

	SmallVector &operator=(const SmallVector &other)
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

	SmallVector &operator=(SmallVector &&other)
	{
		if (this != &other)
		{
			reset();
			move_from(other);
		}
		return *this;
	}

	~SmallVector() { reset(); }

	// --- capacity ---

	intptr_t size() const noexcept { return m_size; }
	intptr_t capacity() const noexcept { return m_capacity; }
	bool empty() const noexcept { return m_size == 0; }
	bool is_inline() const noexcept { return m_data == inline_storage(); }

	void reserve(intptr_t n)
	{
		if (n > m_capacity)
			reallocate(n);
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

	void erase(intptr_t i)
	{
		PHON_ASSERT(i >= 0 && i < m_size);
		for (intptr_t j = i; j + 1 < m_size; ++j)
			m_data[j] = std::move(m_data[j + 1]);
		pop_back();
	}

	void erase_unordered(intptr_t i)
	{
		PHON_ASSERT(i >= 0 && i < m_size);
		if (i != m_size - 1)
			m_data[i] = std::move(m_data[m_size - 1]);
		pop_back();
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

	void clear() noexcept
	{
		destroy_range(m_data, m_size);
		m_size = 0;
	}

private:
	T *inline_storage() noexcept { return reinterpret_cast<T *>(m_inline); }
	const T *inline_storage() const noexcept { return reinterpret_cast<const T *>(m_inline); }

	void grow_one()
	{
		intptr_t next = m_capacity + m_capacity / 2;
		if (next <= m_capacity)
			next = m_capacity + 1;
		reallocate(next);
	}

	void reallocate(intptr_t new_capacity)
	{
		PHON_ASSERT(new_capacity >= m_size && new_capacity > N);
		T *new_data = static_cast<T *>(raw_alloc(new_capacity * static_cast<intptr_t>(sizeof(T)),
		                                         static_cast<intptr_t>(alignof(T))));
		relocate_range(new_data, m_data, m_size);
		if (!is_inline())
			raw_free(m_data, static_cast<intptr_t>(alignof(T)));
		m_data = new_data;
		m_capacity = new_capacity;
	}

	// Take ownership of other's contents, leaving other empty and inline.
	void move_from(SmallVector &other)
	{
		if (other.is_inline())
		{
			// Must relocate element-by-element into our own inline storage.
			relocate_range(m_data, other.m_data, other.m_size);
			m_size = other.m_size;
		}
		else
		{
			// Steal the heap buffer wholesale.
			m_data = other.m_data;
			m_capacity = other.m_capacity;
			m_size = other.m_size;
			other.m_data = other.inline_storage();
			other.m_capacity = N;
		}
		other.m_size = 0;
	}

	// Destroy contents and release any heap buffer, returning to inline/empty.
	void reset() noexcept
	{
		destroy_range(m_data, m_size);
		if (!is_inline())
			raw_free(m_data, static_cast<intptr_t>(alignof(T)));
		m_data = inline_storage();
		m_capacity = N;
		m_size = 0;
	}

	alignas(T) unsigned char m_inline[sizeof(T) * static_cast<size_t>(N)];
	T *m_data;
	intptr_t m_size = 0;
	intptr_t m_capacity;
};

} // namespace phonometrica

#endif // PHON_CORE_SMALL_VECTOR_HPP
