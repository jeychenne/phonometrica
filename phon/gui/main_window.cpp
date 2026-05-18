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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <algorithm>
#include <vector>
#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>
#include <phon/gui/file_dialog.hpp>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QDesktopServices>
#include <QUrl>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QCloseEvent>
#include <QLabel>
#include <QSettings>
#include <QProcess>
#include <QThread>
#include <QMetaObject>
#include <QTimer>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileOpenEvent>
#include <QFileInfo>
#include <phon/gui/main_window.hpp>
#include <phon/gui/start_view.hpp>
#include <phon/gui/file_manager.hpp>
#include <phon/gui/view.hpp>
#include <phon/gui/view_panel.hpp>
#include <phon/gui/script_view.hpp>
#include <phon/gui/note_view.hpp>
#include <phon/gui/sound_view.hpp>
#include <phon/gui/annotation_view.hpp>
#include <phon/gui/dataset_view.hpp>
#include <phon/gui/info_panel.hpp>
#include <phon/gui/help_browser.hpp>
#include <phon/gui/user_dialog.hpp>
#include <phon/gui/preferences_dialog.hpp>
#include <phon/gui/conc/query_editor.hpp>
#include <phon/gui/conc/formant_query_editor.hpp>
#include <phon/gui/conc/pitch_query_editor.hpp>
#include <phon/gui/conc/intensity_query_editor.hpp>
#include <phon/gui/conc/spectral_moments_query_editor.hpp>
#include <phon/gui/conc/voice_quality_query_editor.hpp>
#include <phon/gui/conc/concordance_view.hpp>
#include <phon/gui/analysis_view.hpp>
#include <phon/gui/batch_save_dialog.hpp>
#include <phon/gui/conc/protocol_query_editor.hpp>
#include <phon/gui/conc/protocol_builder_dialog.hpp>
#include <phon/gui/transcribe_dialog.hpp>
#include <phon/gui/find_silences_dialog.hpp>
#include <phon/gui/record_sound_dialog.hpp>
#include <phon/gui/transcription_worker.hpp>
#include <phon/application/bookmark.hpp>
#include <phon/application/project.hpp>
#include <phon/application/settings.hpp>
#include <phon/application/constants.hpp>
#include <phon/application/praat.hpp>
#include <phon/application/transcriber.hpp>
#include <phon/application/silence_detector.hpp>
#include <phon/utils/file_system.hpp>
#include <phon/utils/zip.hpp>
#include <phon/utils/helpers.hpp>
#include <phon/runtime/file.hpp>

namespace phonometrica {

static constexpr int MAX_RECENT = 10;

MainWindow *MainWindow::s_instance = nullptr;


MainWindow::MainWindow(Runtime &rt, QWidget *parent) :
	QMainWindow(parent), m_runtime(rt)
{
	s_instance = this;
	resize(1200, 800);

	// Make the left and right docks span the full height of the window.
	// The bottom dock (console/output) will only occupy the space between them,
	// underneath the central widget — matching the wx version's layout.
	setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
	setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

	createCentralWidget();
	createDockWidgets();
	createMenus();
	createStatusBar();

	auto *project = Project::get();
	if (project)
	{
		project->notify_update.connect([this]() {
			updateWindowTitle();
			m_file_manager->refresh();
			updateSaveActions();
		});
		project->notify_closed.connect([this]() {
			updateWindowTitle();
			updateSaveActions();
		});
		project->notify_error.connect([this](const String &msg) {
			auto qmsg = QString::fromUtf8(msg.data(), (int) msg.size());
			QMessageBox::warning(this, tr("Warning"), qmsg);
		});
		project->modification_changed.connect([this]() {
			updateSaveActions();
			updateWindowTitle();
		});
	}

	// When any document is modified, update the save actions and refresh
	// the file manager so that the modification star appears immediately.
	Document::file_modified.connect([this]() {
		updateSaveActions();
		updateWindowTitle();
		m_file_manager->refresh();
		// Also refresh every open tab's title so its modified-state asterisk
		// stays in sync. This catches modification paths (e.g. cell edits in
		// concordance, undo/redo of cell edits) that fire the broadcast but
		// do not emit View::titleChanged themselves.
		for (int i = 0; i < m_viewer->count(); i++)
		{
			auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(i));
			if (panel)
				m_viewer->setTabText(i, panel->label());
		}
	});

	updateWindowTitle();

	// Capture the default dock layout before any user customization is applied.
	m_default_state = QMainWindow::saveState();
	restoreWindowState();

	// Accept file drops on the window. Drops over child widgets that don't
	// claim them (file manager tree, viewer tabs, status bar) propagate up to
	// MainWindow::dropEvent(). Drops over the Scintilla script editor are
	// consumed by the editor (it inserts the URL as text), which matches user
	// expectations.
	setAcceptDrops(true);

	// Application-level filter so we catch QFileOpenEvent on macOS regardless
	// of which widget has focus. Uses qApp because QFileOpenEvent is delivered
	// to the QApplication object, not to any widget.
	qApp->installEventFilter(this);
}

void MainWindow::createMenus()
{
	auto *bar = menuBar();
	bar->addMenu(createFileMenu());
	bar->addMenu(createEditMenu());
	bar->addMenu(createSpeechMenu());
	bar->addMenu(createAnalysisMenu());
	bar->addMenu(createToolsMenu());
	bar->addMenu(createWindowMenu());
	bar->addMenu(createHelpMenu());
}

QMenu *MainWindow::createFileMenu()
{
	auto *menu = new QMenu(tr("&File"), this);

	auto *new_menu = menu->addMenu(tr("New"));
	new_menu->addAction(tr("New annotation..."), this, &MainWindow::onNewAnnotation);
	new_menu->addSeparator();
	new_menu->addAction(tr("New script"), QKeySequence::New, this, &MainWindow::onNewScript);
	new_menu->addAction(tr("New note"), this, &MainWindow::onNewNote);
	menu->addSeparator();

	menu->addAction(tr("Open project..."), QKeySequence(tr("Ctrl+O")), this, &MainWindow::onOpenProject);

	m_recent_menu = menu->addMenu(tr("Recent projects"));
	rebuildRecentMenu();

	m_open_recent_action = menu->addAction(tr("Open most recent project"), QKeySequence(tr("Ctrl+Shift+O")), this, [this]() {
		try
		{
			auto &lst = Settings::get_list("recent_projects");
			if (lst.empty()) return;
			if (!clearForProjectSwitch()) return;
			auto path = cast<String>(lst[1]);
			Project::get()->open(path);
			m_file_manager->refresh();
			updateRecentProjects(path);
			updateWindowTitle();
		}
		catch (std::exception &e)
		{
			QMessageBox::warning(this, tr("Error"),
				tr("Could not open project: %1").arg(e.what()));
		}
	});
	m_open_recent_action->setEnabled(m_recent_menu && !m_recent_menu->isEmpty());
	menu->addSeparator();
	menu->addAction(tr("Add files to project..."), QKeySequence(tr("Ctrl+Shift+A")), this, &MainWindow::onAddFiles);
	menu->addAction(tr("Add content of directory to project..."), this, &MainWindow::onAddFolder);
	menu->addSeparator();

	m_save_action = menu->addAction(tr("Save project"), QKeySequence(tr("Ctrl+Shift+S")), this, &MainWindow::onSaveProject);
	m_save_as_action = menu->addAction(tr("Save project as..."), this, &MainWindow::onSaveProjectAs);
	// Always enable save actions
	// m_save_action->setEnabled(false);
	// m_save_as_action->setEnabled(false);
	menu->addSeparator();

	menu->addAction(tr("Preferences..."), this, &MainWindow::onEditPreferences);
	menu->addSeparator();

	auto *import_menu = menu->addMenu(tr("Import"));
	import_menu->addAction(tr("Import metadata from CSV file..."), this, &MainWindow::onImportMetadata);

	auto *export_menu = menu->addMenu(tr("Export"));
	export_menu->addAction(tr("Export annotation(s) to plain text..."), this, &MainWindow::onExportAnnotations);
	export_menu->addAction(tr("Export project metadata to CSV file..."), this, &MainWindow::onExportMetadata);

	menu->addSeparator();
	menu->addAction(tr("Close current view"), QKeySequence(tr("Ctrl+W")), this, &MainWindow::onCloseCurrentView);
	menu->addAction(tr("Close all views"), QKeySequence(tr("Ctrl+Shift+W")), this, &MainWindow::onCloseAllViews);
	menu->addSeparator();
	menu->addAction(tr("Close project"), this, &MainWindow::onCloseProject);
	menu->addSeparator();
	menu->addAction(tr("Quit"), QKeySequence::Quit, this, &MainWindow::onQuit);

	return menu;
}

QMenu *MainWindow::createEditMenu()
{
	auto *menu = new QMenu(tr("&Edit"), this);

	m_undo_action = menu->addAction(tr("Undo"), QKeySequence::Undo, this, &MainWindow::onUndo);
	m_redo_action = menu->addAction(tr("Redo"), QKeySequence::Redo, this, &MainWindow::onRedo);
	m_undo_action->setEnabled(false);
	m_redo_action->setEnabled(false);

	menu->addSeparator();

	m_find_action = menu->addAction(tr("Find..."), this, &MainWindow::onFind);
	m_find_action->setShortcut(QKeySequence::Find);
	m_find_action->setShortcutContext(Qt::WidgetShortcut); // display only; keyboard handled by view
	m_find_action->setEnabled(false);

	m_replace_action = menu->addAction(tr("Replace..."), this, &MainWindow::onReplace);
	m_replace_action->setShortcut(QKeySequence(tr("Ctrl+H")));
	m_replace_action->setShortcutContext(Qt::WidgetShortcut); // display only; keyboard handled by view
	m_replace_action->setEnabled(false);

	// Save current view (Ctrl+S)
	auto *saveView = new QAction(this);
	saveView->setShortcut(QKeySequence::Save);
	connect(saveView, &QAction::triggered, this, &MainWindow::onSaveCurrentView);
	addAction(saveView);

	// Execute script (Ctrl+Return)
	auto *execView = new QAction(this);
	execView->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
	connect(execView, &QAction::triggered, this, &MainWindow::onExecuteCurrentView);
	addAction(execView);

	// Escape
	auto *escapeAction = new QAction(this);
	escapeAction->setShortcut(QKeySequence(Qt::Key_Escape));
	connect(escapeAction, &QAction::triggered, this, &MainWindow::onEscapeCurrentView);
	addAction(escapeAction);

	return menu;
}

QMenu *MainWindow::createAnalysisMenu()
{
	auto *menu = new QMenu(tr("&Analysis"), this);

	menu->addAction(tr("Find in annotations..."), QKeySequence(tr("Ctrl+Shift+F")),
		this, &MainWindow::onFindInAnnotations);

	menu->addSeparator();

	// Acoustic query types
	menu->addAction(tr("Measure formants..."), this, &MainWindow::onMeasureFormants);

	menu->addAction(tr("Measure pitch..."), this, &MainWindow::onMeasurePitch);

	menu->addAction(tr("Measure voice quality..."), this, &MainWindow::onMeasureVoiceQuality);

	menu->addAction(tr("Measure intensity..."), this, &MainWindow::onMeasureIntensity);

	menu->addAction(tr("Measure spectral moments..."), this, &MainWindow::onMeasureSpectralMoments);

	menu->addSeparator();

	menu->addAction(tr("Edit last query..."), QKeySequence(tr("Ctrl+L")),
	this, &MainWindow::onEditLastQuery);

	menu->addSeparator();

	menu->addAction(tr("Analyze data..."), this, &MainWindow::onAnalyzeData);
	menu->addAction(tr("Visualize data..."), this, &MainWindow::onVisualizeData);

	return menu;
}

QMenu *MainWindow::createSpeechMenu()
{
	auto *menu = new QMenu(tr("&Speech"), this);

	menu->addAction(tr("Record sound..."), this, &MainWindow::onRecordSound);

	menu->addSeparator();

	menu->addAction(tr("Find silences..."), this, &MainWindow::onFindSilences);

	menu->addSeparator();

	menu->addAction(tr("Transcribe audio..."), this, &MainWindow::onTranscribe);

	return menu;
}

QMenu *MainWindow::createToolsMenu()
{
	auto *menu = new QMenu(tr("&Plugins"), this);
	m_plugins_menu = menu;

	// Plugin submenus will be inserted before this separator.
	m_plugin_separator = menu->addSeparator();

	menu->addAction(tr("Run script..."), this, &MainWindow::onRunScript);
	menu->addSeparator();
	menu->addAction(tr("Build coding protocol..."), this, [this]() {
		ProtocolBuilderDialog dlg(m_runtime, this);
		dlg.exec();
	});
	menu->addSeparator();
	menu->addAction(tr("Install plugin..."), this, &MainWindow::onInstallPlugin);
	menu->addAction(tr("Uninstall plugin..."), this, &MainWindow::onUninstallPlugin);
	menu->addSeparator();
	menu->addAction(tr("How to extend this menu"), [this]() {
		HelpBrowser::showPage(QStringLiteral("scripting/plugins"), this);
	});

	return menu;
}

QMenu *MainWindow::createWindowMenu()
{
	auto *menu = new QMenu(tr("&Window"), this);

	// Use a custom action for the project dock so the menu always says
	// "File manager" instead of inheriting the dock title (which shows
	// the project name).
	auto *fm_action = new QAction(tr("File manager"), menu);
	fm_action->setCheckable(true);
	fm_action->setChecked(!m_project_dock->isHidden());
	connect(fm_action, &QAction::toggled, m_project_dock, &QDockWidget::setVisible);
	connect(m_project_dock, &QDockWidget::visibilityChanged, fm_action, &QAction::setChecked);
	menu->addAction(fm_action);

	menu->addAction(m_console_dock->toggleViewAction());
	menu->addAction(m_info_dock->toggleViewAction());

	menu->addSeparator();
	menu->addAction(tr("Maximize viewer"), QKeySequence(tr("Ctrl+Up")), this, &MainWindow::onMaximizeViewer);
	menu->addSeparator();
	menu->addAction(tr("Restore default layout"), QKeySequence(tr("Ctrl+Shift+Up")), this, &MainWindow::onRestoreDefaultLayout);

	return menu;
}

