/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any      *
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
#include <QFileDialog>
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
#include <phon/gui/main_window.hpp>
#include <phon/gui/file_manager.hpp>
#include <phon/gui/view.hpp>
#include <phon/gui/view_panel.hpp>
#include <phon/gui/script_view.hpp>
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
#include <phon/gui/conc/concordance_view.hpp>
#include <phon/gui/analysis_view.hpp>
#include <phon/gui/batch_save_dialog.hpp>
#include <phon/gui/conc/protocol_query_editor.hpp>
#include <phon/application/bookmark.hpp>
#include <phon/application/project.hpp>
#include <phon/application/settings.hpp>
#include <phon/utils/file_system.hpp>
#include <phon/utils/zip.hpp>
#include <phon/utils/helpers.hpp>
#include <phon/runtime/file.hpp>

namespace phonometrica {

static constexpr int MAX_RECENT = 10;


MainWindow::MainWindow(Runtime &rt, QWidget *parent) :
	QMainWindow(parent), m_runtime(rt)
{
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
		});
		project->notify_closed.connect([this]() { updateWindowTitle(); });
		project->notify_error.connect([this](const String &msg) {
			auto qmsg = QString::fromUtf8(msg.data(), (int) msg.size());
			QMessageBox::warning(this, tr("Warning"), qmsg);
		});
	}

	updateWindowTitle();

	// Capture the default dock layout before any user customization is applied.
	m_default_state = QMainWindow::saveState();
	restoreWindowState();
}

void MainWindow::createMenus()
{
	auto *bar = menuBar();
	bar->addMenu(createFileMenu());
	bar->addMenu(createEditMenu());
	bar->addMenu(createAnalysisMenu());
	bar->addMenu(createToolsMenu());
	bar->addMenu(createWindowMenu());
	bar->addMenu(createHelpMenu());
}

