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
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QFont>
#include <phon/gui/phon_lexer.hpp>

namespace phonometrica {

PhonLexer::PhonLexer(QObject *parent) :
	QsciLexerPython(parent)
{
	// Colors matching the wx version exactly.
	setColor(QColor(153, 0, 76), Keyword);              // keywords: bold magenta
	setColor(QColor(51, 0, 102), HighlightedIdentifier); // type names: bold purple
	setColor(QColor(0, 102, 0), DoubleQuotedString);     // strings: green
	setColor(QColor(0, 102, 0), SingleQuotedString);
	setColor(QColor(0, 0, 102), Number);                 // numbers: blue
	setColor(QColor(96, 96, 96), Comment);               // comments: gray

	// Note: fonts are set by ScriptEditor::setupEditor() after the lexer is
	// attached, because the editor font must be applied to ALL styles and
	// the lexer's built-in per-style fonts would otherwise override it.
}

const char *PhonLexer::keywords(int set) const
{
	// Set 1 mirrors the engine's reserved words exactly. The authoritative list is the
	// Lexeme range FIRST_KEYWORD..LAST_KEYWORD in phon/engine/compile/token.hpp, spelled
	// by lexeme_name() in token.cpp; the scanner builds its keyword table from that same
	// range, so any word added there must be added here (and to the docs' Pygments lexer
	// in docs/phon_lexer/phon/lexer.py, which carries the same list).
	if (set == 1)
	{
		return "and as break cast catch class const continue div do else elsif end false field finally for foreach "
		       "function global if import in is local method mod not null open or ref repeat return spawn step then "
		       "this throw to true try until var while";
	}

	// Set 2 is HighlightedIdentifier: type names, i.e. anything usable after `is`/`as` or
	// in a type annotation, plus the `phon` global table. Builtin engine classes come from
	// runtime/bootstrap.cpp and the register_*_class hooks; the application classes from
	// application/project.cpp. Purely internal classes (NativeEnv, Upvalue) are omitted,
	// as is Counter, which only exists in the engine's REPL example.
	if (set == 2)
	{
		return "Array Boolean Class Error File Float Function Integer List Match Null Number Object Real Regex Set "
		       "String Symbol Table "
		       "Channel Thread "
		       "Analysis Annotation Bookmark Concordance Dataset DataTable Directory Document Element Note Query "
		       "Script Sound Spectrum TimeStamp "
		       "FormantQuery IntensityQuery PitchQuery SpectralMomentsQuery VoiceQualityQuery "
		       "Model PriorSpec "
		       "phon";
	}

	return nullptr;
}

} // namespace phonometrica
