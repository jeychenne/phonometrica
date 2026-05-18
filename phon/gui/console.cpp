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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QContextMenuEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMenu>
#include <QTextBlock>
#include <QScrollBar>
#include <QTabWidget>
#include <QApplication>
#include <phon/gui/font_helpers.hpp>
#include <phon/gui/console.hpp>
#include <phon/runtime.hpp>
#include <phon/runtime/file.hpp>

namespace phonometrica {

Console::Console(Runtime &rt, QWidget *parent) :
	QPlainTextEdit(parent), m_runtime(rt), m_prompt(">> ")
{
	setFrameShape(QFrame::NoFrame);
	setUndoRedoEnabled(false);
	setFont(defaultMonoFont());

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

		// Route runtime errors/warnings through showError so they render in red
		// and the console tab is auto-raised. Trailing newlines are stripped
		// because showError appends one itself; callers shouldn't have to know
		// about that detail.
		rt.show_error = [this](const String &s) {
			QString qs = QString::fromUtf8(s.data(), (int) s.size());
			while (qs.endsWith('\n')) qs.chop(1);
			showError(qs);
			m_text_written = true;
		};

		// Install the default clear-output behaviour. When the scripting
		// `clear()` global is invoked from the REPL (i.e. while this
		// callback is active on the runtime), it empties the console.
		// We deliberately do NOT add a prompt here: this callback is
		// only ever fired from inside Console::runCode(), which always
		// appends a prompt when execution returns. Adding one here
		// would leave two prompts stacked. ScriptView::execute() swaps
		// this out while a script is running so that `clear()` targets
		// the OutputPanel instead.
		rt.clear_output = [this] {
			clear();
		};
	}

	// Register the `clear` scripting global. It dispatches through
	// rt.clear_output so the currently active output surface is the one
	// that gets cleared. Note: this is the zero-argument overload of
	// `clear` — the one-argument overloads for List/Table/Array/Set are
	// registered separately in builtins.cpp.
	rt.add_global("clear", [](Runtime &rt, std::span<Variant>) -> Variant {
		if (rt.clear_output) rt.clear_output();
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

void Console::contextMenuEvent(QContextMenuEvent *e)
{
	auto *menu = createStandardContextMenu();
	menu->addSeparator();
	auto *clear_action = menu->addAction(tr("Clear"));
	connect(clear_action, &QAction::triggered, this, [this]() {
		clear();
		addPrompt();
	});
	menu->exec(e->globalPos());
	delete menu;
}


// ---------------------------------------------------------
//  Execution
// ---------------------------------------------------------

void Console::runCode(const QString &code)
{
	m_text_written = false;
	appendPlainText(QString()); // newline after the input

	// Show a busy cursor while the script runs. Script execution is synchronous
	// on the main thread (the GUI blocks until `do_string` returns), so this is
	// the main visual cue that something is happening — especially useful for
	// long operations such as fitting a model. RAII ensures the cursor is
	// restored even if an exception escapes the try blocks below.
	struct WaitCursorGuard
	{
		WaitCursorGuard()
		{
			QApplication::setOverrideCursor(Qt::WaitCursor);
			// Repaint pending events once so the cursor actually shows before
			// we enter the (blocking) runtime call.
			QApplication::processEvents();
		}
		~WaitCursorGuard() { QApplication::restoreOverrideCursor(); }
	} wait_guard;

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
		// If the exception carries a call-stack trace (RuntimeError and its
		// subclass ScriptException do; bare std::exception does not), render it
		// under the message. For console-typed code the trace is usually one
		// entry deep (just `<chunk>`), but anything calling helpers or imports
		// can produce a useful chain.
		if (auto re = dynamic_cast<RuntimeError*>(&e)) {
			showTrace(re->trace());
		}
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

void Console::showTrace(const std::vector<TraceEntry> &trace)
{
	if (trace.empty()) return;

	auto cursor = textCursor();
	cursor.movePosition(QTextCursor::End);

	// Muted red so the trace reads as subordinate to the error message above
	// without disappearing into the default text colour.
	QTextCharFormat traceFmt;
	traceFmt.setForeground(QColor(180, 100, 100));

	for (const auto &entry : trace)
	{
		// Display the basename rather than the full path: traces shown inline
		// in a fixed-width console get unreadable fast with absolute paths.
		// "<inline>" is the marker for chunks evaluated via do_string with no
		// associated file (typed in the console, or an unsaved editor buffer).
		QString file = entry.file.empty()
			? QStringLiteral("<inline>")
			: QFileInfo(QString::fromStdString(entry.file)).fileName();

		// `Routine::name()` returns "" for the top-level chunk; mirror what
		// `Function::name()` does for user-facing display and surface it as
		// "<chunk>". The runtime already substitutes this string when building
		// the trace, so the empty-check here is defence in depth.
		QString function = entry.routine.empty()
			? QStringLiteral("<chunk>")
			: QString::fromStdString(entry.routine);

		QString row = QStringLiteral("  at %1:%2 in %3")
		                  .arg(file)
		                  .arg(qlonglong(entry.line))
		                  .arg(function);

		cursor.insertText(row, traceFmt);
		// Newline in default format so a subsequent addPrompt() / appendOutput
		// inherits a clean cursor state. Mirrors the pattern in showError().
		cursor.insertText("\n", QTextCharFormat());
	}

	setTextCursor(cursor);
	goToEnd();
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
