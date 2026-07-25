// Phonometrica engine — tokens for the scripting language (design/design.md §12).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// A Token is the scanner's unit of output: a Lexeme (its syntactic category), the
// source text it spans, and the (line, column) where it starts. The parser (M3
// step 3) consumes a stream of these. Identifiers and keywords share the spelling
// machinery; keyword recognition happens at scan time (keyword_lexeme()), so the
// parser never string-compares against reserved words.

#ifndef PHON_COMPILE_TOKEN_HPP
#define PHON_COMPILE_TOKEN_HPP

#include <phon/engine/base/definitions.hpp>
#include <phon/engine/types/string.hpp>

namespace phonometrica {

// Syntactic categories. Ordering groups keywords, operators, punctuation, literals,
// interpolation markers, and structural tokens; the exact numeric values are not
// load-bearing (do not persist them).
enum class Lexeme : uint8_t
{
	Unknown = 0,

	// --- keywords (design §12) ---
	And, As, Break, Cast, Catch, Class, Const, Continue, Debug, Div, Do, Else,
	Elsif, End, False, Field, Finally, For, Function, Global, If, Import, In, Is,
	Local, Method, Mod, Not, Null, Open, Option, Or, Ref, Repeat, Return, Spawn,
	Step, Then, This, Throw, To, True, Try, Until, Var, While,

	// --- operators ---
	Plus, Minus, Star, Slash, Caret,        // + - * / ^
	Concat,                                 // &
	Assign,                                 // =
	PlusEq, MinusEq, StarEq, SlashEq, ConcatEq, // += -= *= /= &=
	Eq, NotEq, Less, LessEq, Greater, GreaterEq, // == != < <= > >=
	Arrow,                                  // ->  (lambda)

	// --- punctuation ---
	LParen, RParen, LBrace, RBrace, LSquare, RSquare,
	Comma, Colon, Dot, Semicolon,
	At,       // @  (array literal: @[...])
	Ellipsis, // ... (variadic parameter `T...` and call-site splat `xs...`)

	// --- literals ---
	Identifier, Integer, Float, String,

	// --- string-interpolation markers ---
	// "a{x}b{y}c"  ->  InterpStart("a") x InterpMid("b") y InterpEnd("c")
	// A single "{x}" segment is InterpStart("") x InterpEnd("").
	InterpStart, InterpMid, InterpEnd,

	// --- structural ---
	Newline, // statement separator (the continuation rule already applied)
	Eot      // end of text
};

// The first and last keyword lexemes, for range checks.
inline constexpr Lexeme FIRST_KEYWORD = Lexeme::And;
inline constexpr Lexeme LAST_KEYWORD = Lexeme::While;

inline bool is_keyword(Lexeme l) noexcept
{
	return l >= FIRST_KEYWORD && l <= LAST_KEYWORD;
}

struct Token final
{
	Token() = default;

	Token(Lexeme id, String spelling, intptr_t line, intptr_t column)
	    : spelling(std::move(spelling)), line(line), column(column), id(id)
	{
	}

	bool is(Lexeme l) const noexcept { return id == l; }
	bool is_eot() const noexcept { return id == Lexeme::Eot; }
	bool is_separator() const noexcept { return id == Lexeme::Newline || id == Lexeme::Semicolon; }

	// A human-readable rendering for diagnostics and golden dumps
	// (e.g. `keyword 'while'`, `'+'`, `identifier 'snd'`, `integer '42'`).
	String describe() const;

	// The decoded/raw text the token spans. For String and the Interp* markers
	// this is the *decoded* content (escapes applied); for numbers and
	// identifiers it is the source text; for fixed tokens it is the canonical
	// spelling ("+", "while", ...).
	String spelling;

	// 1-based line and 0-based byte column of the token's first character.
	intptr_t line = 0;
	intptr_t column = 0;

	Lexeme id = Lexeme::Unknown;
};

// The canonical spelling of a fixed-form lexeme (keyword or operator), e.g.
// Lexeme::While -> "while", Lexeme::PlusEq -> "+=". Returns a descriptive
// placeholder for variable-text lexemes (Identifier, Integer, ...).
const char *lexeme_name(Lexeme l) noexcept;

// If `text` is a reserved word, its keyword Lexeme; otherwise Lexeme::Identifier.
Lexeme keyword_lexeme(std::string_view text) noexcept;

} // namespace phonometrica

#endif // PHON_COMPILE_TOKEN_HPP
