// Public forwarding header — a string-keyed map (heavily used in Phonometrica).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#ifndef PHON_DICTIONARY_INC_HPP
#define PHON_DICTIONARY_INC_HPP

#include <phon/hashmap.hpp>
#include <phon/string.hpp>

namespace phonometrica {

template <typename Val>
using Dictionary = Hashmap<String, Val>;

} // namespace phonometrica

#endif // PHON_DICTIONARY_INC_HPP
