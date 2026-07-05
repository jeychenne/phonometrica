// Phonometrica engine — Symbol: a 32-bit interned-identifier handle.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// A Symbol is a 32-bit index into the global atom table (design/architecture.md
// §5.1). Symbols are hot in dispatch and as field/enum names; they compare and
// hash in one machine word. The table itself (interning, id->string) lives in
// types/atom_table.hpp (M1c). This header only defines the id wrapper so that
// value.hpp can box a Symbol without depending on the table.

#ifndef PHON_CORE_SYMBOL_HPP
#define PHON_CORE_SYMBOL_HPP

#include <phon/base/definitions.hpp>

namespace phonometrica {

struct Symbol
{
	uint32_t id = 0;

	constexpr Symbol() = default;
	explicit constexpr Symbol(uint32_t i) noexcept : id(i) {}

	constexpr bool operator==(Symbol o) const noexcept { return id == o.id; }
	constexpr bool operator!=(Symbol o) const noexcept { return id != o.id; }

	constexpr explicit operator bool() const noexcept { return id != 0; }
};

// Id 0 is reserved to mean "no symbol" (the atom table never assigns it).
inline constexpr Symbol NO_SYMBOL{0};

} // namespace phonometrica

#endif // PHON_CORE_SYMBOL_HPP
