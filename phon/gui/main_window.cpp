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
#include <QLabel>
#include <phon/gui/main_window.hpp>
#include <phon/gui/file_manager.hpp>
#include <phon/application/project.hpp>

namespace phonometrica {

MainWindow::MainWindow(Runtime &rt, QWidget *parent) :
	QMainWindow(parent), m_runtime(rt)
{
	setWindowTitle("Phonometrica");
	resize(1200, 800);

	createCentralWidget();
	createDockWidgets();
	createMenus();
	createStatusBar();
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
	menu->addAction(tr("Add files to project..."), QKeySequence(tr("Ctrl+Shift+A")), this, &MainWindow::onAddFiles);
	menu->addAction(tr("Add directory to project..."), this, &MainWindow::onAddFolder);
	menu->addAction(tr("Close project"), this, &MainWindow::onCloseProject);
	menu->addSeparator();

	menu->addAction(tr("Save project"), QKeySequence(tr("Ctrl+Shift+S")), this, &MainWindow::onSaveProject);
	menu->addAction(tr("Save project as..."), this, &MainWindow::onSaveProjectAs);
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

	return menu;
}

QMenu *MainWindow::createAnalysisMenu()
{
	auto *menu = new QMenu(tr("&Analysis"), this);
	// Placeholder — will be populated when query/analysis features are wired up.
	menu->addAction(tr("(coming soon)"))->setEnabled(false);
	return menu;
}

QMenu *MainWindow::createToolsMenu()
{
	auto *menu = new QMenu(tr("&Tools"), this);
	// Placeholder — plugins will be loaded here.
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
               "<p>&copy; 2019-2025 Léa Courdès-Murphy</p>"
               "<a target=\"_blank\" href=\"https://icons8.com/icon/Wy3XKG1CjyKf/database\">Database</a> icons by <a target=\"_blank\" href=\"https://icons8.com\">Icons8</a>"));
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
		// TODO: check for unsaved changes before closing
		m_viewer->removeTab(index);
	});

	// Show a welcome message when no tabs are open
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

	m_console = new QPlainTextEdit(m_console_dock);
	m_console->setReadOnly(true);
	m_console->setMaximumBlockCount(1000);

#if PHON_MACOS
	m_console->setFont(QFont("Monaco", 13));
#elif PHON_WINDOWS
	m_console->setFont(QFont("Consolas", 10));
#else
	m_console->setFont(QFont("Monospace", 12));
#endif

	m_console_dock->setWidget(m_console);
	addDockWidget(Qt::BottomDockWidgetArea, m_console_dock);
}

void MainWindow::createStatusBar()
{
	statusBar()->showMessage(tr("Ready"));
}


// --- Slots ---

void MainWindow::onNewScript()
{
	// TODO: create a script editor tab
	statusBar()->showMessage(tr("New script"), 2000);
}

void MainWindow::onOpenProject()
{
	auto path = QFileDialog::getOpenFileName(this, tr("Open project"),
		QString(), tr("Phonometrica project (*.phon-project)"));

	if (!path.isEmpty())
	{
		auto bytes = path.toUtf8();
		Project::get()->open(String(bytes.constData(), bytes.size()));
		m_file_manager->refresh();
		statusBar()->showMessage(tr("Opened project: %1").arg(path), 3000);
	}
}

void MainWindow::onAddFiles()
{
	auto files = QFileDialog::getOpenFileNames(this, tr("Add files to project"));

	if (!files.isEmpty())
	{
		auto *project = Project::get();
		for (auto &f : files)
		{
			auto bytes = f.toUtf8();
			project->import_file(String(bytes.constData(), bytes.size()));
		}
		m_file_manager->refresh();
		statusBar()->showMessage(tr("Added %1 file(s)").arg(files.size()), 3000);
	}
}

void MainWindow::onAddFolder()
{
	auto dir = QFileDialog::getExistingDirectory(this, tr("Add directory to project"));

	if (!dir.isEmpty())
	{
		auto bytes = dir.toUtf8();
		Project::get()->import_directory(String(bytes.constData(), bytes.size()));
		m_file_manager->refresh();
		statusBar()->showMessage(tr("Added directory: %1").arg(dir), 3000);
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
			project->save();
	}

	Project::close();
	m_file_manager->refresh();
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
	project->save();
	statusBar()->showMessage(tr("Project saved"), 2000);
}

void MainWindow::onSaveProjectAs()
{
	auto path = QFileDialog::getSaveFileName(this, tr("Save project as"),
		QString(), tr("Phonometrica project (*.phon-project)"));

	if (!path.isEmpty())
	{
		auto bytes = path.toUtf8();
		Project::get()->save(String(bytes.constData(), bytes.size()));
		statusBar()->showMessage(tr("Project saved as: %1").arg(path), 3000);
	}
}

void MainWindow::onQuit()
{
	auto *project = Project::get();
	if (project && project->modified())
	{
		auto answer = QMessageBox::question(this, tr("Quit"),
			tr("The project has unsaved changes. Save before quitting?"),
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

		if (answer == QMessageBox::Cancel)
			return;
		if (answer == QMessageBox::Save)
			project->save();
	}
	close();
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

	// TODO: open the document in the appropriate viewer tab.
	// For now, show a placeholder with the file path.
	auto label = doc->label();
	auto qlabel = QString::fromUtf8(label.data(), (int) label.size());

	// Check if this document is already open in a tab.
	for (int i = 0; i < m_viewer->count(); i++)
	{
		if (m_viewer->tabText(i) == qlabel)
		{
			m_viewer->setCurrentIndex(i);
			return;
		}
	}

	// Create a placeholder tab for now.
	auto &path = doc->path();
	auto qpath = QString::fromUtf8(path.data(), (int) path.size());
	auto *placeholder = new QLabel(tr("<h3>%1</h3><p>%2</p>").arg(qlabel, qpath));
	placeholder->setAlignment(Qt::AlignCenter);

	int tabIndex = m_viewer->addTab(placeholder, qlabel);
	m_viewer->setCurrentIndex(tabIndex);

	statusBar()->showMessage(tr("Opened: %1").arg(qlabel), 2000);
}

} // namespace phonometrica