QMenu *MainWindow::createFileMenu()
{
	auto *menu = new QMenu(tr("&File"), this);

	menu->addAction(tr("New script"), QKeySequence::New, this, &MainWindow::onNewScript);
	menu->addSeparator();

	menu->addAction(tr("Open project..."), QKeySequence(tr("Ctrl+O")), this, &MainWindow::onOpenProject);

	m_recent_menu = menu->addMenu(tr("Recent projects"));
	rebuildRecentMenu();

	menu->addAction(tr("Open most recent project"), QKeySequence(tr("Ctrl+Shift+O")), this, [this]() {
		try
		{
			auto &lst = Settings::get_list("recent_projects");
			if (lst.empty()) return;
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
	menu->addSeparator();
	menu->addAction(tr("Add files to project..."), QKeySequence(tr("Ctrl+Shift+A")), this, &MainWindow::onAddFiles);
	menu->addAction(tr("Add content of directory to project..."), this, &MainWindow::onAddFolder);
	menu->addSeparator();

	menu->addAction(tr("Save project"), QKeySequence(tr("Ctrl+Shift+S")), this, &MainWindow::onSaveProject);
	menu->addAction(tr("Save project as..."), this, &MainWindow::onSaveProjectAs);
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

	m_find_action = menu->addAction(tr("Find..."), QKeySequence::Find, this, &MainWindow::onFind);
	m_replace_action = menu->addAction(tr("Replace..."), QKeySequence(tr("Ctrl+H")), this, &MainWindow::onReplace);

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

	menu->addAction(tr("Measure intensity..."), this, &MainWindow::onMeasureIntensity);

	menu->addSeparator();

	menu->addAction(tr("Edit last query..."), QKeySequence(tr("Ctrl+L")),
	this, &MainWindow::onEditLastQuery);

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
	menu->addAction(tr("Install plugin..."), this, &MainWindow::onInstallPlugin);
	menu->addAction(tr("Uninstall plugin..."), this, &MainWindow::onUninstallPlugin);
	menu->addSeparator();
	menu->addAction(tr("How to extend this menu"), [this]() {
		HelpBrowser::showPage("scripting/plugins.html", this);
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
		HelpBrowser::showPage(QStringLiteral("scripting"), this);
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
		QMessageBox::about(this, tr("About Phonometrica"),
			tr("<h3>Phonometrica</h3>"
			   "<p>An open-source platform for the annotation "
			   "and analysis of speech corpora.</p>"
               "<p>&copy; 2019-2026 Julien Eychenne</p>"
               "<p>&copy; 2019-2025 Léa Courdès-Murphy</p>"));
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

	auto *welcome = new QLabel(tr("<h2>Welcome to Phonometrica</h2>"
	                              "<p>Open a project or add files to get started.</p>"));
	welcome->setAlignment(Qt::AlignCenter);
	m_viewer->addTab(welcome, tr("Welcome"));

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
	bottom_tabs->setStyleSheet(R"(
    QTabWidget::pane {
        border: none;
    })");

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
}


// ---------------------------------------------------------
//  File dialog helpers
// ---------------------------------------------------------

QString MainWindow::lastDirectory() const
{
	try
	{
		auto dir = Settings::get_last_directory();
		if (!dir.empty())
			return QString::fromUtf8(dir.data(), (int) dir.size());
	}
	catch (...) { }

	return QString();
}

void MainWindow::setLastDirectory(const QString &path)
{
	if (!path.isEmpty())
	{
		auto bytes = path.toUtf8();
		Settings::set_last_directory(String(bytes.constData(), bytes.size()));
	}
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

void MainWindow::onOpenProject()
{
	auto path = QFileDialog::getOpenFileName(this, tr("Open project"),
		lastDirectory(), tr("Phonometrica project (*.phon-project)"));

	if (path.isEmpty())
		return;

	setLastDirectory(path);

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
	dlg.exec();
}

void MainWindow::onCloseCurrentView()
{
	int idx = m_viewer->currentIndex();
	if (idx >= 0)
		closeTab(idx);
}

void MainWindow::onCloseAllViews()
{
	// ── Collect unsaved concordances vs other unsaved tabs ──
	struct TabInfo { QWidget *widget; QString label; bool preCheck; };
	QList<TabInfo> conc_tabs;

	for (int i = 0; i < m_viewer->count(); i++)
	{
		auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(i));
		if (!panel || !panel->isModified()) continue;

		auto *conc_view = qobject_cast<ConcordanceView *>(panel->primaryView());
		if (conc_view) {
			bool pre = !conc_view->path().empty();
			conc_tabs.append({m_viewer->widget(i), panel->label(), pre});
		}
	}

	// ── Batch dialog for 2+ unsaved concordances ──
	QSet<QWidget *> handled;
	if (conc_tabs.size() >= 2)
	{
		QStringList labels;
		QList<bool> preChecked;
		for (auto &t : conc_tabs) {
			labels << t.label;
			preChecked << t.preCheck;
		}

		BatchSaveDialog dlg(labels, preChecked, this);
		if (dlg.exec() == QDialog::Rejected) return;

		auto checked = dlg.checkedItems();
		for (int k = 0; k < conc_tabs.size(); k++)
		{
			auto *panel = qobject_cast<ViewPanel *>(conc_tabs[k].widget);
			if (!panel) continue;

			if (dlg.action() == BatchSaveDialog::SaveSelected && checked[k]) {
				panel->saveAll();
			}
			else {
				for (auto *v : panel->views())
					v->discardChanges();
			}
			handled.insert(conc_tabs[k].widget);
		}
	}

	// ── Close from last to first ──
	// Concordances handled above are closed without prompting.
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
	// ── Collect unsaved concordances vs other unsaved tabs ──
	struct TabInfo { int index; QString label; bool preCheck; };
	QList<TabInfo> conc_tabs;
	QList<int> other_modified;

	for (int i = 0; i < m_viewer->count(); i++)
	{
		auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(i));
		if (!panel || !panel->isModified())
			continue;

		auto *conc_view = qobject_cast<ConcordanceView *>(panel->primaryView());
		if (conc_view) {
			// Pre-check concordances that were previously saved (user invested effort).
			bool pre = !conc_view->path().empty();
			conc_tabs.append({i, panel->label(), pre});
		}
		else {
			other_modified.append(i);
		}
	}

	// ── Batch dialog for 2+ unsaved concordances ──
	QSet<int> handled;
	if (conc_tabs.size() >= 2)
	{
		QStringList labels;
		QList<bool> preChecked;
		for (auto &t : conc_tabs) {
			labels << t.label;
			preChecked << t.preCheck;
		}

		BatchSaveDialog dlg(labels, preChecked, this);
		if (dlg.exec() == QDialog::Rejected) return false;

		auto checked = dlg.checkedItems();
		for (int k = 0; k < conc_tabs.size(); k++)
		{
			int idx = conc_tabs[k].index;
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
			handled.insert(idx);
		}
	}

	// ── Prompt individually for non-concordances (and any single concordance) ──
	for (int i = 0; i < m_viewer->count(); i++)
	{
		if (handled.contains(i)) continue;

		auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(i));
		if (!panel || !panel->isModified())
			continue;

		// Bring the tab into view so the user knows which one we're asking about.
		m_viewer->setCurrentIndex(i);

		auto answer = QMessageBox::question(this, tr("Unsaved changes"),
			tr("The tab \"%1\" has unsaved changes. Save before closing?").arg(panel->label()),
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

		if (answer == QMessageBox::Cancel)
			return false;

		if (answer == QMessageBox::Save)
		{
			if (!panel->saveAll())
				return false; // Save was cancelled (e.g. user dismissed the file dialog).
		}
		else
		{
			for (auto *v : panel->views())
				v->discardChanges();
		}
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
}

void MainWindow::updateUndoRedoState()
{
	if (!m_undo_action || !m_redo_action)
		return;
	auto *view = currentView();
	m_undo_action->setEnabled(view && view->canUndo());
	m_redo_action->setEnabled(view && view->canRedo());
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

		// Check all views in the panel for a matching path.
		for (auto *v : panel->views())
		{
			if (v->path() == doc->path())
			{
				m_viewer->setCurrentIndex(i);
				return;
			}
		}
	}

	// Handle scripts with a proper editor
	if (doc->is<Script>())
	{
		auto *script = static_cast<Script *>(doc);
		openScript(Handle<Script>(script));
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

	int tabIndex = m_viewer->addTab(panel, panel->label());
	m_viewer->setCurrentIndex(tabIndex);

	return panel;
}

void MainWindow::openScript(const Handle<Script> &script)
{
	auto *view = new ScriptView(m_runtime, m_console, script);
	addViewTab(view);
}

void MainWindow::openDataset(Handle<Dataset> ds)
{
	auto *view = new DatasetView(ds);

	connect(view, &DatasetView::requestAnalysis,
		this, static_cast<void (MainWindow::*)(Handle<DataTable>)>(&MainWindow::openAnalysis));

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

	addViewTab(view);
}

void MainWindow::openAnalysis(Handle<DataTable> source)
{
	auto analysis = make_handle<Analysis>(nullptr, std::move(source));
	auto *view = new AnalysisView(std::move(analysis));
	addViewTab(view);
}

void MainWindow::openAnalysis(Handle<Analysis> analysis)
{
	auto *view = new AnalysisView(std::move(analysis));
	addViewTab(view);
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
		auto path = QFileDialog::getOpenFileName(this,
			QString::fromUtf8(msg.data(), (int) msg.size()));
		if (path.isEmpty()) return Variant();
		return String(path.toUtf8().constData());
	};

	auto open_files_dialog = [this](Runtime &rt, std::span<Variant> args) -> Variant {
		auto &msg = cast<String>(args[0]);
		auto paths = QFileDialog::getOpenFileNames(this,
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
		auto path = QFileDialog::getExistingDirectory(this,
			QString::fromUtf8(msg.data(), (int) msg.size()));
		if (path.isEmpty()) return Variant();
		return String(path.toUtf8().constData());
	};

	auto save_file_dialog = [this](Runtime &, std::span<Variant> args) -> Variant {
		auto &msg = cast<String>(args[0]);
		auto path = QFileDialog::getSaveFileName(this,
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
//  Plugin support
// ---------------------------------------------------------

void MainWindow::postInitialize()
{
	// Register scripting functions before loading plugins, so plugin
	// scripts can call GUI functions like info(), warning(), etc.
	setShellFunctions();

	// Load system plugins/scripts first, then user plugins/scripts.
	auto resources_dir = Settings::resources_directory();
	auto user_dir = Settings::settings_directory();
	loadPluginsAndScripts(resources_dir);
	loadPluginsAndScripts(user_dir);

	// Autoload the most recent project if the preference is enabled.
	if (Settings::get_boolean("autoload"))
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