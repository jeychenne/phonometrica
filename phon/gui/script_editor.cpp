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

#include <QKeyEvent>
#include <QApplication>
#include <QTimer>
#include <QMenu>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QToolTip>
#include <QHelpEvent>
#include <QCursor>
#include <Qsci/qsciapis.h>
#include <phon/gui/script_editor.hpp>
#include <phon/gui/phon_lexer.hpp>
#include <phon/gui/font_helpers.hpp>
#include <phon/gui/script_index.hpp>
#include <phon/include/autocompletion_list.hpp>
#include <phon/include/function_declarations.hpp>

namespace phonometrica {

ScriptEditor::ScriptEditor(QWidget *parent) :
	QsciScintilla(parent)
{
	setupEditor();
	setupApis();

	// Debounce timer driving the single background parse that feeds both
	// user-symbol autocompletion and live syntax-error squiggles. Restarted
	// on every text change, so the parse runs only once the typing settles.
	m_parse_timer = new QTimer(this);
	m_parse_timer->setSingleShot(true);
	m_parse_timer->setInterval(PARSE_DEBOUNCE_MS);
	connect(m_parse_timer, &QTimer::timeout, this, &ScriptEditor::runBackgroundParse);

	// Notify the view on text changes.
	connect(this, &QsciScintilla::textChanged, this, &ScriptEditor::contentModified);

	// Clear error indicators whenever the text is modified (keyboard, paste, undo, etc.).
	// The next debounced parse will repaint them if the error is still present.
	connect(this, &QsciScintilla::textChanged, this, &ScriptEditor::clearErrors);

	// Restart the debounce on every text change.
	connect(this, &QsciScintilla::textChanged, this,
	        [this]() { m_parse_timer->start(); });
}

void ScriptEditor::setRuntime(Runtime *rt)
{
	m_runtime = rt;
	// Trigger one immediate parse so symbols declared in a freshly-loaded
	// script are available without waiting for the first edit, and any
	// pre-existing syntax error is surfaced right away.
	if (m_runtime) {
		runBackgroundParse();
	}
}


// ─────────────────────────────────────────────────
//  Editor setup
// ─────────────────────────────────────────────────

void ScriptEditor::setupEditor()
{
	// Lexer (must be set before configuring styles).
	m_lexer = new PhonLexer(this);

	// Set the editor font on the lexer so all styles inherit it.
	auto editorFont = defaultMonoFont();

	m_lexer->setDefaultFont(editorFont);
	setLexer(m_lexer);

	// QsciLexerPython defines per-style fonts that override setDefaultFont().
	// We must explicitly set our font on every style so the editor looks uniform.
	for (int style = 0; style < 128; style++)
		m_lexer->setFont(editorFont, style);

	// Now make keywords and type names bold.
	auto boldFont = editorFont;
	boldFont.setBold(true);
	m_lexer->setFont(boldFont, QsciLexerPython::Keyword);
	m_lexer->setFont(boldFont, QsciLexerPython::HighlightedIdentifier);

	// Tabs.
	setTabWidth(TAB_WIDTH);
	setIndentationsUseTabs(true);

	// We handle indentation ourselves in keyPressEvent.
	setAutoIndent(false);

	// Line numbers.
	setMarginType(0, NumberMargin);
	setMarginWidth(0, QStringLiteral("00000"));
#if defined(Q_OS_WIN)
	// On Windows, QPalette::AlternateBase can resolve to black (or very
	// nearly so) under the native Windows style, which turns the gutter
	// into a solid dark strip and makes the line numbers invisible against
	// the dark-grey QPalette::PlaceholderText foreground. Use explicit
	// light-theme colours so the gutter is always readable.
	setMarginsBackgroundColor(QColor(0xF4, 0xF4, 0xF4));
	setMarginsForegroundColor(QColor(0x80, 0x80, 0x80));
#else
	setMarginsBackgroundColor(QApplication::palette().color(QPalette::AlternateBase));
	setMarginsForegroundColor(QApplication::palette().color(QPalette::PlaceholderText));
#endif
	setMarginsFont(editorFont);

	// Hide the default symbol margin (margin 1).
	setMarginWidth(1, 0);

	// Error indicator (squiggly red underline, same as the wx version).
	SendScintilla(SCI_INDICSETSTYLE, (unsigned long)ERROR_INDICATOR, (unsigned long)INDIC_SQUIGGLE);
	SendScintilla(SCI_INDICSETFORE, (unsigned long)ERROR_INDICATOR, QColor(Qt::red));

	// Caret and selection.
	setCaretLineVisible(true);
#if defined(Q_OS_WIN)
	// See comment on margin colours above — same palette-role problem here.
	auto caretBg = QColor(0xF4, 0xF4, 0xF4);
#else
	auto caretBg = QApplication::palette().color(QPalette::AlternateBase);
#endif
	setCaretLineBackgroundColor(caretBg);

	// Brace matching.
	setBraceMatching(SloppyBraceMatch);
}

// Populate the QsciAPIs table with the built-in identifiers. Used both at
// initial setup and on every debounced rebuild — m_apis is cleared first by
// the caller in the rebuild path.
static void populate_builtins(QsciAPIs *apis)
{
	// Add all autocompletion words.
	QStringList words = QString(autocompletion_list).split(' ', Qt::SkipEmptyParts);
	for (const auto &w : words)
		apis->add(w);

	// Add function signatures (these also provide call tips).
	// Entries like "clear(ref table as Table)" serve as both a completion for "clear"
	// and a call tip shown when the user types "clear(".
	for (auto &def : function_declarations)
	{
		for (auto &sig : def.second)
		{
			// The wx version stored entries like:
			//   "clear(ref table as Table)\nRemoves all the elements in the table.\002"
			// We strip the description after \n and the navigation chars \001 \002.
			auto clean = sig;
			int nl = clean.indexOf('\n');
			if (nl >= 0)
				clean = clean.left(nl);
			clean.remove(QChar('\001'));
			clean.remove(QChar('\002'));

			if (!clean.isEmpty())
				apis->add(clean);
		}
	}
}

void ScriptEditor::setupApis()
{
	m_apis = new QsciAPIs(m_lexer);
	populate_builtins(m_apis);
	m_apis->prepare();

	// Autocompletion settings.
	setAutoCompletionSource(AcsAPIs);
	setAutoCompletionThreshold(2);           // show popup after 2 characters
	setAutoCompletionCaseSensitivity(false);
	setAutoCompletionReplaceWord(true);

	// Call tips.
	setCallTipsStyle(CallTipsNoContext);
	setCallTipsVisible(0);                   // show all matching tips
}

void ScriptEditor::runBackgroundParse()
{
	// Cheap no-op when no runtime is available yet (the editor can be
	// instantiated before ScriptView wires the runtime in).
	if (!m_runtime || !m_apis) return;

	auto utf8 = text().toUtf8();
	String source(utf8.constData(), intptr_t(utf8.size()));
	m_last_index = index_script(*m_runtime, source);

	// (1) Rebuild the autocompletion table. On parse failure the symbol list
	// is empty, so user-declared names temporarily disappear from the popup
	// until the script parses again — preferable to showing stale names from
	// a previous successful parse.
	m_apis->clear();
	populate_builtins(m_apis);
	for (auto &name : m_last_index.distinct_names()) {
		QString qname = QString::fromUtf8(name.data(), int(name.size()));
		if (!qname.isEmpty())
			m_apis->add(qname);
	}
	// Emit one entry per user-declared routine carrying the formatted parameter
	// list. QsciAPIs treats entries of the form "name(args)" as both completion
	// candidates *and* call tips — the same shape built-ins use (see
	// populate_builtins above). Each RoutineDefinition contributes its own entry,
	// so overloads (multiple dispatch) naturally stack in the call tip the way
	// they do for overloaded built-ins.
	for (auto &sym : m_last_index.symbols()) {
		if (sym.signature.empty()) continue;
		QString qsig = QString::fromUtf8(sym.signature.data(), int(sym.signature.size()));
		if (!qsig.isEmpty())
			m_apis->add(qsig);
	}
	m_apis->prepare();

	// (2) Paint or clear the live-error squiggle. The toggle gates only the
	// painting; the parse runs either way (autocompletion needs it).
	if (m_error_checking_active && m_last_index.has_error && m_last_index.error_line > 0) {
		// Cache the error range and message so the hover tooltip in
		// event(QHelpEvent::ToolTip) can recognise positions inside it
		// without re-parsing.
		m_error_line    = m_last_index.error_line;
		m_error_column  = m_last_index.error_column;
		m_error_length  = m_last_index.error_length;
		m_error_message = QString::fromUtf8(m_last_index.error_message.data(),
		                                    int(m_last_index.error_message.size()));

		if (m_error_column >= 0 && m_error_length > 0) {
			showError(m_error_line, m_error_column, m_error_length);
		} else {
			// Parse-time error with no precise column (defensive fallback):
			// highlight the whole line. Use paintLineError, NOT showError(int),
			// because this path fires on every keystroke-after-pause during
			// live editing — transient syntax errors are normal while typing,
			// and the user must keep their caret where it is.
			paintLineError(m_error_line);
		}
	} else {
		clearErrors();
	}
}


// ─────────────────────────────────────────────────
//  Error highlighting
// ─────────────────────────────────────────────────

void ScriptEditor::paintLineError(int lineNumber)
{
	// Pure paint: indicator only, no caret movement, no ensureLineVisible.
	// lineNumber is 1-based to match the showError() public API; convert here
	// so callers can keep using the parser's 1-based line numbers directly.
	int line = lineNumber - 1;

	// Use raw Scintilla messages — QScintilla wrapper signatures vary across versions.
	long start = SendScintilla(SCI_POSITIONFROMLINE, (unsigned long)line);
	long end = SendScintilla(SCI_GETLINEENDPOSITION, (unsigned long)line);
	long len = end - start;

	if (len > 0)
	{
		// Wipe any previous indicator paint without touching the cached
		// error state — that's runBackgroundParse's responsibility, and it
		// has typically just populated it for us.
		long total = SendScintilla(SCI_GETTEXTLENGTH);
		SendScintilla(SCI_SETINDICATORCURRENT, (unsigned long)ERROR_INDICATOR);
		if (total > 0) {
			SendScintilla(SCI_INDICATORCLEARRANGE, (unsigned long)0, (unsigned long)total);
		}
		SendScintilla(SCI_INDICATORFILLRANGE, (unsigned long)start, (unsigned long)len);
	}
}

void ScriptEditor::showError(int lineNumber)
{
	// Paint then jump-to-error. This overload is the "explicit user action"
	// path — it's called from ScriptView::execute() after a script run has
	// failed, where taking the user to the offending line is exactly the
	// helpful behaviour. The live-parse pipeline must NOT use this overload;
	// it calls paintLineError directly to avoid stealing the caret while the
	// user is typing.
	paintLineError(lineNumber);

	int line = lineNumber - 1;
	setCursorPosition(line, 0);
	ensureLineVisible(line);
}

void ScriptEditor::showError(int lineNumber, int column, int length)
{
	// (line, column) come from the parser: line is 1-based, column is a
	// 0-based byte offset from the start of the line. Scintilla addresses
	// positions in document bytes, which is what we have.
	if (lineNumber <= 0 || column < 0 || length <= 0) {
		// Defensive fallback — should not happen given the caller's checks.
		// Use paintLineError, NOT showError(int): this three-arg overload
		// is exclusively the live-parse path (see comment near the end of
		// this function), and live parse must never steal the caret.
		paintLineError(lineNumber);
		return;
	}
	int line = lineNumber - 1;

	long line_start = SendScintilla(SCI_POSITIONFROMLINE, (unsigned long)line);
	long line_end = SendScintilla(SCI_GETLINEENDPOSITION, (unsigned long)line);
	long start = line_start + column;
	long end = start + length;
	if (start > line_end) start = line_end;
	if (end   > line_end) end   = line_end;
	long len = end - start;

	// Wipe any previous indicator paint without touching the cached error
	// state — that's runBackgroundParse's responsibility.
	long total = SendScintilla(SCI_GETTEXTLENGTH);
	SendScintilla(SCI_SETINDICATORCURRENT, (unsigned long)ERROR_INDICATOR);
	if (total > 0) {
		SendScintilla(SCI_INDICATORCLEARRANGE, (unsigned long)0, (unsigned long)total);
	}

	// If the resulting range is empty (e.g. the error position is past the
	// end of the line's actual content, or the line is blank), the narrow
	// indicator wouldn't paint anything visible. Fall back to a whole-line
	// indicator so the user still sees that *something* is wrong — better an
	// imprecise underline than a silent failure.
	if (len <= 0) {
		long whole_len = line_end - line_start;
		if (whole_len > 0) {
			SendScintilla(SCI_INDICATORFILLRANGE, (unsigned long)line_start, (unsigned long)whole_len);
		}
		// Note: unlike the whole-line variant we still do NOT scroll the
		// view here. See comment below.
		return;
	}

	SendScintilla(SCI_INDICATORFILLRANGE, (unsigned long)start, (unsigned long)len);
	// Note: unlike the whole-line variant we do NOT scroll the view here.
	// This overload is called from the debounced background parse on every
	// keystroke-after-pause; auto-scrolling would steal the caret from the
	// user mid-edit. The whole-line variant is still called from execute()
	// where scrolling-to-error is the desired behaviour.
}

void ScriptEditor::clearErrors()
{
	// Wipe the cached error state so the hover tooltip stops claiming the
	// (now stale) error range. The next background parse will repopulate
	// these if the error is still present after the user's edit.
	m_error_line = 0;
	m_error_column = -1;
	m_error_length = 0;
	m_error_message.clear();

	long total = SendScintilla(SCI_GETTEXTLENGTH);
	if (total > 0)
	{
		SendScintilla(SCI_SETINDICATORCURRENT, (unsigned long)ERROR_INDICATOR);
		SendScintilla(SCI_INDICATORCLEARRANGE, (unsigned long)0, (unsigned long)total);
	}
}


// ─────────────────────────────────────────────────
//  Selection helpers
// ─────────────────────────────────────────────────

std::pair<int, int> ScriptEditor::selectedLines() const
{
	int lineFrom, indexFrom, lineTo, indexTo;
	getSelection(&lineFrom, &indexFrom, &lineTo, &indexTo);

	if (lineFrom < 0)
	{
		int line, index;
		getCursorPosition(&line, &index);
		return {line, line};
	}

	return {lineFrom, lineTo};
}

void ScriptEditor::addStartCharacter(const QString &s)
{
	auto [first, last] = selectedLines();

	beginUndoAction();
	for (int i = first; i <= last; i++)
		insertAt(s, i, 0);
	endUndoAction();
}

void ScriptEditor::removeStartCharacter(const QString &s)
{
	auto [first, last] = selectedLines();

	beginUndoAction();
	for (int i = first; i <= last; i++)
	{
		auto line = text(i);
		if (line.startsWith(s))
		{
			setSelection(i, 0, i, s.length());
			replaceSelectedText(QString());
		}
	}
	endUndoAction();
}


// ─────────────────────────────────────────────────
//  Hints toggle
// ─────────────────────────────────────────────────

void ScriptEditor::activateHints(bool value)
{
	m_hints_active = value;

	if (m_hints_active)
	{
		setAutoCompletionSource(AcsAPIs);
		setCallTipsStyle(CallTipsNoContext);
	}
	else
	{
		setAutoCompletionSource(AcsNone);
		setCallTipsStyle(CallTipsNone);
		SendScintilla(SCI_AUTOCCANCEL);
		SendScintilla(SCI_CALLTIPCANCEL);
	}
}

void ScriptEditor::activateErrorChecking(bool value)
{
	m_error_checking_active = value;

	if (!m_error_checking_active) {
		// Wipe any squiggle currently on screen. The next background parse
		// will keep refreshing the index but won't repaint the indicator
		// until the user turns checking back on.
		clearErrors();
	}
	else {
		// Trigger an immediate parse so the squiggle reappears right away if
		// the script is still broken — otherwise the user would wait for the
		// next debounced timer fire after the next keystroke.
		runBackgroundParse();
	}
}


// ─────────────────────────────────────────────────
//  Key handling & smart auto-indentation
// ─────────────────────────────────────────────────

bool ScriptEditor::event(QEvent *e)
{
	// QScintilla accepts ShortcutOverride for many key combos, preventing
	// Qt's shortcut system from dispatching them to MainWindow actions.
	// We explicitly ignore the combos we handle at application level.
	if (e->type() == QEvent::ShortcutOverride)
	{
		auto *ke = static_cast<QKeyEvent *>(e);
		if (ke->matches(QKeySequence::Find) ||
			(ke->modifiers() == Qt::ControlModifier && ke->key() == Qt::Key_H))
		{
			e->ignore();
			return false;
		}
	}

	// Hover tooltip for live-error squiggles: when the user lets the mouse
	// rest over text, Qt fires a QHelpEvent::ToolTip and we get to decide
	// whether (and what) to show. We show the cached parse-error message
	// only when the mouse is inside the squiggle range — outside it we
	// suppress the tooltip so QScintilla's defaults still apply.
	if (e->type() == QEvent::ToolTip && m_error_line > 0 && !m_error_message.isEmpty())
	{
		auto *he = static_cast<QHelpEvent *>(e);
		QPoint pt = he->pos();
		long doc_pos = SendScintilla(SCI_POSITIONFROMPOINT,
		                             (unsigned long)pt.x(),
		                             (unsigned long)pt.y());
		if (doc_pos >= 0)
		{
			long mouse_line = SendScintilla(SCI_LINEFROMPOSITION, (unsigned long)doc_pos);
			int  err_line   = m_error_line - 1;  // -> 0-based

			bool over_error = false;
			if ((int)mouse_line == err_line)
			{
				long line_start = SendScintilla(SCI_POSITIONFROMLINE, (unsigned long)err_line);
				long line_end   = SendScintilla(SCI_GETLINEENDPOSITION, (unsigned long)err_line);
				long range_start, range_end;
				if (m_error_column >= 0 && m_error_length > 0)
				{
					range_start = line_start + m_error_column;
					range_end   = range_start + m_error_length;
					if (range_end > line_end) range_end = line_end;
				}
				else
				{
					// Whole-line fallback: any hover on the line counts.
					range_start = line_start;
					range_end   = line_end;
				}
				over_error = (doc_pos >= range_start && doc_pos < range_end);
			}

			if (over_error)
			{
				QToolTip::showText(he->globalPos(), m_error_message, this);
				return true;
			}
		}
		QToolTip::hideText();
		// Fall through to let QScintilla provide its own tooltips if any.
	}

	return QsciScintilla::event(e);
}

void ScriptEditor::mousePressEvent(QMouseEvent *e)
{
	// Ctrl+left-click on a USER-DEFINED identifier jumps to its definition.
	// We intentionally do NOT also move the caret to the click position
	// first: the act of Ctrl-clicking is unambiguously "I want to go there",
	// and a brief caret stop at the click site would be jarring.
	//
	// For built-in names (or anything else the script index doesn't know
	// about) we fall through to the default click handler — there is
	// nothing in the user's script to navigate to, and silently leaving the
	// click acting as a normal click is preferable to popping a "Definition
	// not found" tooltip on every Ctrl+click of `print`, `assert`, etc.
	if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier))
	{
		QString word = wordAtPoint(e->pos());
		if (hasUserDefinition(word))
		{
			goToDefinition(word);
			e->accept();
			return;
		}
	}
	QsciScintilla::mousePressEvent(e);
}

