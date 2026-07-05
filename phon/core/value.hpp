// Phonometrica engine — Value: the 64-bit NaN-boxed value representation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// [INVARIANT] This is the single choke point for the value encoding
// (design/architecture.md §3.1, §16.1). No code outside this header may extract
// pointers from Value::bits directly. Every accessor is force-inlined and routes
// through the constants below so the encoding can evolve in one place.
//
// A Value is a `double` unless (bits & MASK_BOX) == MASK_BOX. Boxed values live
// in the quiet-NaN space; the 3-bit tag is {bit63, bit49, bit48} and the payload
// is bits 47..0:
//
//   tag 001 IMMEDIATE  payload 0 = null, 1 = false, 2 = true
//   tag 010 INTEGER    48-bit two's-complement (range +/-2^47)
//   tag 011 SYMBOL     32-bit atom-table index
//   tag 100 CELL       48-bit pointer to a heap Cell
//   tag 101 REF        48-bit pointer to a Value slot (second-class ref)
//
// Because the box signature occupies bits 62..50 and the tag adds bits 63/49/48,
// the type of a boxed value is fully determined by its top 16 bits.

#ifndef PHON_CORE_VALUE_HPP
#define PHON_CORE_VALUE_HPP

#include <phon/base/bits.hpp>
#include <phon/base/definitions.hpp>
#include <phon/core/symbol.hpp>

namespace phonometrica {

struct Cell; // opaque here; only value.hpp turns bits into a Cell*

class Value
{
public:
	// --- encoding constants ---

	// Quiet-NaN box signature: bits 62..50 set (bit 50 avoids colliding with
	// hardware-generated qNaNs, which leave it clear).
	static constexpr uint64_t MASK_BOX = 0x7FFC'0000'0000'0000ull;

	// Payload occupies the low 48 bits.
	static constexpr uint64_t PAYLOAD_MASK = 0x0000'FFFF'FFFF'FFFFull;

	// The type of a boxed value is encoded in its top 16 bits.
	static constexpr uint64_t TAG_MASK = 0xFFFF'0000'0000'0000ull;

	static constexpr uint64_t SIG_IMMEDIATE = 0x7FFD'0000'0000'0000ull; // box | bit48
	static constexpr uint64_t SIG_INTEGER = 0x7FFE'0000'0000'0000ull;   // box | bit49
	static constexpr uint64_t SIG_SYMBOL = 0x7FFF'0000'0000'0000ull;    // box | bit49|bit48
	static constexpr uint64_t SIG_CELL = 0xFFFC'0000'0000'0000ull;      // box | bit63
	static constexpr uint64_t SIG_REF = 0xFFFD'0000'0000'0000ull;       // box | bit63|bit48

	// Immediate payloads.
	static constexpr uint64_t IMM_NULL = 0;
	static constexpr uint64_t IMM_FALSE = 1;
	static constexpr uint64_t IMM_TRUE = 2;

	// Integer range: signed 48-bit.
	static constexpr int64_t INT_MIN_VALUE = -(int64_t(1) << 47);
	static constexpr int64_t INT_MAX_VALUE = (int64_t(1) << 47) - 1;

	// --- construction ---

	Value() = default; // uninitialized bits; use make_null() for a defined null

	static PHON_FORCE_INLINE Value from_bits(uint64_t bits) noexcept
	{
		Value v;
		v.m_bits = bits;
		return v;
	}

	static PHON_FORCE_INLINE Value make(double d) noexcept
	{
		return from_bits(bit_cast<uint64_t>(d));
	}

	static PHON_FORCE_INLINE Value make_null() noexcept
	{
		return from_bits(SIG_IMMEDIATE | IMM_NULL);
	}

	static PHON_FORCE_INLINE Value make_bool(bool b) noexcept
	{
		return from_bits(SIG_IMMEDIATE | (b ? IMM_TRUE : IMM_FALSE));
	}

	static PHON_FORCE_INLINE Value make_int(int64_t i) noexcept
	{
		PHON_ASSERT_MSG(i >= INT_MIN_VALUE && i <= INT_MAX_VALUE, "integer out of 48-bit range");
		return from_bits(SIG_INTEGER | (static_cast<uint64_t>(i) & PAYLOAD_MASK));
	}

	static PHON_FORCE_INLINE Value make_symbol(Symbol s) noexcept
	{
		return from_bits(SIG_SYMBOL | static_cast<uint64_t>(s.id));
	}

	static PHON_FORCE_INLINE Value make_cell(Cell *c) noexcept
	{
		auto p = reinterpret_cast<uintptr_t>(c);
		PHON_ASSERT_MSG((p & ~PAYLOAD_MASK) == 0, "cell pointer exceeds 48 bits");
		return from_bits(SIG_CELL | static_cast<uint64_t>(p));
	}

