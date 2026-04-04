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
 * Purpose: Abstract base class for all content views (scripts, sounds, annotations, concordances, etc.).              *
 *          Each view is a QWidget that provides a common interface for tab management.                                 *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_VIEW_HPP
#define PHONOMETRICA_VIEW_HPP

#include <QWidget>
#include <phon/string.hpp>
#include <phon/gui/command.hpp>

namespace phonometrica {

class Document;

class View : public QWidget
{
	Q_OBJECT

public:

	explicit View(QWidget *parent = nullptr) : QWidget(parent) {}

	virtual ~View() = default;

	// Label shown in the tab (e.g. "untitled.phon *").
	virtual QString label() const = 0;

	// File path associated with this view (empty if unsaved/untitled).
	virtual String path() const { return {}; }

	// The Document backing this view, or nullptr if not applicable.
	// Used for pointer-based identity when the document has no disk path
	// (e.g. in-memory subsets created by filter() or set operations).
	virtual Document* document() const { return nullptr; }

	// Whether the content has unsaved changes.
	virtual bool isModified() const { return false; }

	// Save the content. Returns true on success.
	virtual bool save() { return true; }

	// Discard unsaved changes without saving.
	virtual void discardChanges() {}

	// Execute the content (meaningful for scripts).
	virtual void execute() {}

	// Dismiss transient UI elements (e.g. close a search bar).
	virtual void escape() {}

	// Show the find bar.
	virtual void find() {}

	// Show the find & replace bar.
	virtual void replace() {}

	// Whether this view supports find (and find & replace).
	virtual bool supportsFind() const { return false; }

	// Undo/redo support. The default implementation uses the command processor.
	// ScriptView overrides these to delegate to QPlainTextEdit's built-in undo.
	virtual void undo();
	virtual void redo();

	// Submit a command: execute it and push onto the undo stack.
	bool submit(AutoCommand cmd);

	// Record a command that has already been executed externally.
	// Pushes it onto the undo stack without calling execute().
	void record(AutoCommand cmd);

	bool canUndo() const { return m_commands.can_undo(); }
	bool canRedo() const { return m_commands.can_redo(); }

	// Help anchor for context-sensitive help.
	// Returns a Sphinx page name relative to the doc root, without extension.
	// Examples: "sound", "scripting/index", "intro/install".
	// An empty string means the view has no dedicated help page.
	virtual QString helpAnchor() const { return {}; }

signals:

	// Emitted when the display label changes (e.g. after save or modification).
	void titleChanged(const QString &title);

	// Emitted when the modification state changes.
	void modificationChanged(bool modified);

	// Emitted when this view registers a new file with the project.
	void addedToProject();

	// Emitted when undo/redo availability changes.
	void undoRedoChanged(bool canUndo, bool canRedo);

protected:

	// Strip the file extension from a document label for tab display.
	// E.g. "my_query.phon-conc" → "my_query".
	static QString tabLabel(const QString &name)
	{
		int dot = name.lastIndexOf(QLatin1Char('.'));
		return (dot > 0) ? name.left(dot) : name;
	}

	CommandProcessor m_commands;
};

} // namespace phonometrica

#endif // PHONOMETRICA_VIEW_HPP
