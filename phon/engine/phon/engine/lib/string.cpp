// Phonometrica engine — string standard library (architecture §12).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Ported from the old engine's func_string.hpp against the typed registration API.
// Two shapes appear: non-mutating queries/derivations take the string by `const
// String &` and return a new value; in-place mutators take it by `String &` (a `ref`
// parameter that writes back — the old engine's REF-marked functions), matching the
// field-tested `trim(s)` / `append(s, x)` semantics. Positions are 1-based graphemes,
// as everywhere in the engine; `find` returns 0 when the needle is absent.

#include <phon/engine/lib/lib.hpp>
#include <phon/engine/runtime/native_traits.hpp>
#include <phon/engine/types/list.hpp>
#include <phon/engine/types/string.hpp>
#include <phon/engine/vm/isolate.hpp> // Isolate::raise (to_int/to_float)

namespace phonometrica {

void register_string_lib()
{
	// --- non-mutating queries and derivations ---
	register_function("find", [](const String &s, const String &needle) { return s.find(needle); });
	register_function("find", [](const String &s, const String &needle, int64_t from) {
		return s.find(needle, from);
	});
	register_function("count", [](const String &s, const String &needle) { return s.count(needle); });
	register_function("contains", [](const String &s, const String &needle) { return s.contains(needle); });
	register_function("starts_with",
	                  [](const String &s, const String &prefix) { return s.starts_with(prefix); });
	register_function("ends_with",
	                  [](const String &s, const String &suffix) { return s.ends_with(suffix); });
	register_function("is_empty", [](const String &s) { return s.empty(); });
	register_function("to_upper", [](const String &s) { return s.to_upper(); });
	register_function("to_lower", [](const String &s) { return s.to_lower(); });
	register_function("left", [](const String &s, int64_t n) { return s.left(n); });
	register_function("right", [](const String &s, int64_t n) { return s.right(n); });
	register_function("char", [](const String &s, int64_t i) { return String(s.at(i)); });
	register_function("slice", [](const String &s, int64_t from) { return String(s.mid(from)); });
	register_function("slice", [](const String &s, int64_t from, int64_t to) {
		return String(s.mid(from, to - from + 1));
	});

	// split(s, sep) -> List of the pieces between occurrences of `sep`. A leading,
	// trailing, or doubled separator yields an empty field, so split/join round-trip.
	register_function("split", [](const String &s, const String &sep) {
		List out;
		if (sep.empty())
		{
			out.append(Variant::make(s));
			return out;
		}
		intptr_t n = s.length();
		intptr_t sep_len = sep.length();
		intptr_t start = 1; // 1-based grapheme cursor
		while (start <= n)
		{
			intptr_t pos = s.find(sep, start);
			if (pos == 0)
			{
				out.append(Variant::make(String(s.mid(start))));
				return out;
			}
			out.append(Variant::make(String(s.mid(start, pos - start))));
			start = pos + sep_len;
		}
		// The string is empty or ends with the separator: one final empty field.
		out.append(Variant::make(String()));
		return out;
	});

	// --- conversions ---
	// to_int / to_float parse the whole (whitespace-trimmed) string; an unparseable
	// string raises a [Value error] rather than yielding a silent 0.
	register_function("to_int", [](Isolate &iso, const String &s) -> int64_t {
		bool ok = false;
		intptr_t v = s.to_int(&ok);
		if (!ok)
			iso.raise(String("[Value error] cannot convert '") + s + "' to Integer", 0);
		return static_cast<int64_t>(v);
	});
	register_function("to_float", [](Isolate &iso, const String &s) -> double {
		bool ok = false;
		double v = s.to_float(&ok);
		if (!ok)
			iso.raise(String("[Value error] cannot convert '") + s + "' to Float", 0);
		return v;
	});

	// --- in-place mutators: the `String &` parameter writes back to the caller ---
	register_function("append", [](String &s, const String &suffix) { s.append(suffix); });
	register_function("prepend", [](String &s, const String &prefix) { s.prepend(prefix); });
	register_function("trim", [](String &s) { s.trim(); });
	register_function("ltrim", [](String &s) { s.ltrim(); });
	register_function("rtrim", [](String &s) { s.rtrim(); });
	register_function("reverse", [](String &s) { s = s.reverse(); });
	register_function("replace", [](String &s, const String &before, const String &after) {
		s.replace(before, after);
	});
	register_function("remove", [](String &s, const String &what) { s.remove(what); });
}

} // namespace phonometrica
