// Phonometrica engine — value-level hashing and equality implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/object/value_ops.hpp>

#include <phon/base/bits.hpp>
#include <phon/core/cell.hpp>
#include <phon/core/hash.hpp>
#include <phon/core/reference.hpp>
#include <phon/object/class.hpp>

namespace phonometrica {

uint64_t value_hash(Value v) noexcept
{
	v = deref(v); // a reference hashes as the value it stands for
	if (v.is_double())
	{
		double d = v.as_double();
		if (d == 0.0)
			d = 0.0; // normalize -0.0 so it hashes like +0.0
		return hash_mix(bit_cast<uint64_t>(d));
	}
	if (v.is_cell())
	{
		Cell *c = v.as_cell();
		Class *k = get_class(c->class_id());
		if (k->hash)
			return k->hash(c);
		return hash_mix(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(c)));
	}
	// int / symbol / immediate: the bits are the canonical key.
	return hash_mix(v.bits());
}

bool value_equals(Value a, Value b) noexcept
{
	a = deref(a); // compare the values references stand for, not the boxes
	b = deref(b);
	if (a.bits() == b.bits())
		return true; // identical: same immediate/int/symbol/double-bits/cell pointer
	if (a.is_cell() && b.is_cell())
	{
		Cell *ca = a.as_cell();
		Cell *cb = b.as_cell();
		if (ca->class_id() != cb->class_id())
			return false;
		Class *k = get_class(ca->class_id());
		return k->equals ? k->equals(ca, cb) : false;
	}
	// -0.0 vs +0.0 differ in bits but compare equal.
	if (a.is_double() && b.is_double())
		return a.as_double() == b.as_double();
	return false;
}

} // namespace phonometrica
