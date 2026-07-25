// Phonometrica engine — token names and keyword recognition.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/compile/token.hpp>

#include <phon/engine/core/flat_hash_map.hpp>
#include <phon/engine/types/atom.hpp>

namespace phonometrica {

const char *lexeme_name(Lexeme l) noexcept
{
	switch (l)
	{
	// keywords
	case Lexeme::And: return "and";
	case Lexeme::As: return "as";
	case Lexeme::Break: return "break";
	case Lexeme::Cast: return "cast";
	case Lexeme::Catch: return "catch";
	case Lexeme::Class: return "class";
	case Lexeme::Const: return "const";
	case Lexeme::Continue: return "continue";
	case Lexeme::Debug: return "debug";
	case Lexeme::Div: return "div";
	case Lexeme::Do: return "do";
	case Lexeme::Else: return "else";
	case Lexeme::Elsif: return "elsif";
	case Lexeme::End: return "end";
	case Lexeme::False: return "false";
	case Lexeme::Field: return "field";
	case Lexeme::Finally: return "finally";
	case Lexeme::For: return "for";
	case Lexeme::Function: return "function";
	case Lexeme::Global: return "global";
	case Lexeme::If: return "if";
	case Lexeme::Import: return "import";
	case Lexeme::In: return "in";
	case Lexeme::Is: return "is";
	case Lexeme::Local: return "local";
	case Lexeme::Method: return "method";
	case Lexeme::Mod: return "mod";
	case Lexeme::Not: return "not";
	case Lexeme::Null: return "null";
	case Lexeme::Open: return "open";
	case Lexeme::Option: return "option";
	case Lexeme::Or: return "or";
	case Lexeme::Ref: return "ref";
	case Lexeme::Repeat: return "repeat";
	case Lexeme::Return: return "return";
	case Lexeme::Spawn: return "spawn";
	case Lexeme::Step: return "step";
	case Lexeme::Then: return "then";
	case Lexeme::This: return "this";
	case Lexeme::Throw: return "throw";
	case Lexeme::To: return "to";
	case Lexeme::True: return "true";
	case Lexeme::Try: return "try";
	case Lexeme::Until: return "until";
	case Lexeme::Var: return "var";
	case Lexeme::While: return "while";
	// operators
	case Lexeme::Plus: return "+";
	case Lexeme::Minus: return "-";
	case Lexeme::Star: return "*";
	case Lexeme::Slash: return "/";
	case Lexeme::Caret: return "^";
	case Lexeme::Concat: return "&";
	case Lexeme::Assign: return "=";
	case Lexeme::PlusEq: return "+=";
	case Lexeme::MinusEq: return "-=";
	case Lexeme::StarEq: return "*=";
	case Lexeme::SlashEq: return "/=";
	case Lexeme::ConcatEq: return "&=";
	case Lexeme::Eq: return "==";
	case Lexeme::NotEq: return "!=";
	case Lexeme::Less: return "<";
	case Lexeme::LessEq: return "<=";
	case Lexeme::Greater: return ">";
	case Lexeme::GreaterEq: return ">=";
	case Lexeme::Arrow: return "->";
	// punctuation
	case Lexeme::LParen: return "(";
	case Lexeme::RParen: return ")";
	case Lexeme::LBrace: return "{";
	case Lexeme::RBrace: return "}";
	case Lexeme::LSquare: return "[";
	case Lexeme::RSquare: return "]";
	case Lexeme::Comma: return ",";
	case Lexeme::Colon: return ":";
	case Lexeme::Dot: return ".";
	case Lexeme::Semicolon: return ";";
	case Lexeme::At: return "@";
	case Lexeme::Ellipsis: return "...";
	// variable-text and structural
	case Lexeme::Identifier: return "identifier";
	case Lexeme::Integer: return "integer";
	case Lexeme::Float: return "float";
	case Lexeme::String: return "string";
	case Lexeme::InterpStart: return "interp-start";
	case Lexeme::InterpMid: return "interp-mid";
	case Lexeme::InterpEnd: return "interp-end";
	case Lexeme::Newline: return "newline";
	case Lexeme::Eot: return "end of text";
	case Lexeme::Unknown: return "unknown";
	}
	return "unknown";
}

// Keyword table: interned-symbol id -> keyword Lexeme. Built once, lazily. Keyed
// on the Symbol id (a uint32_t) so the map uses the integral hasher; the scanner
// interns each identifier's bytes anyway, so recognition is a single lookup.
namespace {

struct KeywordTable
{
	FlatHashMap<uint32_t, Lexeme> map;

	KeywordTable()
	{
		for (int i = static_cast<int>(FIRST_KEYWORD); i <= static_cast<int>(LAST_KEYWORD); ++i)
		{
			auto lex = static_cast<Lexeme>(i);
			Symbol s = intern(lexeme_name(lex));
			map.insert(s.id, lex);
		}
	}
};

const KeywordTable &keyword_table()
{
	static const KeywordTable table;
	return table;
}

} // namespace

Lexeme keyword_lexeme(std::string_view text) noexcept
{
	Symbol s = intern(text);
	const auto &map = keyword_table().map;
	auto it = map.find(s.id);
	return it != map.end() ? it->second : Lexeme::Identifier;
}

String Token::describe() const
{
	switch (id)
	{
	case Lexeme::Identifier:
		return String("identifier '") + spelling.view() + "'";
	case Lexeme::Integer:
		return String("integer '") + spelling.view() + "'";
	case Lexeme::Float:
		return String("float '") + spelling.view() + "'";
	case Lexeme::String:
	case Lexeme::InterpStart:
	case Lexeme::InterpMid:
	case Lexeme::InterpEnd:
		return String("string");
	case Lexeme::Newline:
		return String("end of line");
	case Lexeme::Eot:
		return String("end of text");
	default:
		break;
	}

	if (is_keyword(id))
		return String("keyword '") + lexeme_name(id) + "'";

	return String("'") + lexeme_name(id) + "'";
}

} // namespace phonometrica
