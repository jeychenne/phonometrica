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
 * Created: 21/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: main application window.                                                                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_MAIN_WINDOW_HPP
#define PHONOMETRICA_MAIN_WINDOW_HPP

#include <unordered_map>
#include <phon/gui/console.hpp>
#include <phon/gui/output_panel.hpp>
#include <phon/gui/ipa_panel.hpp>
#include <phon/application/script.hpp>
#include <phon/application/note.hpp>
#include <phon/application/dataset.hpp>
#include <phon/application/conc/query.hpp>
#include <phon/application/plugin.hpp>
#include <phon/application/analysis.hpp>
#include <QMainWindow>
#include <QTabWidget>
#include <QDockWidget>
#include <QProgressBar>
#include <QProgressDialog>
#include <QAction>
#include <QMenu>
#include <QStringList>

class QDragEnterEvent;
class QDropEvent;

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

	// Open one or more file paths. Project files (.phon-project) trigger a
	// project switch (with the usual "save before closing?" prompt); other
	// recognized files are imported into the current project. Folders are
	// imported via Project::import_directory(). Errors per-path are surfaced
	// via QMessageBox; the file manager refresh is batched at the end. Safe to
	// call from drag-drop handlers, the macOS file-open event filter, or
	// (indirectly, via setPendingArgvPaths) from cold-start argv.
	void openPaths(const QStringList &paths);

	// Stash file paths from the command line so they get processed once the
	// event loop is running. Schedules a queued openPaths() call. Posting this
	// before postInitialize() ensures the cold-start paths run before the
	// "autoload most recent project" logic, which itself is now deferred.
	void setPendingArgvPaths(const QStringList &paths);

	// Access to installed plugins, needed by dialogs that enumerate protocols (e.g. the
	// protocol builder's "Load" menu). Phonometrica is a single-MainWindow application, so the
	// static instance pointer is set in the constructor body and cleared in the destructor.
	const Array<AutoPlugin> &plugins() const { return m_plugins; }
	static MainWindow *instance() { return s_instance; }

protected:

	void closeEvent(QCloseEvent *event) override;

	// Drag-and-drop of OS file URLs onto the window. Accepts any drag carrying
	// at least one local file URL; routes the drop to openPaths().
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dropEvent(QDropEvent *event) override;

	// Application-level event filter, installed on qApp in the constructor.
	// Catches QFileOpenEvent on macOS (Dock-icon drop, "Open With", or
	// double-click on an associated file) and routes the path to openPaths().
	bool eventFilter(QObject *watched, QEvent *event) override;

private slots:

	// File menu
	void onNewScript();
	void onNewNote();
	void onNewAnnotation();
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
	void onMeasureIntensity();
	void onMeasureSpectralMoments();
	void onMeasureVoiceQuality();
	void onEditLastQuery();

	// Speech menu
	void onRecordSound();
	void onFindSilences();
	void onTranscribe();

	// Analysis menu (data analysis / visualization)
	void onAnalyzeData();
	void onVisualizeData();

	// Start view: analyze data with file import fallback
	void onQuickAnalyzeData();

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
	void updateFindReplaceState();

	// Plugins
	void onRunScript();
	void onInstallPlugin();
	void onUninstallPlugin();

	// File manager
	void onDocumentRequested(Document *doc);

	// Help menu
	void onCheckForUpdates();

private:

	void createMenus();
	void createDockWidgets();
	void createCentralWidget();
	void createStatusBar();

	QMenu *createFileMenu();
	QMenu *createEditMenu();
	QMenu *createSpeechMenu();
	QMenu *createAnalysisMenu();
	QMenu *createToolsMenu();
	QMenu *createWindowMenu();
	QMenu *createHelpMenu();

	void checkForUpdates(bool silent);

	void updateWindowTitle();
	void updateRecentProjects(const String &mostRecent = String());
	void updateSaveActions();
	void rebuildRecentMenu();
	QString lastDirectory() const;
	void setLastDirectory(const QString &path);

	// Add a view wrapped in a ViewPanel to the tab widget.
	ViewPanel *addViewTab(View *view);

	void openScript(const Handle<Script> &script);
	void openNote(const Handle<Note> &note);

	void openDataset(Handle<Dataset> ds);

	// Open a concordance in a new tab and wire its signals.
	void openConcordance(Handle<Concordance> conc);

	void openAnalysis(Handle<DataTable> source);
	void openAnalysis(Handle<Analysis> analysis);

	// Bring the source data view to the front and select a specific row.
	// Used by AnalysisView's click-to-source on the EDA plot. If a view for
	// the source is already open, switches to its tab; otherwise opens a new
	// DatasetView/ConcordanceView (dispatched on the source's concrete type)
	// and then selects the row. If the row is hidden by an active filter in
	// the open view, displays an information dialog and makes no selection.
	// No-op for null sources or out-of-range rows.
	void revealSourceRow(Handle<DataTable> source, intptr_t source_row);

	// Show a dialog to pick a concordance or dataset from the project.
	// Returns a null handle if the user cancels or there are no data tables.
	Handle<DataTable> selectDataTable();

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

	// Close all views and clear project-specific state (for project switch/close).
	// Returns false if user cancelled.
	bool clearForProjectSwitch();

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

	// Set up Praat integration callbacks based on the configured path.
	void setupPraat();

	Runtime &m_runtime;

	QMenu *m_recent_menu = nullptr;
	QAction *m_open_recent_action = nullptr;
	QAction *m_save_action = nullptr;
	QAction *m_save_as_action = nullptr;

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

	// File paths from cold-start argv waiting to be opened once the event loop
	// is running. Drained by a queued openPaths() invocation scheduled in
	// setPendingArgvPaths().
	QStringList m_pending_argv_paths;

	// Single-instance pointer used by MainWindow::instance(). Set in the constructor body,
	// cleared in the destructor. Phonometrica is not designed to run multiple MainWindows.
	static MainWindow *s_instance;
};

} // namespace phonometrica

#endif // PHONOMETRICA_MAIN_WINDOW_HPP
