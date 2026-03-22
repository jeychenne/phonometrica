/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 21/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: main application window.                                                                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_MAIN_WINDOW_HPP
#define PHONOMETRICA_MAIN_WINDOW_HPP

#include <phon/string.hpp>
#include <phon/gui/console.hpp>
#include <phon/application/script.hpp>
#include <QMainWindow>
#include <QTabWidget>
#include <QDockWidget>
#include <QAction>
#include <QMenu>

namespace phonometrica {

class Runtime;
class Project;
class FileManager;
class Document;
class ScriptView;

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:

	explicit MainWindow(Runtime &rt, QWidget *parent = nullptr);

	~MainWindow() override = default;

private slots:

	// File menu
	void onNewScript();
	void onOpenProject();
	void onAddFiles();
	void onAddFolder();
	void onCloseProject();
	void onSaveProject();
	void onSaveProjectAs();
	void onQuit();

	// Edit menu
	void onUndo();
	void onRedo();
	void onFind();
	void onReplace();

	// View actions
	void onSaveCurrentView();
	void onExecuteCurrentView();
	void onEscapeCurrentView();

	// Window menu
	void onToggleProjectPanel(bool visible);
	void onToggleConsolePanel(bool visible);

	// File manager
	void onDocumentRequested(Document *doc);

private:

	void createMenus();
	void createDockWidgets();
	void createCentralWidget();
	void createStatusBar();

	QMenu *createFileMenu();
	QMenu *createEditMenu();
	QMenu *createAnalysisMenu();
	QMenu *createToolsMenu();
	QMenu *createWindowMenu();
	QMenu *createHelpMenu();

	void updateWindowTitle();
	void updateRecentProjects(const String &mostRecent = String());
	void rebuildRecentMenu();
	QString lastDirectory() const;
	void setLastDirectory(const QString &path);

	void openScript(const Handle<Script> &script);
	ScriptView *currentScriptView() const;

	Runtime &m_runtime;

	QMenu *m_recent_menu = nullptr;

	// Central area: tabbed views for sounds, annotations, scripts, etc.
	QTabWidget *m_viewer = nullptr;

	// Dock widgets
	QDockWidget *m_project_dock = nullptr;
	QDockWidget *m_console_dock = nullptr;

	// Project file manager
	FileManager *m_file_manager = nullptr;

	// Interactive scripting console
	class Console *m_console = nullptr;

	// Edit actions (need references for enable/disable)
	QAction *m_undo_action = nullptr;
	QAction *m_redo_action = nullptr;
	QAction *m_find_action = nullptr;
	QAction *m_replace_action = nullptr;
};

} // namespace phonometrica

#endif // PHONOMETRICA_MAIN_WINDOW_HPP
