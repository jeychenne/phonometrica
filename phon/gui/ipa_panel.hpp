/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
