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
 * Purpose: interactive scripting console (REPL). The console redirects Runtime::print to display output inline.       *
 * The user types code after a ">> " prompt. Enter executes, Up/Down navigate history.                                 *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CONSOLE_HPP
#define PHONOMETRICA_CONSOLE_HPP

#include <deque>
#include <vector>
#include <QPlainTextEdit>
#include <phon/runtime/error.hpp>

namespace phonometrica {

class Runtime;

class Console : public QPlainTextEdit
{
	Q_OBJECT

public:

	explicit Console(Runtime &rt, QWidget *parent = nullptr);

	// Append an error message (displayed in red).
	void showError(const QString &msg);

	// Append a call-stack trace under an error message. Each entry is rendered
	// on its own indented line in muted red as "  at <file>:<line> in <func>".
	// Entries are displayed innermost-first (the throw site is the topmost
	// line of the trace block); empty traces produce no output. Intended to be
	// called between `showError(e.what())` and `addPrompt()`.
	void showTrace(const std::vector<TraceEntry> &trace);

	// Display a new prompt at the current position.
	void addPrompt();

	// Append a newline (used before script output so it doesn't land on the prompt line).
	void appendNewLine();

	// Scroll to the end of the console.
	void scrollToEnd();

	// Run a script file in the console.
	void runScript(const QString &path);

	// Run a code string in the console.
	void runCode(const QString &code);

signals:

	// Emitted after code is executed (so the UI can refresh if needed).
	void codeExecuted();

protected:

	void keyPressEvent(QKeyEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

private:

	// Extract the text after the prompt on the current (last) line.
	QString currentLine() const;

	// Replace the text after the prompt on the last line.
	void replaceCurrentLine(const QString &text);

	// Move the cursor to the end of the document.
	void goToEnd();

	// Returns the character position where user input starts on the last line.
	int inputStart() const;

	// Append text at the end (used by the print callback).
	void appendOutput(const QString &text);

	Runtime &m_runtime;

	std::deque<QString> m_history;
	size_t m_history_pos = 0;

	QString m_prompt;

	// Track whether the runtime printed something during execution.
	bool m_text_written = false;

	// True while runCode() is executing; the runtime print/show_error callbacks
	// use this to decide whether they need to add a fresh prompt after writing.
	// When invoked from the REPL flow, runCode itself appends the trailing prompt
	// and we must not duplicate it; outside the REPL (e.g. queries dispatched
	// from a dialog), there's no caller to add the prompt, so the callbacks do it.
	bool m_in_runcode = false;

	static constexpr size_t HISTORY_LIMIT = 50;
};

} // namespace phonometrica

#endif // PHONOMETRICA_CONSOLE_HPP
