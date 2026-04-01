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
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QKeyEvent>
#include <QTextBlock>
#include <QScrollBar>
#include <QTabWidget>
#include <phon/string.hpp>
#include <phon/gui/console.hpp>
#include <phon/runtime.hpp>
#include <phon/runtime/file.hpp>

namespace phonometrica {

Console::Console(Runtime &rt, QWidget *parent) :
	QPlainTextEdit(parent), m_runtime(rt), m_prompt(">> ")
{
	setFrameShape(QFrame::NoFrame);
	setUndoRedoEnabled(false);

	// Monospace font.
#if PHON_MACOS
	setFont(QFont("Monaco", 13));
#elif PHON_WINDOWS
	setFont(QFont("Consolas", 10));
#else
	setFont(QFont("Monospace", 12));
#endif

	// Make the console accessible from the runtime (used by ScriptView::execute).
	rt.console = this;

	// Redirect runtime output to the console.
	if (!rt.is_text_mode())
	{
		rt.print = [this](const String &s) {
			auto qs = QString::fromUtf8(s.data(), (int) s.size());
			appendOutput(qs);
			m_text_written = true;
		};
	}

	// Register a "reset" function to clear the console.
	rt.add_global("reset", [this](Runtime &, std::span<Variant>) -> Variant {
		clear();
		return Variant();
	}, {});

	addPrompt();
}

void Console::keyPressEvent(QKeyEvent *e)
{
	int key = e->key();
	int start = inputStart();
	int cursorPos = textCursor().position();

	// --- History navigation ---

	if (key == Qt::Key_Up)
	{
		if (m_history_pos > 0)
		{
			--m_history_pos;
			if (m_history_pos < m_history.size())
				replaceCurrentLine(m_history[m_history_pos]);
		}
		return;
	}

	if (key == Qt::Key_Down)
	{
		if (!m_history.empty())
		{
			if (m_history_pos < m_history.size())
				++m_history_pos;

			if (m_history_pos < m_history.size())
				replaceCurrentLine(m_history[m_history_pos]);
			else
				replaceCurrentLine(QString());
		}
		return;
	}

	// --- Prevent editing before the prompt ---

	if (key == Qt::Key_Backspace || key == Qt::Key_Left)
	{
		if (cursorPos <= start)
			return;
	}

	if (key == Qt::Key_Home)
	{
		// Home goes to the start of user input, not the start of the line.
		auto cursor = textCursor();
		cursor.setPosition(start, e->modifiers() & Qt::ShiftModifier
			? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
		setTextCursor(cursor);
		return;
	}

	// --- Execute on Enter ---

	if (key == Qt::Key_Return || key == Qt::Key_Enter)
	{
		// Move cursor to end first so we always execute from the last line.
		goToEnd();

		auto line = currentLine().trimmed();

		if (line.isEmpty())
		{
			addPrompt();
			return;
		}

		// Add to history.
		m_history.push_back(line);
		while (m_history.size() > HISTORY_LIMIT)
			m_history.pop_front();
		m_history_pos = m_history.size();

		auto bytes = line.toUtf8();
		auto code = String(bytes.constData(), bytes.size());
		runCode(line);

		return;
	}

	// --- Block input before the prompt ---

	if (cursorPos < start && !e->text().isEmpty())
	{
		goToEnd();
	}

	QPlainTextEdit::keyPressEvent(e);
}


// ---------------------------------------------------------
//  Execution
// ---------------------------------------------------------

void Console::runCode(const QString &code)
{
	m_text_written = false;
	appendPlainText(QString()); // newline after the input

	try
	{
		auto result = m_runtime.do_string(code);

		if (!result.is_null())
		{
			auto s = result.to_string(true);
			appendOutput(s);
		}
	}
	catch (std::exception &e)
	{
		showError(e.what());
	}

	addPrompt();
	emit codeExecuted();
}

void Console::runScript(const QString &path)
{
	auto bytes = path.toUtf8();
	auto content = File::read_all(String(bytes.constData(), bytes.size()));
	auto qcontent = QString::fromUtf8(content.data(), (int) content.size());
	runCode(qcontent);
}


// ---------------------------------------------------------
//  Output
// ---------------------------------------------------------

void Console::showError(const QString &msg)
{
	auto cursor = textCursor();
	cursor.movePosition(QTextCursor::End);

	QTextCharFormat errorFmt;
	errorFmt.setForeground(QColor(220, 50, 50));
	cursor.insertText(msg, errorFmt);

	// Reset to default format.
	cursor.insertText("\n", QTextCharFormat());

	setTextCursor(cursor);
	goToEnd();

	// Auto-switch to the Console tab so the user doesn't miss the error.
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

void Console::addPrompt()
{
	// If the last line isn't empty, add a newline first.
	auto cursor = textCursor();
	cursor.movePosition(QTextCursor::End);
	QString lastLine = cursor.block().text();

	if (!lastLine.isEmpty() && document()->characterCount() > 1)
		cursor.insertText("\n");

	cursor.insertText(m_prompt);
	setTextCursor(cursor);
	goToEnd();
}

void Console::appendOutput(const QString &text)
{
	auto cursor = textCursor();
	cursor.movePosition(QTextCursor::End);
	cursor.insertText(text, QTextCharFormat());
	setTextCursor(cursor);
	goToEnd();
}

void Console::appendNewLine()
{
	auto cursor = textCursor();
	cursor.movePosition(QTextCursor::End);
	cursor.insertText("\n", QTextCharFormat());
	setTextCursor(cursor);
}

void Console::scrollToEnd()
{
	goToEnd();
}


// ---------------------------------------------------------
//  Line manipulation helpers
// ---------------------------------------------------------

QString Console::currentLine() const
{
	// Get the text of the last block and strip the prompt prefix.
	auto cursor = textCursor();
	cursor.movePosition(QTextCursor::End);
	QString line = cursor.block().text();

	if (line.startsWith(m_prompt))
		return line.mid(m_prompt.size());

	return line;
}

void Console::replaceCurrentLine(const QString &text)
{
	auto cursor = textCursor();
	cursor.movePosition(QTextCursor::End);
	cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::MoveAnchor);
	cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
	cursor.insertText(m_prompt + text);
	setTextCursor(cursor);
	goToEnd();
}

void Console::goToEnd()
{
	auto cursor = textCursor();
	cursor.movePosition(QTextCursor::End);
	setTextCursor(cursor);
	auto *sb = verticalScrollBar();
	sb->setValue(sb->maximum());
}

int Console::inputStart() const
{
	// The position right after the prompt on the last block.
	auto block = document()->lastBlock();
	return block.position() + m_prompt.size();
}

} // namespace phonometrica
