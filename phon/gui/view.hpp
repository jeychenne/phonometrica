/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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

	// Undo/redo support. The default implementation uses the command processor.
	// ScriptView overrides these to delegate to QPlainTextEdit's built-in undo.
	virtual void undo();
	virtual void redo();

	// Submit a command: execute it and push onto the undo stack.
	bool submit(AutoCommand cmd);

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

	CommandProcessor m_commands;
};

} // namespace phonometrica

#endif // PHONOMETRICA_VIEW_HPP