void ScriptEditor::contextMenuEvent(QContextMenuEvent *e)
{
	// Build on top of QScintilla's standard context menu (Cut, Copy, Paste,
	// Select All, etc.) rather than replacing it — users expect those to
	// remain available.
	QMenu *menu = createStandardContextMenu();
	if (!menu) {
		// Defensive: some QScintilla versions can return null in unusual
		// states; fall back to the parent implementation.
		QsciScintilla::contextMenuEvent(e);
		return;
	}

	QString word = wordAtPoint(e->pos());

	// Mouse-modifier hint sits inline as a parenthetical rather than using
	// the `\t`-as-right-aligned-shortcut trick: that idiom is Qt's rendering
	// of QAction::shortcut(), and Ctrl+Click is not a QKeySequence-shaped
	// keyboard shortcut. The parenthetical form is portable across styles
	// and doesn't masquerade as a real keyboard binding.
	auto *goto_action = new QAction(tr("Go to definition (Ctrl+Click)"), menu);
	// Disable rather than hide when there's nothing to navigate to — keeps
	// the feature discoverable. We additionally gate on
	// hasUserDefinition() so the action is greyed out for built-ins and
	// undeclared names; goToDefinition can only navigate to symbols in the
	// user's script, and exposing the entry only when it would succeed
	// avoids the misleading "Definition not found" toast.
	goto_action->setEnabled(hasUserDefinition(word));
	connect(goto_action, &QAction::triggered, this, [this, word]() {
		goToDefinition(word);
	});

	// Insert at the top so it's the most-prominent action.
	auto existing = menu->actions();
	QAction *anchor = existing.isEmpty() ? nullptr : existing.first();
	menu->insertAction(anchor, goto_action);
	menu->insertSeparator(anchor);

	menu->exec(e->globalPos());
	delete menu;
}

