/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cctype>
#include <phon/analysis/formula.hpp>

namespace phonometrica::stats {

// =====================================================================
// FixedTerm
// =====================================================================

bool FixedTerm::operator==(const FixedTerm &other) const
{
	if (variables.size() != other.variables.size()) return false;
	for (intptr_t i = 1; i <= variables.size(); i++)
	{
		if (variables[i] != other.variables[i]) return false;
	}
	return true;
}

// Quote a variable name if it contains spaces or other non-syntactic characters.
static String quote_name(const String &name)
{
	bool needs_quoting = false;
	for (auto c = name.begin(); c != name.end(); ++c)
	{
		unsigned char ch = static_cast<unsigned char>(*c);
		if (ch == ' ' || ch == '\t') {
			needs_quoting = true;
			break;
		}
	}
	if (!needs_quoting) return name;

	String result("'");
	result.append(name);
	result.append("'");
	return result;
}

String FixedTerm::to_string() const
{
	String result;
	for (intptr_t i = 1; i <= variables.size(); i++)
	{
		if (i > 1) result.append(":");
		result.append(quote_name(variables[i]));
	}
	return result;
}


// =====================================================================
// RandomTerm
// =====================================================================

String RandomTerm::to_string() const
{
	String result("(");
	if (intercept)
	{
		result.append("1");
		for (intptr_t i = 1; i <= slopes.size(); i++)
		{
			result.append(" + ");
			result.append(quote_name(slopes[i]));
		}
	}
	else
	{
		result.append("0");
		for (intptr_t i = 1; i <= slopes.size(); i++)
		{
			result.append(" + ");
			result.append(quote_name(slopes[i]));
		}
	}
	result.append(" | ");
	result.append(quote_name(group));
	result.append(")");
	return result;
}


// =====================================================================
// Formula
// =====================================================================

String Formula::to_string() const
{
	String result = quote_name(response);
	result.append(" ~ ");

	if (!intercept)
	{
		result.append("0 + ");
	}

	for (intptr_t i = 1; i <= fixed.size(); i++)
	{
		if (i > 1) result.append(" + ");
		result.append(fixed[i].to_string());
	}

	for (intptr_t i = 1; i <= random.size(); i++)
	{
		if (!fixed.empty() || i > 1) result.append(" + ");
		result.append(random[i].to_string());
	}

	return result;
}

Array<String> Formula::all_variables() const
{
	Array<String> vars;

	// Helper to add if not already present.
	auto add_unique = [&](const String &v)
	{
		for (intptr_t i = 1; i <= vars.size(); i++)
		{
			if (vars[i] == v) return;
		}
		vars.append(v);
	};

	add_unique(response);

	for (intptr_t i = 1; i <= fixed.size(); i++)
	{
		auto &ft = fixed[i];
		for (intptr_t j = 1; j <= ft.variables.size(); j++) {
			add_unique(ft.variables[j]);
		}
	}

	for (intptr_t i = 1; i <= random.size(); i++)
	{
		auto &rt = random[i];
		add_unique(rt.group);
		for (intptr_t j = 1; j <= rt.slopes.size(); j++) {
			add_unique(rt.slopes[j]);
		}
	}

	return vars;
}


// =====================================================================
// Tokenizer
// =====================================================================

namespace {

enum class TokenType
{
	Name,      // identifier (column name)
	Number,    // 0 or 1 (intercept control)
	Tilde,     // ~
	Plus,      // +
	Minus,     // -
	Star,      // *
	Colon,     // :
	Pipe,      // |
	LParen,    // (
	RParen,    // )
	End        // end of input
};

struct Token
{
	TokenType type;
	String text;
	int value = 0;  // for Number tokens: 0 or 1
};


class Tokenizer
{
public:

	explicit Tokenizer(const String &input)
		: m_input(input.data()), m_pos(input.data()), m_end(input.data() + input.size())
	{
	}

