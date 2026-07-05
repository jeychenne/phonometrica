// Phonometrica engine — Unicode property table declarations.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// The tables are defined in base/unicode_tables.cpp. These extern declarations
// give them external linkage (namespace-scope `const` is internal by default in
// C++) so base/unicode.cpp can consume them. Layout is documented in the
// generator, tools/unicode/generate_tables.py.

#ifndef PHON_BASE_UNICODE_TABLES_HPP
#define PHON_BASE_UNICODE_TABLES_HPP

#include <cstddef>
#include <cstdint>

namespace phonometrica {
namespace unicode {

extern const uint8_t phon_uni_stage1[];
extern const uint8_t phon_uni_stage2[];
extern const uint8_t phon_uni_lower_stage1[];
extern const uint32_t phon_uni_lower_stage2[];
extern const uint32_t phon_uni_lower_side[];
extern const uint8_t phon_uni_upper_stage1[];
extern const uint32_t phon_uni_upper_stage2[];
extern const uint32_t phon_uni_upper_side[];
extern const uint32_t phon_uni_white_space_ranges[];
extern const size_t phon_uni_white_space_range_count;
extern const uint8_t phon_uni_id_stage1[];
extern const uint8_t phon_uni_id_stage2[];

} // namespace unicode
} // namespace phonometrica

#endif // PHON_BASE_UNICODE_TABLES_HPP
