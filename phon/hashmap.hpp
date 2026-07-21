// Forwarding header — Phonometrica-compatible name for the NEW engine's hash map
// (A1 base-type swap). The engine's type is FlatHashMap; app code refers to the
// container as Hashmap (see also dictionary.hpp).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#ifndef PHONSCRIPT_HASHMAP_INC_HPP
#define PHONSCRIPT_HASHMAP_INC_HPP

#include <phon/engine/core/flat_hash_map.hpp>

namespace phonometrica {

template<typename K, typename V, typename Hash = Hasher<K>, typename Eq = KeyEqual<K>>
using Hashmap = FlatHashMap<K, V, Hash, Eq>;

} // namespace phonometrica

#endif
