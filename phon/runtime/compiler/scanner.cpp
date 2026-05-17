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
 * purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <vector>
#include <phon/file.hpp>
#include <phon/runtime/compiler/scanner.hpp>

namespace phonometrica {

Scanner::Scanner() :
    m_source(std::make_shared<SourceCode>())
{
    m_line_no = 0;
    m_pos = nullptr;
    m_char = 0;
    m_char_byte = 0;
    m_token_line = 0;
    m_token_column = 0;
}

void Scanner::load_file(const String &path)
{
    reset();
    m_source->load_file(path);
    read_char();
}

void Scanner::load_string(const String &code)
{
    reset();
    m_source->load_code(code);
    read_char();
}

void Scanner::reset()
{
    m_pos = nullptr;
    m_line_no = 0;
    m_line.clear();
    m_char = 0;
    m_char_byte = 0;
    m_token_line = 0;
    m_token_column = 0;
}

void Scanner::rewind()
{
    if (m_char != Token::ETX)
    {
        // Move back to the beginning of the string
        m_pos = m_line.begin();
        read_char();
    }
    else
    {
        m_pos = nullptr;
    }
}

void Scanner::read_char()
{
    // Never read past the end of the source
    assert(m_char != Token::ETX);

    if (m_pos == m_line.end() || m_pos == nullptr)
    {
        read_line();
        rewind();
    }
    else
    {
        get_char();
    }
}

void Scanner::get_char()
{
    // Capture byte offset of m_char *before* next_codepoint advances m_pos.
    // m_pos points to the first byte of the code point about to be read.
    m_char_byte = (m_pos != nullptr) ? intptr_t(m_pos - m_line.begin()) : 0;
    m_char = m_line.next_codepoint(m_pos);
}

void Scanner::set_line(intptr_t index)
{
    m_line = m_source->get_line(index);
}

void Scanner::read_line()
{
    if (m_line_no == m_source->size())
    {
        m_line.clear();
        m_char = Token::ETX;
    }
    else
    {
        set_line(++m_line_no);
    }
}

void Scanner::skip_white()
{

    while (check_space(m_char))
    {
        read_char();
    }
}

void Scanner::skip()
{
    read_char();
}

void Scanner::accept()
{
    m_spelling.append(m_char);
    read_char();
}

void Scanner::scan_digits()
{
    // Allow '_' as a group separator
    while (isdigit(m_char) || m_char == '_')
    {
        if (m_char == '_')
            skip();
        else
            accept();
    }
}

Token Scanner::scan_string(char32_t end)
{
    // Capture the opening-delimiter position. Both the StringLiteral we may
    // return and the leading LParen / first StringLiteral chunk we may queue
    // (in the interpolated case) point back to this column so the editor's
    // squiggle anchors at the start of the literal.
    intptr_t open_line = m_token_line;
    intptr_t open_col = m_token_column;
    skip();

    // If the character following the opening delimiter is the same delimiter, and the one
    // after that is also the same, this is the opener of a triple-quoted (multi-line) string.
    if (m_char == end && peek_char() == end)
    {
        skip(); // second delimiter
        skip(); // third delimiter
        return scan_triple_string(end, open_line, open_col);
    }

    return scan_single_string(end, open_line, open_col);
}

Token Scanner::scan_single_string(char32_t end, intptr_t open_line, intptr_t open_col)
{
    intptr_t start_line = m_line_no;
    bool has_interp = false;
    // Buffer of synthetic tokens produced by ${...} desugaring for THIS string.
    // We accumulate the whole desugared sequence locally and only transfer it
    // onto m_pending in finish_string_token. Pushing onto m_pending earlier
    // would corrupt subsequent recursive read_token() calls inside later
    // ${...} segments — those calls drain m_pending before scanning the
    // source, and would return our own queued tokens instead of new source
    // tokens, which fails for any string with more than one ${...}.
    std::vector<Token> synthetic;

    while (m_char != end && m_char != Token::ETX)
    {
        // Single-line strings cannot span lines: a raw newline before the closing delimiter
        // is an error. Use triple quotes (""" or ''') for multi-line strings.
        if (m_char == U'\n')
        {
            auto message = utils::format("[Syntax error] File \"%\" at line %\n"
                                         "Newline in string literal (use triple quotes \"\"\" or ''' for multi-line strings)",
                                         m_source->filename(), start_line);
            throw RuntimeError(start_line, message);
        }

        // Interpolation: "${" begins an embedded expression. Detected before
        // escape handling so that a backslash-escaped "\$" (handled below) can
        // suppress interpolation. Same-line check via peek_char is implicit:
        // if m_char is '$' here, we are not on the trailing '\n' of a line
        // (that case is the newline error above), so peek_char looks at the
        // very next codepoint in the same line.
        if (m_char == U'$' && peek_char() == U'{')
        {
            scan_interpolation_segment(!has_interp, open_line, open_col, synthetic);
            has_interp = true;
            continue;
        }

        if (m_char == '\\')
        {
            // Skip for now. It may be restored if it's not a special character.
            skip();

            if (m_char == 'n')
            {
                m_char = '\n'; // line feed (new line)
            }
            else if (m_char == 't')
            {
                m_char = '\t'; // horizontal tab
            }
            else if (m_char == 'r')
            {
                m_char = '\r'; // carriage return
            }
            else if (m_char == '\\')
            {
                m_char = '\\'; // backslash
            }
            else if (m_char == '\'')
            {
                m_char = '\''; // single quote
            }
            else if (m_char == '"')
            {
                m_char = '\"'; // double quote
            }
            else if (m_char == 'v')
            {
                m_char = '\v'; // vertical tab
            }
            else if (m_char == 'a')
            {
                m_char = '\a'; // audible bell
            }
            else if (m_char == 'b')
            {
                m_char = '\b'; // backspace
            }
            else if (m_char == 'f')
            {
                m_char = '\f'; // form feed (new page)
            }
            else if (m_char == '$')
            {
                // \$ suppresses string interpolation: the resulting character is a
                // literal '$', and because m_char advances to whatever follows on
                // the next loop iteration, the interpolation check above will not
                // re-fire on this '$'.
                m_char = '$';
            }
            else
            {
                // Restore
                m_spelling.push_back('\\');
            }
        }
        accept();
    }

    // If we haven't reached the end of the text, ignore string terminating character.
    // Otherwise the string is unterminated (we hit ETX before the closing delimiter).
    if (m_char == end)
    {
        skip();
    }
    else
    {
        auto message = utils::format("[Syntax error] File \"%\" at line %\nUnterminated string literal",
                                     m_source->filename(), start_line);
        throw RuntimeError(start_line, message);
    }

    return finish_string_token(has_interp, open_line, open_col, synthetic);
}

Token Scanner::scan_triple_string(char32_t end, intptr_t open_line, intptr_t open_col)
{
    // Triple-quoted strings ("""..."""  or  '''...''') may span multiple lines.
    // Newlines and single occurrences of the delimiter are part of the content;
    // the string is closed only by three delimiters in a row. Escape sequences
    // are processed exactly as in single-line strings, and ${...} interpolation
    // is supported on the same terms.
    intptr_t start_line = m_line_no;
    bool has_interp = false;
    std::vector<Token> synthetic;

    while (m_char != Token::ETX)
    {
        if (m_char == end)
        {
            // Tentatively consume up to two more delimiters. If we find a third in a row,
            // this is the closing triple. Otherwise, the one or two delimiters we consumed
            // belong to the string content and must be appended literally.
            skip();
            if (m_char == end)
            {
                skip();
                if (m_char == end)
                {
                    skip();
                    return finish_string_token(has_interp, open_line, open_col, synthetic);
                }
                m_spelling.append(end);
                m_spelling.append(end);
                continue;
            }
            m_spelling.append(end);
            continue;
        }

        // Interpolation works inside triple-quoted strings too, with the same
        // ${...} syntax and the same \$ escape. Inner expressions may span
        // multiple physical lines; Eol tokens within the embedded expression
        // are dropped by scan_interpolation_segment.
        if (m_char == U'$' && peek_char() == U'{')
        {
            scan_interpolation_segment(!has_interp, open_line, open_col, synthetic);
            has_interp = true;
            continue;
        }

        if (m_char == '\\')
        {
            skip();

            if      (m_char == 'n')  { m_char = '\n'; }
            else if (m_char == 't')  { m_char = '\t'; }
            else if (m_char == 'r')  { m_char = '\r'; }
            else if (m_char == '\\') { m_char = '\\'; }
            else if (m_char == '\'') { m_char = '\''; }
            else if (m_char == '"')  { m_char = '\"'; }
            else if (m_char == 'v')  { m_char = '\v'; }
            else if (m_char == 'a')  { m_char = '\a'; }
            else if (m_char == 'b')  { m_char = '\b'; }
            else if (m_char == 'f')  { m_char = '\f'; }
            else if (m_char == '$')  { m_char = '$'; }
            else
            {
                m_spelling.push_back('\\');
            }
        }
        accept();
    }

    auto message = utils::format("[Syntax error] File \"%\" at line %\nUnterminated triple-quoted string literal",
                                 m_source->filename(), start_line);
    throw RuntimeError(start_line, message);
}

void Scanner::scan_interpolation_segment(bool first_interp, intptr_t open_line, intptr_t open_col, std::vector<Token> &synthetic)
{
    // The position of '$' is the most informative anchor for the synthetic
    // tokens we are about to emit for THIS segment. The leading-LParen and the
    // very first StringLiteral chunk, however, point back to the string's
    // opening delimiter (open_line/open_col): they conceptually belong to the
    // string as a whole, not to any specific ${...}.
    intptr_t interp_line = m_line_no;
    intptr_t interp_col = m_char_byte;

    // Snapshot the leading literal chunk BEFORE doing anything that touches
    // m_spelling. Recursive read_token() calls below overwrite m_spelling
    // freely (it's the scratch buffer for identifiers, numbers, nested strings,
    // ...) so we must move it out now.
    String leading = std::move(m_spelling);
    m_spelling.clear();

    // Consume the literal "${" from the source before scanning inner tokens.
    skip(); // '$'
    skip(); // '{'

    // CRITICAL: scan the embedded-expression tokens by calling read_token
    // recursively, but accumulate them in a LOCAL vector — and in `synthetic`
    // (the per-string accumulator) only at the END of this function, never
    // mid-flight. read_token() drains m_pending before scanning the source,
    // and `synthetic` is moved into m_pending only by finish_string_token().
    // The invariant we rely on is therefore: while a string is being scanned,
    // m_pending is empty except for tokens belonging to a JUST-FINISHED nested
    // string — those are the legitimate input of the recursive read_token
    // loop below. Anything we've already produced for the current string sits
    // in `synthetic` instead. Without this two-buffer split, a second ${...}
    // in the same string would see the first segment's tokens come back out
    // of read_token() and never advance the source pointer.
    //
    // Brace depth tracks LCurl ('{') tokens (NOT parens — table literals use
    // braces too, e.g. `${ {a:1}["a"] }`); we stop when an RCurl drops the
    // depth back to zero — that RCurl is the '}' closing this ${...} and is
    // NOT recorded. Eol tokens are dropped: the embedded expression is a
    // single expression regardless of how the user laid it out across source
    // lines (matters mostly inside triple-quoted strings).
    std::vector<Token> inner;
    int depth = 1;
    while (depth > 0)
    {
        Token t = read_token();
        if (t.is(Token::Lexeme::Eot))
        {
            auto message = utils::format("[Syntax error] File \"%\" at line %\n"
                                         "Unterminated interpolation in string literal "
                                         "(expected closing '}' for ${...})",
                                         m_source->filename(), interp_line);
            throw RuntimeError(interp_line, message);
        }
        if (t.is(Token::Lexeme::LCurl))
        {
            ++depth;
            inner.push_back(std::move(t));
        }
        else if (t.is(Token::Lexeme::RCurl))
        {
            --depth;
            if (depth == 0) break;
            inner.push_back(std::move(t));
        }
        else if (t.is(Token::Lexeme::Eol))
        {
            // drop — embedded expression is a single expression
        }
        else
        {
            inner.push_back(std::move(t));
        }
    }

    if (inner.empty())
    {
        auto message = utils::format("[Syntax error] File \"%\" at line %\n"
                                     "Empty interpolation \"${}\" in string literal",
                                     m_source->filename(), interp_line);
        throw RuntimeError(interp_line, message);
    }

    // Now extend the per-string `synthetic` buffer with this segment's
    // contribution to the desugared token stream.
    if (first_interp)
    {
        // First ${...} in this string. Wrap the whole desugared chain in an
        // outer LParen so that the resulting concatenation parses as a single
        // primary expression regardless of surrounding precedence (e.g. inside
        // `2 + "x${y}z"` we must NOT let the outer '+' bind tighter than the
        // synthetic '&'). The leading literal chunk goes in next; it may be
        // empty (e.g. "${x}foo") and that's fine — Concat coerces everything
        // to string anyway, and keeping an empty chunk guarantees Concat fires
        // even when the string contains a single ${expr} and nothing else.
        synthetic.push_back(Token(Token::Lexeme::LParen, "(", open_line, open_col));
        synthetic.push_back(Token(Token::Lexeme::StringLiteral, std::move(leading), open_line, open_col));
    }
    else
    {
        // Subsequent ${...}: continue the existing concat chain.
        synthetic.push_back(Token(Token::Lexeme::OpConcat, "&", interp_line, interp_col));
        synthetic.push_back(Token(Token::Lexeme::StringLiteral, std::move(leading), interp_line, interp_col));
    }

    // Wrap the embedded expression in its own LParen/RParen pair so that we
    // can substitute *any* expression here without worrying about how it
    // interacts with the synthetic '&'s on either side (e.g. `${a + b}` must
    // not let the outer concat split a/b).
    synthetic.push_back(Token(Token::Lexeme::OpConcat, "&", interp_line, interp_col));
    synthetic.push_back(Token(Token::Lexeme::LParen, "(", interp_line, interp_col));
    for (auto &t : inner) synthetic.push_back(std::move(t));
    // Close the inner-expression wrap. The position is taken from where we
    // currently are in the source — i.e., just past the matching '}'.
    synthetic.push_back(Token(Token::Lexeme::RParen, ")", m_line_no, m_char_byte));

    // Make sure m_spelling is empty so the outer literal loop starts fresh.
    m_spelling.clear();
}

Token Scanner::finish_string_token(bool has_interp, intptr_t open_line, intptr_t open_col, std::vector<Token> &synthetic)
{
    if (!has_interp)
    {
        // Fast path / pre-interpolation behavior: a single StringLiteral with
        // the accumulated literal content. The synthetic buffer is empty in
        // this case and m_pending is not touched.
        return Token(Token::Lexeme::StringLiteral, m_spelling, open_line, open_col);
    }

    // Interpolated string: append the trailing literal chunk (possibly empty,
    // e.g. for "x${y}") and the closing RParen to the local synthetic buffer,
    // then transfer everything except the very first token onto m_pending and
    // hand that first token (an LParen) back to the parser right now. The
    // parser will see the rest of the desugared sequence on subsequent calls
    // to read_token, which drains m_pending FIFO.
    intptr_t close_line = m_line_no;
    intptr_t close_col = m_char_byte;
    synthetic.push_back(Token(Token::Lexeme::OpConcat, "&", close_line, close_col));
    synthetic.push_back(Token(Token::Lexeme::StringLiteral, std::move(m_spelling), close_line, close_col));
    synthetic.push_back(Token(Token::Lexeme::RParen, ")", close_line, close_col));
    m_spelling.clear();

    Token first = std::move(synthetic.front());
    for (size_t i = 1; i < synthetic.size(); ++i)
    {
        m_pending.push_back(std::move(synthetic[i]));
    }
    return first;
}

char32_t Scanner::peek_char() const
{
    // Look at the next codepoint without advancing scanner state. If we are at the
    // end of the current line, peek at the first codepoint of the next line.
    if (m_pos != nullptr && m_pos != m_line.end())
    {
        auto pos = m_pos;
        return m_line.next_codepoint(pos);
    }
    if (m_line_no < m_source->size())
    {
        const String next = m_source->get_line(m_line_no + 1);
        if (!next.empty())
        {
            auto pos = next.begin();
            return next.next_codepoint(pos);
        }
    }
    return Token::ETX;
}


// Read one token from the source code
Token Scanner::read_token()
{
    // String interpolation has the scanner emit a multi-token sequence
    // (e.g. ( "foo" & ( x ) & "bar" )) from a single literal in the source.
    // The first token of that sequence is returned by scan_string itself; the
    // rest is queued here and returned in FIFO order on subsequent calls. We
    // must drain this queue before doing any further lexing so the parser sees
    // exactly the synthetic sequence in order, never interleaved with whatever
    // happens to follow the closing delimiter in the source.
    if (!m_pending.empty())
    {
        Token t = std::move(m_pending.front());
        m_pending.pop_front();
        return t;
    }

    m_spelling.clear();
    skip_white();

    // Snapshot the position of the token we are about to scan. Every Token
    // constructor below reads from these so the (line, column) pair on a Token
    // always refers to its starting position, even when scanning crosses line
    // boundaries (e.g. triple-quoted strings).
    m_token_line = m_line_no;
    m_token_column = m_char_byte;

    // An identifier must start with a Unicode "alphabetic character". This includes characters such as
    // Chinese '漢' or Korean '한'.
    if (String::is_letter(m_char))
    {
        accept();

        while (String::is_letter(m_char) || isdigit(m_char) || m_char == U'_')
        {
            accept();
        }

        // Variable can end with '$'. This is used for "special" symbols, normally used for implementation details.
        // Private instance members, especially those used to implement computed fields, should also end with '$',
		// although this is not enforced.
        if (m_char == U'$')
        {
            accept();
            // Allow '$'*, so that users can for instance create their own `var$$` symbol if they want to.
            while (m_char == U'$')
            { accept(); }
        }

        return Token(m_spelling, m_token_line, m_token_column, true);
    }

    // Scan a number.
    if (isdigit(m_char))
    {
        accept();
        scan_digits();

        bool is_float = false;

        if (m_char == U'.')
        {
            accept();
            scan_digits();
            is_float = true;
        }

        // Optional scientific-notation exponent: [eE][+-]?[0-9_]+
        // Examples: 1e10, 1.5e-3, 2.3E+05. The resulting token is always a
        // FloatLiteral (to_float() already accepts scientific notation via
        // std::from_chars). We commit to consuming the exponent only if its
        // first significant character (after an optional sign) is a digit;
        // otherwise we leave 'e'/'E' for the next token, so e.g. an
        // identifier 'e2' immediately following a literal would still parse
        // as it did before. Scientific-notation literals never span lines.
        if (m_char == U'e' || m_char == U'E')
        {
            auto pos = m_pos;
            char32_t c1 = (pos != nullptr && pos != m_line.end())
                          ? m_line.next_codepoint(pos) : Token::ETX;
            bool has_sign = (c1 == U'+' || c1 == U'-');
            char32_t lead = c1;
            if (has_sign)
            {
                lead = (pos != m_line.end()) ? m_line.next_codepoint(pos) : Token::ETX;
            }

            if (isdigit(lead))
            {
                accept();              // 'e' or 'E'
                if (has_sign) accept(); // '+' or '-'
                scan_digits();         // exponent digits
                is_float = true;
            }
        }

        if (is_float)
            return Token(Token::Lexeme::FloatLiteral, m_spelling, m_token_line, m_token_column);

        return Token(Token::Lexeme::IntegerLiteral, m_spelling, m_token_line, m_token_column);
    }

    switch (m_char)
    {
    case U'=':
    {
        accept();

        if (m_char == U'=')
        {
            accept();
            return Token(m_spelling, m_token_line, m_token_column, false);
        }
        else
        {
            return Token(m_spelling, m_token_line, m_token_column, false);
        }
    }
    case U'#':
    {
	    do skip(); while (m_char != '\n' && m_char != Token::ETX);
		if (m_char == Token::ETX) {
			return Token(Token::Lexeme::Eot, "EOT", m_token_line, m_token_column);
		}
		[[fallthrough]];
    }
    case U'\n':
	{
		accept();
		return Token(Token::Lexeme::Eol, String(), m_token_line, m_token_column);
	}
    case U'"':
    {
        return scan_string(U'"');
    }
    case U'\'':
	{
		return scan_string(U'\'');
	}
    case Token::ETX:
    {
        // Don't accept token since we reached the end.
        return Token(Token::Lexeme::Eot, "EOT", m_token_line, m_token_column);
    }
    case U'(':
    {
	    accept();
	    return Token(Token::Lexeme::LParen, "(", m_token_line, m_token_column);
    }
    case U')':
    {
	    accept();
	    return Token(Token::Lexeme::RParen, ")", m_token_line, m_token_column);
    }
    case U'{':
    {
	    accept();
	    return Token(Token::Lexeme::LCurl, "{", m_token_line, m_token_column);
    }
    case U'}':
    {
	    accept();
	    return Token(Token::Lexeme::RCurl, "}", m_token_line, m_token_column);
    }
    case U'[':
    {
	    accept();
	    return Token(Token::Lexeme::LSquare, "[", m_token_line, m_token_column);
    }
    case U']':
    {
	    accept();
	    return Token(Token::Lexeme::RSquare, "]", m_token_line, m_token_column);
    }
    case U'+':
    {
	    accept();
		if (m_char == U'=')
		{
			accept();
			return Token(Token::Lexeme::OpAssignPlus, "+=", m_token_line, m_token_column);
		}
	    return Token(Token::Lexeme::OpPlus, "+", m_token_line, m_token_column);
    }
    case U'-':
    {
	    accept();
		if (m_char == U'=')
		{
			accept();
			return Token(Token::Lexeme::OpAssignMinus, "-=", m_token_line, m_token_column);
		}
	    return Token(Token::Lexeme::OpMinus, "-", m_token_line, m_token_column);
    }
    case U'*':
    {
	    accept();
		if (m_char == U'=')
		{
			accept();
			return Token(Token::Lexeme::OpAssignStar, "*=", m_token_line, m_token_column);
		}
	    return Token(Token::Lexeme::OpStar, "*", m_token_line, m_token_column);
    }
    case U'/':
    {
	    accept();
		if (m_char == U'=')
		{
			accept();
			return Token(Token::Lexeme::OpAssignSlash, "/=", m_token_line, m_token_column);
		}
	    return Token(Token::Lexeme::OpSlash, "/", m_token_line, m_token_column);
    }
    case U'^':
	{
		accept();
		if (m_char == U'=')
		{
			accept();
			return Token(Token::Lexeme::OpAssignPower, "^=", m_token_line, m_token_column);
		}
		return Token(Token::Lexeme::OpPower, "^", m_token_line, m_token_column);
	}
   	case U'%':
	{
		accept();
		if (m_char == U'=')
		{
			accept();
			return Token(Token::Lexeme::OpAssignMod, "%=", m_token_line, m_token_column);
		}
		return Token(Token::Lexeme::OpMod, "%", m_token_line, m_token_column);
	}
    case U'&':
    {
	    accept();
		if (m_char == U'=')
		{
			accept();
			return Token(Token::Lexeme::OpAssignConcat, "&=", m_token_line, m_token_column);
		}
	    return Token(Token::Lexeme::OpConcat, "&", m_token_line, m_token_column);
    }
    case U',':
    {
	    accept();
	    return Token(Token::Lexeme::Comma, ",", m_token_line, m_token_column);
    }
    case U';':
    {
	    accept();
	    return Token(Token::Lexeme::Semicolon, ";", m_token_line, m_token_column);
    }
    case U':':
    {
	    accept();
	    return Token(Token::Lexeme::Colon, ":", m_token_line, m_token_column);
    }
    case U'.':
    {
        accept();
        return Token(Token::Lexeme::Dot, ".", m_token_line, m_token_column);
    }
	case U'|':
	{
		accept();
		return Token(Token::Lexeme::Pipe, "|", m_token_line, m_token_column);
	}
	case U'~':
	{
		accept();
		return Token(Token::Lexeme::Tilde, "~", m_token_line, m_token_column);
	}
    case U'!':
    {
        accept();

        if (m_char == U'=')
        {
            accept();
            return Token(Token::Lexeme::OpNotEqual, m_spelling, m_token_line, m_token_column);
        }

        report_error("invalid token");
        break; // never reached.
    }
    case U'<':
    {
        accept();

        if (m_char == U'=')
        {
            accept();

            if (m_char == U'>')
            {
                accept();
                return Token(Token::Lexeme::OpCompare, m_spelling, m_token_line, m_token_column);
            }
            else
            {
                return Token(Token::Lexeme::OpLessEqual, m_spelling, m_token_line, m_token_column);
            }
        }
        else
        {
            return Token(Token::Lexeme::OpLessThan, m_spelling, m_token_line, m_token_column);
        }
    }
    case U'>':
    {
        accept();

        if (m_char == U'=')
        {
            accept();
            return Token(Token::Lexeme::OpGreaterEqual, m_spelling, m_token_line, m_token_column);
        }
        else
        {
            return Token(Token::Lexeme::OpGreaterThan, m_spelling, m_token_line, m_token_column);
        }
    }
	case U'@':
	{
		accept();
		return Token(Token::Lexeme::OpAt, m_spelling, m_token_line, m_token_column);
	}

    default:
        break;
    }

    report_error("invalid token");

    return Token();
}

void Scanner::report_error(const std::string &hint, intptr_t offset, const char *error_type,
                           intptr_t err_column, intptr_t err_length)
{
	assert(m_line_no != 0);
	String line = m_source->get_line(m_line_no);
	// These must be computed before trimming, since the iterator will be invalidated
	auto step_back = intptr_t(m_pos > line.begin());
	auto left_space = intptr_t(m_pos - line.begin());

	line.rtrim();

	// normalize tabs
	auto old_size = line.size();
	line.replace("\t", "    ");
	auto additional_padding = line.size() - old_size;

	// Set spacing to the location of the error
	auto beginning = step_back; // move 1 char back, unless we are at the beginning
	intptr_t count = left_space + additional_padding - offset - beginning;
	String filler;
	filler.fill(U' ', count);

	auto message = utils::format("[% error] File \"%\" at line %\n%\n%^",
	                             error_type, m_source->filename(), m_line_no, line, filler);

	if (!hint.empty())
	{
		message.append("\nHint: ");
		message.append(hint);
	}

	// Caller-supplied (column, length) wins; otherwise fall back to the
	// scanner's current position so even raw `report_error("invalid token")`
	// calls carry a usable squiggle anchor.
	intptr_t col = (err_column >= 0) ? err_column : m_char_byte;
	intptr_t len = (err_length > 0) ? err_length : 1;

	RuntimeError err(m_line_no, message);
	err.set_position(col, len);
	throw err;
}

bool Scanner::check_space(char32_t c)
{
	switch (c)
	{
		case ' ':
		case '\t':
		case '\r':
		case '\f':
		case '\v':
			return true;
		default:
			return false;
	}
}

} // namespace phonometrica