bool ScriptEditor::hasUserDefinition(const QString &word) const
{
	if (word.isEmpty()) return false;
	QByteArray utf8 = word.toUtf8();
	String name(utf8.constData(), intptr_t(utf8.size()));
	return m_last_index.find(name) != nullptr;
}

void ScriptEditor::goToDefinition(const QString &word)
{
	if (word.isEmpty()) return;

	QByteArray utf8 = word.toUtf8();
	String name(utf8.constData(), intptr_t(utf8.size()));
	const ScriptSymbol *sym = m_last_index.find(name);
	if (!sym)
	{
		// Entry points (Ctrl+click, context menu) already gate on
		// hasUserDefinition, so we expect to reach this branch only when
		// the index changed between the check and the navigation — e.g.
		// the user deleted the declaration in the milliseconds between
		// opening the context menu and selecting the item. The toast is
		// the safety net for that race; in steady state it should not be
		// reachable.
		QToolTip::showText(QCursor::pos(), tr("Definition not found"), this);
		return;
	}

	int target_line = sym->line - 1;        // -> 0-based for Scintilla
	long line_start = SendScintilla(SCI_POSITIONFROMLINE, (unsigned long)target_line);
	long target_pos = line_start + sym->column;

	SendScintilla(SCI_GOTOPOS, (unsigned long)target_pos);
	ensureLineVisible(target_line);
	setFocus();
}