QMenu *MainWindow::createHelpMenu()
{
	auto *menu = new QMenu(tr("&Help"), this);

	auto *manual_action = menu->addAction(tr("Documentation"), [this]() {
		HelpBrowser::showPage({}, this);
	});
	manual_action->setShortcut(QKeySequence::HelpContents);

	menu->addAction(tr("Scripting"), [this]() {
		HelpBrowser::showPage(QStringLiteral("scripting/index"), this);
	});

	menu->addSeparator();

	menu->addAction(tr("Go to website"), []() {
		QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.phonometrica-ling.org")));
	});

	menu->addAction(tr("Acknowledgements"), [this]() {
		HelpBrowser::showPage(QStringLiteral("about/acknowledgements"), this);
	});

	menu->addSeparator();

	menu->addAction(tr("Sound information"), [this]() {
		auto formats = Sound::supported_sound_format_names();
		auto joined = String::join(formats, ", ");
		auto msg = tr("Supported sound formats on this platform:\n%1\n\n"
		              "libsndfile: version %2\n\n"
		              "RTAudio: version %3")
			.arg(QString::fromUtf8(joined.data(), (int) joined.size()))
			.arg(QString::fromUtf8(Sound::libsndfile_version().data()))
			.arg(QString::fromUtf8(Sound::rtaudio_version().data()));
		QMessageBox::information(this, tr("Sound information"), msg);
	});

	menu->addSeparator();

	menu->addAction(tr("About Phonometrica"), [this]() {
		auto version = QString::fromStdString(utils::get_version());
		auto date = QString::fromStdString(utils::get_date());
		QMessageBox::about(this, tr("About Phonometrica"),
			tr("<h3>Phonometrica %1</h3>"
			   "<p>Release date: %2</p>"
			   "<p>An open-source platform for the annotation "
			   "and analysis of speech corpora.</p>"
			   "<p>&copy; 2019-2026 Julien Eychenne</p>"
			   "<p>&copy; 2019-2025 Léa Courdès-Murphy</p>")
			.arg(version, date));
	});

	return menu;
}

void MainWindow::createCentralWidget()
{
	m_viewer = new QTabWidget(this);
	m_viewer->setTabsClosable(true);
	m_viewer->setMovable(true);
	m_viewer->setDocumentMode(true);

	connect(m_viewer, &QTabWidget::tabCloseRequested, [this](int index) {
		closeTab(index);
	});

	connect(m_viewer, &QTabWidget::currentChanged, this, &MainWindow::onActiveTabChanged);

	auto *start_view = new StartView(this);
	m_viewer->addTab(start_view, tr("Start"));

	connect(start_view, &StartView::openProjectRequested,    this, &MainWindow::onOpenProject);
	connect(start_view, &StartView::addFilesRequested,       this, &MainWindow::onAddFiles);
	connect(start_view, &StartView::newAnnotationRequested,  this, &MainWindow::onNewAnnotation);
	connect(start_view, &StartView::analyzeDataRequested,    this, &MainWindow::onQuickAnalyzeData);
	connect(start_view, &StartView::documentationRequested,  this, [this]() {
		HelpBrowser::showPage({}, this);
	});
	connect(start_view, &StartView::recentProjectRequested,  this, [this](const String &path) {
		try
		{
			if (!clearForProjectSwitch()) return;
			Project::get()->open(path);
			m_file_manager->refresh();
			updateRecentProjects(path);
			updateWindowTitle();
		}
		catch (std::exception &e)
		{
			QMessageBox::warning(this, tr("Error"),
				tr("Could not open project: %1").arg(e.what()));
		}
	});

	setCentralWidget(m_viewer);
}

void MainWindow::createDockWidgets()
{
	// Project manager dock (left)
	m_project_dock = new QDockWidget(tr("Project"), this);
	m_project_dock->setObjectName(QStringLiteral("ProjectDock"));
	m_project_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

	m_file_manager = new FileManager(Project::get(), m_project_dock);
	connect(m_file_manager, &FileManager::documentRequested, this, &MainWindow::onDocumentRequested);

	connect(m_file_manager, &FileManager::noteRequested, this, [this](Directory *dir) {
		auto note = make_handle<Note>(dir);
		openNote(note);
		statusBar()->showMessage(tr("New note"), 2000);
	});
	connect(m_file_manager, &FileManager::analysisRequested, this, [this](DataTable *dt) {
		openAnalysis(Handle<DataTable>(dt));
	});
	connect(m_file_manager, &FileManager::bookmarkRequested, this, [this](TimeStamp *ts) {
		if (!ts || !ts->annotation()) return;
		onDocumentRequested(ts->annotation().get());
		// Navigate to the bookmarked location in the annotation view.
		if (auto *panel = currentPanel())
		{
			for (auto *v : panel->views())
			{
				if (auto *av = qobject_cast<AnnotationView *>(v))
				{
					av->openSelection(ts->layer(), ts->start(), ts->end());
					break;
				}
			}
		}
	});

	connect(m_file_manager, &FileManager::scriptRunRequested, this, [this](const QString &path) {
		m_console->runScript(path);
	});

	m_project_dock->setWidget(m_file_manager);
	addDockWidget(Qt::LeftDockWidgetArea, m_project_dock);

	// Console & Output dock (bottom)
	m_console_dock = new QDockWidget(tr("Tools"), this);
	m_console_dock->setObjectName(QStringLiteral("ToolsDock"));
	m_console_dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

	auto *bottom_tabs = new QTabWidget(m_console_dock);
	m_console = new Console(m_runtime, bottom_tabs);
	m_output = new OutputPanel(bottom_tabs);
	m_ipa_panel = new IpaPanel(bottom_tabs);
	bottom_tabs->addTab(m_console, tr("Console"));
	bottom_tabs->addTab(m_output, tr("Output"));
	bottom_tabs->addTab(m_ipa_panel, tr("IPA"));

	m_console_dock->setWidget(bottom_tabs);
	addDockWidget(Qt::BottomDockWidgetArea, m_console_dock);
	// Constrain the tools panel to a reasonable initial height.
	resizeDocks({m_console_dock}, {150}, Qt::Vertical);

	// Information panel dock (right)
	m_info_dock = new QDockWidget(tr("Information"), this);
	m_info_dock->setObjectName(QStringLiteral("InfoDock"));
	m_info_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

	m_info_panel = new InfoPanel(Project::get(), m_info_dock);
	m_info_dock->setWidget(m_info_panel);
	addDockWidget(Qt::RightDockWidgetArea, m_info_dock);

	// Harmonize the dock title bar height with the viewer's tab bar.
#if PHON_MACOS
	auto dock_style = QStringLiteral(
		"QDockWidget { font-size: 13px; }"
		"QDockWidget::title { padding: 4px 6px; }");
	m_project_dock->setStyleSheet(dock_style);
	m_console_dock->setStyleSheet(dock_style);
	m_info_dock->setStyleSheet(dock_style);
#endif

	// Wire file manager selection to info panel.
	connect(m_file_manager, &FileManager::selectionChanged, m_info_panel, &InfoPanel::onSelectionChanged);
}

void MainWindow::createStatusBar()
{
	m_progress_bar = new QProgressBar(this);
	m_progress_bar->setMaximumWidth(200);
	m_progress_bar->setMaximumHeight(16);
	m_progress_bar->setTextVisible(false);
	m_progress_bar->setVisible(false);
	statusBar()->addPermanentWidget(m_progress_bar);
	statusBar()->showMessage(tr("Ready"));
}


// ---------------------------------------------------------
//  Window title
// ---------------------------------------------------------

void MainWindow::updateWindowTitle()
{
	auto *project = Project::get();

	// Window title is always just "Phonometrica"
	setWindowTitle(tr("Phonometrica"));

	// Project name goes in the dock widget title
	if (project && m_project_dock)
	{
		auto label = project->label();
		auto title = QString::fromUtf8(label.data(), (int) label.size());
		if (project->modified())
			title += tr(" *");
		m_project_dock->setWindowTitle(title);

		// Show full project path as a tooltip on the dock title bar.
		if (project->has_path()) {
			auto &pp = project->path();
			m_project_dock->setToolTip(QString::fromUtf8(pp.data(), (int) pp.size()));
		} else {
			m_project_dock->setToolTip(tr("Not saved yet"));
		}
	}
	else if (m_project_dock)
	{
		m_project_dock->setWindowTitle(tr("Project"));
	}
}


// ---------------------------------------------------------
//  Recent projects
// ---------------------------------------------------------

void MainWindow::updateRecentProjects(const String &mostRecent)
{
	try
	{
		auto &lst = Settings::get_list("recent_projects");

		if (!mostRecent.empty())
		{
			lst.remove(mostRecent);
			lst.prepend(mostRecent);

			while (lst.size() > MAX_RECENT)
				lst.pop_back();
		}
	}
	catch (...)
	{
	}

	rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
	if (!m_recent_menu)
		return;

	m_recent_menu->clear();

	try
	{
		auto &lst = Settings::get_list("recent_projects");

		for (intptr_t i = 1; i <= lst.size() && i <= MAX_RECENT; i++)
		{
			auto path = cast<String>(lst[i]);
			auto qpath = QString::fromUtf8(path.data(), (int) path.size());
			m_recent_menu->addAction(qpath, [this, path]() {
				try
				{
					if (!clearForProjectSwitch()) return;
					Project::get()->open(path);
					m_file_manager->refresh();
					updateRecentProjects(path);
					updateWindowTitle();
				}
				catch (std::exception &e)
				{
					QMessageBox::warning(this, tr("Error"),
						tr("Could not open project: %1").arg(e.what()));
				}
			});
		}

		if (!lst.empty())
		{
			m_recent_menu->addSeparator();
			m_recent_menu->addAction(tr("Clear recent projects"), [this]() {
				Settings::reset_recent_projects();
				rebuildRecentMenu();
			});
		}
	}
	catch (...)
	{
	}

	m_recent_menu->setEnabled(!m_recent_menu->isEmpty());

	if (m_open_recent_action)
		m_open_recent_action->setEnabled(!m_recent_menu->isEmpty());
}


// ---------------------------------------------------------
//  File dialog helpers
// ---------------------------------------------------------

QString MainWindow::lastDirectory() const
{
	return phonometrica::lastDirectory();
}

void MainWindow::setLastDirectory(const QString &path)
{
	phonometrica::setLastDirectory(path);
}


// ---------------------------------------------------------
//  Slots
// ---------------------------------------------------------

void MainWindow::onNewScript()
{
	auto script = make_handle<Script>(Project::get()->scripts().get());
	openScript(script);
	statusBar()->showMessage(tr("New script"), 2000);
}

void MainWindow::onNewNote()
{
	auto note = make_handle<Note>(Project::get()->notes().get());
	openNote(note);
	statusBar()->showMessage(tr("New note"), 2000);
}

void MainWindow::onNewAnnotation()
{
	auto sounds = Project::get()->get_sounds();
	if (sounds.empty())
	{
		QMessageBox::warning(this, tr("New annotation"),
			tr("There are no sound files in the project. Please add a sound file first."));
		return;
	}

	QStringList items;
	for (auto &snd : sounds)
	{
		auto lbl = snd->browser_label();
		items << QString::fromUtf8(lbl.data(), (int) lbl.size());
	}

	bool ok = false;
	auto chosen = QInputDialog::getItem(this, tr("New annotation"),
		tr("Select a sound file:"), items, 0, false, &ok);

	if (!ok || chosen.isEmpty())
		return;

	int index = items.indexOf(chosen);
	if (index < 0)
		return;

	auto &sound = sounds[index + 1]; // 1-based Array

	auto annot = make_handle<Annotation>();
	annot->set_sound(sound);
	annot->create_layer(1, "default", false);

	auto *view = createAnnotationView(annot);
	if (view)
		addViewTab(view);

	statusBar()->showMessage(tr("New annotation"), 2000);
}

void MainWindow::onOpenProject()
{
	auto path = QFileDialog::getOpenFileName(this, tr("Open project"),
		lastDirectory(), tr("Phonometrica project (*.phon-project)"));

	if (path.isEmpty())
		return;

	setLastDirectory(path);

	// Close all existing views and clear state before switching projects.
	if (!clearForProjectSwitch())
		return;

	try
	{
		Project::get()->open(String(path.toUtf8().constData()));
		m_file_manager->refresh();
		updateRecentProjects(String(path.toUtf8().constData()));
		updateWindowTitle();
		statusBar()->showMessage(tr("Opened project: %1").arg(path), 3000);
	}
	catch (std::exception &e)
	{
		QMessageBox::warning(this, tr("Error"),
			tr("Could not open project: %1").arg(e.what()));
	}
}

void MainWindow::onAddFiles()
{
	auto files = QFileDialog::getOpenFileNames(this, tr("Add files to project"),
		lastDirectory());

	if (files.isEmpty())
		return;

	setLastDirectory(files.first());

	auto *project = Project::get();
	int added = 0;

	for (auto &f : files)
	{
		try
		{
			project->import_file(String(f.toUtf8().constData()));
			added++;
		}
		catch (std::exception &e)
		{
			QMessageBox::warning(this, tr("Import error"),
				tr("Could not import \"%1\": %2").arg(f, e.what()));
		}
	}

	m_file_manager->refresh();
	updateWindowTitle();
	statusBar()->showMessage(tr("Added %1 file(s)").arg(added), 3000);
}

