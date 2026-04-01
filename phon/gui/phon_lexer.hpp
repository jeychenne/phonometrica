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
 * Purpose: Scintilla lexer for the Phonometrica scripting language. We reuse the Python lexer (which uses '#' for     *
 * comments) and override the keyword sets and style colors.                                                           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PHON_LEXER_HPP
#define PHONOMETRICA_PHON_LEXER_HPP

#include <Qsci/qscilexerpython.h>

namespace phonometrica {

class PhonLexer final : public QsciLexerPython
{
	Q_OBJECT

public:

	explicit PhonLexer(QObject *parent = nullptr);

	const char *keywords(int set) const override;
};

} // namespace phonometrica

#endif // PHONOMETRICA_PHON_LEXER_HPP
