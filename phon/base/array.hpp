/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 13/07/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: dynamic array with copy-on-write semantics — the new base-class layer's replacement for the old 1-based    *
 * Array. Indices are 0-based and non-negative throughout: the 1-based (possibly negative) indices of the scripting    *
 * language are converted at the script/native bridge only (see phon/runtime/script_index.hpp), never here and never   *
 * in native code. Copies share the underlying buffer; any mutation through a shared handle first detaches (clones)    *
 * the buffer, so reads never copy and writes never alias. A 1-dimension array grows dynamically; a 2-dimension array  *
 * (matrix, column-major) has a fixed size, as before.                                                                 *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_BASE_ARRAY_HPP
#define PHONOMETRICA_BASE_ARRAY_HPP

#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <span>
#include <type_traits>
#include <phon/error.hpp>
#include <phon/runtime/traits.hpp>
#include <phon/utils/alloc.hpp>
#include <phon/utils/helpers.hpp>
#include <phon/utils/ref_count.hpp>

namespace phonometrica {

template<class T>
class Array final
{
public:

	using size_type = intptr_t; // non-standard-compliant, since the standard mandates an unsigned type.
	using difference_type = intptr_t;
	using value_type = T;
	using reference = value_type&;
	using const_reference = const value_type&;
	using pointer = value_type*;
	using const_pointer = const value_type*;
	using iterator = pointer;
	using const_iterator = const_pointer;
	using reverse_iterator = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;

	static constexpr bool has_scalar_type = std::is_scalar<value_type>::value;
	static constexpr bool has_safely_movable_value_type = traits::is_safely_movable<value_type>::value;

	// Result of find()/rfind() when the value is absent.
	static constexpr size_type npos = -1;

	// Empty 1-dimension array.
	Array() : m_buf(new Buffer, typename IntrusivePtr<Buffer>::Raw()) { }

	// Empty 1-dimension array with a given capacity.
	explicit Array(size_type requested) :
		m_buf(new Buffer(utils::find_capacity(requested)), typename IntrusivePtr<Buffer>::Raw())
	{ }

	// 1-dimension array filled with a given value.
	Array(size_type count, const_reference value) : Array(count)
	{
		copy_construct_n(m_buf->data, count, value);
		m_buf->size = count;
	}

	Array(const_iterator from, size_type count) : Array(count)
	{
		copy_construct_range(from, from + count, m_buf->data);
		m_buf->size = count;
	}

	Array(const_iterator start, const_iterator end) :
		Array(start, size_type(end - start))
	{ }

	Array(std::span<const T> span) :
		Array(span.data(), size_type(span.size()))
	{ }

	Array(std::span<T> span) :
		Array(span.data(), size_type(span.size()))
	{ }

	// 1-dimension array constructed from a list of values.
	Array(std::initializer_list<value_type> items) :
		Array(items.begin(), size_type(items.size()))
	{ }

	// Default-initialized matrix (column-major).
	Array(size_type nrow, size_type ncol) :
		m_buf(new Buffer(nrow, ncol), typename IntrusivePtr<Buffer>::Raw())
	{
		default_construct_n(m_buf->data, nrow * ncol);
		m_buf->size = nrow * ncol;
	}

	// Matrix filled with an explicit value (column-major).
	Array(size_type nrow, size_type ncol, const_reference value) :
		m_buf(new Buffer(nrow, ncol), typename IntrusivePtr<Buffer>::Raw())
	{
		copy_construct_n(m_buf->data, nrow * ncol, value);
		m_buf->size = nrow * ncol;
	}

	// Copying shares the buffer (copy-on-write); the first mutation through either handle detaches.
	// An array of a move-only type cannot be copied (there is no way to clone the buffer), so it
	// keeps a single owner and copy-on-write never has anything to do.
	Array(const Array &other) requires std::is_copy_constructible_v<value_type> = default;
	Array(const Array &other) requires (!std::is_copy_constructible_v<value_type>) = delete;
	Array(Array &&other) noexcept = default;

