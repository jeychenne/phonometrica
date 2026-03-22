/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
