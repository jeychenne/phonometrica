/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 18/07/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: the scanner performs lexical analysis of a chunk of source code, read from a file or from a string. The    *
 * source is expected (and assumed) to be encoded in UTF-8, and is scanned one code point at a time.                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SCANNER_HPP
#define PHONOMETRICA_SCANNER_HPP

#include <deque>
#include <memory>
#include <vector>
#include <phon/runtime/compiler/token.hpp>
#include <phon/runtime/compiler/source_code.hpp>

namespace phonometrica {

class Scanner final
{
public:

    Scanner();

    // Set source code from a file on disk
    void load_file(const String &path);

    // Set source code from a string
    void load_string(const String &code);

    Token read_token();


    void report_error(const std::string &hint, intptr_t offset = 0, const char *error_type = "Syntax",
                      intptr_t err_column = -1, intptr_t err_length = 0);

    intptr_t line_no() const { return m_line_no; }

    // Byte offset of the current code point (m_char) from the start of its
    // line. Updated by get_char() before m_pos advances past the code point.
    intptr_t column() const { return m_char_byte; }

    bool has_content() const { return !m_source->empty(); }

    std::shared_ptr<SourceCode> source_code() const { return m_source; }

private:

	// Source code (from a file or string).
    std::shared_ptr<SourceCode> m_source;

    // Current word (accumulates code points)
    String m_spelling;

    // Current line
    String m_line;

    // Current line number
    intptr_t m_line_no;

    // Current position in the current line
    String::const_iterator m_pos;

    // Current code point
    char32_t m_char;

    // Byte offset of m_char in m_line. Captured in get_char() before m_pos
    // advances. Used both for the caret in error messages and as the column
    // recorded on each Token.
    intptr_t m_char_byte = 0;

    // Source position at the start of the token currently being scanned in
    // read_token(). Captured once after skip_white() and reused by every
    // Token(...) construction in that call.
    intptr_t m_token_line = 0;
    intptr_t m_token_column = 0;

    void reset();

    void rewind();

    void read_char();

    void get_char();

    void set_line(intptr_t index);

    void read_line();

    void skip();

    void skip_white();

    void accept();

    void scan_digits();

    // Top-level string scan. Consumes the opening delimiter, dispatches to the
    // triple-quoted or single-line variant, and returns the first token of the
    // resulting sequence. For plain (non-interpolated) strings this is a single
    // StringLiteral, identical to the pre-interpolation behavior. For interpolated
    // strings (those containing one or more ${expr} segments) the helpers below
    // desugar the string into a parenthesized concatenation chain and queue the
    // remaining tokens on m_pending.
    Token scan_string(char32_t end);

    // Single-line ("...") string body scanning. The opening delimiter has already
    // been consumed by scan_string. open_line/open_col carry the position of that
    // opening delimiter for diagnostics and for the leading LParen / StringLiteral
    // tokens emitted in the interpolated case.
    Token scan_single_string(char32_t end, intptr_t open_line, intptr_t open_col);

    // Triple-quoted ("""..."""  /  '''...''') string body scanning. Spans lines
    // and supports interpolation on the same terms as scan_single_string.
    Token scan_triple_string(char32_t end, intptr_t open_line, intptr_t open_col);

    // Called from scan_single_string / scan_triple_string when a ${ is detected
    // in the literal body. Appends the desugaring tokens for one interpolated
    // segment to the per-string `synthetic` accumulator and consumes the ${...}
    // from the source. On entry, m_char points at '$' and m_spelling holds the
    // literal chunk accumulated since the previous segment (or since the opening
    // delimiter, when first_interp is true). On exit, m_spelling is empty and
    // m_char points just past the matching '}'. open_line/open_col are used as
    // the position for the opening LParen and the first StringLiteral chunk.
    // The synthetic buffer is owned by the calling scan_*_string and only moves
    // onto m_pending at the very end of the string, in finish_string_token —
    // see the body of scan_interpolation_segment for why early m_pending pushes
    // break multi-segment strings.
    void scan_interpolation_segment(bool first_interp, intptr_t open_line, intptr_t open_col, std::vector<Token> &synthetic);

    // Build the Token returned from scan_single_string / scan_triple_string.
    // For non-interpolated strings, builds a single StringLiteral token from the
    // accumulated m_spelling and leaves m_pending untouched. For interpolated
    // strings, completes the per-string `synthetic` sequence (trailing chunk +
    // closing RParen), transfers all but its first token onto m_pending, and
    // returns that first token so the caller has something to hand back to the
    // parser.
    Token finish_string_token(bool has_interp, intptr_t open_line, intptr_t open_col, std::vector<Token> &synthetic);

    // Return the next codepoint without advancing the scanner state. Reaches across
    // line boundaries when needed. Returns Token::ETX past end of source.
    char32_t peek_char() const;

	// Same as isspace(), but does not consider '\n' as a space since it's used by the parser.
	static bool check_space(char32_t c);

    // Pre-scanned tokens that read_token() must return before scanning any
    // further source. Populated by string-interpolation desugaring: when a
    // string literal like "foo${x}bar" is seen, the scanner expands it into the
    // token sequence ( "foo" & ( x ) & "bar" ) and queues that sequence here.
    // read_token() drains this deque (FIFO) before doing any normal lexing.
    std::deque<Token> m_pending;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SCANNER_HPP
