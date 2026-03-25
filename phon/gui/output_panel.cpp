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
#include <QApplication>
#include <QClipboard>
#include <QTabWidget>
#include <phon/gui/output_panel.hpp>

namespace phonometrica {

OutputPanel *OutputPanel::s_instance = nullptr;

OutputPanel::OutputPanel(QWidget *parent) :
	QWidget(parent)
{
	s_instance = this;

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	// Toolbar with Clear and Copy All buttons.
	m_toolbar = new QToolBar(this);
	m_toolbar->setIconSize(QSize(16, 16));
	m_toolbar->setMovable(false);

	auto *copy_action = m_toolbar->addAction(QIcon(":/icons/clipboard-copy.svg"), tr("Copy all"));
	connect(copy_action, &QAction::triggered, this, &OutputPanel::onCopyAll);

	auto *clear_action = m_toolbar->addAction(QIcon(":/icons/eraser.svg"), tr("Clear"));
	connect(clear_action, &QAction::triggered, this, &OutputPanel::clear);

	layout->addWidget(m_toolbar);

	// Read-only text area.
	m_text = new QPlainTextEdit(this);
	m_text->setReadOnly(true);
	m_text->setUndoRedoEnabled(false);

	// Use a monospace font for aligned columns.
	QFont font("monospace");
	font.setStyleHint(QFont::Monospace);
	font.setPointSize(QApplication::font().pointSize());
	m_text->setFont(font);

	layout->addWidget(m_text);
}

void OutputPanel::appendResult(const QString &heading, const QString &body)
{
	// If the output isn't empty, add a blank line before the new block.
	if (!m_text->document()->isEmpty())
		m_text->appendPlainText(QString());

	QString separator = QStringLiteral("── ") + heading + QStringLiteral(" ──");
	m_text->appendPlainText(separator);
	m_text->appendPlainText(body);

	// Scroll to the bottom.
	auto cursor = m_text->textCursor();
	cursor.movePosition(QTextCursor::End);
	m_text->setTextCursor(cursor);
	m_text->ensureCursorVisible();

	// Auto-switch to the Output tab.
	QWidget *ancestor = parentWidget();
	while (ancestor)
	{
		auto *tabs = qobject_cast<QTabWidget *>(ancestor);
		if (tabs)
		{
			tabs->setCurrentWidget(this);
			break;
		}
		ancestor = ancestor->parentWidget();
	}
}

void OutputPanel::appendText(const QString &text)
{
	m_text->appendPlainText(text);

	auto cursor = m_text->textCursor();
	cursor.movePosition(QTextCursor::End);
	m_text->setTextCursor(cursor);
	m_text->ensureCursorVisible();
}

void OutputPanel::clear()
{
	m_text->clear();
}

void OutputPanel::onCopyAll()
{
	auto *clipboard = QApplication::clipboard();
	clipboard->setText(m_text->toPlainText());
}

} // namespace phonometrica
