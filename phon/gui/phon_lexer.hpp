/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
