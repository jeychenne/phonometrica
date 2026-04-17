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
#include <Qsci/qsciapis.h>
#include <phon/gui/script_editor.hpp>
#include <phon/gui/phon_lexer.hpp>
#include <phon/gui/font_helpers.hpp>
#include <phon/include/autocompletion_list.hpp>
#include <phon/include/function_declarations.hpp>

namespace phonometrica {

ScriptEditor::ScriptEditor(QWidget *parent) :
	QsciScintilla(parent)
{
	setupEditor();
	setupApis();

	// Notify the view on text changes.
	connect(this, &QsciScintilla::textChanged, this, &ScriptEditor::contentModified);

	// Clear error indicators whenever the text is modified (keyboard, paste, undo, etc.).
	connect(this, &QsciScintilla::textChanged, this, &ScriptEditor::clearErrors);
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

void ScriptEditor::setupApis()
{
	auto *apis = new QsciAPIs(m_lexer);

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

	apis->prepare();

	// Autocompletion settings.
	setAutoCompletionSource(AcsAPIs);
	setAutoCompletionThreshold(2);           // show popup after 2 characters
	setAutoCompletionCaseSensitivity(false);
	setAutoCompletionReplaceWord(true);

	// Call tips.
	setCallTipsStyle(CallTipsNoContext);
	setCallTipsVisible(0);                   // show all matching tips
}


// ─────────────────────────────────────────────────
//  Error highlighting
// ─────────────────────────────────────────────────

void ScriptEditor::showError(int lineNumber)
{
	// lineNumber is 1-based from the runtime.
	int line = lineNumber - 1;

	// Use raw Scintilla messages — QScintilla wrapper signatures vary across versions.
	long start = SendScintilla(SCI_POSITIONFROMLINE, (unsigned long)line);
	long end = SendScintilla(SCI_GETLINEENDPOSITION, (unsigned long)line);
	long len = end - start;

	if (len > 0)
	{
		SendScintilla(SCI_SETINDICATORCURRENT, (unsigned long)ERROR_INDICATOR);
		SendScintilla(SCI_INDICATORFILLRANGE, (unsigned long)start, (unsigned long)len);
	}

	// Scroll to the error line.
	setCursorPosition(line, 0);
	ensureLineVisible(line);
}

void ScriptEditor::clearErrors()
{
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
	return QsciScintilla::event(e);
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

	bool needsBlock = false;
	bool needsExtraIndent = false;

	// Patterns that open a block (need "end" / "}" / "]" inserted).
	if ((trimmed.startsWith("if ") && trimmed.endsWith(" then")) ||
		trimmed.endsWith(" do") ||
		trimmed.startsWith("class ") || trimmed.startsWith("local class ") ||
		trimmed.startsWith("method ") ||
		trimmed.startsWith("function ") || trimmed.startsWith("local function "))
	{
		needsBlock = true;
		needsExtraIndent = true;
	}
	else if (trimmed == "else" || (trimmed.startsWith("elsif ") && trimmed.endsWith(" then")))
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
