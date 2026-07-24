// Phonometrica engine — Value NaN-boxing encoding tests.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/core/value.hpp>
#include "test_framework.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <random>

using namespace phonometrica;

TEST_CASE("Value immediates round-trip")
{
	Value n = Value::make_null();
	CHECK(n.is_null());
	CHECK(n.is_immediate());
	CHECK(!n.is_bool());
	CHECK(!n.is_double());
	CHECK(!n.is_number());

	Value t = Value::make_bool(true);
	Value f = Value::make_bool(false);
	CHECK(t.is_bool() && t.is_true() && !t.is_false());
	CHECK(f.is_bool() && f.is_false() && !f.is_true());
	CHECK(t.as_bool() == true);
	CHECK(f.as_bool() == false);
	CHECK(!t.identical(f));
	CHECK(!n.identical(f)); // null != false bitwise
}

TEST_CASE("Value integer boundaries round-trip (+/-2^47)")
{
	int64_t cases[] = {0,
	                   1,
	                   -1,
	                   2,
	                   -2,
	                   127,
	                   -128,
	                   1000000,
	                   -1000000,
	                   Value::INT_MAX_VALUE,
	                   Value::INT_MIN_VALUE,
	                   Value::INT_MAX_VALUE - 1,
	                   Value::INT_MIN_VALUE + 1};
	for (int64_t x : cases)
	{
		Value v = Value::make_int(x);
		CHECK(v.is_int());
		CHECK(v.is_number());
		CHECK(!v.is_double());
		CHECK(v.as_int() == x);
		CHECK(static_cast<int64_t>(v.to_double()) == x);
	}
	CHECK(Value::INT_MAX_VALUE == (int64_t(1) << 47) - 1);
	CHECK(Value::INT_MIN_VALUE == -(int64_t(1) << 47));
}

TEST_CASE("Value integer fuzz round-trip")
{
	std::mt19937_64 rng(555);
	for (int i = 0; i < 200000; ++i)
	{
		// Uniform in [-2^47, 2^47 - 1].
		int64_t x = static_cast<int64_t>(rng() % (uint64_t(1) << 48)) - (int64_t(1) << 47);
		Value v = Value::make_int(x);
		REQUIRE(v.is_int());
		REQUIRE(v.as_int() == x);
	}
}

TEST_CASE("Value doubles round-trip incl. specials")
{
	double cases[] = {0.0,
	                  -0.0,
	                  1.0,
	                  -1.0,
	                  3.141592653589793,
	                  2.718281828459045,
	                  1e300,
	                  -1e300,
	                  std::numeric_limits<double>::infinity(),
	                  -std::numeric_limits<double>::infinity(),
	                  std::numeric_limits<double>::min(),
	                  std::numeric_limits<double>::max()};
	for (double d : cases)
	{
		Value v = Value::make(d);
		CHECK(v.is_double());
		CHECK(v.is_number());
		CHECK(!v.is_int());
		// Bitwise round-trip (handles -0.0 vs 0.0 distinctly).
		CHECK(bit_cast<uint64_t>(v.as_double()) == bit_cast<uint64_t>(d));
	}
}

TEST_CASE("Value NaN payloads survive")
{
	// Hardware NaNs leave bit 50 clear, so they never collide with the box
	// signature and remain doubles with their payload intact.
	uint64_t nan_bits[] = {
	    0x7FF8000000000000ull, // canonical quiet NaN
	    0xFFF8000000000000ull, // negative quiet NaN
	    0x7FF0000000000001ull, // signaling NaN, payload 1
	    0x7FF4000000000000ull, // NaN with a mid payload (bit 50 clear)
	    0xFFF123456789ABCDull, // arbitrary negative NaN payload
	};
	for (uint64_t b : nan_bits)
	{
		double d = bit_cast<double>(b);
		REQUIRE(std::isnan(d));
		Value v = Value::make(d);
		CHECK(v.is_double());
		CHECK(bit_cast<uint64_t>(v.as_double()) == b); // payload preserved
	}
}

TEST_CASE("Value fuzz: random doubles are never misclassified as boxed")
{
	std::mt19937_64 rng(4242);
	for (int i = 0; i < 200000; ++i)
	{
		uint64_t b = rng();
		double d = bit_cast<double>(b);
		Value v = Value::make(d);
		// A finite/normal double must classify as double. NaNs that happen to
		// land on the exact box pattern (bit 50 set) are the documented
		// sacrifice; skip those.
		if ((b & Value::MASK_BOX) == Value::MASK_BOX)
			continue; // would be interpreted as a boxed value
		REQUIRE(v.is_double());
		REQUIRE(bit_cast<uint64_t>(v.as_double()) == b);
	}
}

TEST_CASE("Value symbols round-trip")
{
	uint32_t ids[] = {0, 1, 42, 0x7FFFFFFFu, 0xFFFFFFFFu, 0x80000000u};
	for (uint32_t id : ids)
	{
		Value v = Value::make_symbol(Symbol{id});
		CHECK(v.is_symbol());
		CHECK(!v.is_number());
		CHECK(v.as_symbol().id == id);
	}
}

TEST_CASE("Value cell pointers round-trip")
{
	// Real heap allocations on Linux sit well below 2^48.
	for (int i = 0; i < 1000; ++i)
	{
		void *raw = std::malloc(64);
		auto *c = reinterpret_cast<Cell *>(raw);
		Value v = Value::make_cell(c);
		CHECK(v.is_cell());
		CHECK(!v.is_number());
		CHECK(v.as_cell() == c);
		std::free(raw);
	}
}

TEST_CASE("Value references round-trip")
{
	// A first-class reference carries a pointer to its heap box Cell (§5).
	void *raw = std::malloc(64);
	auto *box = reinterpret_cast<Cell *>(raw);
	Value v = Value::make_reference(box);
	CHECK(v.is_reference());
	CHECK(!v.is_cell());
	CHECK(!v.is_number());
	CHECK(v.as_reference_box() == box);
	std::free(raw);
}

TEST_CASE("Value both_double fast path")
{
	Value a = Value::make(1.5);
	Value b = Value::make(2.5);
	Value i = Value::make_int(3);
	Value n = Value::make_null();
	CHECK(Value::both_double(a, b));
	CHECK(!Value::both_double(a, i));
	CHECK(!Value::both_double(i, b));
	CHECK(!Value::both_double(a, n));
}

TEST_CASE("Value tags are mutually exclusive")
{
	Value vals[] = {Value::make_null(),        Value::make_bool(true),
	                Value::make_int(5),         Value::make(1.5),
	                Value::make_symbol(Symbol{9})};
	// Exactly one predicate holds per value (double counts as its own category).
	for (Value v : vals)
	{
		int hits = int(v.is_immediate()) + int(v.is_int()) + int(v.is_symbol()) +
		           int(v.is_cell()) + int(v.is_reference()) + int(v.is_double());
		CHECK(hits == 1);
	}
}
