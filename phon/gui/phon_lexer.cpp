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
	if (set == 1)
	{
		return "and as assert break class continue debug do downto else elsif end false field for foreach function "
		       "if in inherits let local method nan not null option or pass print ref repeat return step super then this throw "
		       "to true until while";
	}

	if (set == 2)
	{
		return "Array Boolean File Float Function Integer json List Module Number Object phon Regex Set String Table";
	}

	return nullptr;
}

} // namespace phonometrica