void MainWindow::onAddFolder()
{
	auto dir = QFileDialog::getExistingDirectory(this, tr("Add directory to project"),
		lastDirectory());

	if (dir.isEmpty())
		return;

	setLastDirectory(dir);

	try
	{
		Project::get()->import_directory(String(dir.toUtf8().constData()));
		m_file_manager->refresh();
		updateWindowTitle();
		statusBar()->showMessage(tr("Added directory: %1").arg(dir), 3000);
	}
	catch (std::exception &e)
	{
		QMessageBox::warning(this, tr("Import error"),
			tr("Could not import directory: %1").arg(e.what()));
	}
}

void MainWindow::onCloseProject()
{
	auto *project = Project::get();
	if (project->modified())
	{
		auto answer = QMessageBox::question(this, tr("Close project"),
			tr("The project has unsaved changes. Save before closing?"),
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

		if (answer == QMessageBox::Cancel)
			return;
		if (answer == QMessageBox::Save)
		{
			if (!project->has_path())
			{
				onSaveProjectAs();
				if (!project->has_path())
					return;
			}
			else
			{
				project->save();
			}
		}
	}

	if (!clearForProjectSwitch())
		return;

	Project::close();
	m_file_manager->refresh();
	updateWindowTitle();
	statusBar()->showMessage(tr("Project closed"), 2000);
}

void MainWindow::onSaveProject()
{
	auto *project = Project::get();
	if (!project->has_path())
	{
		onSaveProjectAs();
		return;
	}

	try
	{
		// Save all modified views (annotations, scripts, etc.) first.
		for (int i = 0; i < m_viewer->count(); i++)
		{
			auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(i));
			if (panel)
				panel->saveAll();
		}

		project->save();
		updateWindowTitle();
		m_file_manager->refresh();
		statusBar()->showMessage(tr("Project saved"), 2000);
	}
	catch (std::exception &e)
	{
		QMessageBox::warning(this, tr("Error"),
			tr("Could not save project: %1").arg(e.what()));
	}
}

void MainWindow::onSaveProjectAs()
{
	auto path = QFileDialog::getSaveFileName(this, tr("Save project as"),
		lastDirectory(), tr("Phonometrica project (*.phon-project)"));

	if (path.isEmpty())
		return;

	setLastDirectory(path);

	try
	{
		// Save all modified views first.
		for (int i = 0; i < m_viewer->count(); i++)
		{
			auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(i));
			if (panel)
				panel->saveAll();
		}

		Project::get()->save(String(path.toUtf8().constData()));
		updateRecentProjects(String(path.toUtf8().constData()));
		updateWindowTitle();
		m_file_manager->refresh();
		statusBar()->showMessage(tr("Project saved as: %1").arg(path), 3000);
	}
	catch (std::exception &e)
	{
		QMessageBox::warning(this, tr("Error"),
			tr("Could not save project: %1").arg(e.what()));
	}
}

void MainWindow::onImportMetadata()
{
	m_info_panel->onImportMetadata();
}

void MainWindow::onExportMetadata()
{
	m_info_panel->onExportMetadata();
}

void MainWindow::onExportAnnotations()
{
	try
	{
		run_script(m_runtime, transphon);
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Export error"), QString::fromUtf8(e.what()));
	}
}

void MainWindow::onEditPreferences()
{
	PreferencesDialog dlg(this);
	if (dlg.exec() == QDialog::Accepted && dlg.praatPathChanged())
		setupPraat();
}

void MainWindow::onCloseCurrentView()
{
	int idx = m_viewer->currentIndex();
	if (idx < 0)
		return;

	// If the current tab is split, Ctrl+W closes the rightmost non-primary
	// pane first (unwinding the split) and only closes the whole tab on a
	// subsequent press. In concordance split view, that secondary pane is
	// the annotation; the concordance itself anchors the tab and stays put.
	auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(idx));
	if (panel && panel->isSplit())
	{
		panel->closeSecondaryView();
		return;
	}

	closeTab(idx);
}

void MainWindow::onCloseAllViews()
{
	// ── Collect all unsaved tabs ──
	struct TabInfo { QWidget *widget; QString label; bool preCheck; };
	QList<TabInfo> unsaved;

	for (int i = 0; i < m_viewer->count(); i++)
	{
		auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(i));
		if (!panel || !panel->isModified()) continue;

		bool pre = true;
		unsaved.append({m_viewer->widget(i), panel->label(), pre});
	}

	// ── Batch dialog for 2+ unsaved tabs ──
	QSet<QWidget *> handled;
	if (unsaved.size() >= 2)
	{
		QStringList labels;
		QList<bool> preChecked;
		for (auto &t : unsaved) {
			labels << t.label;
			preChecked << t.preCheck;
		}

		BatchSaveDialog dlg(labels, preChecked, this);
		if (dlg.exec() == QDialog::Rejected) return;

		auto checked = dlg.checkedItems();
		for (int k = 0; k < unsaved.size(); k++)
		{
			auto *panel = qobject_cast<ViewPanel *>(unsaved[k].widget);
			if (!panel) continue;

			if (dlg.action() == BatchSaveDialog::SaveSelected && checked[k]) {
				panel->saveAll();
			}
			else {
				for (auto *v : panel->views())
					v->discardChanges();
			}
			handled.insert(unsaved[k].widget);
		}
	}

	// ── Close from last to first ──
	// Tabs handled above are closed without prompting.
	while (m_viewer->count() > 0)
	{
		int idx = m_viewer->count() - 1;
		auto *w = m_viewer->widget(idx);
		if (handled.contains(w))
		{
			m_viewer->removeTab(idx);
			updateWindowTitle();
			m_file_manager->refresh();
		}
		else
		{
			if (!closeTab(idx))
				break;
		}
	}
}

bool MainWindow::closeTab(int index)
{
	auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(index));

	if (panel && panel->isModified())
	{
		// Bring the tab into view so the user knows which one we're asking about.
		m_viewer->setCurrentIndex(index);

		auto answer = QMessageBox::question(this, tr("Unsaved changes"),
			tr("The tab \"%1\" has unsaved changes. Save before closing?").arg(panel->label()),
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

		if (answer == QMessageBox::Cancel)
			return false;
		if (answer == QMessageBox::Save)
			panel->saveAll();
		else
		{
			for (auto *v : panel->views())
				v->discardChanges();
		}
	}

	m_viewer->removeTab(index);
	updateWindowTitle();
	m_file_manager->refresh();
	return true;
}

bool MainWindow::clearForProjectSwitch()
{
	// Handle all unsaved tabs in one batch dialog (or single prompt).
	if (!promptSaveUnsavedTabs())
		return false; // user cancelled

	// Close all tabs from last to first (changes already saved/discarded above).
	for (int i = m_viewer->count() - 1; i >= 0; i--)
	{
		auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(i));
		if (!panel)
			continue; // keep non-ViewPanel tabs (Welcome)

		m_viewer->removeTab(i);
	}

	// Clear project-specific state.
	m_last_query = {};
	m_info_panel->onSelectionChanged({});
	updateWindowTitle();
	m_file_manager->refresh();

	return true;
}

// ---------------------------------------------------------------------------
//  External file opening: drag-drop, macOS QFileOpenEvent, cold-start argv.
// ---------------------------------------------------------------------------

void MainWindow::openPaths(const QStringList &paths)
{
	if (paths.isEmpty())
		return;

	// Partition into a single project file (if any) and the rest. We only act
	// on at most one project per call: if the drop contains multiple .phon-project
	// files, we open the first and treat the others as plain files (which will
	// fail import — but that's a clearer signal than silently picking one).
	QString project_path;
	QStringList other_paths;
	other_paths.reserve(paths.size());

	for (const auto &p : paths)
	{
		QFileInfo info(p);
		// Detect project files by extension. PHON_EXT_PROJECT includes the leading dot.
		if (p.endsWith(QLatin1String(PHON_EXT_PROJECT), Qt::CaseInsensitive))
		{
			if (project_path.isEmpty())
				project_path = info.absoluteFilePath();
			else
				other_paths.append(p); // multiple projects: keep only the first
		}
		else
		{
			other_paths.append(p);
		}
	}

	// 1. Project switch first, if a project was dropped. clearForProjectSwitch()
	//    handles the "save unsaved tabs?" prompt and bails on cancel.
	if (!project_path.isEmpty())
	{
		if (!clearForProjectSwitch())
			return;

		try
		{
			auto native = String(project_path.toUtf8().constData());
			Project::get()->open(native);
			m_file_manager->refresh();
			updateRecentProjects(native);
			updateWindowTitle();
			statusBar()->showMessage(tr("Opened project: %1").arg(project_path), 3000);
		}
		catch (std::exception &e)
		{
			QMessageBox::warning(this, tr("Error"),
				tr("Could not open project: %1").arg(e.what()));
			return;
		}
	}

	// 2. Import any non-project paths into the current project. This includes
	//    the case where no project was dropped — files just go into whatever
	//    project is currently open (which may be the empty default project
	//    that exists at startup).
	if (other_paths.isEmpty())
		return;

	auto *project = Project::get();
	int added = 0;
	int failed = 0;

	for (const auto &p : other_paths)
	{
		try
		{
			auto native = String(QFileInfo(p).absoluteFilePath().toUtf8().constData());

			if (filesystem::is_directory(native))
				project->import_directory(native);
			else
				project->import_file(native);

			added++;
		}
		catch (std::exception &e)
		{
			failed++;
			QMessageBox::warning(this, tr("Import error"),
				tr("Could not import \"%1\": %2").arg(p, e.what()));
		}
	}

	m_file_manager->refresh();
	updateWindowTitle();
	if (added > 0)
		statusBar()->showMessage(tr("Added %1 file(s)").arg(added), 3000);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
	const auto *mime = event->mimeData();
	if (!mime || !mime->hasUrls())
		return; // default: ignore — Qt will not deliver dropEvent.

	// Accept if at least one URL is a local file. We don't probe the file system
	// here (drag-enter must be cheap); per-path validation happens in openPaths().
	for (const auto &url : mime->urls())
	{
		if (url.isLocalFile())
		{
			event->acceptProposedAction();
			return;
		}
	}
}

void MainWindow::dropEvent(QDropEvent *event)
{
	const auto *mime = event->mimeData();
	if (!mime || !mime->hasUrls())
		return;

	QStringList paths;
	paths.reserve(mime->urls().size());
	for (const auto &url : mime->urls())
	{
		if (url.isLocalFile())
			paths.append(url.toLocalFile());
	}

	if (paths.isEmpty())
		return;

	event->acceptProposedAction();

	// Defer the actual work: dropEvent runs inside the OS drag-drop loop, and
	// modal dialogs (the "save unsaved tabs?" prompt from clearForProjectSwitch,
	// or per-file error boxes) opened from inside that loop misbehave on macOS.
	// Posting through the event loop is also more consistent with how
	// QFileOpenEvent and the argv path arrive.
	QTimer::singleShot(0, this, [this, paths]() { openPaths(paths); });
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::FileOpen)
	{
		// macOS-only in practice. Fired on Dock-icon drop, "Open With ->
		// Phonometrica", or double-click on a file with a registered association
		// (CFBundleDocumentTypes in Info.plist). Delivered to qApp, hence the
		// app-level filter. Defer to the event loop for the same reason as
		// dropEvent: avoids reentrancy issues with modal dialogs.
		auto *fe = static_cast<QFileOpenEvent *>(event);
		QString path = fe->file();
		if (!path.isEmpty())
		{
			QTimer::singleShot(0, this, [this, path]() {
				openPaths(QStringList{path});
			});
			return true;
		}
	}
	return QMainWindow::eventFilter(watched, event);
}

void MainWindow::setPendingArgvPaths(const QStringList &paths)
{
	m_pending_argv_paths = paths;
	if (m_pending_argv_paths.isEmpty())
		return;

	// Drain on the next event-loop turn. By that time the constructor has
	// returned, show() has been called, and postInitialize() has run — meaning
	// plugins are loaded and the autoload-recent-project logic (now also
	// deferred, see postInitialize) has had a chance to either skip itself
	// (because m_pending_argv_paths was non-empty) or run before us.
	QTimer::singleShot(0, this, [this]() {
		auto paths = std::move(m_pending_argv_paths);
		m_pending_argv_paths.clear();
		openPaths(paths);
	});
}

void MainWindow::onMaximizeViewer()
{
	m_project_dock->hide();
	m_console_dock->hide();
	m_info_dock->hide();
}

void MainWindow::onRestoreDefaultLayout()
{
	// Show all docks and restore to the layout captured at construction.
	m_project_dock->show();
	m_console_dock->show();
	m_info_dock->show();
	QMainWindow::restoreState(m_default_state);
}

void MainWindow::onQuit()
{
	close(); // This triggers closeEvent().
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	bool autosave = Settings::get_boolean("autosave");

	// ── Save open view paths for "Restore views" ──
	if (Settings::get_boolean("restore_views"))
	{
		Array<Variant> views;
		for (int i = 0; i < m_viewer->count(); i++)
		{
			auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(i));
			if (!panel) continue;
			for (auto *v : panel->views())
			{
				auto p = v->path();
				if (!p.empty())
					views.append(p);
			}
		}
		Settings::set_value("selected_view", intptr_t(m_viewer->currentIndex()));
		Settings::set_value("recent_views", std::move(views));
	}

	// ── Handle unsaved tabs ──
	if (autosave)
	{
		// Silently save all modified tabs.
		for (int i = 0; i < m_viewer->count(); i++)
		{
			auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(i));
			if (panel && panel->isModified())
				panel->saveAll();
		}
	}
	else
	{
		if (!promptSaveUnsavedTabs())
		{
			event->ignore();
			return;
		}
	}

	// ── Handle unsaved project ──
	auto *project = Project::get();
	if (project && project->modified())
	{
		if (autosave)
		{
			if (project->has_path())
			{
				project->save();
			}
			else
			{
				// Can't silently save a project that has never been saved — need Save As.
				if (!promptSaveProject())
				{
					event->ignore();
					return;
				}
			}
		}
		else
		{
			if (!promptSaveProject())
			{
				event->ignore();
				return;
			}
		}
	}

	saveWindowState();
	event->accept();
}

