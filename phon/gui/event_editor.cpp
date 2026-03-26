/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 25/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QKeyEvent>
#include <QShortcut>
#include <phon/gui/event_editor.hpp>

namespace phonometrica {

EventEditor::EventEditor(const QString &text, const QPoint &globalPos, QWidget *parent) :
	QDialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowStaysOnTopHint)
{
	setWindowTitle(tr("Edit event..."));
	resize(400, 120);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);

	m_edit = new QTextEdit(this);
	m_edit->setPlainText(text);
	m_edit->selectAll();

	// Use a larger font for comfortable editing.
	QFont font = m_edit->font();
	font.setPointSizeF(font.pointSizeF() * 1.3);
	m_edit->setFont(font);

	// Accept tabs as text (rather than focus navigation).
	m_edit->setTabChangesFocus(false);

	layout->addWidget(m_edit);

	// Position the dialog centered on the given global point.
	int x = globalPos.x() - width() / 2;
	int y = globalPos.y() - height() / 2;
	move(x, y);
	m_initial_pos = pos();

	m_edit->setFocus();

	// Intercept Enter/Return to accept the dialog.
	// We use an event filter because QTextEdit consumes Enter for newline
	// insertion before QShortcut gets a chance to fire.
	m_edit->installEventFilter(this);

	// Escape rejects.
	auto *rejectShortcut = new QShortcut(Qt::Key_Escape, this);
	connect(rejectShortcut, &QShortcut::activated, this, &QDialog::reject);
}

QString EventEditor::text() const
{
	return m_edit->toPlainText();
}

int EventEditor::yShift() const
{
	return pos().y() - m_initial_pos.y();
}

bool EventEditor::eventFilter(QObject *obj, QEvent *event)
{
	if (obj == m_edit && event->type() == QEvent::KeyPress)
	{
		auto *ke = static_cast<QKeyEvent *>(event);
		if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
			&& !(ke->modifiers() & Qt::ShiftModifier))
		{
			accept();
			return true;
		}
	}
	return QDialog::eventFilter(obj, event);
}

} // namespace phonometrica
