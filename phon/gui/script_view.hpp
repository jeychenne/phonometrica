/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: View for scripts. This is a tab widget that contains a toolbar, the script editor and a find/replace bar.  *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SCRIPT_VIEW_HPP
#define PHONOMETRICA_SCRIPT_VIEW_HPP

#include <QWidget>
#include <QToolBar>
#include <QAction>
#include <phon/runtime.hpp>
#include <phon/application/script.hpp>

namespace phonometrica {

class ScriptEditor;
class SearchBar;
class Console;

class ScriptView : public QWidget
{
	Q_OBJECT

public:

	ScriptView(Runtime &rt, Console *console, const Handle<Script> &script, QWidget *parent = nullptr);

	void save();
	void execute();
	void find();
	void replace();
	void escape();

	bool isModified() const;
	void discardChanges();

	QString label() const;
	String path() const;

	Handle<Script> script() const { return m_script; }

signals:

	void titleChanged(const QString &title);

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
