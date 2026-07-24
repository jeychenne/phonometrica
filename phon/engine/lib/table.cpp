// Phonometrica engine — Table standard library (architecture §12).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Ported from the old engine's func_table.hpp. Queries take `const Table &`;
// mutators take `Table &` (write-back ref, copy-on-write like the List module).
// Table/Set are unordered (DEVIATIONS M1 #7), so keys()/values() come back in
// unspecified order. `contains` distinguishes a stored null from a missing key —
// the one thing an indexed read (which yields null for both) cannot do.

#include <phon/engine/lib/lib.hpp>
#include <phon/engine/runtime/native_traits.hpp>
#include <phon/engine/types/list.hpp>
#include <phon/engine/types/table.hpp>

namespace phonometrica {

void register_table_lib()
{
	// --- queries ---
	register_function("contains", [](const Table &t, Variant key) { return t.contains(key); });
	register_function("is_empty", [](const Table &t) { return t.empty(); });
	register_function("keys", [](const Table &t) { return t.keys(); });
	register_function("values", [](const Table &t) { return t.values(); });

	// --- in-place mutators (the `Table &` parameter writes back to the caller) ---
	register_function("remove", [](Table &t, Variant key) { t.remove(key); });
	register_function("clear", [](Table &t) { t.clear(); });
}

} // namespace phonometrica