	Token next()
	{
		skip_whitespace();

		if (m_pos >= m_end) {
			return { TokenType::End, "", 0 };
		}

		char c = *m_pos;

		switch (c)
		{
		case '~': m_pos++; return { TokenType::Tilde, "~", 0 };
		case '+': m_pos++; return { TokenType::Plus, "+", 0 };
		case '-': m_pos++; return { TokenType::Minus, "-", 0 };
		case '*': m_pos++; return { TokenType::Star, "*", 0 };
		case ':': m_pos++; return { TokenType::Colon, ":", 0 };
		case '|': m_pos++; return { TokenType::Pipe, "|", 0 };
		case '(': m_pos++; return { TokenType::LParen, "(", 0 };
		case ')': m_pos++; return { TokenType::RParen, ")", 0 };
		default:
			break;
		}

		// Quoted name: 'name with spaces'
		if (c == '\'')
		{
			m_pos++; // skip opening quote
			const char *start = m_pos;
			while (m_pos < m_end && *m_pos != '\'') {
				m_pos++;
			}
			if (m_pos >= m_end) {
				throw error("Unterminated quoted name in formula");
			}
			String name(start, m_pos - start);
			m_pos++; // skip closing quote
			return { TokenType::Name, std::move(name), 0 };
		}

		// Number: 0 or 1 (only when not part of a name)
		if ((c == '0' || c == '1') && !is_name_char(peek(1)))
		{
			m_pos++;
			return { TokenType::Number, String(&c, 1), c - '0' };
		}

		// Name: [a-zA-Z_][a-zA-Z0-9_.]* 
		if (is_name_start(c))
		{
			const char *start = m_pos;
			while (m_pos < m_end && is_name_char(*m_pos)) {
				m_pos++;
			}
			return { TokenType::Name, String(start, m_pos - start), 0 };
		}

		throw error("Unexpected character '%' in formula", String(&c, 1));
	}

	// Peek at the next token without consuming it.
	Token peek_token()
	{
		const char *saved = m_pos;
		Token tok = next();
		m_pos = saved;
		return tok;
	}

private:

	void skip_whitespace()
	{
		while (m_pos < m_end && std::isspace(static_cast<unsigned char>(*m_pos))) {
			m_pos++;
		}
	}

	char peek(int offset) const
	{
		const char *p = m_pos + offset;
		return (p < m_end) ? *p : '\0';
	}

	static bool is_name_start(char c)
	{
		return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
	}

	static bool is_name_char(char c)
	{
		return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
	}

	const char *m_input;
	const char *m_pos;
	const char *m_end;
};


// =====================================================================
// Parser
// =====================================================================

class FormulaParser
{
public:

	explicit FormulaParser(const String &text)
		: m_tokenizer(text)
	{
		advance();
	}

	Formula parse()
	{
		Formula f;

		// Response
		expect(TokenType::Name, "Expected response variable name");
		f.response = m_current.text;
		advance();

		// Tilde
		expect(TokenType::Tilde, "Expected '~' after response variable");
		advance();

		// Right-hand side
		parse_rhs(f);

		expect(TokenType::End, "Unexpected token after formula");

		return f;
	}

private:

	void advance()
	{
		m_current = m_tokenizer.next();
	}

	void expect(TokenType type, const char *msg)
	{
		if (m_current.type != type) {
			throw error(msg);
		}
	}

	// Check if a fixed term already exists in the formula.
	static bool has_term(const Formula &f, const FixedTerm &term)
	{
		for (intptr_t i = 1; i <= f.fixed.size(); i++)
		{
			if (f.fixed[i] == term) return true;
		}
		return false;
	}

	// Add a fixed term if not already present.
	static void add_term(Formula &f, FixedTerm term)
	{
		if (!has_term(f, term)) {
			f.fixed.append(std::move(term));
		}
	}

	// rhs := rhs_element (('+' | '-') rhs_element)*
	void parse_rhs(Formula &f)
	{
		parse_rhs_element(f);

		while (m_current.type == TokenType::Plus || m_current.type == TokenType::Minus)
		{
			bool adding = (m_current.type == TokenType::Plus);
			advance();

			if (!adding)
			{
				// "- 1" removes the intercept
				if (m_current.type == TokenType::Number && m_current.value == 1)
				{
					f.intercept = false;
					advance();
					continue;
				}
				else
				{
					throw error("Only '- 1' (remove intercept) is supported after '-' in formula");
				}
			}

			// "+ 0" also removes the intercept
			if (m_current.type == TokenType::Number && m_current.value == 0)
			{
				f.intercept = false;
				advance();
				continue;
			}

			parse_rhs_element(f);
		}
	}