	~Array() = default;

	Array &operator=(Array other) noexcept
	{
		swap(other);
		return *this;
	}

	// --- buffer sharing (copy-on-write) ---

	size_t use_count() const noexcept { return m_buf->use_count(); }
	bool shared() const noexcept { return m_buf->shared(); }
	bool unique() const noexcept { return m_buf->unique(); }

	// Ensure this handle uniquely owns its buffer, cloning it if it is shared. This is the single
	// copy-on-write trigger: every mutating method and every non-const accessor routes through it.
	// Read-only (const) paths never call it and never copy.
	void detach()
	{
		if constexpr (std::is_copy_constructible_v<value_type>)
		{
			if (m_buf->shared()) {
				clone_buffer(capacity_hint());
			}
		}
		else
		{
			// A move-only element type cannot be copied, so its buffer can never be shared.
			assert(m_buf->unique());
		}
	}

	// Bypass copy-on-write: direct access to the (possibly shared) storage. Reserved for engine
	// internals that must observe or mark elements in place without changing their logical value
	// (e.g. GC traversal of a List's items). Any semantic mutation through this pointer on a shared
	// buffer is a bug.
	pointer raw_data() noexcept { return m_buf->data; }
	const_pointer raw_data() const noexcept { return m_buf->data; }

	// --- iterators (non-const forms detach) ---

	iterator begin() { detach(); return m_buf->data; }
	const_iterator begin() const noexcept { return m_buf->data; }
	const_iterator cbegin() const noexcept { return begin(); }
	iterator end() { detach(); return m_buf->data + size(); }
	const_iterator end() const noexcept { return m_buf->data + size(); }
	const_iterator cend() const noexcept { return end(); }

	reverse_iterator rbegin() { return reverse_iterator(end()); }
	const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
	const_reverse_iterator crbegin() const noexcept { return rbegin(); }
	reverse_iterator rend() { return reverse_iterator(begin()); }
	const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
	const_reverse_iterator crend() const noexcept { return rend(); }

	// --- shape ---

	size_type ndim() const noexcept { return m_buf->ndim; }
	size_type size() const noexcept { return m_buf->size; }
	bool empty() const noexcept { return size() == 0; }
	size_type max_size() const noexcept { return (std::numeric_limits<size_type>::max)(); }

	size_type capacity() const
	{
		assert(ndim() == 1);
		return m_buf->dim.d1.capacity;
	}

	size_type nrow() const noexcept
	{
		if (ndim() == 1) return size();
		assert(ndim() == 2);
		return m_buf->dim.d2.nrow;
	}

	size_type ncol() const noexcept
	{
		if (ndim() == 1) return 1;
		assert(ndim() == 2);
		return m_buf->dim.d2.ncol;
	}

	const_pointer data() const noexcept { return m_buf->data; }
	pointer data() { detach(); return m_buf->data; }

	void reserve(size_type capacity)
	{
		expand(capacity);
	}

	// --- element access (0-based, non-negative) ---

	reference operator[](size_type pos)
	{
		assert(pos >= 0 && pos < this->size());
		detach();
		return m_buf->data[pos];
	}

	const_reference operator[](size_type pos) const noexcept
	{
		assert(pos >= 0 && pos < this->size());
		return m_buf->data[pos];
	}

	reference operator()(size_type pos) { return (*this)[pos]; }
	const_reference operator()(size_type pos) const noexcept { return (*this)[pos]; }

	reference operator()(size_type row, size_type col)
	{
		detach();
		return m_buf->data[to_flat(row, col)];
	}

	const_reference operator()(size_type row, size_type col) const
	{
		return m_buf->data[to_flat(row, col)];
	}

	// Bounds-checked access.
	reference at(size_type i)
	{
		check_index(i, size());
		detach();
		return m_buf->data[i];
	}

	const_reference at(size_type i) const
	{
		check_index(i, size());
		return m_buf->data[i];
	}

