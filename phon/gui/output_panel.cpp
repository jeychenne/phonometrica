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
 * Created: 25/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QApplication>
#include <QClipboard>
#include <QTabWidget>
#include <phon/gui/file_dialog.hpp>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <phon/gui/output_panel.hpp>
#include <phon/gui/font_helpers.hpp>

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

	auto *save_action = m_toolbar->addAction(QIcon(":/icons/save.svg"), tr("Save to file..."));
	connect(save_action, &QAction::triggered, this, &OutputPanel::onSaveToFile);

	auto *clear_action = m_toolbar->addAction(QIcon(":/icons/eraser.svg"), tr("Clear"));
	connect(clear_action, &QAction::triggered, this, &OutputPanel::clear);

	layout->addWidget(m_toolbar);

	// Read-only text area.
	m_text = new QPlainTextEdit(this);
	m_text->setReadOnly(true);
	m_text->setUndoRedoEnabled(false);

	// Use a monospace font for aligned columns.
	m_text->setFont(defaultMonoFont());

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

void OutputPanel::clear()
{
	m_text->clear();
}

void OutputPanel::onCopyAll()
{
	auto *clipboard = QApplication::clipboard();
	clipboard->setText(m_text->toPlainText());
}

void OutputPanel::onSaveToFile()
{
	auto path = getSaveFileName(this, tr("Save output to file"),
		tr("Text files (*.txt);;All files (*)"), QStringLiteral("output.txt"));

	if (path.isEmpty())
		return;

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
	{
		QMessageBox::warning(this, tr("Error"),
			tr("Could not write to file: %1").arg(file.errorString()));
		return;
	}

	QTextStream out(&file);
	out << m_text->toPlainText();
}

} // namespace phonometrica
