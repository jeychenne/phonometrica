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
 * Purpose: Script editing widget based on QScintilla. This is a direct port of the wxWidgets ScriptControl, which     *
 * was itself based on wxStyledTextCtrl (Scintilla). Since QScintilla wraps the same Scintilla library, the mapping    *
 * is nearly 1:1: auto-completion, call tips, indicators and line numbering are provided by Scintilla natively.        *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SCRIPT_EDITOR_HPP
#define PHONOMETRICA_SCRIPT_EDITOR_HPP

#include <Qsci/qsciscintilla.h>

namespace phonometrica {

class PhonLexer;

class ScriptEditor : public QsciScintilla
{
	Q_OBJECT

public:

	explicit ScriptEditor(QWidget *parent = nullptr);

	// Highlight a line that contains an error (1-based line number from the runtime).
	void showError(int lineNumber);

	// Clear all error highlights.
	void clearErrors();

	// Get the pair of (first, last) selected line numbers (0-based).
	std::pair<int,int> selectedLines() const;

	// Insert text at the beginning of each selected line.
	void addStartCharacter(const QString &s);

	// Remove text from the beginning of each selected line.
	void removeStartCharacter(const QString &s);

	// Enable or disable auto-completion and call tips.
	void activateHints(bool value);

	bool hintsActive() const { return m_hints_active; }

signals:

	void contentModified();

protected:

	void keyPressEvent(QKeyEvent *e) override;

private:

	void setupEditor();
	void setupApis();
	void handleSmartIndent();

	PhonLexer *m_lexer = nullptr;

	bool m_hints_active = false;

	static constexpr int ERROR_INDICATOR = 8;
	static constexpr int TAB_WIDTH = 4;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SCRIPT_EDITOR_HPP
