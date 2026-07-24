// Public forwarding header — Phonometrica-compatible name for the engine hash map.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Ported Phonometrica code refers to the container as `Hashmap`; the engine's type is
// `FlatHashMap`. This alias lets that code compile unchanged (see also dictionary.hpp).

#ifndef PHON_HASHMAP_INC_HPP
#define PHON_HASHMAP_INC_HPP

#include <phon/engine/core/flat_hash_map.hpp>

namespace phonometrica {

template<typename K, typename V, typename Hash = Hasher<K>, typename Eq = KeyEqual<K>>
using Hashmap = FlatHashMap<K, V, Hash, Eq>;

} // namespace phonometrica

#endif // PHON_HASHMAP_INC_HPP