	reference at(size_type row, size_type col)
	{
		check_index(row, nrow());
		check_index(col, ncol());
		detach();
		return m_buf->data[col * nrow() + row];
	}

	const_reference at(size_type row, size_type col) const
	{
		check_index(row, nrow());
		check_index(col, ncol());
		return m_buf->data[col * nrow() + row];
	}

	reference first() { detach(); return m_buf->data[0]; }
	const_reference first() const noexcept { return m_buf->data[0]; }

	reference last() { detach(); return m_buf->data[size() - 1]; }
	const_reference last() const noexcept { return m_buf->data[size() - 1]; }

	// For compatibility with std::vector.
	reference front() { return first(); }
	const_reference front() const noexcept { return first(); }
	reference back() { return last(); }
	const_reference back() const noexcept { return last(); }

	// --- modifiers (all trigger copy-on-write when the buffer is shared) ---

	void push_back(const_reference value) { append(value); }
	void push_back(value_type &&value) { append(std::move(value)); }
	void pop_back() { take_last(); }

	template<class... Args>
	void emplace_back(Args &&... args)
	{
		expand(size() + 1);
		new (&m_buf->data[m_buf->size++]) value_type(std::forward<Args>(args)...);
	}

	void append(const_reference value)
	{
		expand(size() + 1);
		new (&m_buf->data[m_buf->size++]) value_type(value);
	}

	void append(value_type &&value)
	{
		expand(size() + 1);
		new (&m_buf->data[m_buf->size++]) value_type(std::move(value));
	}

	void append(const Array &other)
	{
		auto extra = other.size();
		expand(size() + extra);
		// `other` may alias `this`; after expand() the source buffer is ours and stable.
		auto src = (other.m_buf == m_buf) ? m_buf->data : other.m_buf->data;
		copy_construct_range(src, src + extra, m_buf->data + size());
		m_buf->size += extra;
	}

	void append(std::initializer_list<value_type> values)
	{
		auto extra = size_type(values.size());
		expand(size() + extra);
		copy_construct_range(values.begin(), values.end(), m_buf->data + size());
		m_buf->size += extra;
	}

	void prepend(const_reference value) { insert(size_type(0), value); }
	void prepend(value_type &&value) { insert(size_type(0), std::move(value)); }

	// Insert before 0-based position `pos`, with 0 <= pos <= size() (pos == size() appends).
	void insert(size_type pos, const_reference value)
	{
		check_insert_pos(pos);
		expand(size() + 1);
		shift_up(pos, 1);
		new (&m_buf->data[pos]) value_type(value);
		m_buf->size++;
	}

	void insert(size_type pos, value_type &&value)
	{
		check_insert_pos(pos);
		expand(size() + 1);
		shift_up(pos, 1);
		new (&m_buf->data[pos]) value_type(std::move(value));
		m_buf->size++;
	}

	iterator insert(const_iterator pos, const_reference value)
	{
		auto offset = size_type(pos - cbegin());
		insert(offset, value);
		return m_buf->data + offset;
	}

	iterator insert(const_iterator pos, value_type &&value)
	{
		auto offset = size_type(pos - cbegin());
		insert(offset, std::move(value));
		return m_buf->data + offset;
	}

	template<class InputIterator>
	void insert(size_type pos, InputIterator first, InputIterator last)
	{
		check_insert_pos(pos);
		auto range_size = size_type(std::distance(first, last));
		expand(size() + range_size);
		shift_up(pos, range_size);
		auto out = m_buf->data + pos;
		for (auto it = first; it != last; ++it) {
			new (out++) value_type(*it);
		}
		m_buf->size += range_size;
	}

	template<class InputIterator>
	iterator insert(const_iterator pos, InputIterator first, InputIterator last)
	{
		auto offset = size_type(pos - cbegin());
		insert(offset, first, last);
		return m_buf->data + offset;
	}

