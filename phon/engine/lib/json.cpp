// Phonometrica engine — JSON standard library (architecture §12).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Two flat-global functions mirroring the old engine's json module:
//   to_json(value[, indent])  serialize a value to a JSON string. With a positive
//                             `indent` the output is pretty-printed (that many spaces
//                             per nesting level); otherwise it is compact.
//   from_json(str)            parse a JSON document into a value (null/Boolean/Integer/
//                             Float/String/List/Table).
// The mapping is the natural one: JSON object -> Table (string keys), array -> List,
// number -> Integer when it has no fraction/exponent and fits, else Float. Positions in
// error messages are 1-based grapheme columns, as everywhere in the engine.

#include <phon/engine/lib/lib.hpp>
#include <phon/engine/object/class.hpp>
#include <phon/engine/runtime/native_traits.hpp>
#include <phon/engine/types/list.hpp>
#include <phon/engine/types/string.hpp>
#include <phon/engine/types/table.hpp>
#include <phon/engine/vm/interpreter.hpp>
#include <phon/engine/vm/isolate.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace phonometrica {

namespace {

bool is_string_v(Value v) { return v.is_cell() && class_of(v) == CID_STRING; }
bool is_list_v(Value v) { return v.is_cell() && class_of(v) == CID_LIST; }
bool is_table_v(Value v) { return v.is_cell() && class_of(v) == CID_TABLE; }

// --- serialization ----------------------------------------------------------

// Append `str` as a quoted, escaped JSON string literal.
void write_quoted(String &out, const String &str)
{
	out.append('"');
	auto it = str.begin();
	auto end = str.end();
	while (it != end)
	{
		char32_t c = str.next_codepoint(it);
		switch (c)
		{
			case '"': out.append(Substring("\\\"")); break;
			case '\\': out.append(Substring("\\\\")); break;
			case '\b': out.append(Substring("\\b")); break;
			case '\f': out.append(Substring("\\f")); break;
			case '\n': out.append(Substring("\\n")); break;
			case '\r': out.append(Substring("\\r")); break;
			case '\t': out.append(Substring("\\t")); break;
			default:
				if (c < 0x20)
				{
					// Other control characters must be escaped as \u00XX.
					static const char *hex = "0123456789abcdef";
					char buf[6] = {'\\', 'u', '0', '0', 0, 0};
					buf[4] = hex[(c >> 4) & 0xf];
					buf[5] = hex[c & 0xf];
					out.append(Substring(buf, 6));
				}
				else
				{
					out.append(c); // pass printable Unicode through as UTF-8
				}
				break;
		}
	}
	out.append('"');
}

void write_indent(String &out, int indent, int depth)
{
	out.append('\n');
	for (int i = 0; i < indent * depth; ++i)
		out.append(' ');
}

void serialize(Isolate &iso, String &out, Value v, int indent, int depth);

void serialize_list(Isolate &iso, String &out, const List &l, int indent, int depth)
{
	if (l.size() == 0)
	{
		out.append(Substring("[]"));
		return;
	}
	out.append('[');
	for (intptr_t i = 1; i <= l.size(); ++i)
	{
		if (i > 1)
			out.append(indent > 0 ? Substring(",") : Substring(", "));
		if (indent > 0)
			write_indent(out, indent, depth + 1);
		serialize(iso, out, l.get(i).value(), indent, depth + 1);
	}
	if (indent > 0)
		write_indent(out, indent, depth);
	out.append(']');
}

void serialize_table(Isolate &iso, String &out, const Table &t, int indent, int depth)
{
	List keys = t.keys();
	if (keys.size() == 0)
	{
		out.append(Substring("{}"));
		return;
	}
	out.append('{');
	for (intptr_t i = 1; i <= keys.size(); ++i)
	{
		if (i > 1)
			out.append(indent > 0 ? Substring(",") : Substring(", "));
		if (indent > 0)
			write_indent(out, indent, depth + 1);
		Variant key = keys.get(i);
		// JSON keys are always strings; a non-string key is emitted by its text form.
		if (is_string_v(key.value()))
			write_quoted(out, String::from_value(key.value()));
		else
			write_quoted(out, stringify(key.value()));
		out.append(indent > 0 ? Substring(": ") : Substring(":"));
		serialize(iso, out, t.get(key).value(), indent, depth + 1);
	}
	if (indent > 0)
		write_indent(out, indent, depth);
	out.append('}');
}

void serialize(Isolate &iso, String &out, Value v, int indent, int depth)
{
	if (v.is_null())
	{
		out.append(Substring("null"));
	}
	else if (v.is_bool())
	{
		out.append(v.as_bool() ? Substring("true") : Substring("false"));
	}
	else if (v.is_int())
	{
		out.append(String::convert(static_cast<intptr_t>(v.as_int())).view());
	}
	else if (v.is_double())
	{
		double d = v.as_double();
		if (!std::isfinite(d))
			iso.raise(String("[JSON error] Cannot serialize a non-finite number to JSON"), 0);
		out.append(String::convert(d).view());
	}
	else if (is_string_v(v))
	{
		write_quoted(out, String::from_value(v));
	}
	else if (is_list_v(v))
	{
		serialize_list(iso, out, List::from_value(v), indent, depth);
	}
	else if (is_table_v(v))
	{
		serialize_table(iso, out, Table::from_value(v), indent, depth);
	}
	else
	{
		Class *c = get_class(class_of(v));
		String msg("[JSON error] Cannot serialize a value of type ");
		msg.append(c && c->name ? Substring(c->name) : Substring("Object"));
		msg.append(Substring(" to JSON"));
		iso.raise(std::move(msg), 0);
	}
}

// --- parsing ----------------------------------------------------------------

// Recursive-descent parser over the UTF-8 bytes of the source. Everything is byte-wise
// until a string literal, whose escapes decode to codepoints; unescaped multi-byte UTF-8
// is copied through verbatim. Errors carry a 1-based grapheme column.
struct Parser
{
	Isolate &iso;
	const String &src;
	const char *p;
	const char *end;

