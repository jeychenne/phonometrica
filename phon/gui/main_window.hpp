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
#include <phon/gui/output_panel.hpp>
#include <phon/application/script.hpp>
#include <QMainWindow>
#include <QTabWidget>
#include <QDockWidget>
#include <QProgressBar>
#include <QAction>
#include <QMenu>

namespace phonometrica {

class Runtime;
class Project;
class FileManager;
class Document;
class View;
class ViewPanel;

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:

	explicit MainWindow(Runtime &rt, QWidget *parent = nullptr);

	~MainWindow() override = default;

protected:

	void closeEvent(QCloseEvent *event) override;

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

	// View actions (forwarded to active view in the current panel)
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

	// Add a view wrapped in a ViewPanel to the tab widget.
	ViewPanel *addViewTab(View *view);

	void openScript(const Handle<Script> &script);

	// Returns the ViewPanel in the current tab (or nullptr).
	ViewPanel *currentPanel() const;

	// Returns the active View in the current panel (or nullptr).
	View *currentView() const;

	// Prompt to save all modified tabs. Returns false if user cancelled.
	bool promptSaveUnsavedTabs();

	// Prompt to save the project if modified. Returns false if user cancelled.
	bool promptSaveProject();

	Runtime &m_runtime;

	QMenu *m_recent_menu = nullptr;

	// Central area: tabbed panels (each panel contains one or more views).
	QTabWidget *m_viewer = nullptr;

	// Dock widgets
	QDockWidget *m_project_dock = nullptr;
	QDockWidget *m_console_dock = nullptr;

	// Project file manager
	FileManager *m_file_manager = nullptr;

	// Interactive scripting console
	class Console *m_console = nullptr;

	// Output panel for measurement results
	OutputPanel *m_output = nullptr;

	// Edit actions (need references for enable/disable)
	QAction *m_undo_action = nullptr;
	QAction *m_redo_action = nullptr;
	QAction *m_find_action = nullptr;
	QAction *m_replace_action = nullptr;

	// Progress bar in the status bar (for loading sounds, etc.)
	QProgressBar *m_progress_bar = nullptr;
};

} // namespace phonometrica

#endif // PHONOMETRICA_MAIN_WINDOW_HPP
