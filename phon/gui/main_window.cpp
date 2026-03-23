/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 21/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QLabel>
#include <phon/gui/main_window.hpp>
#include <phon/gui/file_manager.hpp>
#include <phon/gui/view.hpp>
#include <phon/gui/view_panel.hpp>
#include <phon/gui/script_view.hpp>
#include <phon/gui/sound_view.hpp>
#include <phon/application/project.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

static constexpr int MAX_RECENT = 10;


MainWindow::MainWindow(Runtime &rt, QWidget *parent) :
	QMainWindow(parent), m_runtime(rt)
{
	resize(1200, 800);

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
	}

	updateWindowTitle();
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
	menu->addAction(tr("(coming soon)"))->setEnabled(false);
	return menu;
}

QMenu *MainWindow::createToolsMenu()
{
	auto *menu = new QMenu(tr("&Tools"), this);
	menu->addAction(tr("(coming soon)"))->setEnabled(false);
	return menu;
}

QMenu *MainWindow::createWindowMenu()
{
	auto *menu = new QMenu(tr("&Window"), this);

	auto *project_action = menu->addAction(tr("Project manager"));
	project_action->setCheckable(true);
	project_action->setChecked(true);
	connect(project_action, &QAction::toggled, this, &MainWindow::onToggleProjectPanel);

	auto *console_action = menu->addAction(tr("Console"));
	console_action->setCheckable(true);
	console_action->setChecked(true);
	connect(console_action, &QAction::toggled, this, &MainWindow::onToggleConsolePanel);

	return menu;
}

QMenu *MainWindow::createHelpMenu()
{
	auto *menu = new QMenu(tr("&Help"), this);

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
		auto *panel = qobject_cast<ViewPanel *>(m_viewer->widget(index));

		if (panel && panel->isModified())
		{
			auto answer = QMessageBox::question(this, tr("Unsaved changes"),
				tr("The tab \"%1\" has unsaved changes. Save before closing?").arg(panel->label()),
				QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

			if (answer == QMessageBox::Cancel)
				return;
			if (answer == QMessageBox::Save)
				panel->saveAll();
			else
			{
				// Discard changes in all views.
				for (auto *v : panel->views())
					v->discardChanges();
			}
		}

		m_viewer->removeTab(index);
	});

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
	m_project_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

	m_file_manager = new FileManager(Project::get(), m_project_dock);
	connect(m_file_manager, &FileManager::documentRequested, this, &MainWindow::onDocumentRequested);

	m_project_dock->setWidget(m_file_manager);
	addDockWidget(Qt::LeftDockWidgetArea, m_project_dock);

	// Console dock (bottom)
	m_console_dock = new QDockWidget(tr("Console"), this);
	m_console_dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
	m_console = new Console(m_runtime, m_console_dock);
	m_console_dock->setWidget(m_console);
	addDockWidget(Qt::BottomDockWidgetArea, m_console_dock);
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
		project->save();
		updateWindowTitle();
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
		Project::get()->save(String(path.toUtf8().constData()));
		updateRecentProjects(String(path.toUtf8().constData()));
		updateWindowTitle();
		statusBar()->showMessage(tr("Project saved as: %1").arg(path), 3000);
	}
	catch (std::exception &e)
	{
		QMessageBox::warning(this, tr("Error"),
			tr("Could not save project: %1").arg(e.what()));
	}
}

void MainWindow::onQuit()
{
	close(); // This triggers closeEvent().
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	if (!promptSaveUnsavedTabs())
	{
		event->ignore();
		return;
	}

	if (!promptSaveProject())
	{
		event->ignore();
		return;
	}

	event->accept();
}

bool MainWindow::promptSaveUnsavedTabs()
{
	for (int i = 0; i < m_viewer->count(); i++)
	{
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
	// TODO: wire to command processor
}

void MainWindow::onRedo()
{
	// TODO: wire to command processor
}

void MainWindow::onToggleProjectPanel(bool visible)
{
	m_project_dock->setVisible(visible);
}

void MainWindow::onToggleConsolePanel(bool visible)
{
	m_console_dock->setVisible(visible);
}

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

} // namespace phonometrica