	void insert(size_type pos, const Array &values)
	{
		if (values.m_buf == m_buf)
		{
			Array copy(values.cbegin(), values.cend());
			insert(pos, copy.cbegin(), copy.cend());
		}
		else
		{
			insert(pos, values.cbegin(), values.cend());
		}
	}

	void insert(size_type pos, std::initializer_list<value_type> values)
	{
		insert(pos, values.begin(), values.end());
	}

	template<class... Args>
	iterator emplace(size_type pos, Args &&... args)
	{
		check_insert_pos(pos);
		expand(size() + 1);
		shift_up(pos, 1);
		new (&m_buf->data[pos]) value_type(std::forward<Args>(args)...);
		m_buf->size++;

		return m_buf->data + pos;
	}

	// Remove all the occurrences of `value` in the array.
	void remove(const_reference value)
	{
		detach();

		for (size_type i = this->size(); i-- > 0; )
		{
			if (m_buf->data[i] == value) {
				remove_at(i);
			}
		}
	}

	// Remove the leftmost occurrence of `value` in the array.
	void remove_first(const_reference value)
	{
		auto pos = find(value);
		if (pos != npos) {
			remove_at(pos);
		}
	}

	// Remove the rightmost occurrence of `value` in the array.
	void remove_last(const_reference value)
	{
		auto pos = rfind(value);
		if (pos != npos) {
			remove_at(pos);
		}
	}

	// Remove value at a 0-based position.
	void remove_at(size_type pos)
	{
		assert(ndim() == 1);
		check_index(pos, size());
		detach();
		std::move(m_buf->data + pos + 1, m_buf->data + size(), m_buf->data + pos);
		m_buf->data[--m_buf->size].~value_type();
	}

	iterator remove_at(const_iterator pos)
	{
		auto offset = size_type(pos - cbegin());
		remove_at(offset);
		return m_buf->data + offset;
	}

	// Remove `count` items starting at 0-based position `pos`.
	void remove(size_type pos, size_type count)
	{
		assert(count >= 0);
		if (count == 0) return;
		check_index(pos, size());
		check_index(pos + count - 1, size());
		detach();
		std::move(m_buf->data + pos + count, m_buf->data + size(), m_buf->data + pos);
		destroy_range(m_buf->data + size() - count, m_buf->data + size());
		m_buf->size -= count;
	}

	iterator remove(const_iterator from, const_iterator to)
	{
		auto offset = size_type(from - cbegin());
		remove(offset, size_type(to - from));
		return m_buf->data + offset;
	}

	// Remove the n last items.
	void drop(size_type n)
	{
		assert(ndim() == 1);
		assert(n >= 0);
		detach();
		destroy_range(m_buf->data + size() - n, m_buf->data + size());
		m_buf->size -= n;
	}

	// Remove and return the first value in the array.
	value_type take_first()
	{
		detach();
		value_type tmp(std::move(m_buf->data[0]));
		remove_at(size_type(0));

		return tmp;
	}

	// Remove and return the last value in the array.
	value_type take_last()
	{
		detach();
		value_type tmp(std::move(m_buf->data[size() - 1]));
		m_buf->data[--m_buf->size].~value_type();

		return tmp;
	}

	value_type take_at(size_type pos)
	{
		check_index(pos, size());
		detach();
		value_type value(std::move(m_buf->data[pos]));
		remove_at(pos);

		return value;
	}

	// Remove the first value in the array.
	void pop_first() { remove_at(size_type(0)); }

	// Remove the last value in the array.
	void pop_last()
	{
		assert(!empty());
		detach();
		m_buf->data[--m_buf->size].~value_type();
	}

	void clear()
	{
		assert(ndim() == 1);
		if (empty()) return;

		if (m_buf->shared())
		{
			// No point copying elements we are about to discard: point to a fresh empty buffer.
			m_buf = IntrusivePtr<Buffer>(new Buffer, typename IntrusivePtr<Buffer>::Raw());
		}
		else
		{
			destroy_range(m_buf->data, m_buf->data + size());
			m_buf->size = 0;
		}
	}

