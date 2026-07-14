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
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 * Note: The core architecture and integration logic were designed and authored by Julien Eychenne. Portions of the    *
 * statistical estimation logic in this file were developed with the assistance of Claude Opus 4.6 (Anthropic), based  *
 * on published statistical literature and reference R implementations.                                                *
 * All AI-assisted logic has been manually audited, refactored, and validated against a diverse suite of datasets and  *
 * reference R packages to ensure mathematical accuracy and implementation integrity.                                  *
 * While every effort has been made to ensure reliability, this software is provided without a guarantee of being      *
 * bug-free. In the event that discrepancies or errors are discovered, the author will do his best to address them.    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cstdlib>
#include <string>
#include <phon/analysis/formula.hpp>

namespace phonometrica::stats {

// =====================================================================
// FixedTerm
// =====================================================================

bool FixedTerm::operator==(const FixedTerm &other) const
{
	if (variables.size() != other.variables.size()) return false;
	for (intptr_t i = 0; i < variables.size(); i++)
	{
		if (variables[i] != other.variables[i]) return false;
	}
	return true;
}

// Quote a variable name if it contains any character outside the formula
// tokenizer's name alphabet ([A-Za-z0-9_.] + Unicode letters), if it starts
// with a digit, or if it is empty. This is the single source of truth used
// by the serialiser below and re-exposed via formula.hpp for the GUI.
String quote_name(const String &name)
{
	auto is_name_start = [](char32_t cp) -> bool {
		if (cp < 0x80) {
			char c = static_cast<char>(cp);
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
		}
		return String::is_letter(cp);
	};

	auto is_name_cont = [](char32_t cp) -> bool {
		if (cp < 0x80) {
			char c = static_cast<char>(cp);
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			       (c >= '0' && c <= '9') || c == '_' || c == '.';
		}
		return String::is_letter(cp);
	};

	bool needs_quoting = false;

	if (name.empty())
	{
		needs_quoting = true;
	}
	else
	{
		auto it = name.begin();
		auto end = name.end();
		char32_t first = name.next_codepoint(it);
		if (!is_name_start(first)) {
			needs_quoting = true;
		}
		else
		{
			while (it < end)
			{
				char32_t cp = name.next_codepoint(it);
				if (!is_name_cont(cp)) {
					needs_quoting = true;
					break;
				}
			}
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
	for (intptr_t i = 0; i < variables.size(); i++)
	{
		if (i > 0) result.append(":");
		result.append(quote_name(variables[i]));
	}
	return result;
}


// Helper: append a list of FixedTerms to `result`, collapsing any pattern
// where the singletons {a} and {b} appear together with the two-way
// interaction {a, b} back into the compact "a * b" form. Two-way collapse
// only; three-way and higher interactions stay in colon notation, matching
// the conservative policy documented in Formula::to_string. Used by both
// the fixed-effects serialiser and the random-slope serialiser so that
// `a*b` round-trips identically in both contexts.
//
// `need_plus`: in/out. If true on entry, emits " + " before the first
// term. Set to true on exit if any terms were emitted.
static void append_term_array(String &result,
                              const Array<FixedTerm> &terms,
                              bool &need_plus)
{
	const intptr_t n = terms.size();
	Array<bool> consumed;
	for (intptr_t i = 0; i < n; i++)
		consumed.append(false);

	auto find_singleton = [&](const String &v) -> intptr_t {
		for (intptr_t k = 0; k < n; k++) {
			if (consumed[k]) continue;
			const auto &t = terms[k];
			if (t.variables.size() == 1 && t.variables[0] == v)
				return k;
		}
		return -1;
	};

	for (intptr_t i = 0; i < n; i++)
	{
		if (consumed[i]) continue;
		const auto &term = terms[i];

		// Pattern A: singleton {a}; look ahead for an interaction {a, b}
		// whose other singleton {b} is also present.
		if (term.variables.size() == 1)
		{
			const String &a = term.variables[0];
			intptr_t collapse_pair_idx = -1;
			intptr_t collapse_singleton_idx = -1;
			String b_var;

			for (intptr_t k = 0; k < n; k++)
			{
				if (k == i || consumed[k]) continue;
				const auto &t = terms[k];
				if (t.variables.size() != 2) continue;
				if (t.variables[0] == a) {
					b_var = t.variables[1];
				} else if (t.variables[1] == a) {
					b_var = t.variables[0];
				} else {
					continue;
				}
				intptr_t s = find_singleton(b_var);
				if (s != -1) {
					collapse_pair_idx = k;
					collapse_singleton_idx = s;
					break;
				}
			}

			if (collapse_pair_idx != -1)
			{
				if (need_plus) result.append(" + ");
				result.append(quote_name(a));
				result.append(" * ");
				result.append(quote_name(b_var));
				need_plus = true;
				consumed[i] = true;
				consumed[collapse_pair_idx] = true;
				consumed[collapse_singleton_idx] = true;
				continue;
			}
		}
		// Pattern B: interaction {a, b}; look for both singletons.
		else if (term.variables.size() == 2)
		{
			const String &a = term.variables[0];
			const String &b = term.variables[1];
			intptr_t sa = find_singleton(a);
			intptr_t sb = find_singleton(b);
			if (sa != -1 && sb != -1)
			{
				if (need_plus) result.append(" + ");
				result.append(quote_name(a));
				result.append(" * ");
				result.append(quote_name(b));
				need_plus = true;
				consumed[i] = true;
				consumed[sa] = true;
				consumed[sb] = true;
				continue;
			}
		}

		// Default: emit this term as-is (lone singleton, lone interaction,
		// or any higher-order term).
		if (need_plus) result.append(" + ");
		result.append(term.to_string());
		need_plus = true;
		consumed[i] = true;
	}
}


// =====================================================================
// SmoothTerm
// =====================================================================

String SmoothTerm::to_string() const
{
	String result("s(");
	result.append(quote_name(variable));
	if (!by.empty())
	{
		result.append(", by=");
		result.append(quote_name(by));
	}
	if (basis != "cr")
	{
		result.append(", bs=");
		result.append(basis);
	}
	if (k != 10)
	{
		result.append(", k=");
		result.append(std::to_string(k));
	}
	result.append(")");
	return result;
}


// =====================================================================
// RandomTerm
// =====================================================================

String RandomTerm::to_string() const
{
	String result("(");
	result.append(intercept ? "1" : "0");
	// Either "1" or "0" was always emitted, so any slope must be preceded by " + ".
	bool need_plus = true;
	append_term_array(result, slopes, need_plus);
	result.append(" | ");
	if (is_synthetic_group)
	{
		// Split the colon-joined name and quote each component
		// individually so the result reparses as a synthetic group
		// rather than as a literal column name containing colons.
		bool first = true;
		intptr_t start = 0;
		intptr_t n = group.size();
		const char *data = group.data();
		for (intptr_t i = 0; i <= n; i++)
		{
			if (i == n || data[i] == ':')
			{
				String component(data + start, i - start);
				if (!first) result.append(":");
				result.append(quote_name(component));
				first = false;
				start = i + 1;
			}
		}
	}
	else
	{
		result.append(quote_name(group));
	}
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

	bool has_terms = !fixed.empty() || !smooth.empty() || !random.empty();

	if (!has_terms)
	{
		// Intercept-only model: emit explicit "1" (or "0" if intercept was removed).
		result.append(intercept ? "1" : "0");
		return result;
	}

	if (!intercept)
	{
		result.append("0 + ");
	}

	bool need_plus = false;

	// Walk the fixed-effects array, collapsing any pattern where a two-way
	// interaction {a, b} is accompanied by both singletons {a} and {b} into
	// "a * b". The * notation is only used when all three terms are present
	// so that we never silently introduce or drop main effects:
	//   a + b + a:b   →  a * b
	//   a + b         →  a + b      (no a:b, no collapse)
	//   a:b           →  a:b        (no main effects, no collapse)
	// Three-way and higher interactions stay as colon notation (collapsing
	// is conservative on purpose). The same logic is applied to random-slope
	// arrays in RandomTerm::to_string via the shared `append_term_array`
	// helper, so `a*b` round-trips identically in both contexts.
	append_term_array(result, fixed, need_plus);

	for (intptr_t i = 0; i < smooth.size(); i++)
	{
		if (need_plus) result.append(" + ");
		result.append(smooth[i].to_string());
		need_plus = true;
	}

	for (intptr_t i = 0; i < random.size(); i++)
	{
		if (need_plus) result.append(" + ");
		result.append(random[i].to_string());
		need_plus = true;
	}

	if (!offset.empty())
	{
		if (need_plus) result.append(" + ");
		result.append("offset(");
		result.append(quote_name(offset));
		result.append(")");
	}

	return result;
}

Array<String> Formula::all_variables() const
{
	Array<String> vars;

	// Helper to add if not already present.
	auto add_unique = [&](const String &v)
	{
		for (intptr_t i = 0; i < vars.size(); i++)
		{
			if (vars[i] == v) return;
		}
		vars.append(v);
	};

	add_unique(response);

	for (intptr_t i = 0; i < fixed.size(); i++)
	{
		auto &ft = fixed[i];
		for (intptr_t j = 0; j < ft.variables.size(); j++) {
			add_unique(ft.variables[j]);
		}
	}

	for (intptr_t i = 0; i < smooth.size(); i++)
	{
		add_unique(smooth[i].variable);
		if (!smooth[i].by.empty()) {
			add_unique(smooth[i].by);
		}
	}

	for (intptr_t i = 0; i < random.size(); i++)
	{
		auto &rt = random[i];
		if (rt.is_synthetic_group)
		{
			// Split the colon-joined name and add each component
			// (each is a real column in the data).
			intptr_t start = 0;
			intptr_t n = rt.group.size();
			const char *data = rt.group.data();
			for (intptr_t j = 0; j <= n; j++)
			{
				if (j == n || data[j] == ':')
				{
					if (j > start) {
						add_unique(String(data + start, j - start));
					}
					start = j + 1;
				}
			}
		}
		else
		{
			add_unique(rt.group);
		}
		for (intptr_t j = 0; j < rt.slopes.size(); j++) {
			auto &st = rt.slopes[j];
			for (intptr_t v = 0; v < st.variables.size(); v++) {
				add_unique(st.variables[v]);
			}
		}
	}

	if (!offset.empty()) {
		add_unique(offset);
	}

	return vars;
}


// =====================================================================
// Tokenizer (Unicode-aware via String::next_codepoint / is_letter)
// =====================================================================

namespace {

enum class TokenType
{
	Name,      // identifier (column name)
	Number,    // integer (0, 1 for intercept control; any integer for k= in smooth terms)
	Tilde,     // ~
	Plus,      // +
	Minus,     // -
	Star,      // *
	Slash,     // /  (only valid inside a random-effects term, between grouping factors)
	Colon,     // :
	Pipe,      // |
	LParen,    // (
	RParen,    // )
	Comma,     // ,
	Equals,    // =
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
		: m_input(input), m_pos(input.begin()), m_end(input.end())
	{
	}

	Token next()
	{
		skip_whitespace();

		if (m_pos >= m_end) {
			return { TokenType::End, "", 0 };
		}

		// Peek at the leading byte. All operator tokens and digits are ASCII,
		// so a single-byte check is safe: UTF-8 continuation bytes are always >= 0x80,
		// and multi-byte leading bytes are also >= 0x80.
		unsigned char lead = static_cast<unsigned char>(*m_pos);

		if (lead < 0x80)
		{
			char c = static_cast<char>(lead);

			switch (c)
			{
			case '~': m_pos++; return { TokenType::Tilde, "~", 0 };
			case '+': m_pos++; return { TokenType::Plus, "+", 0 };
			case '-': m_pos++; return { TokenType::Minus, "-", 0 };
			case '*': m_pos++; return { TokenType::Star, "*", 0 };
			case '/': m_pos++; return { TokenType::Slash, "/", 0 };
			case ':': m_pos++; return { TokenType::Colon, ":", 0 };
			case '|': m_pos++; return { TokenType::Pipe, "|", 0 };
			case '(': m_pos++; return { TokenType::LParen, "(", 0 };
			case ')': m_pos++; return { TokenType::RParen, ")", 0 };
			case ',': m_pos++; return { TokenType::Comma, ",", 0 };
			case '=': m_pos++; return { TokenType::Equals, "=", 0 };
			default:
				break;
			}

			// Quoted name: 'name with spaces' or "name with spaces"
			if (c == '\'' || c == '"')
			{
				char quote = c;
				m_pos++; // skip opening quote
				const char *start = m_pos;
				while (m_pos < m_end && *m_pos != quote) {
					m_pos++;
				}
				if (m_pos >= m_end) {
					throw error("Unterminated quoted name in formula");
				}
				String name(start, m_pos - start);
				m_pos++; // skip closing quote
				return { TokenType::Name, std::move(name), 0 };
			}

			// Integer: one or more ASCII digits.
			if (c >= '0' && c <= '9')
			{
				const char *start = m_pos;
				while (m_pos < m_end && *m_pos >= '0' && *m_pos <= '9') {
					m_pos++;
				}
				String num_str(start, m_pos - start);
				int value = std::atoi(std::string(start, m_pos - start).c_str());
				return { TokenType::Number, std::move(num_str), value };
			}

			// ASCII letter or underscore: start of a name.
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')
			{
				return scan_name();
			}

			// Unrecognized ASCII character.
			throw error("Unexpected character '%' in formula", String(m_pos, 1));
		}

		// Non-ASCII leading byte: must be the start of a Unicode letter for a name.
		// Decode the codepoint to check.
		auto saved = m_pos;
		char32_t cp = m_input.next_codepoint(m_pos);

		if (String::is_letter(cp))
		{
			// Put the iterator back and let scan_name() handle the whole name.
			m_pos = saved;
			return scan_name();
		}

		// Not a letter — error.
		m_pos = saved;
		String bad_char(cp, 1);
		throw error("Unexpected character '%' in formula", bad_char);
	}

private:

	// Scan a name token. The caller has verified that the current position
	// starts with a valid name-start character (Unicode letter, ASCII letter, or '_').
	// Name characters: Unicode letters, ASCII digits, '_', '.'
	Token scan_name()
	{
		const char *start = m_pos;

		while (m_pos < m_end)
		{
			auto before = m_pos;
			unsigned char lead = static_cast<unsigned char>(*m_pos);

			if (lead < 0x80)
			{
				// ASCII range: allow [a-zA-Z0-9_.]
				char c = static_cast<char>(lead);
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				    (c >= '0' && c <= '9') || c == '_' || c == '.')
				{
					m_pos++;
					continue;
				}
				break; // ASCII but not a name character
			}
			else
			{
				// Multi-byte: decode and check if it's a Unicode letter.
				char32_t cp = m_input.next_codepoint(m_pos);
				if (String::is_letter(cp)) {
					continue; // m_pos already advanced by next_codepoint
				}
				m_pos = before; // not a letter, put back
				break;
			}
		}

		return { TokenType::Name, String(start, m_pos - start), 0 };
	}

	void skip_whitespace()
	{
		while (m_pos < m_end)
		{
			unsigned char ch = static_cast<unsigned char>(*m_pos);
			if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
				m_pos++;
			} else {
				break;
			}
		}
	}

	const String &m_input;
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
		for (intptr_t i = 0; i < f.fixed.size(); i++)
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

	// Helper: given the first variable name (already consumed), continue
	// parsing a chain "a", "a:b:c", or "a*b*c" and expand * to the set of
	// all subsets (lme4 semantics on both fixed and random sides).
	// Returns one FixedTerm per resulting expanded term.
	Array<FixedTerm> expand_term_chain(const String &first)
	{
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

		Array<FixedTerm> result;

		if (vars.size() == 1)
		{
			result.append(FixedTerm(vars[0]));
		}
		else if (has_star)
		{
			// "a * b" expands to "a + b + a:b"
			// "a * b * c" expands to "a + b + c + a:b + a:c + b:c + a:b:c"
			intptr_t n = vars.size();
			intptr_t total = (1 << n); // 2^n subsets

			for (intptr_t mask = 1; mask < total; mask++)
			{
				FixedTerm t;
				for (intptr_t bit = 0; bit < n; bit++)
				{
					if (mask & (1 << bit)) {
						t.variables.append(vars[bit]);
					}
				}
				result.append(std::move(t));
			}
		}
		else
		{
			// Pure colon interaction: "a:b:c" — just the interaction, no main effects
			FixedTerm t;
			for (intptr_t i = 0; i < vars.size(); i++) {
				t.variables.append(vars[i]);
			}
			result.append(std::move(t));
		}

		return result;
	}

	// fixed_term := name (('*' | ':') name)*
	// Special case: if name is "s" and followed by '(', parse as smooth term.
	// "a * b" expands to: a, b, a:b
	// "a : b" is just the interaction: a:b
	void parse_fixed_term(Formula &f)
	{
		expect(TokenType::Name, "Expected variable name");
		String first = m_current.text;
		advance();

		// Detect smooth term: s(...)
		if (first == "s" && m_current.type == TokenType::LParen)
		{
			parse_smooth_term(f);
			return;
		}

		// Detect offset term: offset(column_name)
		if (first == "offset" && m_current.type == TokenType::LParen)
		{
			parse_offset_term(f);
			return;
		}

		auto terms = expand_term_chain(first);
		for (intptr_t i = 0; i < terms.size(); i++) {
			add_term(f, std::move(terms[i]));
		}
	}

	// smooth_term := 's' '(' name [',' option]* ')'
	// option := 'k' '=' number | 'by' '=' name | 'bs' '=' name
	//
	// Examples:
	//   s(duration)                → SmoothTerm{"duration", "", "cr", 10}
	//   s(duration, k=15)          → SmoothTerm{"duration", "", "cr", 15}
	//   s(duration, by=speaker)    → SmoothTerm{"duration", "speaker", "cr", 10}
	//   s(speaker, bs=re)          → SmoothTerm{"speaker", "", "re", 10}
	//   s(duration, by=speaker, k=15)
	//
	// Precondition: "s" has been consumed as a Name, current token is LParen.
	void parse_smooth_term(Formula &f)
	{
		expect(TokenType::LParen, "Expected '(' after 's'");
		advance();

		// Variable name
		expect(TokenType::Name, "Expected variable name inside s()");
		SmoothTerm st;
		st.variable = m_current.text;
		advance();

		// Optional arguments: , k=N , by=name , bs=type
		while (m_current.type == TokenType::Comma)
		{
			advance(); // skip comma

			expect(TokenType::Name, "Expected option name (e.g. 'k', 'by', 'bs') inside s()");
			String option = m_current.text;
			advance();

			expect(TokenType::Equals, "Expected '=' after option name inside s()");
			advance();

			if (option == "k")
			{
				expect(TokenType::Number, "Expected integer value for k= inside s()");
				st.k = m_current.value;
				if (st.k < 3) {
					throw error("Smooth term basis dimension k must be at least 3 (got %)", st.k);
				}
				advance();
			}
			else if (option == "by")
			{
				expect(TokenType::Name, "Expected variable name for by= inside s()");
				st.by = m_current.text;
				advance();
			}
			else if (option == "bs")
			{
				expect(TokenType::Name, "Expected basis type for bs= inside s()");
				st.basis = m_current.text;
				if (st.basis != "cr" && st.basis != "re") {
					throw error("Unknown basis type '%' (supported: cr, re)", st.basis);
				}
				advance();
			}
			else
			{
				throw error("Unknown smooth term option '%' (supported: k, by, bs)", option);
			}
		}

		// Closing paren
		expect(TokenType::RParen, "Expected ')' to close s()");
		advance();

		// Check for duplicate smooth on the same variable+by+basis combination.
		for (intptr_t i = 0; i < f.smooth.size(); i++)
		{
			if (f.smooth[i].variable == st.variable && f.smooth[i].by == st.by
			    && f.smooth[i].basis == st.basis)
			{
				throw error("Duplicate smooth term for variable '%'", st.variable);
			}
		}

		f.smooth.append(std::move(st));
	}

	// offset_term := 'offset' '(' name ')'
	//
	// Example:
	//   offset(log_duration)  → Formula::offset = "log_duration"
	//
	// Only one offset term is allowed per formula.
	// Precondition: "offset" has been consumed as a Name, current token is LParen.
	void parse_offset_term(Formula &f)
	{
		expect(TokenType::LParen, "Expected '(' after 'offset'");
		advance();

		expect(TokenType::Name, "Expected column name inside offset()");
		String col = m_current.text;
		advance();

		expect(TokenType::RParen, "Expected ')' to close offset()");
		advance();

		if (!f.offset.empty()) {
			throw error("Only one offset() term is allowed per formula");
		}

		f.offset = std::move(col);
	}

	// random_term := '(' random_inner '|' group_spec ')'
	// random_inner := elem ('+' elem)*
	// elem := '0' | '1' | term_chain
	// term_chain := name (('*' | ':') name)*
	// group_spec := name (('/' | ':') name)*
	//
	// '*' on the slope side expands at parse time exactly as on the fixed
	// side, matching lme4 semantics: (1 + a*b | g) → (1 + a + b + a:b | g).
	// Duplicate slope terms are silently dropped: (1 + a + a | g) → (1 + a | g).
	//
	// On the grouping side:
	//   '/' is slash sugar — (slopes | g1/g2) expands to two terms:
	//       (slopes | g1) + (slopes | g2:g1). Chains follow the lme4
	//       convention with the inner factor on the LEFT:
	//           g1/g2/g3 → g1, g2:g1, g3:g2:g1
	//       The same slope list (and intercept flag) is replicated
	//       across all expanded terms.
	//   ':' is a synthetic group — (slopes | g1:g2) is a single term whose
	//       grouping factor is the observed (g1, g2) pairs. is_synthetic_group
	//       is set to true on the resulting RandomTerm and `group` stores the
	//       colon-joined name *in source order*. (Bare colon respects the
	//       user's order; only slash sugar reverses to match lme4.)
	// '/' and ':' may not be mixed in the same group_spec.
	void parse_random_term(Formula &f)
	{
		expect(TokenType::LParen, "Expected '(' for random effects term");
		advance();

		RandomTerm rt;
		rt.intercept = true;

		// Helper to dedupe: append a slope only if not already present.
		auto add_slope_unique = [&rt](FixedTerm &&t)
		{
			for (intptr_t i = 0; i < rt.slopes.size(); i++) {
				if (rt.slopes[i] == t) return;
			}
			rt.slopes.append(std::move(t));
		};

		while (m_current.type != TokenType::Pipe)
		{
			if (m_current.type == TokenType::Number)
			{
				if (m_current.value == 0) {
					rt.intercept = false;
				}
				// "1" is a no-op (intercept is the default)
				advance();
			}
			else if (m_current.type == TokenType::Name)
			{
				String first = m_current.text;
				advance();

				auto terms = expand_term_chain(first);
				for (intptr_t i = 0; i < terms.size(); i++) {
					add_slope_unique(std::move(terms[i]));
				}
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

		// Grouping factor — first name is required.
		expect(TokenType::Name, "Expected grouping variable name after '|'");
		std::vector<String> group_chain;
		group_chain.push_back(m_current.text);
		advance();

		// Optional slash- or colon-separated continuation. The first separator
		// fixes the mode for the rest of this group_spec; mixing is an error.
		TokenType sep_mode = TokenType::End; // sentinel: not yet decided
		while (m_current.type == TokenType::Slash || m_current.type == TokenType::Colon)
		{
			if (sep_mode == TokenType::End) {
				sep_mode = m_current.type;
			} else if (m_current.type != sep_mode) {
				throw error("Cannot mix '/' and ':' in grouping factor specification — use one or the other");
			}
			advance();
			if (m_current.type != TokenType::Name) {
				throw error("Expected grouping variable name after '%' in random effects term",
				            (sep_mode == TokenType::Slash) ? "/" : ":");
			}
			group_chain.push_back(m_current.text);
			advance();
		}

		// Closing paren
		expect(TokenType::RParen, "Expected ')' to close random effects term");
		advance();

		// Emit one or more RandomTerms.
		auto join_with_colon = [](const std::vector<String> &parts, size_t up_to) -> String
		{
			String out = parts[0];
			for (size_t i = 1; i < up_to; i++) {
				out.append(":");
				out.append(parts[i]);
			}
			return out;
		};

		if (sep_mode == TokenType::Slash)
		{
			// lme4/glmmTMB convention: (slopes | g1/g2/g3) →
			//     (slopes | g1) + (slopes | g2:g1) + (slopes | g3:g2:g1)
			// The new factor at depth k is *prepended* to the previously-
			// accumulated synthetic name, not appended. Matching this
			// convention is what makes Phonometrica's ranef-table names
			// agree with glmmTMB's VarCorr() names verbatim — which the
			// validation suite relies on for lookup-by-name.
			for (size_t i = 0; i < group_chain.size(); i++)
			{
				RandomTerm copy;
				copy.intercept = rt.intercept;
				copy.slopes = rt.slopes;             // deep copy
				if (i == 0)
				{
					copy.group = group_chain[0];
					copy.is_synthetic_group = false;
				}
				else
				{
					// inner-most (group_chain[i]) goes on the left;
					// walk back through the chain prepending each ancestor.
					String joined = group_chain[i];
					for (size_t j = i; j > 0; j--) {
						joined.append(":");
						joined.append(group_chain[j - 1]);
					}
					copy.group = joined;
					copy.is_synthetic_group = true;
				}
				f.random.append(std::move(copy));
			}
		}
		else
		{
			// Either a bare name (group_chain.size()==1) or a single
			// colon-joined synthetic group (size() >= 2).
			rt.group = join_with_colon(group_chain, group_chain.size());
			rt.is_synthetic_group = (group_chain.size() > 1);
			f.random.append(std::move(rt));
		}
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