bool MainWindow::promptSaveUnsavedTabs()
{
	// ── Collect all unsaved tabs ──
	struct TabInfo { int index; QString label; bool preCheck; };
	QList<TabInfo> unsaved;

	for (int i = 0; i < m_viewer->count(); i++)
	{
		auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(i));
		if (!panel || !panel->isModified())
			continue;

		// Pre-check all items by default.
		bool pre = true;
		unsaved.append({i, panel->label(), pre});
	}

	if (unsaved.isEmpty())
		return true;

	// ── Batch dialog for 2+ unsaved tabs ──
	if (unsaved.size() >= 2)
	{
		QStringList labels;
		QList<bool> preChecked;
		for (auto &t : unsaved) {
			labels << t.label;
			preChecked << t.preCheck;
		}

		BatchSaveDialog dlg(labels, preChecked, this);
		if (dlg.exec() == QDialog::Rejected) return false;

		auto checked = dlg.checkedItems();
		for (int k = 0; k < unsaved.size(); k++)
		{
			int idx = unsaved[k].index;
			auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(idx));
			if (!panel) continue;

			if (dlg.action() == BatchSaveDialog::SaveSelected && checked[k]) {
				if (!panel->saveAll())
					return false; // Save was cancelled (e.g. user dismissed the file dialog).
			}
			else {
				for (auto *v : panel->views())
					v->discardChanges();
			}
		}
		return true;
	}

	// ── Single unsaved tab: prompt individually ──
	int i = unsaved.first().index;
	auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(i));
	if (!panel)
		return true;

	m_viewer->setCurrentIndex(i);

	auto answer = QMessageBox::question(this, tr("Unsaved changes"),
		tr("The tab \"%1\" has unsaved changes. Save before closing?").arg(panel->label()),
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

	if (answer == QMessageBox::Cancel)
		return false;

	if (answer == QMessageBox::Save)
	{
		if (!panel->saveAll())
			return false;
	}
	else
	{
		for (auto *v : panel->views())
			v->discardChanges();
	}

	return true;
}