	void resize(size_type new_size)
	{
		assert(ndim() == 1);
		assert(new_size >= 0);

		if (new_size < size())
		{
			detach();
			destroy_range(m_buf->data + new_size, m_buf->data + size());
		}
		else if (new_size > size())
		{
			expand(new_size);
			default_construct_n(m_buf->data + size(), new_size - size());
		}

		m_buf->size = new_size;
	}

	// --- queries (read-only: never detach) ---

	bool contains(const_reference value) const
	{
		return find(value) != npos;
	}

	bool starts_with(const_reference value) const
	{
		return !this->empty() && first() == value;
	}

	bool ends_with(const_reference value) const
	{
		return !this->empty() && last() == value;
	}

	// Find the 0-based index of the leftmost occurrence of `value`, starting the search at 0-based
	// position `from`. Returns npos if the value is absent.
	size_type find(const_reference value, size_type from = 0) const
	{
		assert(from >= 0);
		for (size_type i = from; i < size(); i++)
		{
			if (m_buf->data[i] == value) {
				return i;
			}
		}

		return npos;
	}

	const_iterator find(const_reference value, const_iterator from) const
	{
		return std::find(from, cend(), value);
	}

	// Find the 0-based index of the rightmost occurrence of `value`, searching backward from 0-based
	// position `from` (inclusive; npos means "from the last element"). Returns npos if absent.
	size_type rfind(const_reference value, size_type from = npos) const
	{
		if (empty()) return npos;
		auto start = (from == npos) ? size() - 1 : from;
		assert(start >= 0 && start < size());

		for (size_type i = start; i >= 0; i--)
		{
			if (m_buf->data[i] == value) {
				return i;
			}
		}

		return npos;
	}

	bool operator==(const Array &other) const
	{
		if (m_buf == other.m_buf)
			return true;

		if (this->size() != other.size() || this->ndim() != other.ndim())
			return false;

		if (ndim() == 2 && (nrow() != other.nrow() || ncol() != other.ncol()))
			return false;

		for (size_type i = 0; i < this->size(); i++)
		{
			if (m_buf->data[i] != other.m_buf->data[i])
				return false;
		}

		return true;
	}

	bool operator!=(const Array &other) const { return !(*this == other); }

	void swap(Array &other) noexcept
	{
		m_buf.swap(other.m_buf);
	}

	void check_dim(const Array &other) const
	{
		if (this->ndim() != other.ndim()) {
			throw error("Arrays have different dimensions");
		}
		if (this->size() != other.size()) {
			throw error("Arrays have different sizes");
		}
		if (ndim() == 2 && (this->nrow() != other.nrow() || this->ncol() != other.ncol())) {
			throw error("Arrays have different shapes");
		}
	}

	operator std::span<T>() { detach(); return { m_buf->data, size_t(size()) }; }
	operator std::span<const T>() const noexcept { return { m_buf->data, size_t(size()) }; }

private:

	struct Buffer : public Countable<Buffer>
	{
		// Empty 1-dimension buffer.
		Buffer() noexcept
		{
			ndim = 1;
			size = 0;
			dim.d1.capacity = 0;
			data = nullptr;
		}

		// 1-dimension buffer with a given capacity.
		explicit Buffer(size_type capacity)
		{
			ndim = 1;
			size = 0;
			dim.d1.capacity = capacity;
			data = utils::allocate<value_type>(capacity);
		}

		// Fixed-size 2-dimension buffer (elements constructed by the caller).
		Buffer(size_type nrow, size_type ncol)
		{
			ndim = 2;
			size = 0;
			dim.d2.nrow = nrow;
			dim.d2.ncol = ncol;
			data = utils::allocate<value_type>(nrow * ncol);
		}

		~Buffer()
		{
			if (data)
			{
				for (size_type i = size; i-- > 0; ) {
					data[i].~value_type();
				}
				utils::free(data);
			}
		}

