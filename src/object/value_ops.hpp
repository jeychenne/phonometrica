// Phonometrica engine — value-level hashing and equality.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Structural hash/equality over any Value, dispatched through the per-class
// hooks (§6). Used as key semantics for the script Map/Set and by List search.
// Numeric types are type-exact keys: Integer 1 and Float 1.0 are distinct keys
// (cross-type numeric `==` is a language operator, resolved by dispatch in M2).

#ifndef PHON_OBJECT_VALUE_OPS_HPP
#define PHON_OBJECT_VALUE_OPS_HPP

#include "core/value.hpp"

namespace phonometrica {

// Structural hash. Equal values (per value_equals) hash equally.
uint64_t value_hash(Value v) noexcept;

// Structural equality. Two distinct String cells with the same bytes are equal;
// two cells of a class without an equals hook are equal only by identity.
bool value_equals(Value a, Value b) noexcept;

// Functors for using Value as a FlatHashMap/FlatHashSet key (the script Map/Set).
struct ValueHash
{
	uint64_t operator()(Value v) const noexcept { return value_hash(v); }
};

struct ValueEqual
{
	bool operator()(Value a, Value b) const noexcept { return value_equals(a, b); }
};

} // namespace phonometrica

#endif // PHON_OBJECT_VALUE_OPS_HPP