bool MainWindow::promptSaveProject()
{
	auto *project = Project::get();
	if (!project || !project->modified())
		return true;

	auto answer = QMessageBox::question(this, tr("Quit"),
		tr("The project has unsaved changes. Save before quitting?"),
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

	if (answer == QMessageBox::Cancel)
		return false;

	if (answer == QMessageBox::Save)
	{
		if (!project->has_path())
		{
			onSaveProjectAs();
			if (!project->has_path())
				return false; // User cancelled the "save as" dialog.
		}
		else
		{
			project->save();
		}
	}

	return true;
}

void MainWindow::onUndo()
{
	if (auto *view = currentView())
		view->undo();
}

void MainWindow::onRedo()
{
	if (auto *view = currentView())
		view->redo();
}

void MainWindow::onActiveTabChanged(int /*index*/)
{
	updateUndoRedoState();
	updateFindReplaceState();
}

void MainWindow::updateUndoRedoState()
{
	if (!m_undo_action || !m_redo_action)
		return;
	auto *view = currentView();
	m_undo_action->setEnabled(view && view->canUndo());
	m_redo_action->setEnabled(view && view->canRedo());
}

void MainWindow::updateFindReplaceState()
{
	if (!m_find_action || !m_replace_action)
		return;
	auto *view = currentView();
	bool supported = view && view->supportsFind();
	m_find_action->setEnabled(supported);
	m_replace_action->setEnabled(supported);
}

void MainWindow::updateSaveActions()
{
	// auto *project = Project::get();
	// bool empty = !project || project->empty();
	// Always enable save actions
	// m_save_action->setEnabled(!empty && project->modified());
	// m_save_as_action->setEnabled(!empty);
}


// ---------------------------------------------------------
//  Analysis menu slots
// ---------------------------------------------------------

void MainWindow::onFindInAnnotations()
{
	QueryEditor editor(this);

	if (editor.exec() == QDialog::Accepted)
	{
		m_last_query = editor.query();
		auto conc = editor.concordance();

		if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
		{
			openConcordance(conc);
			statusBar()->showMessage(
				tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
		}
		else
		{
			QMessageBox::information(this, tr("Search"), tr("No matches found."));
		}
	}
}

void MainWindow::onMeasureFormants()
{
	FormantQueryEditor editor(this);

	if (editor.exec() == QDialog::Accepted)
	{
		m_last_query = editor.query();
		auto conc = editor.concordance();

		if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
		{
			openConcordance(conc);
			statusBar()->showMessage(
				tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
		}
		else
		{
			QMessageBox::information(this, tr("Search"), tr("No matches found."));
		}
	}
}

void MainWindow::onMeasurePitch()
{
	PitchQueryEditor editor(this);

	if (editor.exec() == QDialog::Accepted)
	{
		m_last_query = editor.query();
		auto conc = editor.concordance();

		if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
		{
			openConcordance(conc);
			statusBar()->showMessage(
				tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
		}
		else
		{
			QMessageBox::information(this, tr("Search"), tr("No matches found."));
		}
	}
}

void MainWindow::onMeasureIntensity()
{
	IntensityQueryEditor editor(this);

	if (editor.exec() == QDialog::Accepted)
	{
		m_last_query = editor.query();
		auto conc = editor.concordance();

		if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
		{
			openConcordance(conc);
			statusBar()->showMessage(
				tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
		}
		else
		{
			QMessageBox::information(this, tr("Search"), tr("No matches found."));
		}
	}
}

void MainWindow::onMeasureSpectralMoments()
{
	SpectralMomentsQueryEditor editor(this);

	if (editor.exec() == QDialog::Accepted)
	{
		m_last_query = editor.query();
		auto conc = editor.concordance();

		if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
		{
			openConcordance(conc);
			statusBar()->showMessage(
				tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
		}
		else
		{
			QMessageBox::information(this, tr("Search"), tr("No matches found."));
		}
	}
}

void MainWindow::onMeasureVoiceQuality()
{
	VoiceQualityQueryEditor editor(this);

	if (editor.exec() == QDialog::Accepted)
	{
		m_last_query = editor.query();
		auto conc = editor.concordance();

		// Voice quality is undefined on instants — the kernel needs a span of
		// samples to detect pulses. Surface a single aggregate warning if any
		// match's reference target was an instant; the offending rows show NaN.
		if (auto vq = recast<VoiceQualityQuery>(m_last_query)) {
			intptr_t n_instants = vq->instant_target_count();
			if (n_instants > 0) {
				QMessageBox::warning(this, tr("Voice quality"),
					tr("%1 match(es) had an instant target and could not be measured "
					   "(voice quality requires an interval). The corresponding cells are blank.")
					.arg((qlonglong) n_instants));
			}
		}

		if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
		{
			openConcordance(conc);
			statusBar()->showMessage(
				tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
		}
		else
		{
			QMessageBox::information(this, tr("Search"), tr("No matches found."));
		}
	}
}

void MainWindow::onRecordSound()
{
	RecordSoundDialog dlg(this);
	if (dlg.exec() != QDialog::Accepted)
		return;

	auto path = dlg.savedPath();
	if (path.empty())
		return;

	// Update the last directory so the next dialog (whatever it is) starts
	// from where the recording was just saved.
	{
		QFileInfo fi(QString::fromUtf8(path.data(), int(path.size())));
		setLastDirectory(fi.absolutePath());
	}

	if (dlg.addToProject())
	{
		try
		{
			Project::get()->import_file(path);
			m_file_manager->refresh();
			updateWindowTitle();
		}
		catch (std::exception &e)
		{
			QMessageBox::warning(this, tr("Record sound"),
				tr("Recording was saved, but could not be added to the project:\n%1").arg(e.what()));
			return;
		}
	}

	statusBar()->showMessage(
		tr("Recording saved: %1").arg(QString::fromUtf8(path.data(), int(path.size()))), 5000);
}

void MainWindow::onFindSilences()
{
	if (Project::get()->get_sounds().empty())
	{
		QMessageBox::warning(this, tr("Find silences"),
			tr("There are no sound files in the project. Please add a sound file first."));
		return;
	}

	FindSilencesDialog dlg(this);
	if (dlg.exec() != QDialog::Accepted)
		return;

	auto sound = dlg.sound();
	if (!sound)
		return;

	auto det_opts     = dlg.options();
	auto layer_name   = dlg.layerName();
	auto silence_text = dlg.silenceLabel();
	auto speech_text  = dlg.speechLabel();

	// Silence detection runs on the native-rate audio. The dominant cost is loading
	// the audio buffer (first access) rather than the energy pass itself, so on large
	// files most of the wait happens inside SilenceDetector::find_speech_regions. We
	// show a wait cursor and report the outcome via the status bar.
	std::vector<SilenceDetector::Region> regions;
	QString error_msg;

	QApplication::setOverrideCursor(Qt::WaitCursor);
	try
	{
		regions = SilenceDetector::find_speech_regions(*sound, det_opts);
	}
	catch (std::exception &e)
	{
		error_msg = QString::fromUtf8(e.what());
	}
	QApplication::restoreOverrideCursor();

	if (!error_msg.isEmpty())
	{
		QMessageBox::warning(this, tr("Find silences"),
			tr("Silence detection failed:\n%1").arg(error_msg));
		return;
	}

	// Build the output annotation with one interval layer. We fill the timeline with
	// alternating silence/speech intervals (Praat's "To TextGrid (silences)" model):
	// a leading silence (if the first speech region doesn't start at 0), then each
	// speech region, separated by silence intervals, then a trailing silence (if the
	// last speech region doesn't end at the sound's duration). When no speech is
	// detected, the whole timeline becomes a single silence interval.

	auto annot = make_handle<Annotation>();
	annot->set_sound(sound);
	auto layer_label_str = String(layer_name.toUtf8().constData());
	annot->create_empty_layer(1, layer_label_str, false);

	const auto silence_str = String(silence_text.toUtf8().constData());
	const auto speech_str  = String(speech_text.toUtf8().constData());
	const double duration  = sound->duration();

	auto add_silence = [&](double start, double end) {
		if (end > start)
			annot->add_interval(1, start, end, silence_str);
	};
	auto add_speech = [&](double start, double end) {
		if (end > start)
			annot->add_interval(1, start, end, speech_str);
	};

	if (regions.empty())
	{
		add_silence(0.0, duration);
	}
	else
	{
		// Leading silence, if any.
		if (regions.front().start > 0.0)
			add_silence(0.0, regions.front().start);

		// Speech regions and the silences between them.
		for (size_t i = 0; i < regions.size(); i++)
		{
			const auto &r = regions[i];
			add_speech(r.start, r.end);

			if (i + 1 < regions.size())
			{
				const auto &next = regions[i + 1];
				if (next.start > r.end)
					add_silence(r.end, next.start);
			}
		}

		// Trailing silence, if any.
		if (regions.back().end < duration)
			add_silence(regions.back().end, duration);
	}

	auto *view = createAnnotationView(annot);
	if (view)
		addViewTab(view);

	const int n_speech = int(regions.size());
	statusBar()->showMessage(
		tr("Silence detection complete: %1 speech region(s)").arg(n_speech), 5000);
}

void MainWindow::onTranscribe()
{
	if (Project::get()->get_sounds().empty())
	{
		QMessageBox::warning(this, tr("Transcribe audio"),
			tr("There are no sound files in the project. Please add a sound file first."));
		return;
	}

	TranscribeDialog dlg(this);
	if (dlg.exec() != QDialog::Accepted)
		return;

	auto sound = dlg.sound();
	auto opts  = dlg.options();
	if (!sound)
		return;

	// Run whisper on a background thread. Both `thread` and `worker` live on the stack of this
	// slot; we join the thread via wait() before reading results, so lifetime is simple and
	// there is no need for deleteLater() dances.
	QThread thread;
	TranscriptionWorker worker(sound, opts);
	worker.moveToThread(&thread);

	QProgressDialog progress(tr("Transcribing audio..."), QString(), 0, 100, this);
	progress.setWindowTitle(tr("Transcribe"));
	progress.setWindowModality(Qt::ApplicationModal);
	progress.setMinimumDuration(0);
	progress.setAutoClose(false);
	progress.setAutoReset(false);
	progress.setValue(0);

	// Custom cancel button: the default QProgressDialog behavior on cancel hides the dialog
	// synchronously before we get a chance to keep it visible during the "canceling..."
	// window. We bypass that by owning the button and disconnecting QProgressDialog's default
	// clicked -> cancel() wiring.
	auto *cancelBtn = new QPushButton(tr("Cancel"));
	progress.setCancelButton(cancelBtn);
	disconnect(cancelBtn, &QPushButton::clicked, &progress, &QProgressDialog::cancel);
	connect(cancelBtn, &QPushButton::clicked, this, [&worker, &progress, cancelBtn]() {
		// Runs on the GUI thread (context = this). worker.cancel() touches only an atomic
		// and is safe to call from either thread; the UI updates here must stay on the GUI
		// thread, which is why we use `this` rather than `&worker` as the context object.
		worker.cancel();
		cancelBtn->setEnabled(false);
		progress.setLabelText(tr("Canceling..."));
	});

	// Drive the worker: start on thread-started; progress/finished are queued across threads.
	connect(&thread, &QThread::started,        &worker, &TranscriptionWorker::run);
	connect(&worker, &TranscriptionWorker::progress, &progress, &QProgressDialog::setValue);
	connect(&worker, &TranscriptionWorker::finished,
	        &progress, &QProgressDialog::accept, Qt::QueuedConnection);

	thread.start();
	progress.exec();

	// Ensure the worker has finished before we touch its fields.
	thread.quit();
	thread.wait();

	if (worker.succeeded())
	{
		const Layer &layer = worker.result();

		auto annot = make_handle<Annotation>();
		annot->set_sound(sound);
		annot->create_empty_layer(1, opts.layer_label, false);

		for (intptr_t i = 1; i <= layer.count(); i++)
		{
			const auto &ev = layer.events[i];
			annot->add_interval(1, ev.start, ev.end, ev.text);
		}

		auto *view = createAnnotationView(annot);
		if (view)
			addViewTab(view);

		statusBar()->showMessage(
			tr("Transcription complete: %1 segment(s)").arg((int) layer.count()), 5000);
	}
	else
	{
		QMessageBox::warning(this, tr("Transcribe"),
			tr("Transcription failed:\n%1").arg(worker.errorMessage()));
	}
}

void MainWindow::onEditLastQuery()
{
	if (!m_last_query)
	{
		QMessageBox::information(this, tr("No query"),
			tr("You must first run a query."));
		return;
	}

	auto copy = m_last_query->copy();

	if (m_last_query->is_formant_query())
	{
		auto fq = recast<FormantQuery>(copy);
		FormantQueryEditor editor(fq, this);

		if (editor.exec() == QDialog::Accepted)
		{
			m_last_query = editor.query();
			auto conc = editor.concordance();

			if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
			{
				openConcordance(conc);
				statusBar()->showMessage(
					tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
			}
			else
			{
				QMessageBox::information(this, tr("Search"), tr("No matches found."));
			}
		}
	}
	else if (m_last_query->is_pitch_query())
	{
		auto pq = recast<PitchQuery>(copy);
		PitchQueryEditor editor(pq, this);

		if (editor.exec() == QDialog::Accepted)
		{
			m_last_query = editor.query();
			auto conc = editor.concordance();

			if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
			{
				openConcordance(conc);
				statusBar()->showMessage(
					tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
			}
			else
			{
				QMessageBox::information(this, tr("Search"), tr("No matches found."));
			}
		}
	}
	else if (m_last_query->is_intensity_query())
	{
		auto iq = recast<IntensityQuery>(copy);
		IntensityQueryEditor editor(iq, this);

		if (editor.exec() == QDialog::Accepted)
		{
			m_last_query = editor.query();
			auto conc = editor.concordance();

			if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
			{
				openConcordance(conc);
				statusBar()->showMessage(
					tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
			}
			else
			{
				QMessageBox::information(this, tr("Search"), tr("No matches found."));
			}
		}
	}
	else if (m_last_query->is_spectral_moments_query())
	{
		auto sq = recast<SpectralMomentsQuery>(copy);
		SpectralMomentsQueryEditor editor(sq, this);

		if (editor.exec() == QDialog::Accepted)
		{
			m_last_query = editor.query();
			auto conc = editor.concordance();

			if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
			{
				openConcordance(conc);
				statusBar()->showMessage(
					tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
			}
			else
			{
				QMessageBox::information(this, tr("Search"), tr("No matches found."));
			}
		}
	}
	else if (m_last_query->is_voice_quality_query())
	{
		auto vq = recast<VoiceQualityQuery>(copy);
		VoiceQualityQueryEditor editor(vq, this);

		if (editor.exec() == QDialog::Accepted)
		{
			m_last_query = editor.query();
			auto conc = editor.concordance();

			if (auto vqr = recast<VoiceQualityQuery>(m_last_query)) {
				intptr_t n_instants = vqr->instant_target_count();
				if (n_instants > 0) {
					QMessageBox::warning(this, tr("Voice quality"),
						tr("%1 match(es) had an instant target and could not be measured "
						   "(voice quality requires an interval). The corresponding cells are blank.")
						.arg((qlonglong) n_instants));
				}
			}

			if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
			{
				openConcordance(conc);
				statusBar()->showMessage(
					tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
			}
			else
			{
				QMessageBox::information(this, tr("Search"), tr("No matches found."));
			}
		}
	}
	else
	{
		QueryEditor editor(copy, this);

		if (editor.exec() == QDialog::Accepted)
		{
			m_last_query = editor.query();
			auto conc = editor.concordance();

			if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
			{
				openConcordance(conc);
				statusBar()->showMessage(
					tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
			}
			else
			{
				QMessageBox::information(this, tr("Search"), tr("No matches found."));
			}
		}
	}
}


Handle<DataTable> MainWindow::selectDataTable()
{
	auto concordances = Project::get()->get_concordances();
	auto datasets = Project::get()->get_datasets();

	if (concordances.empty() && datasets.empty())
	{
		QMessageBox::warning(this, tr("No data"),
			tr("There are no data tables in the project. You must first create a concordance or import a CSV dataset."));
		return {};
	}

	QStringList items;
	// Collect all data tables in order: concordances first, then datasets.
	Array<Handle<DataTable>> tables;

	for (auto &c : concordances)
	{
		tables.append(recast<DataTable>(c));
		auto lbl = c->browser_label();
		items << QString::fromUtf8(lbl.data(), (int) lbl.size());
	}
	for (auto &d : datasets)
	{
		tables.append(recast<DataTable>(d));
		auto lbl = d->browser_label();
		items << QString::fromUtf8(lbl.data(), (int) lbl.size());
	}

	bool ok = false;
	auto chosen = QInputDialog::getItem(this, tr("Select data"),
		tr("Select a data table:"), items, 0, false, &ok);

	if (!ok || chosen.isEmpty())
		return {};

	int index = items.indexOf(chosen);
	if (index < 0)
		return {};

	return tables[index + 1]; // 1-based Array
}

void MainWindow::onAnalyzeData()
{
	auto dt = selectDataTable();
	if (!dt)
		return;

	auto analysis = make_handle<Analysis>(nullptr, std::move(dt));
	auto *view = new AnalysisView(std::move(analysis));
	view->setActiveTab(0); // Summary
	addViewTab(view);
	statusBar()->showMessage(tr("Analyze data"), 2000);
}

void MainWindow::onQuickAnalyzeData()
{
	auto *project = Project::get();
	auto concordances = project->get_concordances();
	auto datasets = project->get_datasets();

	// If the project already has data tables, use the normal flow.
	if (!concordances.empty() || !datasets.empty())
	{
		onAnalyzeData();
		return;
	}

	// No data tables: offer to open a CSV/TSV file directly.
	auto path = getOpenFileName(this, tr("Open a data file to analyze"),
		tr("Data files (*.csv *.tsv);;All files (*)"));
	if (path.isEmpty())
		return;

	try
	{
		auto phon_path = String(path.toUtf8().constData());
		project->import_file(phon_path);
		m_file_manager->refresh();

		// Retrieve the freshly imported dataset and open it for analysis.
		auto new_datasets = project->get_datasets();
		if (!new_datasets.empty())
		{
			auto dt = recast<DataTable>(new_datasets[new_datasets.size()]);
			openAnalysis(std::move(dt));
			statusBar()->showMessage(tr("Analyze data"), 2000);
		}
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), e.what());
	}
}

void MainWindow::onVisualizeData()
{
	auto dt = selectDataTable();
	if (!dt)
		return;

	auto analysis = make_handle<Analysis>(nullptr, std::move(dt));
	auto *view = new AnalysisView(std::move(analysis));
	view->setActiveTab(3); // EDA
	addViewTab(view);
	statusBar()->showMessage(tr("Visualize data"), 2000);
}


// ---------------------------------------------------------
//  Opening documents
// ---------------------------------------------------------

void MainWindow::onDocumentRequested(Document *doc)
{
	if (!doc)
		return;

	auto label = doc->label();
	auto qlabel = QString::fromUtf8(label.data(), (int) label.size());

	// Check if this document is already open in a tab.
	for (int i = 0; i < m_viewer->count(); i++)
	{
		auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(i));
		if (!panel)
			continue;

		// Check all views in the panel for a matching document.
		for (auto *v : panel->views())
		{
			if (doc->has_path()) {
				// Path-based identity for file-backed documents.
				if (v->path() == doc->path()) {
					m_viewer->setCurrentIndex(i);
					return;
				}
			} else {
				// Pointer-based identity for in-memory documents.
				if (v->document() == doc) {
					m_viewer->setCurrentIndex(i);
					return;
				}
			}
		}
	}

	try
	{

	// Handle scripts with a proper editor
	if (doc->is<Script>())
	{
		auto *script = static_cast<Script *>(doc);
		openScript(Handle<Script>(script));
		statusBar()->showMessage(tr("Opened: %1").arg(qlabel), 2000);
		return;
	}

	// Handle research notes
	if (doc->is<Note>())
	{
		auto *note = static_cast<Note *>(doc);
		openNote(Handle<Note>(note));
		statusBar()->showMessage(tr("Opened: %1").arg(qlabel), 2000);
		return;
	}

	// Handle annotation files
	if (doc->is<Annotation>())
	{
		auto *annot = static_cast<Annotation *>(doc);

		if (!annot->has_sound())
		{
			QMessageBox::warning(this, tr("Cannot open annotation"),
				tr("You must first bind this annotation to a sound file."));
			return;
		}

		auto *view = createAnnotationView(Handle<Annotation>(annot));
		if (view)
			addViewTab(view);
		return;
	}

	// Handle sound files
	if (doc->is<Sound>())
	{
		auto *sound = static_cast<Sound *>(doc);

		// Connect to the Sound loading signals for progress feedback.
		statusBar()->showMessage(tr("Loading %1...").arg(qlabel));
		m_progress_bar->setValue(0);
		m_progress_bar->setVisible(true);
		QApplication::setOverrideCursor(Qt::WaitCursor);
		QApplication::processEvents();

		auto conn1 = Sound::start_loading.connect([this](const String &, const String &, int max) {
			m_progress_bar->setMaximum(max);
			QApplication::processEvents();
		});

		auto conn2 = Sound::update_loading.connect([this](int value) {
			m_progress_bar->setValue(value);
			QApplication::processEvents();
		});

		auto *view = new SoundView(Handle<Sound>(sound));
		addViewTab(view);

		conn1.disconnect();
		conn2.disconnect();
		QApplication::restoreOverrideCursor();
		m_progress_bar->setVisible(false);
		statusBar()->showMessage(tr("Opened: %1").arg(qlabel), 2000);
		return;
	}

	// Handle query files: open in the query editor dialog, not as a tab.
	if (doc->is<Query>())
	{
		auto *query_doc = static_cast<Query *>(doc);

		if (query_doc->is_formant_query())
		{
			auto fq = recast<FormantQuery>(Handle<Query>(query_doc));
			FormantQueryEditor editor(fq, this);

			if (editor.exec() == QDialog::Accepted)
			{
				m_last_query = editor.query();
				auto conc = editor.concordance();

				if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
				{
					openConcordance(conc);
					statusBar()->showMessage(
						tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
				}
				else
				{
					QMessageBox::information(this, tr("Search"), tr("No matches found."));
				}
			}
		}
		else if (query_doc->is_pitch_query())
		{
			auto pq = recast<PitchQuery>(Handle<Query>(query_doc));
			PitchQueryEditor editor(pq, this);

			if (editor.exec() == QDialog::Accepted)
			{
				m_last_query = editor.query();
				auto conc = editor.concordance();

				if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
				{
					openConcordance(conc);
					statusBar()->showMessage(
						tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
				}
				else
				{
					QMessageBox::information(this, tr("Search"), tr("No matches found."));
				}
			}
		}
		else if (query_doc->is_intensity_query())
		{
			auto iq = recast<IntensityQuery>(Handle<Query>(query_doc));
			IntensityQueryEditor editor(iq, this);

			if (editor.exec() == QDialog::Accepted)
			{
				m_last_query = editor.query();
				auto conc = editor.concordance();

				if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
				{
					openConcordance(conc);
					statusBar()->showMessage(
						tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
				}
				else
				{
					QMessageBox::information(this, tr("Search"), tr("No matches found."));
				}
			}
		}
		else if (query_doc->is_spectral_moments_query())
		{
			auto sq = recast<SpectralMomentsQuery>(Handle<Query>(query_doc));
			SpectralMomentsQueryEditor editor(sq, this);

			if (editor.exec() == QDialog::Accepted)
			{
				m_last_query = editor.query();
				auto conc = editor.concordance();

				if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
				{
					openConcordance(conc);
					statusBar()->showMessage(
						tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
				}
				else
				{
					QMessageBox::information(this, tr("Search"), tr("No matches found."));
				}
			}
		}
		else if (query_doc->is_voice_quality_query())
		{
			auto vq = recast<VoiceQualityQuery>(Handle<Query>(query_doc));
			VoiceQualityQueryEditor editor(vq, this);

			if (editor.exec() == QDialog::Accepted)
			{
				m_last_query = editor.query();
				auto conc = editor.concordance();

				if (auto vqr = recast<VoiceQualityQuery>(m_last_query)) {
					intptr_t n_instants = vqr->instant_target_count();
					if (n_instants > 0) {
						QMessageBox::warning(this, tr("Voice quality"),
							tr("%1 match(es) had an instant target and could not be measured "
							   "(voice quality requires an interval). The corresponding cells are blank.")
							.arg((qlonglong) n_instants));
					}
				}

				if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
				{
					openConcordance(conc);
					statusBar()->showMessage(
						tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
				}
				else
				{
					QMessageBox::information(this, tr("Search"), tr("No matches found."));
				}
			}
		}
		else
		{
			QueryEditor editor(Handle<Query>(query_doc), this);

			if (editor.exec() == QDialog::Accepted)
			{
				m_last_query = editor.query();
				auto conc = editor.concordance();

				if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
				{
					openConcordance(conc);
					statusBar()->showMessage(
						tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
				}
				else
				{
					QMessageBox::information(this, tr("Search"), tr("No matches found."));
				}
			}
		}
		return;
	}

	// Handle concordance files
	if (doc->is<Concordance>())
	{
		auto *conc_doc = static_cast<Concordance *>(doc);
		openConcordance(Handle<Concordance>(conc_doc));
		statusBar()->showMessage(tr("Opened: %1").arg(qlabel), 2000);
		return;
	}

	// Handle analysis files
	if (doc->is<Analysis>())
	{
		auto *analysis = static_cast<Analysis *>(doc);
		openAnalysis(Handle<Analysis>(analysis));
		statusBar()->showMessage(tr("Opened: %1").arg(qlabel), 2000);
		return;
	}
	// Handle dataset files
	if (doc->is<Dataset>())
	{
		auto *ds = static_cast<Dataset *>(doc);
		openDataset(Handle<Dataset>(ds));
		statusBar()->showMessage(tr("Opened: %1").arg(qlabel), 2000);
		return;
	}

	// Fallback placeholder for other document types
	auto &path = doc->path();
	auto qpath = QString::fromUtf8(path.data(), (int) path.size());
	auto *placeholder = new QLabel(tr("<h3>%1</h3><p>%2</p>").arg(qlabel, qpath));
	placeholder->setAlignment(Qt::AlignCenter);

	int tabIndex = m_viewer->addTab(placeholder, qlabel);
	m_viewer->setCurrentIndex(tabIndex);

	statusBar()->showMessage(tr("Opened: %1").arg(qlabel), 2000);

	} // try
	catch (std::exception &e)
	{
		QApplication::restoreOverrideCursor();
		m_progress_bar->setVisible(false);
		QMessageBox::critical(this, tr("Error opening file"),
			tr("Could not open \"%1\":\n%2").arg(qlabel, QString::fromUtf8(e.what())));
	}
	catch (...)
	{
		QApplication::restoreOverrideCursor();
		m_progress_bar->setVisible(false);
		QMessageBox::critical(this, tr("Error opening file"),
			tr("Could not open \"%1\": an unexpected error occurred.").arg(qlabel));
	}
}


// ---------------------------------------------------------
//  Tab / View helpers
// ---------------------------------------------------------

ViewPanel *MainWindow::addViewTab(View *view)
{
	auto *panel = new ViewPanel(view, m_viewer);

	connect(panel, &ViewPanel::titleChanged, [this, panel](const QString &title) {
		int index = m_viewer->indexOf(panel);
		if (index >= 0)
			m_viewer->setTabText(index, title);
		// Update the project dock title (shows * when project is modified)
		// and refresh the file manager (shows * on modified files).
		updateWindowTitle();
		m_file_manager->refresh();
	});

	// When a view is detached from a split, wrap it in its own tab.
	connect(panel, &ViewPanel::viewDetached, this, [this](View *detached) {
		addViewTab(detached);
	});

	// If the last view is removed from a split, close the (now empty) tab.
	connect(panel, &ViewPanel::lastViewClosed, this, [this, panel]() {
		int index = m_viewer->indexOf(panel);
		if (index >= 0)
			m_viewer->removeTab(index);
		panel->deleteLater();
	});

	// Keep undo/redo actions in sync with the view's command processor.
	connect(view, &View::undoRedoChanged, this, &MainWindow::updateUndoRedoState);

	// Refresh the file manager when a view registers a new file with the project.
	connect(view, &View::addedToProject, this, [this]() {
		m_file_manager->refresh();
	});

	int tabIndex = m_viewer->addTab(panel, panel->label());
	m_viewer->setCurrentIndex(tabIndex);

	return panel;
}

void MainWindow::openScript(const Handle<Script> &script)
{
	auto *view = new ScriptView(m_runtime, m_console, script);
	addViewTab(view);
}

void MainWindow::openNote(const Handle<Note> &note)
{
	auto *view = new NoteView(note);
	addViewTab(view);
}

void MainWindow::openDataset(Handle<Dataset> ds)
{
	auto *view = new DatasetView(ds);

	connect(view, &DatasetView::requestAnalysis,
		this, static_cast<void (MainWindow::*)(Handle<DataTable>)>(&MainWindow::openAnalysis));

	connect(view, &DatasetView::datasetCreated,
		this, &MainWindow::openDataset);

	connect(view, &DatasetView::concordanceCreated,
		this, &MainWindow::openConcordance);

	addViewTab(view);
}

void MainWindow::openConcordance(Handle<Concordance> conc)
{
	auto *view = new ConcordanceView(conc);

	connect(view, &ConcordanceView::openAnnotation,
		this, [this, view](const Handle<Annotation> &annot, intptr_t layer, double start, double end, bool split)
	{
		if (split)
		{
			// ── Split view: open annotation beside the concordance ──
			auto *panel = findPanelForView(view);
			if (!panel)
				return;

			// Check if this annotation is already in the same panel.
			AnnotationView *av = nullptr;
			for (auto *v : panel->views())
			{
				auto *candidate = qobject_cast<AnnotationView *>(v);
				if (candidate && candidate->path() == annot->path())
				{
					av = candidate;
					break;
				}
			}

			if (!av)
			{
				// Check if this annotation is open in another tab.
				for (int i = 0; i < m_viewer->count(); i++)
				{
					auto *other = qobject_cast<ViewPanel *>(m_viewer->widget(i));
					if (!other || other == panel)
						continue;

					for (auto *v : other->views())
					{
						auto *candidate = qobject_cast<AnnotationView *>(v);
						if (candidate && candidate->path() == annot->path())
						{
							// Detach from the other panel and move here.
							other->detachView(candidate);
							panel->addView(candidate, Qt::Horizontal);
							av = candidate;
							break;
						}
					}
					if (av) break;
				}
			}

			if (!av)
			{
				// Create a new AnnotationView and add it to the concordance's panel.
				av = createAnnotationView(annot);
				if (av)
				{
					connect(av, &View::undoRedoChanged, this, &MainWindow::updateUndoRedoState);
					panel->addView(av, Qt::Horizontal);
				}
			}

			if (av)
			{
				av->openSelection(layer, start, end);
				m_viewer->setCurrentWidget(panel);
			}
		}
		else
		{
			// ── New tab: open annotation in its own tab ──
			onDocumentRequested(annot.get());

			// Find the AnnotationView in the current tab and navigate to the match.
			if (auto *panel = currentPanel())
			{
				for (auto *v : panel->views())
				{
					if (auto *av = qobject_cast<AnnotationView *>(v))
					{
						av->openSelection(layer, start, end);
						break;
					}
				}
			}
		}
	});

	connect(view, &ConcordanceView::requestAnalysis,
		this, static_cast<void (MainWindow::*)(Handle<DataTable>)>(&MainWindow::openAnalysis));

	connect(view, &ConcordanceView::concordanceCreated,
		this, &MainWindow::openConcordance);

	addViewTab(view);
}

void MainWindow::openAnalysis(Handle<DataTable> source)
{
	auto analysis = make_handle<Analysis>(nullptr, std::move(source));
	auto *view = new AnalysisView(std::move(analysis));
	connect(view, &AnalysisView::requestOpenSourceRow,
	        this, &MainWindow::revealSourceRow);
	addViewTab(view);
}

void MainWindow::openAnalysis(Handle<Analysis> analysis)
{
	auto *view = new AnalysisView(std::move(analysis));
	connect(view, &AnalysisView::requestOpenSourceRow,
	        this, &MainWindow::revealSourceRow);
	addViewTab(view);
}

void MainWindow::revealSourceRow(Handle<DataTable> source, intptr_t source_row)
{
	if (!source || source_row < 0)
		return;

	Document *doc = source.get();

	// Search every panel/view for an existing view of this DataTable. We use
	// pointer-based identity (matches onDocumentRequested's in-memory path)
	// because the source is always a live Handle.
	for (int i = 0; i < m_viewer->count(); i++)
	{
		auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(i));
		if (!panel)
			continue;

		for (auto *v : panel->views())
		{
			if (v->document() != doc)
				continue;

			m_viewer->setCurrentIndex(i);

			bool ok = false;
			if (auto *dv = qobject_cast<DatasetView *>(v))
				ok = dv->selectSourceRow((int) source_row);
			else if (auto *cv = qobject_cast<ConcordanceView *>(v))
				ok = cv->selectSourceRow((int) source_row);

			if (!ok) {
				QMessageBox::information(this, tr("Row not visible"),
					tr("The selected observation is hidden by the current filter "
					   "in the data view. Clear the filter to inspect this row."));
			}
			return;
		}
	}

	// No existing view — open a new one keyed on the concrete source type,
	// then select the row. openDataset / openConcordance schedule the new
	// tab as the current widget; the new view is the most recently added.
	if (doc->is<Dataset>()) {
		auto *ds = static_cast<Dataset *>(doc);
		openDataset(Handle<Dataset>(ds));
	}
	else if (doc->is<Concordance>()) {
		auto *conc = static_cast<Concordance *>(doc);
		openConcordance(Handle<Concordance>(conc));
	}
	else {
		return;  // Unknown source type — nothing to open.
	}

	auto *panel = qobject_cast<ViewPanel *>(m_viewer->currentWidget());
	if (!panel) return;
	for (auto *v : panel->views()) {
		if (auto *dv = qobject_cast<DatasetView *>(v))
			dv->selectSourceRow((int) source_row);
		else if (auto *cv = qobject_cast<ConcordanceView *>(v))
			cv->selectSourceRow((int) source_row);
	}
}

AnnotationView *MainWindow::createAnnotationView(const Handle<Annotation> &annot)
{
	auto label = annot->label();
	auto qlabel = QString::fromUtf8(label.data(), (int) label.size());

	statusBar()->showMessage(tr("Loading %1...").arg(qlabel));
	m_progress_bar->setValue(0);
	m_progress_bar->setVisible(true);
	QApplication::setOverrideCursor(Qt::WaitCursor);
	QApplication::processEvents();

	auto conn1 = Sound::start_loading.connect([this](const String &, const String &, int max) {
		m_progress_bar->setMaximum(max);
		QApplication::processEvents();
	});

	auto conn2 = Sound::update_loading.connect([this](int value) {
		m_progress_bar->setValue(value);
		QApplication::processEvents();
	});

	auto *view = new AnnotationView(annot);

	conn1.disconnect();
	conn2.disconnect();
	QApplication::restoreOverrideCursor();
	m_progress_bar->setVisible(false);
	statusBar()->showMessage(tr("Opened: %1").arg(qlabel), 2000);

	return view;
}

ViewPanel *MainWindow::findPanelForView(View *view) const
{
	for (int i = 0; i < m_viewer->count(); i++)
	{
		auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(i));
		if (!panel)
			continue;

		for (auto *v : panel->views())
		{
			if (v == view)
				return panel;
		}
	}
	return nullptr;
}

ViewPanel *MainWindow::currentPanel() const
{
	return qobject_cast<ViewPanel *>(m_viewer->currentWidget());
}

View *MainWindow::currentView() const
{
	auto *panel = currentPanel();
	return panel ? panel->activeView() : nullptr;
}


// ---------------------------------------------------------
//  View-level actions (forwarded to active view)
// ---------------------------------------------------------

void MainWindow::onSaveCurrentView()
{
	if (auto *v = currentView())
		v->save();
}

void MainWindow::onExecuteCurrentView()
{
	if (auto *v = currentView())
		v->execute();
}

void MainWindow::onEscapeCurrentView()
{
	if (auto *v = currentView())
		v->escape();
}

void MainWindow::onFind()
{
	if (auto *v = currentView())
		v->find();
}

void MainWindow::onReplace()
{
	if (auto *v = currentView())
		v->replace();
}


// ---------------------------------------------------------
//  Window geometry persistence
// ---------------------------------------------------------

void MainWindow::saveWindowState()
{
	QSettings settings;
	settings.setValue("mainwindow/geometry", QMainWindow::saveGeometry());
	settings.setValue("mainwindow/state", QMainWindow::saveState());
}

void MainWindow::restoreWindowState()
{
	QSettings settings;
	auto geometry = settings.value("mainwindow/geometry").toByteArray();
	auto state = settings.value("mainwindow/state").toByteArray();

	if (!geometry.isEmpty())
		QMainWindow::restoreGeometry(geometry);
	if (!state.isEmpty())
		QMainWindow::restoreState(state);
}


// ---------------------------------------------------------
//  Scripting shell functions
// ---------------------------------------------------------

void MainWindow::setShellFunctions()
{
	// ── Message dialogs ──

	auto warning1 = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &msg = cast<String>(args[0]);
		QMessageBox::warning(this, tr("Warning"),
			QString::fromUtf8(msg.data(), (int) msg.size()));
		return Variant();
	};

	auto warning2 = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &msg = cast<String>(args[0]);
		auto &title = cast<String>(args[1]);
		QMessageBox::warning(this,
			QString::fromUtf8(title.data(), (int) title.size()),
			QString::fromUtf8(msg.data(), (int) msg.size()));
		return Variant();
	};

	auto alert1 = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &msg = cast<String>(args[0]);
		QMessageBox::critical(this, tr("Error"),
			QString::fromUtf8(msg.data(), (int) msg.size()));
		return Variant();
	};

	auto alert2 = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &msg = cast<String>(args[0]);
		auto &title = cast<String>(args[1]);
		QMessageBox::critical(this,
			QString::fromUtf8(title.data(), (int) title.size()),
			QString::fromUtf8(msg.data(), (int) msg.size()));
		return Variant();
	};

	auto info1 = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &msg = cast<String>(args[0]);
		QMessageBox::information(this, tr("Information"),
			QString::fromUtf8(msg.data(), (int) msg.size()));
		return Variant();
	};

	auto info2 = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &msg = cast<String>(args[0]);
		auto &title = cast<String>(args[1]);
		QMessageBox::information(this,
			QString::fromUtf8(title.data(), (int) title.size()),
			QString::fromUtf8(msg.data(), (int) msg.size()));
		return Variant();
	};

	auto ask1 = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &msg = cast<String>(args[0]);
		auto answer = QMessageBox::question(this, tr("Question"),
			QString::fromUtf8(msg.data(), (int) msg.size()),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		return (answer == QMessageBox::Yes);
	};

	auto ask2 = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &msg = cast<String>(args[0]);
		auto &title = cast<String>(args[1]);
		auto answer = QMessageBox::question(this,
			QString::fromUtf8(title.data(), (int) title.size()),
			QString::fromUtf8(msg.data(), (int) msg.size()),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		return (answer == QMessageBox::Yes);
	};

	// ── File dialogs ──

	auto open_file_dialog = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &msg = cast<String>(args[0]);
		auto path = getOpenFileName(this,
			QString::fromUtf8(msg.data(), (int) msg.size()));
		if (path.isEmpty()) return Variant();
		return String(path.toUtf8().constData());
	};

	auto open_files_dialog = [this](Runtime &rt, std::span<Variant> args) -> Variant {
		auto &msg = cast<String>(args[0]);
		auto paths = getOpenFileNames(this,
			QString::fromUtf8(msg.data(), (int) msg.size()));
		if (paths.isEmpty()) return Variant();
		Array<Variant> result;
		for (auto &p : paths) {
			result.append(String(p.toUtf8().constData()));
		}
		std::sort(result.begin(), result.end());
		return make_handle<List>(&rt, std::move(result));
	};

	auto open_directory_dialog = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &msg = cast<String>(args[0]);
		auto path = getExistingDirectory(this,
			QString::fromUtf8(msg.data(), (int) msg.size()));
		if (path.isEmpty()) return Variant();
		return String(path.toUtf8().constData());
	};

	auto save_file_dialog = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &msg = cast<String>(args[0]);
		auto path = getSaveFileName(this,
			QString::fromUtf8(msg.data(), (int) msg.size()));
		if (path.isEmpty()) return Variant();
		return String(path.toUtf8().constData());
	};

	auto input = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &label = cast<String>(args[0]);
		auto &title = cast<String>(args[1]);
		auto &value = cast<String>(args[2]);
		bool ok;
		auto result = QInputDialog::getText(this,
			QString::fromUtf8(title.data(), (int) title.size()),
			QString::fromUtf8(label.data(), (int) label.size()),
			QLineEdit::Normal,
			QString::fromUtf8(value.data(), (int) value.size()),
			&ok);
		if (!ok) return Variant();
		return String(result.toUtf8().constData());
	};

	// ── Progress dialog ──

	auto create_progress_dialog = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &msg = cast<String>(args[0]);
		auto &title = cast<String>(args[1]);
		auto count = (int) cast<intptr_t>(args[2]);
		if (count <= 0) {
			QMessageBox::warning(this, tr("Invalid value"),
				tr("Count value must be positive in progress dialog"));
		}
		else
		{
			m_script_progress = std::make_unique<QProgressDialog>(
				QString::fromUtf8(msg.data(), (int) msg.size()),
				tr("Cancel"), 0, count, this);
			m_script_progress->setWindowTitle(
				QString::fromUtf8(title.data(), (int) title.size()));
			m_script_progress->setWindowModality(Qt::ApplicationModal);
		}
		return Variant();
	};

	auto update_progress_dialog = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto value = (int) cast<intptr_t>(args[0]);
		if (!m_script_progress) return Variant();
		if (value <= 0 || value > m_script_progress->maximum())
		{
			QMessageBox::warning(this, tr("Invalid value"),
				tr("Value out of range in progress dialog"));
			m_script_progress.reset();
			return Variant();
		}
		m_script_progress->setValue(value);
		if (m_script_progress->wasCanceled() || value >= m_script_progress->maximum()) {
			m_script_progress.reset();
			return false;
		}
		return true;
	};

	// ── View text ──

	auto view_text = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &path = cast<String>(args[0]);
		auto &title = cast<String>(args[1]);
		auto content = File::read_all(path);

		auto *dlg = new QDialog(this);
		dlg->setWindowTitle(QString::fromUtf8(title.data(), (int) title.size()));
		dlg->resize(600, 400);
		auto *layout = new QVBoxLayout(dlg);
		auto *text_edit = new QPlainTextEdit(dlg);
		text_edit->setReadOnly(true);
		text_edit->setPlainText(QString::fromUtf8(content.data(), (int) content.size()));
		layout->addWidget(text_edit);
		auto *close_btn = new QPushButton(tr("Close"), dlg);
		layout->addWidget(close_btn, 0, Qt::AlignRight);
		connect(close_btn, &QPushButton::clicked, dlg, &QDialog::accept);
		dlg->exec();
		delete dlg;
		return Variant();
	};

	// ── Browser ──

	auto launch_browser = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &url = cast<String>(args[0]);
		QDesktopServices::openUrl(QUrl(QString::fromUtf8(url.data(), (int) url.size())));
		return Variant();
	};

	// ── Plugin queries ──

	auto get_plugin_version = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &name = cast<String>(args[0]);
		auto *plugin = findPlugin(name);
		if (plugin) return plugin->version();
		return Variant();
	};

	auto get_plugin_resource = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &plugin_name = cast<String>(args[0]);
		auto &name = cast<String>(args[1]);
		auto resource = filesystem::join(Settings::plugin_directory(), plugin_name, "Resources", name);
		return std::move(resource);
	};

	// ── View-level queries ──

	auto get_current_sound = [this](Runtime &, std::span<Variant>) -> Variant {
		auto *view = currentView();
		if (auto *sv = qobject_cast<SoundView *>(view)) {
			return sv->sound();
		}
		return Variant();
	};

	auto get_current_annot = [this](Runtime &, std::span<Variant>) -> Variant {
		auto *view = currentView();
		if (auto *av = qobject_cast<AnnotationView *>(view)) {
			return av->annotation();
		}
		return Variant();
	};

	auto get_visible_channels = [this](Runtime &rt, std::span<Variant>) -> Variant {
		Array<Variant> result;
		auto *view = currentView();
		if (auto *sv = qobject_cast<SoundView *>(view)) {
			for (int ch : sv->visibleChannels()) {
				result.append((intptr_t) ch);
			}
		}
		return make_handle<List>(&rt, std::move(result));
	};

	auto get_window_duration = [this](Runtime &, std::span<Variant>) -> Variant {
		auto *view = currentView();
		if (auto *sv = qobject_cast<SoundView *>(view)) {
			return sv->timeModel()->windowDuration();
		}
		return 0.0;
	};

	auto get_selection_duration = [this](Runtime &, std::span<Variant>) -> Variant {
		auto *view = currentView();
		if (auto *sv = qobject_cast<SoundView *>(view)) {
			auto *model = sv->timeModel();
			if (model->hasSpanSelection()) {
				return model->selectionEnd() - model->selectionStart();
			}
		}
		return 0.0;
	};

	// ── Close current view ──

	auto close_current_view = [this](Runtime &, std::span<Variant>) -> Variant {
		int idx = m_viewer->currentIndex();
		if (idx >= 0) {
			m_viewer->removeTab(idx);
		}
		return Variant();
	};

	// ── User dialog ──

	auto create_dialog1 = [this](Runtime &rt, std::span<Variant> args) -> Variant {
		auto &s = cast<String>(args[0]);
		UserDialog dlg(this, rt, s);
		if (dlg.exec() == QDialog::Accepted) {
			return dlg.getResult();
		}
		return Variant();
	};

	auto create_dialog2 = [this](Runtime &rt, std::span<Variant> args) -> Variant {
		Json js(args[0].resolve());
		UserDialog dlg(this, rt, js);
		if (dlg.exec() == QDialog::Accepted) {
			return dlg.getResult();
		}
		return Variant();
	};

	// ── phon module functions ──

	auto get_version = [](Runtime &, std::span<Variant>) -> Variant {
		return String(utils::get_version());
	};

	auto get_date = [](Runtime &, std::span<Variant>) -> Variant {
		return String(utils::get_date());
	};

	auto get_supported_sound_formats = [](Runtime &rt, std::span<Variant>) -> Variant {
		Array<Variant> sounds;
		for (auto &s : Sound::supported_sound_format_names()) {
			sounds.append(s);
		}
		return make_handle<List>(&rt, std::move(sounds));
	};

	// ── Register everything ──

