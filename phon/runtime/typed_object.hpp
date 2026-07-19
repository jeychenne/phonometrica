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
 * Created: 22/05/2020                                                                                                 *
 *                                                                                                                     *
 * Purpose: template that wraps a value in an object. In general, this should not be used directly. Use                *
 * Runtime::create<T>() instead.                                                                                       *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_TYPED_OBJECT_HPP
#define PHONOMETRICA_TYPED_OBJECT_HPP

#include <phon/runtime/object.hpp>
#include <phon/runtime/traits.hpp>
#include <phon/runtime/class_descriptor.hpp>

namespace phonometrica {

namespace detail {

// Select base class at compile time depending on whether the type is collectable or not.
template<typename T>
using object_base = typename std::conditional<traits::is_collectable<T>::value, Collectable, Atomic>::type;

} // namespace detail


//----------------------------------------------------------------------------------------------------------------------

// Box for plain polymorphic hierarchies (traits::is_poly_boxed). Unlike TObject<T>, which stores the
// payload as a typed member, the payload of a TPolyObject<T> sits at a fixed offset after this header
// so that handles can be type-erased: a Handle<Base> and a Handle<Derived> both point at the same
// PolyObject and recover the payload by offset arithmetic. This mirrors the new engine's boxing of
// plain application classes (box_value_offset in its core/cell.hpp) so the cutover swap is mechanical.
//
// Layout contract (checked by asserts in TPolyObject's constructor):
// - every class in a boxed hierarchy uses single, non-virtual inheritance (no secondary bases), so
//   the payload address is the same through every base in the chain;
// - the payload's alignment does not exceed alignof(std::max_align_t).
class PolyObject : public Atomic
{
protected:

	explicit PolyObject(Class *klass) : Atomic(klass) { }
};

// Offset of the payload inside any TPolyObject<T>, independent of T.
inline constexpr size_t poly_payload_offset =
		(sizeof(PolyObject) + alignof(std::max_align_t) - 1) / alignof(std::max_align_t) * alignof(std::max_align_t);

template<class T>
T *poly_value(PolyObject *box)
{
	return reinterpret_cast<T*>(reinterpret_cast<char*>(box) + poly_payload_offset);
}

template<class T>
const T *poly_value(const PolyObject *box)
{
	return reinterpret_cast<const T*>(reinterpret_cast<const char*>(box) + poly_payload_offset);
}

// Recover the box from a payload pointer. Valid only for payloads that were allocated inside a
// TPolyObject (which is the only way script-facing hierarchy objects are ever created).
template<class T>
PolyObject *poly_box_of(T *payload)
{
	static_assert(traits::is_poly_boxed<T>, "poly_box_of<T> requires a poly-boxed hierarchy type");
	// dynamic_cast<void*> yields the most-derived address, which under the single-inheritance layout
	// contract is also the payload address the box was computed from.
	assert(dynamic_cast<void*>(payload) == static_cast<void*>(payload));
	return reinterpret_cast<PolyObject*>(reinterpret_cast<char*>(payload) - poly_payload_offset);
}

template<class T>
class TPolyObject final : public PolyObject
{
public:

	template<typename ...Params>
	explicit TPolyObject(Params &&... params) :
			PolyObject(detail::ClassDescriptor<T>::get()), m_value(std::forward<Params>(params)...)
	{
		static_assert(alignof(T) <= alignof(std::max_align_t), "payload over-aligned for poly boxing");
		assert(reinterpret_cast<char*>(&m_value) - reinterpret_cast<char*>(static_cast<PolyObject*>(this))
		       == ptrdiff_t(poly_payload_offset));
		assert(static_cast<void*>(static_cast<traits::hierarchy_root_t<T>*>(&m_value))
		       == static_cast<void*>(&m_value));
	}

	~TPolyObject() = default;

	T &value() { return m_value; }

	const T &value() const { return m_value; }

private:

	alignas(std::max_align_t) T m_value;
};


//----------------------------------------------------------------------------------------------------------------------

// All non-primitive types exposed to Phonometrica are wrapped in a typed TObject.

template<class T>
class TObject final : public detail::object_base<T>
{
public:

	using base_type = detail::object_base<T>;

	// Constructor for non collectable objects
	template<typename ...Params>
	explicit TObject(Params &&... params) :
			base_type(detail::ClassDescriptor<T>::get()), m_value(std::forward<Params>(params)...)
	{ }

	// Constructor for collectable objects
	template<typename ...Params>
	explicit TObject(Runtime *rt, Params &&... params) :
			base_type(detail::ClassDescriptor<T>::get(), rt), m_value(std::forward<Params>(params)...)
	{ }

	~TObject() = default;

	T &value()
	{ return m_value; }

