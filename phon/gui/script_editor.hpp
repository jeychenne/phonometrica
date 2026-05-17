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
 * Purpose: Script editing widget based on QScintilla. This is a direct port of the wxWidgets ScriptControl, which     *
 * was itself based on wxStyledTextCtrl (Scintilla). Since QScintilla wraps the same Scintilla library, the mapping    *
 * is nearly 1:1: auto-completion, call tips, indicators and line numbering are provided by Scintilla natively.        *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SCRIPT_EDITOR_HPP
#define PHONOMETRICA_SCRIPT_EDITOR_HPP

#include <Qsci/qsciscintilla.h>
#include <phon/runtime/compiler/script_index.hpp>

class QTimer;
class QsciAPIs;
class QMouseEvent;
class QContextMenuEvent;

namespace phonometrica {

class PhonLexer;
class Runtime;

class ScriptEditor : public QsciScintilla
{
	Q_OBJECT

public:

	explicit ScriptEditor(QWidget *parent = nullptr);

	// Set the runtime used for background parses that feed the symbol index.
	// Must be called once after construction; until it is, user-symbol
	// autocompletion is disabled (the built-in completion list still works).
	void setRuntime(Runtime *rt);

	// Highlight a line that contains an error (1-based line number from the runtime).
	void showError(int lineNumber);

	// Paint a narrow squiggle indicator at (line, column) spanning `length`
	// bytes. Used by the live-error pipeline so the user sees a precise
	// underline on the offending token rather than the entire line.
	void showError(int line, int column, int length);

	// Clear all error highlights.
	void clearErrors();

	// Get the pair of (first, last) selected line numbers (0-based).
	std::pair<int,int> selectedLines() const;

	// Insert text at the beginning of each selected line.
	void addStartCharacter(const QString &s);

	// Remove text from the beginning of each selected line.
	void removeStartCharacter(const QString &s);

	// Enable or disable auto-completion and call tips.
	void activateHints(bool value);

	bool hintsActive() const { return m_hints_active; }

	// Enable or disable live syntax-error squiggles. Background parsing for
	// autocompletion is unaffected by this toggle — only the painting of
	// squiggles. Defaults to enabled; users who find live underlines
	// distracting can switch them off from the toolbar.
	void activateErrorChecking(bool value);

	bool errorCheckingActive() const { return m_error_checking_active; }

	// Jump the caret to the first declaration of `word` in the most recent
	// script index. If `word` is empty or no declaration is recorded, shows
	// a brief "Definition not found" tooltip near the cursor and does
	// nothing. Used by the Ctrl+click handler and the context-menu action.
	void goToDefinition(const QString &word);

signals:

	void contentModified();

protected:

	bool event(QEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

private slots:

	// Re-parse the current text and (a) rebuild autocompletion from the
	// resulting user symbols, (b) paint or clear the live syntax-error
	// squiggle. Single parse drives both. Triggered on a debounced timer.
	void runBackgroundParse();

private:

	void setupEditor();
	void setupApis();
	void handleSmartIndent();

	// Paint the whole-line error indicator without moving the caret or
	// scrolling. Shared between the public showError(int) overload (which
	// adds the caret jump on top, for execute()-style flows where taking
	// the user to the error site is desired) and runBackgroundParse() (which
	// must NOT steal focus from the user — that fires on every
	// keystroke-after-pause and a transient parse error during typing is
	// normal). `lineNumber` is 1-based, matching the public showError API.
	void paintLineError(int lineNumber);

	// True iff `word` names a symbol declared in the user's script (as
	// opposed to a built-in name, an unknown name, or empty). Backs the
	// Ctrl+click and "Go to definition" context-menu entry-point checks:
	// goToDefinition itself can only navigate to user symbols, so the entry
	// points should not even offer the action for anything else. Consults
	// the most recent successful background-parse index. Const + lightweight
	// (one hash lookup) so it is safe to call from event handlers and from
	// the context-menu builder.
	bool hasUserDefinition(const QString &word) const;

	PhonLexer *m_lexer = nullptr;

	// QsciAPIs is owned by the lexer (parent pointer); we keep a non-owning
	// handle so we can clear and repopulate it on each background parse.
	QsciAPIs *m_apis = nullptr;

	// Runtime used by the background indexer. Borrowed; never owned.
	Runtime *m_runtime = nullptr;

	// Debounce timer that drives runBackgroundParse. Single-shot, restarted
	// on every textChanged signal.
	QTimer *m_parse_timer = nullptr;

	// The latest successfully-built symbol index. Used by goToDefinition for
	// click-on-symbol navigation and by rebuildApis for completion. Kept as
	// a member rather than a stack local so click handlers can query it
	// without re-parsing.
	ScriptIndex m_last_index;

	// Last live-parse error state. Populated by runBackgroundParse when a
	// parse fails; cleared by clearErrors. The hover tooltip consults these
	// to decide whether (and what) to show when the mouse dwells inside the
	// squiggle range.
	int     m_error_line = 0;        // 1-based; 0 means "no error"
	int     m_error_column = -1;     // 0-based byte column
	int     m_error_length = 0;
	QString m_error_message;

	bool m_hints_active = false;
	bool m_error_checking_active = true;

	static constexpr int ERROR_INDICATOR = 8;
	static constexpr int TAB_WIDTH = 4;
	static constexpr int PARSE_DEBOUNCE_MS = 1000;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SCRIPT_EDITOR_HPP