#define CLS(T) get_class<T>()
	m_runtime.add_global("view_text", view_text, { CLS(String), CLS(String) });
	m_runtime.add_global("warning", warning1, { CLS(String) });
	m_runtime.add_global("warning", warning2, { CLS(String), CLS(String) });
	m_runtime.add_global("alert", alert1, { CLS(String) });
	m_runtime.add_global("alert", alert2, { CLS(String), CLS(String) });
	m_runtime.add_global("info", info1, { CLS(String) });
	m_runtime.add_global("info", info2, { CLS(String), CLS(String) });
	m_runtime.add_global("ask", ask1, { CLS(String) });
	m_runtime.add_global("ask", ask2, { CLS(String), CLS(String) });
	m_runtime.add_global("open_file_dialog", open_file_dialog, { CLS(String) });
	m_runtime.add_global("open_files_dialog", open_files_dialog, { CLS(String) });
	m_runtime.add_global("open_directory_dialog", open_directory_dialog, { CLS(String) });
	m_runtime.add_global("save_file_dialog", save_file_dialog, { CLS(String) });
	m_runtime.add_global("get_input", input, { CLS(String), CLS(String), CLS(String) });
	m_runtime.add_global("get_plugin_version", get_plugin_version, { CLS(String) });
	m_runtime.add_global("get_plugin_resource", get_plugin_resource, { CLS(String), CLS(String) });
	m_runtime.add_global("create_dialog", create_dialog1, { CLS(String) });
	m_runtime.add_global("create_dialog", create_dialog2, { CLS(Table) });
	m_runtime.add_global("create_progress_dialog", create_progress_dialog, { CLS(String), CLS(String), CLS(intptr_t) });
	m_runtime.add_global("update_progress_dialog", update_progress_dialog, { CLS(intptr_t) });
	m_runtime.add_global("launch_browser", launch_browser, { CLS(String) });
	m_runtime.add_global("get_current_sound", get_current_sound, { });
	m_runtime.add_global("get_visible_channels", get_visible_channels, { });
	m_runtime.add_global("get_current_annotation", get_current_annot, { });
	m_runtime.add_global("get_window_duration", get_window_duration, { });
	m_runtime.add_global("get_selection_duration", get_selection_duration, { });

	auto *rt = &m_runtime;
	auto &phon = cast<Module>(m_runtime["phon"]);
	phon.define(rt, "get_version", get_version, { });
	phon.define(rt, "get_date", get_date, { });
	phon.define(rt, "get_supported_sound_formats", get_supported_sound_formats, { });
	phon.define(rt, "close_current_view", close_current_view, { });