	const T &value() const
	{ return m_value; }


private:

	T m_value;
};


//---------------------------------------------------------------------------------------------------------------------

// Smart pointer for subclasses of Object. This class transparently handles values wrapped in a TObject<T>, which means
// that a type T derived from Object and a type U wrapped in a TObject<U> look the same from the user's perspective.
template<typename T>
class Handle
{
public:

	static constexpr bool is_object = traits::is_object<T>::value;
	static constexpr bool is_poly = traits::is_poly_boxed<T>;
	using object_type = typename std::conditional<is_object, T,
			typename std::conditional<traits::is_poly_boxed<T>, PolyObject, TObject<T>>::type>::type;

	Handle()
	{ ptr = nullptr; }

	Handle(std::nullptr_t) :
		Handle() { }

	// By default we retain the value.
	explicit Handle(object_type *value) {
		ptr = value;
		retain();
	}

	// Poly-boxed hierarchies only: rebuild a (retaining) handle from a payload pointer, e.g.
	// Handle<Element>(this) inside a member function. The payload must live inside a TPolyObject.
	template<typename U = T, typename = std::enable_if_t<traits::is_poly_boxed<U>>>
	explicit Handle(T *payload) {
		ptr = poly_box_of(payload);
		retain();
	}

	// ... but we can simply wrap the pointer without retaining it if needed.
	Handle(object_type *value, std::false_type) {
		ptr = value;
	}

	Handle(const Handle &other) {
		ptr = other.ptr;
		retain();
	}

	Handle(Handle &&other) noexcept {
		ptr = other.ptr;
		other.zero();
	}

	~Handle() noexcept {
		release();
	}

	Handle &operator=(const Handle &other) noexcept
	{
		if (this != &other)
		{
			release();
			ptr = other.ptr;
			retain();
		}

		return *this;
	}

	Handle &operator=(Handle &&other) noexcept
	{
		std::swap(ptr, other.ptr);
		return *this;
	}

	T* get() const
	{
		if constexpr (is_object) {
			return ptr;
		}
		else if constexpr (is_poly) {
			return poly_value<T>(ptr);
		}
		else {
			return &ptr->value();
		}
	}

	T& operator*() const
	{
		return *get();
	}

	T* operator->() const
	{
		return get();
	}

	operator bool() const {
		return ptr != nullptr;
	}

	bool operator==(const Handle &other) const {
		return ptr == other.ptr;
	}

	bool operator!=(const Handle &other) const {
		return ptr != other.ptr;
	}

	void swap(Handle &other) noexcept {
		std::swap(ptr, other.ptr);
	}

	object_type *drop()
	{
		auto tmp = ptr;
		this->zero();
		return tmp;
	}

	void zero() noexcept {
		ptr = nullptr;
	}

	object_type *object() {
		return ptr;
	}

	const object_type *object() const {
		return ptr;
	}

	T &value()
	{
		return *get();
	}

	const T &value() const
	{
		return *get();
	}

	template<typename Base>
	operator Handle<Base>() {
		static_assert(std::is_base_of<Base, T>::value, "Cannot get handle from non-base class");
		if constexpr (is_poly) {
			// Type-erased boxes: the target handle wraps the same PolyObject.
			static_assert(traits::is_poly_boxed<Base>, "cannot convert a poly handle to a non-poly handle");
			return Handle<Base>(ptr);
		}
		else {
			return Handle<Base>(static_cast<Base*>(ptr));
		}
	}

private:

	template<typename> friend class Handle;

	void retain() noexcept {
		if (ptr) ptr->retain();
	}

	void release() noexcept {
		if (ptr) ptr->release();
	}

	object_type *ptr;
};


//---------------------------------------------------------------------------------------------------------------------

// Convenience factory template similar to std::make_shared<T>.
template<class T, class... Args>
Handle<T> make_handle(Args... args)
{
	if constexpr (traits::is_poly_boxed<T>) {
		return Handle<T>(static_cast<PolyObject*>(new TPolyObject<T>(std::forward<Args>(args)...)), std::false_type());
	}
	else {
		return Handle<T>(new typename Handle<T>::object_type(std::forward<Args>(args)...), std::false_type());
	}
}

// Get a downcasted raw pointer from a handle.
template<class Derived, class Base>
Derived *raw_recast(const Handle<Base> &ptr)
{
	return static_cast<Derived*>(ptr.get());
};

// Cast a handle to another handle, which must be related by inheritance
template<class Derived, class Base>
Handle<Derived> recast(const Handle<Base> &ptr)
{
	return Handle<Derived>(raw_recast<Derived, Base>(ptr));
};


} // namespace phonometrica

#endif // PHONOMETRICA_TYPED_OBJECT_HPP
