// Phonometrica engine — source buffers for the compiler front end.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// A Source owns the full UTF-8 text of a script (from a file or a string) plus an
// index of line-start byte offsets, so the scanner can scan the whole buffer in
// one pass and diagnostics can extract the offending line to draw a caret. This
// lives in the compiler layer, where `std::string`/`std::vector` are tolerated
// (architecture §0): it never appears on the VM hot path.

#ifndef PHON_COMPILE_SOURCE_HPP
#define PHON_COMPILE_SOURCE_HPP

#include <phon/engine/base/definitions.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace phonometrica {

class Source final
{
public:
	// Load from a file on disk; throws std::runtime_error if it cannot be read.
	// `path` becomes the display name in diagnostics.
	static Source from_file(const std::string &path);

	// Load from an in-memory string. `name` (default "<string>") is the display
	// name used in diagnostics.
	static Source from_string(std::string text, std::string name = "<string>");

	const std::string &text() const noexcept { return m_text; }
	const std::string &name() const noexcept { return m_name; }

	const char *begin() const noexcept { return m_text.data(); }
	const char *end() const noexcept { return m_text.data() + m_text.size(); }

	// Number of lines (a trailing newline does not add an empty final line;
	// an empty buffer has one empty line).
	intptr_t line_count() const noexcept { return static_cast<intptr_t>(m_line_starts.size()); }

	// The text of 1-based line `n`, without its terminating newline. Out-of-range
	// lines yield an empty view.
	std::string_view line(intptr_t n) const noexcept;

	// Render a two-line caret diagnostic body for (line, column, length): the
	// source line (tabs expanded to spaces) followed by a caret row. Used to
	// build SyntaxError messages and to display them.
	std::string caret(intptr_t line, intptr_t column, intptr_t length) const;

private:
	Source() = default;

	void index_lines();

	std::string m_text;
	std::string m_name;
	// Byte offset of the start of each line; m_line_starts[0] is line 1.
	std::vector<intptr_t> m_line_starts;
};

} // namespace phonometrica

#endif // PHON_COMPILE_SOURCE_HPP
