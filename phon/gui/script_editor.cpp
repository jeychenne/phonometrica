/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QKeyEvent>
#include <Qsci/qsciapis.h>
#include <phon/gui/script_editor.hpp>
#include <phon/gui/phon_lexer.hpp>
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
}


// ─────────────────────────────────────────────────
//  Editor setup
// ─────────────────────────────────────────────────

void ScriptEditor::setupEditor()
{
	// Lexer (must be set before configuring styles).
	m_lexer = new PhonLexer(this);

	// Set the editor font on the lexer so all styles inherit it.
#if PHON_MACOS
	auto editorFont = QFont("Monaco", 13);
#elif PHON_WINDOWS
	auto editorFont = QFont("Consolas", 10);
#else
	auto editorFont = QFont("Monospace", 12);
#endif
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
	setMarginsBackgroundColor(QColor(220, 220, 220));
	setMarginsForegroundColor(QColor(75, 75, 75));
	setMarginsFont(editorFont);

	// Hide the default symbol margin (margin 1).
	setMarginWidth(1, 0);

	// Error indicator (squiggly red underline, same as the wx version).
	SendScintilla(SCI_INDICSETSTYLE, (unsigned long)ERROR_INDICATOR, (unsigned long)INDIC_SQUIGGLE);
	SendScintilla(SCI_INDICSETFORE, (unsigned long)ERROR_INDICATOR, QColor(Qt::red));

	// Caret and selection.
	setCaretLineVisible(true);
	setCaretLineBackgroundColor(QColor(245, 245, 245));

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

	// Clear error indicators on any edit.
	if (!e->text().isEmpty() && e->text().at(0).isPrint())
		clearErrors();

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
