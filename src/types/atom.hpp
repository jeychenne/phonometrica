// Phonometrica engine — the atom table: interned identifiers as Symbols.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Identifiers, field names, and enum names intern to a 32-bit Symbol (design
// §5.1) so they compare and hash in one word and dispatch stays cheap. The table
// is a process-global, append-only structure: interned bytes live forever and
// their storage never moves, so a Symbol->bytes lookup is a plain read.
//
// M1 uses a single mutex; the design's 16-way sharding is a concurrency
// optimization deferred to M7 (recorded in DEVIATIONS.md).

#ifndef PHON_TYPES_ATOM_HPP
#define PHON_TYPES_ATOM_HPP

#include "base/definitions.hpp"
#include "core/symbol.hpp"

#include <string_view>

namespace phonometrica {

// Intern a byte sequence, returning its Symbol (stable for the process). Interning
// the same bytes always returns the same Symbol. Never returns NO_SYMBOL.
Symbol intern(std::string_view s);
Symbol intern(const char *bytes, intptr_t len);

// The bytes backing a Symbol (valid for the process lifetime). NO_SYMBOL yields
// an empty view.
std::string_view symbol_name(Symbol s);

// Number of interned symbols (excludes NO_SYMBOL).
intptr_t symbol_count();

} // namespace phonometrica

#endif // PHON_TYPES_ATOM_HPP
