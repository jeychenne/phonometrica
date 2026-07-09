// Phonometrica engine — the scanner (lexical analysis).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// The scanner turns a UTF-8 Source into a stream of Tokens, one per next() call.
// It is hand-written and scans the whole buffer one code point at a time
// (architecture §9.1). Notable behaviors, all per design/design.md §12:
//
//   * Newline / continuation rule. A newline ends a statement UNLESS the line
//     ends with an operator, comma, or opening bracket, or we are inside
//     brackets / a string interpolation. Insignificant newlines (continuations,
//     blank lines, leading newlines) are suppressed here, so the parser sees a
//     clean stream where every Newline token is a real statement separator.
//
//   * Two string families, each with a single-line and a triple-quoted form.
//     Double quotes "..." / """...""" process escapes and interpolate with
//     {expr}; single quotes '...' / '''...''' are raw (regexes, Windows paths) —
//     no escapes, no interpolation. The single-line forms may not span a physical
//     line; the triple-quoted forms may.
//
//   * Interpolation. "a{x}b" is lexed to InterpStart("a") x InterpEnd("b"); the
//     embedded expressions are ordinary token streams delimited by Interp*
//     markers, so the parser assembles a StringInterpolation node without any
//     synthetic operator tokens. Nesting (a string inside {…}) is supported.
//
// Errors are thrown as SyntaxError (compiler layer; exceptions are permitted).

#ifndef PHON_COMPILE_SCANNER_HPP
#define PHON_COMPILE_SCANNER_HPP

#include <phon/compile/diagnostic.hpp>
#include <phon/compile/source.hpp>
#include <phon/compile/token.hpp>

#include <string>
#include <vector>

namespace phonometrica {

class Scanner final
{
public:
	// Borrows `source`; it must outlive the scanner.
	explicit Scanner(const Source &source);

	// The next token. Returns an Eot token at end of input (repeatedly, safely).
	Token next();

	const Source &source() const noexcept { return m_source; }

private:
	// Sentinel code point meaning "end of text" (not a valid Unicode scalar).
	static constexpr char32_t EOT = 0xFFFFFFFFu;

	// --- code-point cursor ---
	void advance();          // consume m_cp, decode the next code point into m_cp
	char32_t peek() const;   // the code point after m_cp, without consuming
	intptr_t column() const; // 0-based byte column of m_cp on its line

	// --- scanning helpers ---
	void skip_spaces_and_comments();
	void skip_block_comment();
	Token scan_identifier();
	Token scan_number();
	// Scan a single-quoted raw string body (opening delimiter(s) already
	// consumed). `triple` selects the '''...''' form, which may span lines.
	Token scan_raw_string(bool triple);
	// Scan one literal chunk of a double-quoted string starting at the current
	// position (just past the opening quote(s) or a closing '}'). Returns a
	// String token (whole non-interpolated string) or an Interp{Start,Mid,End}
	// marker. `triple` selects the """...""" form, which may span lines.
	Token scan_string_chunk(bool first, bool triple);
	// Decode the escape at the current position; bs_line/bs_col locate the
	// backslash so an invalid escape anchors its diagnostic there.
	char32_t scan_escape(intptr_t bs_line, intptr_t bs_col);

	Token make(Lexeme id, String spelling);
	Token make(Lexeme id, const char *spelling) { return make(id, String(spelling)); }

	// True if a newline following a token of lexeme `l` continues the statement.
	static bool continues_line(Lexeme l) noexcept;

	[[noreturn]] void error(const std::string &message);
	[[noreturn]] void error_at(intptr_t line, intptr_t column, intptr_t length,
	                           const std::string &message);

	const Source &m_source;

	const char *m_cur;   // next byte to decode (just past m_cp)
	const char *m_cp_ptr; // first byte of m_cp
	const char *m_line_start; // first byte of the current line
	char32_t m_cp;       // current code point (EOT at end)
	intptr_t m_line;     // 1-based line of m_cp

	// Start position of the token currently being scanned (captured after
	// skipping whitespace); every Token built in one next() uses these.
	intptr_t m_tok_line = 0;
	intptr_t m_tok_col = 0;

	// Depth of open ( [ { brackets, for newline suppression.
	int m_bracket_depth = 0;

	// Lexeme of the last significant (non-newline) token returned, for the
	// continuation rule. Unknown at the start of input.
	Lexeme m_last = Lexeme::Unknown;

	// The significant token before `m_last`, so the continuation rule can special-case
	// the `import M for *` wildcard: a trailing `*` normally continues the line, but a
	// `*` right after `for` is a selector, not multiplication (design §11).
	Lexeme m_last2 = Lexeme::Unknown;

	// Interpolation stack: one frame per open "{…}" interpolation. `brace_depth`
	// counts '{' braces opened inside the current expression — a '}' seen while
	// it is 0 closes the interpolation. `triple` records whether the enclosing
	// string is triple-quoted, so the resumed literal tail scans in the right
	// mode. Non-empty => inside an interpolation expression (newlines suppressed).
	struct InterpFrame
	{
		int brace_depth;
		bool triple;
	};
	std::vector<InterpFrame> m_interp;

	// Reusable byte buffer for assembling decoded string content.
	std::string m_buf;
};

} // namespace phonometrica

#endif // PHON_COMPILE_SCANNER_HPP
