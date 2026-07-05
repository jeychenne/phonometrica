// Phonometrica engine — bit/alignment utility tests.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include "base/bits.hpp"
#include "test_framework.hpp"

using namespace phonometrica;

TEST_CASE("is_power_of_two")
{
	CHECK(!is_power_of_two(0));
	CHECK(is_power_of_two(1));
	CHECK(is_power_of_two(2));
	CHECK(is_power_of_two(64));
	CHECK(is_power_of_two(1u << 31));
	CHECK(!is_power_of_two(3));
	CHECK(!is_power_of_two(48));
}

TEST_CASE("align_up / align_down")
{
	CHECK(align_up(0, 16) == 0);
	CHECK(align_up(1, 16) == 16);
	CHECK(align_up(16, 16) == 16);
	CHECK(align_up(17, 16) == 32);
	CHECK(align_up(63, 64) == 64);
	CHECK(align_down(63, 64) == 0);
	CHECK(align_down(64, 64) == 64);
	CHECK(align_down(129, 64) == 128);
}

TEST_CASE("next_power_of_two")
{
	CHECK(next_power_of_two(0) == 1);
	CHECK(next_power_of_two(1) == 1);
	CHECK(next_power_of_two(2) == 2);
	CHECK(next_power_of_two(3) == 4);
	CHECK(next_power_of_two(5) == 8);
	CHECK(next_power_of_two(4096) == 4096);
	CHECK(next_power_of_two(4097) == 8192);
}

TEST_CASE("count_trailing_zeros / floor_log2")
{
	CHECK(count_trailing_zeros(uint64_t(1)) == 0);
	CHECK(count_trailing_zeros(uint64_t(8)) == 3);
	CHECK(count_trailing_zeros(uint64_t(512)) == 9);
	CHECK(floor_log2(1) == 0);
	CHECK(floor_log2(2) == 1);
	CHECK(floor_log2(255) == 7);
	CHECK(floor_log2(256) == 8);
}

TEST_CASE("bit_cast round-trips double bits")
{
	double d = 3.14159;
	uint64_t bits = bit_cast<uint64_t>(d);
	CHECK(bit_cast<double>(bits) == d);
}