	// rhs_element := random_term | intercept_control | fixed_term_with_interactions
	void parse_rhs_element(Formula &f)
	{
		// Intercept control at the very start: "0 + ..." or "1 + ..."
		if (m_current.type == TokenType::Number)
		{
			if (m_current.value == 0) {
				f.intercept = false;
			}
			// "1" is a no-op (intercept is the default)
			advance();
			return;
		}

		// Random effects: (...)
		if (m_current.type == TokenType::LParen)
		{
			parse_random_term(f);
			return;
		}

		// Fixed effects term, possibly with * or :
		parse_fixed_term(f);
	}

	// fixed_term := name (('*' | ':') name)*
	// "a * b" expands to: a, b, a:b
	// "a : b" is just the interaction: a:b
	void parse_fixed_term(Formula &f)
	{
		expect(TokenType::Name, "Expected variable name");
		String first = m_current.text;
		advance();

		// Simple term (no operator following, or next is + - ) end)
		if (m_current.type != TokenType::Star && m_current.type != TokenType::Colon)
		{
			FixedTerm t;
			t.variables.append(first);
			add_term(f, std::move(t));
			return;
		}

		// Collect all variables in the chain: a * b * c or a : b : c
		// We track which operators were used.
		Array<String> vars;
		vars.append(first);
		bool has_star = false;

		while (m_current.type == TokenType::Star || m_current.type == TokenType::Colon)
		{
			if (m_current.type == TokenType::Star) {
				has_star = true;
			}
			advance();

			expect(TokenType::Name, "Expected variable name after '*' or ':'");
			vars.append(m_current.text);
			advance();
		}

		if (has_star)
		{
			// "a * b" expands to "a + b + a:b"
			// "a * b * c" expands to "a + b + c + a:b + a:c + b:c + a:b:c"
			// We generate all subsets of size 1..n
			intptr_t n = vars.size();
			intptr_t total = (1 << n); // 2^n subsets

			for (intptr_t mask = 1; mask < total; mask++)
			{
				FixedTerm t;
				for (intptr_t bit = 0; bit < n; bit++)
				{
					if (mask & (1 << bit)) {
						t.variables.append(vars[bit + 1]); // 1-based
					}
				}
				add_term(f, std::move(t));
			}
		}
		else
		{
			// Pure colon interaction: "a:b:c" — just the interaction, no main effects
			FixedTerm t;
			for (intptr_t i = 1; i <= vars.size(); i++) {
				t.variables.append(vars[i]);
			}
			add_term(f, std::move(t));
		}
	}

	// random_term := '(' random_inner '|' name ')'
	// random_inner := elem ('+' elem)*
	// elem := '0' | '1' | name
	void parse_random_term(Formula &f)
	{
		expect(TokenType::LParen, "Expected '(' for random effects term");
		advance();

		RandomTerm rt;
		rt.intercept = true;

		bool explicit_intercept = false;

		// Parse inner elements: "1 + vowel" or "0 + vowel" or just "1"
		while (m_current.type != TokenType::Pipe)
		{
			if (m_current.type == TokenType::Number)
			{
				if (m_current.value == 0) {
					rt.intercept = false;
				}
				explicit_intercept = true;
				advance();
			}
			else if (m_current.type == TokenType::Name)
			{
				rt.slopes.append(m_current.text);
				advance();
			}
			else if (m_current.type == TokenType::End)
			{
				throw error("Unexpected end of formula inside random effects term");
			}
			else
			{
				throw error("Unexpected token '%' inside random effects term", m_current.text);
			}

			// Optional '+' separator between elements
			if (m_current.type == TokenType::Plus) {
				advance();
			}
		}

		// Pipe separator
		expect(TokenType::Pipe, "Expected '|' in random effects term");
		advance();

		// Grouping factor
		expect(TokenType::Name, "Expected grouping variable name after '|'");
		rt.group = m_current.text;
		advance();

		// Closing paren
		expect(TokenType::RParen, "Expected ')' to close random effects term");
		advance();

		f.random.append(std::move(rt));
	}

	Tokenizer m_tokenizer;
	Token m_current;
};

} // anonymous namespace


// =====================================================================
// Public parse entry point
// =====================================================================

Formula Formula::parse(const String &text)
{
	FormulaParser parser(text);
	return parser.parse();
}

} // namespace phonometrica::stats