#undef CLS
}


// ---------------------------------------------------------
//  Praat integration
// ---------------------------------------------------------

void MainWindow::setupPraat()
{
	String praat_path;
	try {
		praat_path = Settings::get_string("praat_path");
	} catch (...) {}

	// If no path is configured, try the platform default.
	if (praat_path.empty())
	{
#if defined(Q_OS_WIN)
		String default_path("C:\\Program Files\\Praat.exe");
#elif defined(Q_OS_MAC)
		String default_path("/Applications/Praat.app/Contents/MacOS/Praat");
#else
		String default_path("/usr/bin/praat");
#endif
		if (filesystem::exists(default_path))
		{
			praat_path = default_path;
			Settings::set_value("praat_path", praat_path);
		}
	}

	if (praat_path.empty())
	{
		praat::send_script = nullptr;
		praat::open_files = nullptr;
		return;
	}

	auto qpath = QString::fromUtf8(praat_path.data(), (int) praat_path.size());

#if defined(Q_OS_MAC)
	// On macOS, send_script must route Praat's --send invocation through
	// /usr/bin/open rather than calling the binary inside
	// Praat.app/Contents/MacOS/ directly.
	//
	// --send is Praat's documented IPC mechanism — the in-binary replacement
	// for the sendpraat tool, needed because sendpraat's X11 implementation
	// does not work under Wayland. On macOS, --send forwards the script to
	// a running Praat instance via Apple Events.
	//
	// Invoking the raw binary bypasses Launch Services. The spawned Praat
	// process is not properly registered, and the --send Apple-Event
	// forwarding silently fails: the running Praat still receives enough
	// activation traffic to come to the front, but the script payload never
	// reaches it. The unregistered helper process flashes briefly in the
	// Dock and exits. This matches the reported symptom ("another instance
	// is briefly opened and closed in the Finder, files are not sent").
	//
	// Routing through `open -n -a <bundle> --args --send <script>` fixes
	// this:
	//
	//   * -n forces Launch Services to spawn a new Praat process even when
	//     one is already running. This is required: without -n, `open`
	//     would silently activate the existing instance and drop --args, so
	//     --send would never be seen on a command line.
	//
	//   * The newly spawned process is properly LS-registered, so its
	//     --send handler can forward the script via Apple Events to the
	//     running Praat, then exit.
	//
	//   * When Praat is not running, the new process simply becomes the
	//     primary instance and executes the script itself, with Launch
	//     Services giving it correct window-server focus (also fixing the
	//     reported "Praat doesn't get focused" symptom on first launch).
	//
	// The helper process still flashes briefly in the Dock when Praat is
	// already running — that is Praat's own --send design, not something we
	// can eliminate from this side without abandoning --send entirely.
	QString bundle;
	{
		// Derive the .app bundle from the configured binary path. The
		// preferences dialog asks the user for the Praat executable, which on
		// a standard install is .../Praat.app/Contents/MacOS/Praat — strip
		// from /Contents/MacOS/ onward and keep the ".app" suffix.
		const QString marker = QStringLiteral(".app/Contents/MacOS/");
		int idx = qpath.indexOf(marker);
		if (idx >= 0)
			bundle = qpath.left(idx + 4);
	}

	if (!bundle.isEmpty())
	{
		praat::send_script = [bundle](const String &script_path) {
			auto qscript = QString::fromUtf8(script_path.data(), (int) script_path.size());
			QStringList args;
			args << QStringLiteral("-n")
			     << QStringLiteral("-a") << bundle
			     << QStringLiteral("--args")
			     << QStringLiteral("--send") << qscript;
			QProcess::startDetached(QStringLiteral("/usr/bin/open"), args);
		};
	}
	else
	{
		// Fallback for non-bundle configurations (e.g. a locally built
		// praat binary not installed as an .app). Direct invocation is the
		// only option; users on this path will still see the original
		// behaviour, but it is an uncommon configuration.
		praat::send_script = [qpath](const String &script_path) {
			auto qscript = QString::fromUtf8(script_path.data(), (int) script_path.size());
			QProcess::startDetached(qpath, {QStringLiteral("--send"), qscript});
		};
	}
#else
	praat::send_script = [qpath](const String &script_path) {
		auto qscript = QString::fromUtf8(script_path.data(), (int) script_path.size());
		QProcess::startDetached(qpath, {QStringLiteral("--send"), qscript});
	};
#endif

	// open_files is currently a legacy callback that is wired but never called
	// — all three "Open in Praat" context menus (sound, annotation, concordance)
	// go through the high-level commands open_sound / open_textgrid / open_at_time,
	// which build a Praat script and route it through send_script above. The
	// callback is kept for now for API stability; if it ever becomes live, its
	// implementation should be migrated to a script + send_script, matching the
	// pattern of the high-level commands (and sharing the macOS Launch-Services
	// fix above for free).
	praat::open_files = [qpath](const std::vector<String> &paths) {
		QStringList args;
		args << QStringLiteral("--open");
		for (auto &p : paths)
			args << QString::fromUtf8(p.data(), (int) p.size());
		QProcess::startDetached(qpath, args);
	};
}


