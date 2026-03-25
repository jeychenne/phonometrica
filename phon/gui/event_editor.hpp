/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 25/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Modal dialog for editing the text of an annotation event. Features a resizable text field with a larger    *
 *          font, a title bar with close button, and the ability to be moved vertically. The vertical shift is          *
 *          remembered so that subsequent edits on the same layer open at the same position.                            *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_EVENT_EDITOR_HPP
#define PHONOMETRICA_EVENT_EDITOR_HPP

#include <QDialog>
#include <QTextEdit>
#include <QPoint>

namespace phonometrica {

class EventEditor : public QDialog
{
	Q_OBJECT

public:

	// Creates the editor with the given text, positioned globally at globalPos.
	EventEditor(const QString &text, const QPoint &globalPos, QWidget *parent = nullptr);

	// Returns the (possibly modified) text.
	QString text() const;

	// Returns the vertical displacement applied by the user moving the dialog.
	// 0 if the dialog was not moved.
	int yShift() const;

private:

	QTextEdit *m_edit;
	QPoint m_initial_pos;
};

} // namespace phonometrica

#endif // PHONOMETRICA_EVENT_EDITOR_HPP
