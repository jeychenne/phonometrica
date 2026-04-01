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
 * Created: 25/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Read-only output panel for displaying measurement results (pitch, intensity, formants, etc.).              *
 *          Results are appended as timestamped blocks. The user can select, copy, and clear the output.               *
 *          Accessible as a singleton via OutputPanel::instance().                                                      *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_OUTPUT_PANEL_HPP
#define PHONOMETRICA_OUTPUT_PANEL_HPP

#include <QWidget>
#include <QPlainTextEdit>
#include <QToolBar>

namespace phonometrica {

class OutputPanel : public QWidget
{
	Q_OBJECT

public:

	explicit OutputPanel(QWidget *parent = nullptr);

	// Singleton accessor. Returns nullptr if no OutputPanel has been created yet.
	static OutputPanel *instance() { return s_instance; }

	// Append a result block to the output. The heading is displayed on a separator line,
	// and the body is indented below it.
	void appendResult(const QString &heading, const QString &body);

	// Append raw text (for scripting integration or freeform output).
	void appendText(const QString &text);

	// Clear all output.
	void clear();

private:

	void onCopyAll();
	void onSaveToFile();

	QPlainTextEdit *m_text;
	QToolBar *m_toolbar;

	static OutputPanel *s_instance;
};

} // namespace phonometrica

#endif // PHONOMETRICA_OUTPUT_PANEL_HPP