void ScriptEditor::keyPressEvent(QKeyEvent *e)
{
	if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
	{
		// If the autocompletion popup is visible, let Scintilla accept the selection.
		if (isListActive())
		{
			QsciScintilla::keyPressEvent(e);
			return;
		}

		// Get the current line BEFORE inserting the newline.
		int line, index;
		getCursorPosition(&line, &index);

		// Insert the newline.
		QsciScintilla::keyPressEvent(e);

		// Smart indent based on the previous line.
		handleSmartIndent();
		return;
	}

	QsciScintilla::keyPressEvent(e);
}

void ScriptEditor::handleSmartIndent()
{
	int curLine, curIndex;
	getCursorPosition(&curLine, &curIndex);

	if (curLine == 0)
		return;

	// Get the previous line's text.
	auto prevText = text(curLine - 1);
	// Remove trailing newline if present.
	if (prevText.endsWith('\n'))
		prevText.chop(1);
	if (prevText.endsWith('\r'))
		prevText.chop(1);

	// Count leading tabs.
	int indent = 0;
	for (auto c : prevText)
	{
		if (c == '\t')
			indent++;
		else
			break;
	}

	auto trimmed = prevText.trimmed();

	// Declaration modifiers may be combined, in the order 'local'/'global', 'open', 'ref'
	// (see Parser::parse_modified_declaration). Strip them so the block test below only
	// has to look at the head keyword, instead of enumerating every combination.
	auto head = trimmed;
	for (auto mod : {QStringLiteral("local "), QStringLiteral("global "),
	                 QStringLiteral("open "), QStringLiteral("ref ")})
	{
		if (head.startsWith(mod))
			head = head.mid(mod.size()).trimmed();
	}

	bool needsBlock = false;
	bool needsExtraIndent = false;

	// Patterns that open a block (need "end" / "until" / "}" / "]" inserted).
	if ((head.startsWith("if ") && head.endsWith(" then")) ||
		head.endsWith(" do") ||
		head == "try" || head == "repeat" ||
		head.startsWith("class ") ||
		head.startsWith("method ") ||
		head.startsWith("function "))
	{
		needsBlock = true;
		needsExtraIndent = true;
	}
	// Clauses that continue an open block: they indent, but the block's terminator has
	// already been inserted by the line that opened it.
	else if (head == "else" || (head.startsWith("elsif ") && head.endsWith(" then")) ||
		head == "catch" || head.startsWith("catch ") || head == "finally")
	{
		needsExtraIndent = true;
	}
	else if (trimmed.endsWith("{"))
	{
		needsBlock = true;
		needsExtraIndent = true;
	}
	else if (trimmed.endsWith("[") || trimmed.endsWith("@["))
	{
		needsBlock = true;
		needsExtraIndent = true;
	}

	beginUndoAction();

	// Insert base indentation (+ extra indent if needed).
	QString tabs;
	for (int i = 0; i < indent; i++)
		tabs += '\t';
	if (needsExtraIndent)
		tabs += '\t';

	insert(tabs);
	setCursorPosition(curLine, tabs.length());

	if (needsBlock)
	{
		// Determine the closing construct.
		QString closing;
		if (trimmed.endsWith("{"))
			closing = "}";
		else if (trimmed.endsWith("[") || trimmed.endsWith("@["))
			closing = "]";
		else if (head == "repeat")
			// 'repeat' is closed by 'until <condition>', so this one is a scaffold: the
			// trailing space is where the user types the condition.
			closing = "until ";
		else
			closing = "end";

		// Build the closing line.
		QString closingLine = "\n";
		for (int i = 0; i < indent; i++)
			closingLine += '\t';
		closingLine += closing;

		// Insert after the current cursor position.
		int posAfterTabs = tabs.length();
		insertAt(closingLine, curLine, posAfterTabs);

		// Keep cursor on the current line at the indentation point.
		setCursorPosition(curLine, posAfterTabs);
	}

	endUndoAction();
}

} // namespace phonometrica