		size_type ndim;
		size_type size;

		union {
			struct { size_type capacity; } d1;              // 1-dimension array
			struct { size_type nrow, ncol; } d2;            // 2-dimension array
		} dim;

		value_type *data;
	};

	size_type capacity_hint() const noexcept
	{
		return (m_buf->ndim == 1) ? (std::max)(m_buf->dim.d1.capacity, m_buf->size) : m_buf->size;
	}

	// Clone the shared buffer into a private one with at least `min_capacity` slots.
	void clone_buffer(size_type min_capacity)
	{
		auto old = m_buf.get();
		Buffer *fresh;

		if (old->ndim == 1)
		{
			fresh = new Buffer((std::max)(min_capacity, old->size));
		}
		else
		{
			fresh = new Buffer(old->dim.d2.nrow, old->dim.d2.ncol);
		}

		try
		{
			copy_construct_range(old->data, old->data + old->size, fresh->data);
		}
		catch (...)
		{
			delete fresh;
			throw;
		}
		fresh->size = old->size;
		m_buf = IntrusivePtr<Buffer>(fresh, typename IntrusivePtr<Buffer>::Raw());
	}

	// Ensure the buffer is unique and can hold `requested` elements. Returns true if the storage moved.
	bool expand(size_type requested)
	{
		assert(ndim() == 1);

		if constexpr (std::is_copy_constructible_v<value_type>)
		{
			if (m_buf->shared())
			{
				clone_buffer((std::max)(requested, capacity()));
				return true;
			}
		}

		if (requested > capacity())
		{
			auto previous_capacity = (std::max<size_type>)(this->capacity(), 8);
			auto capacity = utils::find_capacity(requested, previous_capacity);

			if (m_buf->data) {
				m_buf->data = utils::reallocate<value_type>(m_buf->data, size(), capacity);
			}
			else {
				m_buf->data = utils::allocate<value_type>(capacity);
			}
			m_buf->dim.d1.capacity = capacity;

			return true;
		}

		return false;
	}

	// Shift items [pos, size) up by `count` slots to open a gap (buffer must be unique with room).
	void shift_up(size_type pos, size_type count)
	{
		auto data = m_buf->data;
		auto n = size();

		for (size_type i = n; i-- > pos; )
		{
			new (&data[i + count]) value_type(std::move(data[i]));
			data[i].~value_type();
		}
	}

	static void copy_construct_n(pointer out, size_type count, const_reference value)
	{
		for (size_type i = 0; i < count; i++) {
			new (out + i) value_type(value);
		}
	}

	template<class InputIterator>
	static void copy_construct_range(InputIterator first, InputIterator last, pointer out)
	{
		for (auto it = first; it != last; ++it) {
			new (out++) value_type(*it);
		}
	}

	static void default_construct_n(pointer out, size_type count)
	{
		// utils::allocate() zero-initializes, which is a valid state for scalar types.
		if constexpr (!has_scalar_type)
		{
			for (size_type i = 0; i < count; i++) {
				new (out + i) value_type();
			}
		}
	}

	static void destroy_range(pointer first, pointer last)
	{
		for (auto it = first; it != last; ++it) {
			it->~value_type();
		}
	}

	size_type to_flat(size_type row, size_type col) const
	{
		assert(ndim() == 2);
		assert(row >= 0 && row < nrow());
		assert(col >= 0 && col < ncol());

		return col * nrow() + row;
	}

	void check_index(size_type i, size_type len) const
	{
		if (i < 0 || i >= len) {
			throw error("Index % out of range in array dimension with length %", i, len);
		}
	}

	void check_insert_pos(size_type pos) const
	{
		assert(ndim() == 1);
		if (pos < 0 || pos > size()) {
			throw error("Cannot insert at position % in array containing % items", pos, size());
		}
	}

	IntrusivePtr<Buffer> m_buf;
};

} // namespace phonometrica

#endif // PHONOMETRICA_BASE_ARRAY_HPP
