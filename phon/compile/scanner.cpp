// Phonometrica engine — the scanner. See header.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/compile/scanner.hpp>

#include <phon/base/unicode.hpp>

#include <string>

namespace phonometrica {

namespace {

bool is_digit(char32_t c) noexcept { return c >= U'0' && c <= U'9'; }

void put_cp(std::string &buf, char32_t cp)
{
	char tmp[4];
	size_t n = unicode::encode(cp, tmp);
	buf.append(tmp, n);
}

} // namespace

Scanner::Scanner(const Source &source)
    : m_source(source),
      m_cur(source.begin()),
      m_cp_ptr(source.begin()),
      m_line_start(source.begin()),
      m_cp(0),
      m_line(1)
{
	advance(); // load the first code point
}

void Scanner::advance()
{
	// Line accounting: stepping off a '\n' begins a new line whose first byte is
	// the current cursor (which already sits just past that '\n').
	if (m_cp == U'\n')
	{
		++m_line;
		m_line_start = m_cur;
	}

	if (m_cur >= m_source.end())
	{
		m_cp_ptr = m_source.end();
		m_cp = EOT;
		return;
	}

	m_cp_ptr = m_cur;
	char32_t cp = 0;
	bool valid = false;
	size_t n = unicode::decode(m_cur, m_source.end(), &cp, &valid);
	m_cur += n;
	m_cp = cp;
}

char32_t Scanner::peek() const
{
	if (m_cur >= m_source.end())
		return EOT;
	char32_t cp = 0;
	bool valid = false;
	unicode::decode(m_cur, m_source.end(), &cp, &valid);
	return cp;
}

intptr_t Scanner::column() const
{
	return static_cast<intptr_t>(m_cp_ptr - m_line_start);
}

Token Scanner::make(Lexeme id, String spelling)
{
	switch (id)
	{
	case Lexeme::LParen:
	case Lexeme::LSquare:
	case Lexeme::LBrace:
		++m_bracket_depth;
		break;
	case Lexeme::RParen:
	case Lexeme::RSquare:
	case Lexeme::RBrace:
		if (m_bracket_depth > 0)
			--m_bracket_depth;
		break;
	default:
		break;
	}
	m_last = id;
	return Token(id, std::move(spelling), m_tok_line, m_tok_col);
}

bool Scanner::continues_line(Lexeme l) noexcept
{
	switch (l)
	{
	case Lexeme::Plus:
	case Lexeme::Minus:
	case Lexeme::Star:
	case Lexeme::Slash:
	case Lexeme::Caret:
	case Lexeme::Concat:
	case Lexeme::Assign:
	case Lexeme::PlusEq:
	case Lexeme::MinusEq:
	case Lexeme::StarEq:
	case Lexeme::SlashEq:
	case Lexeme::ConcatEq:
	case Lexeme::Eq:
	case Lexeme::NotEq:
	case Lexeme::Less:
	case Lexeme::LessEq:
	case Lexeme::Greater:
	case Lexeme::GreaterEq:
	case Lexeme::Arrow:
	case Lexeme::Comma:
	case Lexeme::Colon:
	case Lexeme::Dot:
	case Lexeme::LParen:
	case Lexeme::LBrace:
	case Lexeme::LSquare:
	// keyword operators
	case Lexeme::And:
	case Lexeme::Or:
	case Lexeme::Not:
	case Lexeme::Div:
	case Lexeme::Mod:
	case Lexeme::Is:
	case Lexeme::As:
	case Lexeme::In:
	case Lexeme::To:
	case Lexeme::Step:
		return true;
	default:
		return false;
	}
}

void Scanner::skip_block_comment()
{
	intptr_t open_line = m_line;
	intptr_t open_col = column();
	advance(); // '#'
	advance(); // '*'
	for (;;)
	{
		if (m_cp == EOT)
			error_at(open_line, open_col, 2, "unterminated block comment (missing '*#')");
		if (m_cp == U'*' && peek() == U'#')
		{
			advance(); // '*'
			advance(); // '#'
			return;
		}
		advance();
	}
}

void Scanner::skip_spaces_and_comments()
{
	for (;;)
	{
		char32_t c = m_cp;
		if (c == U' ' || c == U'\t' || c == U'\r' || c == U'\f' || c == U'\v')
		{
			advance();
			continue;
		}
		if (c == U'#')
		{
			if (peek() == U'*')
			{
				skip_block_comment();
				continue;
			}
			// Line comment (also covers a `#!` shebang): to end of line, but
			// leave the terminating '\n' so it still separates statements.
			while (m_cp != U'\n' && m_cp != EOT)
				advance();
			continue;
		}
		break;
	}
}

Token Scanner::next()
{
	for (;;)
	{
		skip_spaces_and_comments();
		m_tok_line = m_line;
		m_tok_col = column();
		char32_t c = m_cp;

		if (c == EOT)
			return make(Lexeme::Eot, "<eot>");

		if (c == U'\n')
		{
			advance();
			bool suppress = m_bracket_depth > 0 || !m_interp.empty()
			                || m_last == Lexeme::Newline || m_last == Lexeme::Unknown
			                || continues_line(m_last);
			if (suppress)
				continue;
			return make(Lexeme::Newline, "\n");
		}

		if (unicode::is_id_start(c) || c == U'_')
			return scan_identifier();

		if (is_digit(c))
			return scan_number();

		if (c == U'"')
		{
			advance(); // first '"'
			if (m_cp == U'"')
			{
				advance(); // second '"'
				if (m_cp == U'"')
				{
					advance(); // third '"' -> triple-quoted
					return scan_string_chunk(true, true);
				}
				return make(Lexeme::String, String()); // "" empty string
			}
			return scan_string_chunk(true, false);
		}

		if (c == U'\'')
		{
			advance(); // first '\''
			if (m_cp == U'\'')
			{
				advance(); // second '\''
				if (m_cp == U'\'')
				{
					advance(); // third '\'' -> triple-quoted raw
					return scan_raw_string(true);
				}
				return make(Lexeme::String, String()); // '' empty string
			}
			return scan_raw_string(false);
		}

		switch (c)
		{
		case U'=':
			advance();
			if (m_cp == U'=') { advance(); return make(Lexeme::Eq, "=="); }
			return make(Lexeme::Assign, "=");
		case U'+':
			advance();
			if (m_cp == U'=') { advance(); return make(Lexeme::PlusEq, "+="); }
			return make(Lexeme::Plus, "+");
		case U'-':
			advance();
			if (m_cp == U'=') { advance(); return make(Lexeme::MinusEq, "-="); }
			if (m_cp == U'>') { advance(); return make(Lexeme::Arrow, "->"); }
			return make(Lexeme::Minus, "-");
		case U'*':
			advance();
			if (m_cp == U'=') { advance(); return make(Lexeme::StarEq, "*="); }
			return make(Lexeme::Star, "*");
		case U'/':
			advance();
			if (m_cp == U'=') { advance(); return make(Lexeme::SlashEq, "/="); }
			return make(Lexeme::Slash, "/");
		case U'^':
			advance();
			return make(Lexeme::Caret, "^");
		case U'&':
			advance();
			if (m_cp == U'=') { advance(); return make(Lexeme::ConcatEq, "&="); }
			return make(Lexeme::Concat, "&");
		case U'<':
			advance();
			if (m_cp == U'=') { advance(); return make(Lexeme::LessEq, "<="); }
			return make(Lexeme::Less, "<");
		case U'>':
			advance();
			if (m_cp == U'=') { advance(); return make(Lexeme::GreaterEq, ">="); }
			return make(Lexeme::Greater, ">");
		case U'!':
			advance();
			if (m_cp == U'=') { advance(); return make(Lexeme::NotEq, "!="); }
			error("unexpected '!' (use 'not' for logical negation, '!=' for inequality)");
		case U'(':
			advance();
			return make(Lexeme::LParen, "(");
		case U')':
			advance();
			return make(Lexeme::RParen, ")");
		case U'[':
			advance();
			return make(Lexeme::LSquare, "[");
		case U']':
			advance();
			return make(Lexeme::RSquare, "]");
		case U'{':
			advance();
			if (!m_interp.empty())
				++m_interp.back().brace_depth; // a nested brace inside an interpolation expression
			return make(Lexeme::LBrace, "{");
		case U'}':
			if (!m_interp.empty() && m_interp.back().brace_depth == 0)
			{
				// This '}' closes the current interpolation expression: leave
				// expression mode and resume scanning the string's literal tail
				// in the enclosing string's mode (single-line or triple).
				bool triple = m_interp.back().triple;
				m_interp.pop_back();
				advance(); // '}'
				return scan_string_chunk(false, triple);
			}
			advance();
			if (!m_interp.empty())
				--m_interp.back().brace_depth;
			return make(Lexeme::RBrace, "}");
		case U',':
			advance();
			return make(Lexeme::Comma, ",");
		case U':':
			advance();
			return make(Lexeme::Colon, ":");
		case U'.':
			advance();
			if (m_cp == U'.')
			{
				advance(); // second '.'
				if (m_cp == U'.')
				{
					advance(); // third '.'
					return make(Lexeme::Ellipsis, "...");
				}
				error_at(m_tok_line, m_tok_col, 2,
				         "unexpected '..' (range syntax is not supported; use seq())");
			}
			return make(Lexeme::Dot, ".");
		case U';':
			advance();
			return make(Lexeme::Semicolon, ";");
		case U'@':
			advance();
			return make(Lexeme::At, "@"); // array literal: @[...]
		default:
			break;
		}

		error("unexpected character");
	}
}

Token Scanner::scan_identifier()
{
	const char *start = m_cp_ptr;
	advance();
	while (m_cp != EOT && (unicode::is_id_continue(m_cp) || m_cp == U'_'))
		advance();

	// The identifier spans [start, m_cp_ptr): m_cp is now the first non-id char.
	std::string_view text(start, static_cast<size_t>(m_cp_ptr - start));
	Lexeme kw = keyword_lexeme(text);
	return make(kw, String(Substring(text)));
}

Token Scanner::scan_number()
{
	const char *start = m_cp_ptr;
	bool is_float = false;

	auto scan_digits = [&] {
		while (is_digit(m_cp) || m_cp == U'_')
			advance();
	};

	scan_digits();

	// Fractional part: a '.' is a decimal point only when a digit follows, so
	// `a.b` field access and `1.method(...)` are never mis-lexed as a float.
	if (m_cp == U'.' && is_digit(peek()))
	{
		is_float = true;
		advance(); // '.'
		scan_digits();
	}

	// Scientific-notation exponent: [eE][+-]?[0-9][0-9_]*  (commit only if valid).
	if (m_cp == U'e' || m_cp == U'E')
	{
		const char *p = m_cur; // first byte after the 'e'/'E'
		auto decode_at = [&](const char *&q) -> char32_t {
			if (q >= m_source.end())
				return EOT;
			char32_t cp = 0;
			bool valid = false;
			q += unicode::decode(q, m_source.end(), &cp, &valid);
			return cp;
		};
		char32_t c1 = decode_at(p);
		bool has_sign = (c1 == U'+' || c1 == U'-');
		char32_t lead = has_sign ? decode_at(p) : c1;
		if (is_digit(lead))
		{
			is_float = true;
			advance(); // 'e'/'E'
			if (has_sign)
				advance();
			scan_digits();
		}
	}

	std::string_view text(start, static_cast<size_t>(m_cp_ptr - start));
	return make(is_float ? Lexeme::Float : Lexeme::Integer, String(Substring(text)));
}

char32_t Scanner::scan_escape(intptr_t bs_line, intptr_t bs_col)
{
	char32_t c = m_cp;
	char32_t r;
	switch (c)
	{
	case U'n': r = U'\n'; break;
	case U't': r = U'\t'; break;
	case U'r': r = U'\r'; break;
	case U'\\': r = U'\\'; break;
	case U'"': r = U'"'; break;
	case U'\'': r = U'\''; break;
	case U'0': r = U'\0'; break;
	case U'a': r = U'\a'; break;
	case U'b': r = U'\b'; break;
	case U'f': r = U'\f'; break;
	case U'v': r = U'\v'; break;
	case U'{': r = U'{'; break; // literal brace (suppresses interpolation)
	case U'}': r = U'}'; break;
	case EOT:
		error_at(bs_line, bs_col, 1, "unterminated string literal");
	case U'\n':
		error_at(bs_line, bs_col, 1, "unterminated string literal (newline after '\\')");
	default:
		error_at(bs_line, bs_col, 2, "invalid escape sequence");
	}
	advance();
	return r;
}

Token Scanner::scan_string_chunk(bool first, bool triple)
{
	m_buf.clear();
	for (;;)
	{
		char32_t c = m_cp;
		if (c == EOT)
			error_at(m_tok_line, m_tok_col, 1, "unterminated string literal");
		if (c == U'\n' && !triple)
			error_at(m_tok_line, m_tok_col, 1,
			         "newline in string literal (use triple quotes \"\"\" for multi-line strings)");

		if (c == U'"')
		{
			if (!triple)
			{
				advance(); // closing quote
				return make(first ? Lexeme::String : Lexeme::InterpEnd, String(m_buf));
			}
			// Triple-quoted: only a run of three '"' closes the string; one or
			// two are literal content.
			advance(); // first '"'
			if (m_cp != U'"') { m_buf.push_back('"'); continue; }
			advance(); // second '"'
			if (m_cp != U'"') { m_buf.append("\"\""); continue; }
			advance(); // third '"' -> close
			return make(first ? Lexeme::String : Lexeme::InterpEnd, String(m_buf));
		}

		if (c == U'{')
		{
			advance(); // '{'
			m_interp.push_back({0, triple}); // enter interpolation-expression mode
			return make(first ? Lexeme::InterpStart : Lexeme::InterpMid, String(m_buf));
		}

		if (c == U'\\')
		{
			intptr_t bs_line = m_line;
			intptr_t bs_col = column();
			advance(); // '\'
			put_cp(m_buf, scan_escape(bs_line, bs_col));
			continue;
		}

		// Ordinary content: copy the code point's raw bytes verbatim.
		m_buf.append(m_cp_ptr, m_cur);
		advance();
	}
}

Token Scanner::scan_raw_string(bool triple)
{
	m_buf.clear();
	for (;;)
	{
		char32_t c = m_cp;
		if (c == EOT)
			error_at(m_tok_line, m_tok_col, 1, "unterminated string literal");
		if (c == U'\n' && !triple)
			error_at(m_tok_line, m_tok_col, 1,
			         "newline in string literal (use triple quotes ''' for multi-line strings)");

		if (c == U'\'')
		{
			if (!triple)
			{
				advance(); // closing quote
				return make(Lexeme::String, String(m_buf));
			}
			advance(); // first '\''
			if (m_cp != U'\'') { m_buf.push_back('\''); continue; }
			advance(); // second '\''
			if (m_cp != U'\'') { m_buf.append("''"); continue; }
			advance(); // third '\'' -> close
			return make(Lexeme::String, String(m_buf));
		}

		m_buf.append(m_cp_ptr, m_cur);
		advance();
	}
}

void Scanner::error(const std::string &message)
{
	error_at(m_line, column(), 1, message);
}

void Scanner::error_at(intptr_t line, intptr_t col, intptr_t length, const std::string &message)
{
	std::string full = "[Syntax error] File \"";
	full += m_source.name();
	full += "\" at line ";
	full += std::to_string(line);
	full += '\n';
	full += m_source.caret(line, col, length);
	full += '\n';
	full += message;
	throw SyntaxError(std::move(full), line, col, length);
}

} // namespace phonometrica
