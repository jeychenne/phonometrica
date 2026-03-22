/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
#include <QPlainTextEdit>

namespace phonometrica {

class Runtime;

class Console : public QPlainTextEdit
{
	Q_OBJECT

public:

	explicit Console(Runtime &rt, QWidget *parent = nullptr);

	// Append an error message (displayed in red).
	void showError(const QString &msg);

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

	static constexpr size_t HISTORY_LIMIT = 50;
};

} // namespace phonometrica

#endif // PHONOMETRICA_CONSOLE_HPP
