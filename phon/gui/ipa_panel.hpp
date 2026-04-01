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
 * Created: 27/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Clickable IPA chart panel. Organized in sub-tabs (Consonants, Vowels, Other, Diacritics, Supra).          *
 *          Clicking a symbol inserts it at the cursor in the currently focused text widget without stealing focus.     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_IPA_PANEL_HPP
#define PHONOMETRICA_IPA_PANEL_HPP

#include <QWidget>
#include <QPointer>

namespace phonometrica {

class IpaPanel : public QWidget
{
	Q_OBJECT

public:

	explicit IpaPanel(QWidget *parent = nullptr);

	// Create a small, non-focusable button for a single IPA symbol.
	class QPushButton *makeButton(const QString &symbol, const QString &tooltip);

	// Overload: display text differs from inserted text (used for combining diacritics).
	class QPushButton *makeButton(const QString &display, const QString &insert, const QString &tooltip);

private:

	// Insert text into the last focused text widget and restore focus to it.
	void insertSymbol(const QString &symbol);

	// Returns true if w is a supported text editing widget.
	static bool isEditor(QWidget *w);

	// Helpers to build each sub-tab.
	QWidget *createConsonantsTab();
	QWidget *createVowelsTab();
	QWidget *createOtherTab();
	QWidget *createDiacriticsTab();
	QWidget *createSupraTab();

	// The last text editing widget that had focus (tracked via focusChanged).
	// QPointer auto-nullifies if the widget is destroyed.
	QPointer<QWidget> m_target;
};

} // namespace phonometrica

#endif // PHONOMETRICA_IPA_PANEL_HPP
