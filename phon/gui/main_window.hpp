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

#include <unordered_map>
#include <phon/string.hpp>
#include <phon/gui/console.hpp>
#include <phon/gui/output_panel.hpp>
#include <phon/gui/ipa_panel.hpp>
#include <phon/application/script.hpp>
#include <phon/application/conc/query.hpp>
#include <phon/application/plugin.hpp>
#include <QMainWindow>
#include <QTabWidget>
#include <QDockWidget>
#include <QProgressBar>
#include <QProgressDialog>
#include <QAction>
#include <QMenu>

namespace phonometrica {

class Runtime;
class Project;
class FileManager;
class Document;
class Annotation;
class View;
class ViewPanel;
class InfoPanel;
class ConcordanceView;
class AnnotationView;

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:

	explicit MainWindow(Runtime &rt, QWidget *parent = nullptr);

	~MainWindow() override = default;

	// Called after show() — loads plugins and startup scripts.
	void postInitialize();

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

	// Analysis menu
	void onFindInAnnotations();
	void onMeasureFormants();
	void onMeasurePitch();
	void onEditLastQuery();

	// File menu (import/export)
	void onImportMetadata();
	void onExportMetadata();
	void onExportAnnotations();

	// File menu (other)
	void onEditPreferences();
	void onCloseCurrentView();
	void onCloseAllViews();

	// View actions (forwarded to active view in the current panel)
	void onSaveCurrentView();
	void onExecuteCurrentView();
	void onEscapeCurrentView();

	// Window menu
	void onMaximizeViewer();
	void onRestoreDefaultLayout();

	// Tab management
	void onActiveTabChanged(int index);
	void updateUndoRedoState();

	// Plugins
	void onRunScript();
	void onInstallPlugin();
	void onUninstallPlugin();

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

	// Open a concordance in a new tab and wire its signals.
	void openConcordance(Handle<Concordance> conc);

	// Create an AnnotationView with progress feedback. Returns nullptr on failure.
	AnnotationView *createAnnotationView(const Handle<Annotation> &annot);

	// Find the ViewPanel that contains a given View, or nullptr.
	ViewPanel *findPanelForView(View *view) const;

	// Returns the ViewPanel in the current tab (or nullptr).
	ViewPanel *currentPanel() const;

	// Returns the active View in the current panel (or nullptr).
	View *currentView() const;

	// Prompt to save all modified tabs. Returns false if user cancelled.
	bool promptSaveUnsavedTabs();

	// Close one tab by index, prompting to save if modified. Returns false if cancelled.
	bool closeTab(int index);

	// Prompt to save the project if modified. Returns false if user cancelled.
	bool promptSaveProject();

	// Window geometry persistence (uses QSettings).
	void saveWindowState();
	void restoreWindowState();

	// Plugin support
	void loadPluginsAndScripts(const String &root);
	void loadPlugin(const String &path);
	void uninstallPlugin(int index);
	Plugin *findPlugin(const String &name);

	// Scripting shell functions (expose GUI dialogs to the scripting engine).
	void setShellFunctions();

	Runtime &m_runtime;

	QMenu *m_recent_menu = nullptr;

	// Central area: tabbed panels (each panel contains one or more views).
	QTabWidget *m_viewer = nullptr;

	// Dock widgets
	QDockWidget *m_project_dock = nullptr;
	QDockWidget *m_console_dock = nullptr;
	QDockWidget *m_info_dock = nullptr;

	// Project file manager
	FileManager *m_file_manager = nullptr;

	// Information panel (right dock)
	InfoPanel *m_info_panel = nullptr;

	// Interactive scripting console
	class Console *m_console = nullptr;

	// Output panel for measurement results
	OutputPanel *m_output = nullptr;

	// IPA symbol panel
	IpaPanel *m_ipa_panel = nullptr;

	// Edit actions (need references for enable/disable)
	QAction *m_undo_action = nullptr;
	QAction *m_redo_action = nullptr;
	QAction *m_find_action = nullptr;
	QAction *m_replace_action = nullptr;

	// Progress bar in the status bar (for loading sounds, etc.)
	QProgressBar *m_progress_bar = nullptr;

	// Progress dialog created by scripting functions (create_progress_dialog).
	std::unique_ptr<QProgressDialog> m_script_progress;

	// Last executed query (for "Edit last query" action).
	Handle<Query> m_last_query;

	// Plugin system
	QMenu *m_plugins_menu = nullptr;
	QAction *m_plugin_separator = nullptr;   // separates plugin submenus from built-in actions
	Array<AutoPlugin> m_plugins;
	// Parallel tracking: maps Plugin* to the QAction* that owns its submenu in m_plugins_menu.
	std::unordered_map<Plugin *, QAction *> m_plugin_actions;

	// Default dock layout state, captured at construction before restoreWindowState().
	QByteArray m_default_state;
};

} // namespace phonometrica

#endif // PHONOMETRICA_MAIN_WINDOW_HPP
