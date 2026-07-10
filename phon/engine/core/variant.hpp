// Phonometrica engine — Variant: the public RAII value wrapper over Value.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Variant keeps the current API's name and role (design §3.1): an RAII wrapper
// over the same 8 bytes as Value, whose copy constructor retains and destructor
// releases. This is the value type embedders and containers store; Value itself
// is VM-internal with manual refcounting. Variant is standard-layout with a
// single Value member, so a Value slot can be viewed as a Variant& in place (the
// inline-storage container element trick, as in the current engine).

#ifndef PHON_CORE_VARIANT_HPP
#define PHON_CORE_VARIANT_HPP

#include <phon/engine/core/cell.hpp>
#include <phon/engine/core/value.hpp>

#include <utility>

namespace phonometrica {

class Variant;

// The typed-conversion helpers behind Variant::to<T>() / Variant::make() (design §11.4).
// Declared here but *defined* in runtime/native_traits.hpp, where every value type is
// complete — the container wrappers include this header, so Variant cannot include them
// back. Using `to`/`make` therefore requires the embedding header (runtime.hpp).
namespace detail {
template<class T>
T variant_to(Value v);
template<class T>
Variant variant_from(const T &x);
} // namespace detail

class Variant
{
public:
	Variant() noexcept : m_value(Value::make_null()) {}

	// Wrap a Value, taking a reference if it points to a cell.
	Variant(Value v) noexcept : m_value(v) { retain_cell(); }

	Variant(const Variant &o) noexcept : m_value(o.m_value) { retain_cell(); }

	Variant(Variant &&o) noexcept : m_value(o.m_value) { o.m_value = Value::make_null(); }

	Variant &operator=(const Variant &o) noexcept
	{
		if (this != &o)
		{
			Value old = m_value;
			m_value = o.m_value;
			retain_cell();
			release_value(old);
		}
		return *this;
	}

	Variant &operator=(Variant &&o) noexcept
	{
		if (this != &o)
		{
			release_cell();
			m_value = o.m_value;
			o.m_value = Value::make_null();
		}
		return *this;
	}

	~Variant() { release_cell(); }

	// --- typed constructors (convenience) ---

	static Variant from_double(double d) noexcept { return Variant(Value::make(d)); }
	static Variant from_int(int64_t i) noexcept { return Variant(Value::make_int(i)); }
	static Variant from_bool(bool b) noexcept { return Variant(Value::make_bool(b)); }
	static Variant null() noexcept { return Variant(); }

	// Take ownership of a Value that already carries one reference (the +1 handed out by a
	// native return / RetTraits box), without an extra retain — the counterpart of value().
	static Variant adopt(Value v) noexcept
	{
		Variant out;
		out.m_value = v;
		return out;
	}

	// --- typed conversions (design §11.4; defined via runtime/native_traits.hpp) ---

	// Extract a typed C++ value: `v.to<double>()`, `v.to<String>()`, `v.to<Handle<Sound>>()`.
	// Throws std::runtime_error if the held value is not of (a subtype of) the requested
	// type. Numeric widening follows the same rules as parameter unboxing (an Integer
	// satisfies `to<double>()`).
	template<class T>
	T to() const
	{
		return detail::variant_to<T>(m_value);
	}

	// Build a Variant from a C++ value: `Variant::make(2.5)`, `Variant::make(some_string)`,
	// `Variant::make(handle)`. The inverse of to<T>().
	template<class T>
	static Variant make(const T &x)
	{
		return detail::variant_from<T>(x);
	}

	// --- access ---

	Value value() const noexcept { return m_value; }

	bool is_null() const noexcept { return m_value.is_null(); }
	bool is_bool() const noexcept { return m_value.is_bool(); }
	bool is_int() const noexcept { return m_value.is_int(); }
	bool is_double() const noexcept { return m_value.is_double(); }
	bool is_number() const noexcept { return m_value.is_number(); }
	bool is_symbol() const noexcept { return m_value.is_symbol(); }
	bool is_cell() const noexcept { return m_value.is_cell(); }

	bool as_bool() const noexcept { return m_value.as_bool(); }
	int64_t as_int() const noexcept { return m_value.as_int(); }
	double as_double() const noexcept { return m_value.as_double(); }
	double to_double() const noexcept { return m_value.to_double(); }
	Symbol as_symbol() const noexcept { return m_value.as_symbol(); }
	Cell *as_cell() const noexcept { return m_value.as_cell(); }

private:
	PHON_FORCE_INLINE void retain_cell() noexcept
	{
		if (m_value.owns_cell())
			retain(m_value.cell_ptr());
	}
	PHON_FORCE_INLINE void release_cell() noexcept { release_value(m_value); }
	static PHON_FORCE_INLINE void release_value(Value v) noexcept
	{
		if (v.owns_cell())
			release(v.cell_ptr());
	}

	Value m_value;
};

static_assert(sizeof(Variant) == 8, "Variant must alias a Value slot");

} // namespace phonometrica

#endif // PHON_CORE_VARIANT_HPP