	static PHON_FORCE_INLINE Value make_ref(Value *slot) noexcept
	{
		auto p = reinterpret_cast<uintptr_t>(slot);
		PHON_ASSERT_MSG((p & ~PAYLOAD_MASK) == 0, "ref pointer exceeds 48 bits");
		return from_bits(SIG_REF | static_cast<uint64_t>(p));
	}

	// --- raw bits (VM register moves are memcpy-speed) ---

	PHON_FORCE_INLINE uint64_t bits() const noexcept { return m_bits; }

	// --- predicates ---

	PHON_FORCE_INLINE bool is_double() const noexcept
	{
		return (m_bits & MASK_BOX) != MASK_BOX;
	}
	PHON_FORCE_INLINE bool is_boxed() const noexcept
	{
		return (m_bits & MASK_BOX) == MASK_BOX;
	}
	PHON_FORCE_INLINE bool is_immediate() const noexcept { return tag() == SIG_IMMEDIATE; }
	PHON_FORCE_INLINE bool is_int() const noexcept { return tag() == SIG_INTEGER; }
	PHON_FORCE_INLINE bool is_symbol() const noexcept { return tag() == SIG_SYMBOL; }
	PHON_FORCE_INLINE bool is_cell() const noexcept { return tag() == SIG_CELL; }
	PHON_FORCE_INLINE bool is_ref() const noexcept { return tag() == SIG_REF; }

	PHON_FORCE_INLINE bool is_null() const noexcept
	{
		return m_bits == (SIG_IMMEDIATE | IMM_NULL);
	}
	PHON_FORCE_INLINE bool is_false() const noexcept
	{
		return m_bits == (SIG_IMMEDIATE | IMM_FALSE);
	}
	PHON_FORCE_INLINE bool is_true() const noexcept
	{
		return m_bits == (SIG_IMMEDIATE | IMM_TRUE);
	}
	PHON_FORCE_INLINE bool is_bool() const noexcept
	{
		return is_false() || is_true();
	}
	// A number is either a double or a boxed 48-bit integer.
	PHON_FORCE_INLINE bool is_number() const noexcept { return is_double() || is_int(); }

	// --- extraction (asserts type in debug) ---

	PHON_FORCE_INLINE double as_double() const noexcept
	{
		PHON_ASSERT(is_double());
		return bit_cast<double>(m_bits);
	}

	PHON_FORCE_INLINE int64_t as_int() const noexcept
	{
		PHON_ASSERT(is_int());
		uint64_t payload = m_bits & PAYLOAD_MASK;
		// Sign-extend from bit 47.
		if (payload & (uint64_t(1) << 47))
			payload |= ~PAYLOAD_MASK;
		return static_cast<int64_t>(payload);
	}

	PHON_FORCE_INLINE bool as_bool() const noexcept
	{
		PHON_ASSERT(is_bool());
		return m_bits == (SIG_IMMEDIATE | IMM_TRUE);
	}

	PHON_FORCE_INLINE Symbol as_symbol() const noexcept
	{
		PHON_ASSERT(is_symbol());
		return Symbol{static_cast<uint32_t>(m_bits & 0xFFFF'FFFFull)};
	}

	PHON_FORCE_INLINE Cell *as_cell() const noexcept
	{
		PHON_ASSERT(is_cell());
		return reinterpret_cast<Cell *>(static_cast<uintptr_t>(m_bits & PAYLOAD_MASK));
	}

	PHON_FORCE_INLINE Value *as_ref() const noexcept
	{
		PHON_ASSERT(is_ref());
		return reinterpret_cast<Value *>(static_cast<uintptr_t>(m_bits & PAYLOAD_MASK));
	}

	// Convert a numeric Value to double regardless of int/double representation.
	PHON_FORCE_INLINE double to_double() const noexcept
	{
		return is_int() ? static_cast<double>(as_int()) : as_double();
	}

	// --- arithmetic fast-path helper ---

	// True when both operands are doubles (the hot path for + - * /).
	static PHON_FORCE_INLINE bool both_double(Value a, Value b) noexcept
	{
		// Neither has all box bits set. One AND-compare per operand.
		return ((a.m_bits & MASK_BOX) != MASK_BOX) & ((b.m_bits & MASK_BOX) != MASK_BOX);
	}

	// Bitwise identity (NOT value equality: two NaNs with different payloads
	// differ here, and -0.0 != 0.0 bitwise). Value equality is a generic (M2+).
	PHON_FORCE_INLINE bool identical(Value o) const noexcept { return m_bits == o.m_bits; }

private:
	PHON_FORCE_INLINE uint64_t tag() const noexcept { return m_bits & TAG_MASK; }

	uint64_t m_bits;
};

static_assert(sizeof(Value) == 8, "Value must be exactly 8 bytes");
static_assert(std::is_trivially_copyable_v<Value>, "Value must be trivially copyable");

} // namespace phonometrica

#endif // PHON_CORE_VALUE_HPP
