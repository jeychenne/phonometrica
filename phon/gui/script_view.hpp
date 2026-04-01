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
 * Purpose: View for scripts. This is a tab widget that contains a toolbar, the script editor and a find/replace bar.  *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SCRIPT_VIEW_HPP
#define PHONOMETRICA_SCRIPT_VIEW_HPP

#include <QToolBar>
#include <QAction>
#include <phon/runtime.hpp>
#include <phon/gui/view.hpp>
#include <phon/application/script.hpp>

namespace phonometrica {

class ScriptEditor;
class SearchBar;
class Console;

class ScriptView : public View
{
	Q_OBJECT

public:

	ScriptView(Runtime &rt, Console *console, const Handle<Script> &script, QWidget *parent = nullptr);

	// ── View interface ─────────────────────────────────

	QString label() const override;
	String path() const override;
	bool isModified() const override;
	bool save() override;
	void discardChanges() override;
	void execute() override;
	void find() override;
	void replace() override;
	void escape() override;
	void undo() override;
	void redo() override;

	QString helpAnchor() const override { return QStringLiteral("scripting/index"); }

	// ── Script-specific ────────────────────────────────

	Handle<Script> script() const { return m_script; }

private slots:

	void onModification();
	void onCommentSelection();
	void onUncommentSelection();
	void onIndentSelection();
	void onUnindentSelection();
	void onViewBytecode();
	void onToggleHints(bool checked);
	void onFind();
	void onReplace();
	void onReplaceAll();

private:

	void setupUi();

	Runtime &m_runtime;
	Console *m_console;
	Handle<Script> m_script;

	ScriptEditor *m_editor = nullptr;
	SearchBar *m_searchbar = nullptr;
	QToolBar *m_toolbar = nullptr;

	QAction *m_save_action = nullptr;
	QAction *m_hint_action = nullptr;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SCRIPT_VIEW_HPP