	Parser(Isolate &i, const String &s) : iso(i), src(s), p(s.data()), end(s.data() + s.size()) {}

	[[noreturn]] void fail(const char *what)
	{
		// Column = grapheme distance from the start to the cursor.
		intptr_t col = src.distance(src.begin(), p) + 1;
		String msg("[JSON error] ");
		msg.append(Substring(what));
		msg.append(Substring(" at position "));
		msg.append(String::convert(col).view());
		iso.raise(std::move(msg), 0);
	}

	void skip_ws()
	{
		while (p != end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
			++p;
	}

	char peek() { return p != end ? *p : '\0'; }

	Variant parse_document()
	{
		skip_ws();
		Variant v = parse_value();
		skip_ws();
		if (p != end)
			fail("Trailing characters after JSON value");
		return v;
	}

	Variant parse_value()
	{
		skip_ws();
		if (p == end)
			fail("Unexpected end of input");
		char c = *p;
		switch (c)
		{
			case '{': return parse_object();
			case '[': return parse_array();
			case '"': return Variant::make(parse_string());
			case 't': return parse_keyword("true", Variant(Value::make_bool(true)));
			case 'f': return parse_keyword("false", Variant(Value::make_bool(false)));
			case 'n': return parse_keyword("null", Variant(Value::make_null()));
			default:
				if (c == '-' || (c >= '0' && c <= '9'))
					return parse_number();
				fail("Unexpected character");
		}
	}

	Variant parse_keyword(const char *word, Variant value)
	{
		for (const char *w = word; *w; ++w)
		{
			if (p == end || *p != *w)
				fail("Invalid literal");
			++p;
		}
		return value;
	}

	Variant parse_number()
	{
		const char *start = p;
		bool is_float = false;
		if (peek() == '-')
			++p;
		while (p != end && *p >= '0' && *p <= '9')
			++p;
		if (peek() == '.')
		{
			is_float = true;
			++p;
			while (p != end && *p >= '0' && *p <= '9')
				++p;
		}
		if (peek() == 'e' || peek() == 'E')
		{
			is_float = true;
			++p;
			if (peek() == '+' || peek() == '-')
				++p;
			while (p != end && *p >= '0' && *p <= '9')
				++p;
		}
		std::string text(start, p);
		if (!is_float)
		{
			errno = 0;
			char *tail = nullptr;
			long long n = std::strtoll(text.c_str(), &tail, 10);
			// The engine's Integer is a signed 48-bit NaN-boxed payload; anything wider
			// (or a strtoll overflow) falls through to a Float below.
			constexpr long long kIntMax = (1LL << 47) - 1;
			constexpr long long kIntMin = -(1LL << 47);
			if (errno == 0 && tail == text.c_str() + text.size() && n >= kIntMin && n <= kIntMax)
				return Variant::from_int(static_cast<int64_t>(n));
		}
		char *tail = nullptr;
		double d = std::strtod(text.c_str(), &tail);
		if (tail != text.c_str() + text.size())
			fail("Invalid number");
		return Variant::from_double(d);
	}

	// Decode one \uXXXX escape (cursor sits just past the 'u'); returns the code unit.
	uint32_t parse_hex4()
	{
		uint32_t value = 0;
		for (int i = 0; i < 4; ++i)
		{
			if (p == end)
				fail("Incomplete \\u escape");
			char c = *p++;
			value <<= 4;
			if (c >= '0' && c <= '9')
				value |= uint32_t(c - '0');
			else if (c >= 'a' && c <= 'f')
				value |= uint32_t(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F')
				value |= uint32_t(c - 'A' + 10);
			else
				fail("Invalid hex digit in \\u escape");
		}
		return value;
	}

	String parse_string()
	{
		++p; // opening quote
		String out;
		while (true)
		{
			if (p == end)
				fail("Unterminated string");
			char c = *p++;
			if (c == '"')
				break;
			if (c == '\\')
			{
				if (p == end)
					fail("Unterminated escape");
				char e = *p++;
				switch (e)
				{
					case '"': out.append('"'); break;
					case '\\': out.append('\\'); break;
					case '/': out.append('/'); break;
					case 'b': out.append('\b'); break;
					case 'f': out.append('\f'); break;
					case 'n': out.append('\n'); break;
					case 'r': out.append('\r'); break;
					case 't': out.append('\t'); break;
					case 'u':
					{
						uint32_t cp = parse_hex4();
						if (cp >= 0xD800 && cp <= 0xDBFF)
						{
							// High surrogate: must be followed by a low surrogate.
							if (p + 1 >= end || p[0] != '\\' || p[1] != 'u')
								fail("Unpaired surrogate in \\u escape");
							p += 2;
							uint32_t lo = parse_hex4();
							if (lo < 0xDC00 || lo > 0xDFFF)
								fail("Invalid low surrogate in \\u escape");
							cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						}
						else if (cp >= 0xDC00 && cp <= 0xDFFF)
						{
							fail("Unexpected low surrogate in \\u escape");
						}
						out.append(static_cast<char32_t>(cp));
						break;
					}
					default:
						fail("Invalid escape sequence");
				}
			}
			else
			{
				// Copy the byte through verbatim (multi-byte UTF-8 passes unchanged).
				out.append(Substring(p - 1, 1));
			}
		}
		return out;
	}

	Variant parse_array()
	{
		++p; // '['
		List list;
		skip_ws();
		if (peek() == ']')
		{
			++p;
			return Variant::make(list);
		}
		while (true)
		{
			list.append(parse_value());
			skip_ws();
			char c = peek();
			if (c == ',')
			{
				++p;
				continue;
			}
			if (c == ']')
			{
				++p;
				break;
			}
			fail("Expected ',' or ']' in array");
		}
		return Variant::make(list);
	}

	Variant parse_object()
	{
		++p; // '{'
		Table table;
		skip_ws();
		if (peek() == '}')
		{
			++p;
			return Variant::make(table);
		}
		while (true)
		{
			skip_ws();
			if (peek() != '"')
				fail("Expected string key in object");
			String key = parse_string();
			skip_ws();
			if (peek() != ':')
				fail("Expected ':' after object key");
			++p;
			Variant value = parse_value();
			table.set(Variant::make(key), value);
			skip_ws();
			char c = peek();
			if (c == ',')
			{
				++p;
				continue;
			}
			if (c == '}')
			{
				++p;
				break;
			}
			fail("Expected ',' or '}' in object");
		}
		return Variant::make(table);
	}
};

} // namespace

Variant json_parse(Isolate &iso, const String &text)
{
	Parser parser(iso, text);
	return parser.parse_document();
}

void register_json_lib()
{
	register_function("to_json", [](Isolate &iso, const Variant &v) {
		String out;
		serialize(iso, out, v.value(), 0, 0);
		return out;
	});
	register_function("to_json", [](Isolate &iso, const Variant &v, int64_t indent) {
		String out;
		serialize(iso, out, v.value(), indent > 0 ? static_cast<int>(indent) : 0, 0);
		return out;
	});
	register_function("from_json", [](Isolate &iso, const String &text) {
		Parser parser(iso, text);
		return parser.parse_document();
	});
}

} // namespace phonometrica