// ---------------------------------------------------------
//  Plugin support
// ---------------------------------------------------------

void MainWindow::postInitialize()
{
	// Register scripting functions before loading plugins, so plugin
	// scripts can call GUI functions like info(), warning(), etc.
	setShellFunctions();

	// Set up Praat integration (praat --send / praat --open).
	setupPraat();

	// Install the whisper/ggml log sink. Routes all diagnostic output to the OutputPanel
	// when the "whisper_log" setting is true; drops silently otherwise. Installed here
	// (not in the worker) so that whisper is silent from the first model load onward,
	// without a startup window where logs could leak to stderr. The sink lambda runs on
	// the worker thread when whisper is in flight, so the QWidget update is marshaled to
	// the GUI thread via a queued invokeMethod.
	Transcriber::set_log_sink([](const String &msg) {
		bool enabled = false;
		try {
			enabled = Settings::get_boolean("whisper_log");
		} catch (...) { return; }
		if (!enabled) return;

		auto *panel = OutputPanel::instance();
		if (!panel) return;

		QString qs = QString::fromUtf8(msg.data(), (int) msg.size());
		QMetaObject::invokeMethod(panel, [panel, qs]() {
			panel->appendText(qs);
		}, Qt::QueuedConnection);
	});

	// Load system plugins/scripts first, then user plugins/scripts.
	auto resources_dir = Settings::resources_directory();
	auto user_dir = Settings::settings_directory();
	loadPluginsAndScripts(resources_dir);
	loadPluginsAndScripts(user_dir);

	// Autoload the most recent project if the preference is enabled — but
	// skip it entirely when argv supplied files. Cold-start "open file with
	// Phonometrica" should land in that file, not in the previous session's
	// project. setPendingArgvPaths() ran before postInitialize(), so at this
	// point the queued openPaths() lambda has not fired yet (that needs the
	// event loop) and m_pending_argv_paths is still populated.
	if (Settings::get_boolean("autoload") && m_pending_argv_paths.isEmpty())
	{
		try
		{
			auto &lst = Settings::get_list("recent_projects");
			if (!lst.empty())
			{
				auto path = cast<String>(lst[1]);
				Project::get()->open(path);
				m_file_manager->refresh();
				updateRecentProjects(path);
				updateWindowTitle();
				statusBar()->showMessage(tr("Loaded recent project"), 3000);
			}
		}
		catch (std::exception &e)
		{
			QMessageBox::warning(this, tr("Error"),
				tr("Could not open most recent project: %1").arg(e.what()));
		}

		// Restore views that were open in the previous session.
		if (Settings::get_boolean("restore_views"))
		{
			try
			{
				auto &views = Settings::get_list("recent_views");
				for (auto &view : views)
				{
					auto path = cast<String>(view);
					auto vfile = Project::get()->get(path);
					if (vfile)
						onDocumentRequested(vfile.get());
				}
				auto sel = Settings::get_int("selected_view");
				if (sel >= 0 && sel < m_viewer->count())
					m_viewer->setCurrentIndex(sel);
			}
			catch (...)
			{
				// No saved views or stale paths — silently ignore.
			}
		}
	}
}

void MainWindow::loadPluginsAndScripts(const String &root)
{
	String plugin_dir = filesystem::join(root, "Plugins");

	if (filesystem::exists(plugin_dir))
	{
		auto files = filesystem::list_directory(plugin_dir);
		std::sort(files.begin(), files.end());

		for (auto &name : files)
		{
			String path = filesystem::join(plugin_dir, name);
			if (filesystem::is_directory(path))
			{
				try
				{
					loadPlugin(path);
				}
				catch (std::exception &e)
				{
					QMessageBox::critical(this, tr("Plugin initialization failed"),
						QString::fromUtf8(e.what()));
				}
			}
		}
	}

	String scripts_dir = filesystem::join(root, "Scripts");

	if (filesystem::exists(scripts_dir))
	{
		auto files = filesystem::list_directory(scripts_dir);
		std::sort(files.begin(), files.end());

		for (auto &name : files)
		{
			String path = filesystem::join(scripts_dir, name);
			if (filesystem::is_file(path))
			{
				try
				{
					m_runtime.do_file(path);
				}
				catch (std::exception &e)
				{
					auto msg = utils::format("Error in script %: %", path, e.what());
					QMessageBox::critical(this, tr("Startup script error"),
						QString::fromUtf8(msg.data(), (int) msg.size()));
				}
			}
		}
	}
}

void MainWindow::loadPlugin(const String &path)
{
	// Skip plugins with an ignore.txt marker.
	auto ignore_path = filesystem::join(path, "ignore.txt");
	if (filesystem::exists(ignore_path))
		return;

	statusBar()->showMessage(tr("Loading plugin %1...").arg(
		QString::fromUtf8(path.data(), (int) path.size())));

	auto *menu = new QMenu;

	// Build the callback that the Plugin constructor uses to populate its menu.
	auto script_callback = [this, menu](String name, Plugin::MenuEntry target) {
		if (name.empty())
		{
			menu->addSeparator();
			return;
		}

		auto *action = new QAction(QString::fromUtf8(name.data(), (int) name.size()), menu);
		menu->addAction(action);

		if (target.type() == typeid(String))
		{
			auto script = std::any_cast<String>(target);

			if (script.ends_with(".html"))
			{
				// Open a documentation page in the help browser.
				connect(action, &QAction::triggered, [this, script](bool) {
					auto qpath = QString::fromUtf8(script.data(), (int) script.size());
					HelpBrowser::showPage(qpath, this);
				});
			}
			else
			{
				// Run a script.
				connect(action, &QAction::triggered, [this, script](bool) {
					m_console->runScript(QString::fromUtf8(script.data(), (int) script.size()));
				});
			}
		}
		else
		{
			auto protocol = std::any_cast<AutoProtocol>(target);
			connect(action, &QAction::triggered, [protocol, this](bool) {
				ProtocolQueryEditor editor(protocol, this);
				if (editor.exec() == QDialog::Accepted)
				{
					m_last_query = editor.query();
					auto conc = editor.concordance();
					if (conc && (!conc->empty() || !Settings::get_boolean("concordance", "discard_empty")))
					{
						openConcordance(conc);
						statusBar()->showMessage(
							tr("Found %1 match(es)").arg((int) conc->row_count()), 3000);
					}
					else
					{
						QMessageBox::information(this, tr("Search"), tr("No matches found."));
					}
				}
			});
		}
	};

	auto import_dir = filesystem::join(path, "Scripts");

	try
	{
		m_runtime.add_import_path(import_dir);
		auto plugin = std::make_shared<Plugin>(m_runtime, path, script_callback);

		if (plugin->has_entries())
		{
			auto label = plugin->label();
			auto qlabel = QString::fromUtf8(label.data(), (int) label.size());
			menu->setTitle(qlabel);

			auto desc = plugin->description();
			if (!desc.empty())
			{
				menu->addSeparator();
				auto title = tr("About %1").arg(qlabel);
				auto qdesc = QString::fromUtf8(desc.data(), (int) desc.size());
				auto *about_action = new QAction(title, menu);
				menu->addAction(about_action);

				connect(about_action, &QAction::triggered, [this, title, qdesc](bool) {
					QMessageBox::about(this, title, qdesc);
				});
			}

			// Insert the plugin submenu before the separator that divides
			// plugins from the built-in actions (Run script, Install, etc.).
			auto *menu_action = m_plugins_menu->insertMenu(m_plugin_separator, menu);
			m_plugin_actions[plugin.get()] = menu_action;
		}
		else
		{
			delete menu;
		}

		m_plugins.append(std::move(plugin));
	}
	catch (...)
	{
		m_runtime.remove_import_path(import_dir);
		delete menu;
		throw;
	}

	statusBar()->showMessage(tr("Ready"), 2000);
}

void MainWindow::onRunScript()
{
	auto path = QFileDialog::getOpenFileName(this, tr("Run script..."),
		lastDirectory(), tr("Phonometrica scripts (*.phon)"));

	if (path.isEmpty())
		return;

	setLastDirectory(path);
	m_console->runScript(path);
}

void MainWindow::onInstallPlugin()
{
	auto path = QFileDialog::getOpenFileName(this, tr("Select plugin..."),
		lastDirectory(), tr("ZIP files (*.zip)"));

	if (path.isEmpty())
		return;

	setLastDirectory(path);
	String archive(path.toUtf8().constData());

	// Compare the plugin directory before and after extraction to identify the new plugin.
	auto plugin_dir = Settings::plugin_directory();
	if (!filesystem::exists(plugin_dir))
		filesystem::create_directory(plugin_dir);
	auto before = filesystem::list_directory(plugin_dir);

	try
	{
		utils::unzip(archive, plugin_dir);
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Installation failed"),
			tr("Could not extract plugin archive: %1").arg(e.what()));
		return;
	}

	auto after = filesystem::list_directory(plugin_dir);
	std::sort(before.begin(), before.end());
	std::sort(after.begin(), after.end());

	std::vector<String> diff;
	std::set_difference(after.begin(), after.end(),
		before.begin(), before.end(),
		std::inserter(diff, diff.begin()));

	if (diff.size() == 1)
	{
		String new_path = filesystem::join(plugin_dir, diff.front());

		try
		{
			loadPlugin(new_path);
			auto label = m_plugins.last()->label();
			auto msg = utils::format("The \"%\" plugin has been installed!", label);
			QMessageBox::information(this, tr("Success"),
				QString::fromUtf8(msg.data(), (int) msg.size()));
		}
		catch (std::exception &e)
		{
			QMessageBox::critical(this, tr("Plugin error"),
				QString::fromUtf8(e.what()));
		}
	}
	else
	{
		QMessageBox::critical(this, tr("Error"),
			tr("Plugin installation failed.\n"
			   "If you tried to reinstall an existing plugin, "
			   "you should restart the program."));
	}
}

void MainWindow::onUninstallPlugin()
{
	if (m_plugins.empty())
	{
		QMessageBox::information(this, tr("No plugin found"),
			tr("You don't have any plugin installed!"));
		return;
	}

	QStringList names;
	for (auto &p : m_plugins)
	{
		auto label = p->label();
		names << QString::fromUtf8(label.data(), (int) label.size());
	}

	bool ok;
	auto chosen = QInputDialog::getItem(this, tr("Uninstall plugin"),
		tr("Choose a plugin to uninstall:"), names, 0, false, &ok);

	if (!ok || chosen.isEmpty())
		return;

	// Array is 1-based.
	for (intptr_t i = 1; i <= m_plugins.size(); i++)
	{
		auto &p = m_plugins[i];
		auto label = p->label();
		auto qlabel = QString::fromUtf8(label.data(), (int) label.size());

		if (qlabel == chosen)
		{
			uninstallPlugin((int) i);
			auto msg = utils::format("The \"%\" plugin has been uninstalled!", label);
			QMessageBox::information(this, tr("Success"),
				QString::fromUtf8(msg.data(), (int) msg.size()));
			return;
		}
	}
}

void MainWindow::uninstallPlugin(int index)
{
	auto &p = m_plugins[index];
	auto import_dir = filesystem::join(p->path(), "Scripts");

	// Remove the submenu from the Plugins menu.
	auto it = m_plugin_actions.find(p.get());
	if (it != m_plugin_actions.end())
	{
		m_plugins_menu->removeAction(it->second);
		m_plugin_actions.erase(it);
	}

	// Remove the plugin directory from disk.
	filesystem::remove(p->path());

	// Remove the import path for the plugin's scripts.
	m_runtime.remove_import_path(import_dir);

	// Remove the plugin from the list (this runs ~Plugin which executes finalize.phon).
	m_plugins.remove_at(index);
}

Plugin *MainWindow::findPlugin(const String &name)
{
	for (auto &plugin : m_plugins)
	{
		if (plugin->label() == name)
			return plugin.get();
	}
	return nullptr;
}

} // namespace phonometrica