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

#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QShortcut>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <phon/gui/file_manager.hpp>
#include <phon/gui/project_model.hpp>
#include <phon/application/project.hpp>
#include <phon/application/bookmark.hpp>
#include <phon/application/data_table.hpp>
#include <phon/application/annotation.hpp>

namespace phonometrica {


// =========================================================
//  ProjectFilterModel
// =========================================================

ProjectFilterModel::ProjectFilterModel(QObject *parent) :
	QSortFilterProxyModel(parent)
{
	setRecursiveFilteringEnabled(true);
}

void ProjectFilterModel::setFilterText(const QString &text)
{
	m_filterText = text;
	invalidateFilter();
}

bool ProjectFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
	if (m_filterText.isEmpty())
		return true;

	auto *srcModel = qobject_cast<ProjectModel *>(sourceModel());
	if (!srcModel)
		return true;

	auto index = srcModel->index(sourceRow, 0, sourceParent);
	auto *elem = srcModel->elementFromIndex(index);
	if (!elem)
		return true;

	// Root directories are always visible.
	if (srcModel->isRootDirectory(index))
		return true;

	// Use the VFS's quick_search for leaf-level matching.
	auto bytes = m_filterText.toUtf8();
	auto text = String(bytes.constData(), bytes.size());

	// For documents and bookmarks, check the element itself.
	if (elem->quick_search(text))
		return true;

	// For directories, check whether any descendant matches.
	// (QSortFilterProxyModel::setRecursiveFilteringEnabled handles the ancestor-visibility
	//  part, but we still need to accept directories that have matching children.)
	if (dynamic_cast<Directory *>(elem))
		return hasMatchingDescendant(index);

	return false;
}

bool ProjectFilterModel::hasMatchingDescendant(const QModelIndex &sourceIndex) const
{
	auto *srcModel = qobject_cast<ProjectModel *>(sourceModel());
	if (!srcModel)
		return false;

	int rows = srcModel->rowCount(sourceIndex);
	for (int r = 0; r < rows; r++)
	{
		auto child = srcModel->index(r, 0, sourceIndex);
		auto *childElem = srcModel->elementFromIndex(child);
		if (!childElem)
			continue;

		auto bytes = m_filterText.toUtf8();
		auto text = String(bytes.constData(), bytes.size());

		if (childElem->quick_search(text))
			return true;

		if (dynamic_cast<Directory *>(childElem))
		{
			if (hasMatchingDescendant(child))
				return true;
		}
	}

	return false;
}


// =========================================================
//  FileManager
// =========================================================

FileManager::FileManager(Project *project, QWidget *parent) :
	QWidget(parent), m_project(project)
{
	m_model = new ProjectModel(project, this);
	m_proxy = new ProjectFilterModel(this);
	m_proxy->setSourceModel(m_model);

	setupUi();
	setupKeyboardShortcuts();

	// --- Connect project signals to model refresh ---

	m_project->notify_update.connect([this]() {
		refresh();
	});

	m_project->notify_closed.connect([this]() {
		m_search->clear();
		refresh();
	});

	m_project->metadata_updated.connect([this]() {
		refresh();
	});

	// Preserve expanded directories across model resets (e.g. drag-and-drop).
	connect(m_proxy, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
		m_expanded_save.clear();
		saveExpandedState(QModelIndex());
	});
	connect(m_proxy, &QAbstractItemModel::modelReset, this, [this]() {
		restoreExpandedState(QModelIndex());
		m_expanded_save.clear();
	});
}

void FileManager::setupUi()
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);


	// Tree view
	m_tree = new QTreeView(this);
	m_tree->setModel(m_proxy);

	// Appearance
	m_tree->setHeaderHidden(true);
	m_tree->setRootIsDecorated(true);
	m_tree->setAnimated(true);
	m_tree->setIndentation(16);
	m_tree->setUniformRowHeights(true);
	m_tree->setExpandsOnDoubleClick(false); // we handle double-click ourselves

	// Selection
	m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);

	// Drag and drop
	m_tree->setDragEnabled(true);
	m_tree->setAcceptDrops(true);
	m_tree->setDropIndicatorShown(true);
	m_tree->setDragDropMode(QAbstractItemView::InternalMove);
	m_tree->setDefaultDropAction(Qt::MoveAction);

	// Context menu
	m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_tree, &QTreeView::customContextMenuRequested, this, &FileManager::onContextMenu);

	// Double-click
	connect(m_tree, &QTreeView::doubleClicked, this, &FileManager::onDoubleClicked);

	// Selection changes
	connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged,
	        this, [this]() { onSelectionChanged(); });

	layout->addWidget(m_tree);

    // Search bar
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Filter..."));
    m_search->setClearButtonEnabled(true);
	m_search->addAction(QIcon(":/icons/search.svg"), QLineEdit::LeadingPosition);
    connect(m_search, &QLineEdit::textChanged, this, &FileManager::onFilterTextChanged);
    layout->addWidget(m_search);

	m_tree->viewport()->installEventFilter(this);

    // Make sure roots are visible in the file manager
    refresh();
}

bool FileManager::eventFilter(QObject *obj, QEvent *event)
{
	if (obj == m_tree->viewport() && event->type() == QEvent::MouseButtonPress)
	{
		auto *me = static_cast<QMouseEvent *>(event);
		if (me->button() == Qt::MiddleButton)
		{
			QModelIndex index = m_tree->indexAt(me->pos());
			if (index.isValid())
				m_tree->setExpanded(index, !m_tree->isExpanded(index));
			return true;
		}
	}
	return QWidget::eventFilter(obj, event);
}

void FileManager::setupKeyboardShortcuts()
{
	// Delete — remove selected elements (only when the tree has focus).
	auto *deleteShortcut = new QShortcut(QKeySequence::Delete, m_tree);
	deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(deleteShortcut, &QShortcut::activated, this, &FileManager::removeSelectedElements);

	// Backspace — also remove (common on macOS).
	auto *backspaceShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), m_tree);
	backspaceShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(backspaceShortcut, &QShortcut::activated, this, &FileManager::removeSelectedElements);

	// F2 — rename selected directory
	auto *renameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), m_tree);
	renameShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(renameShortcut, &QShortcut::activated, [this]() {
		auto proxyIndex = m_tree->currentIndex();
		if (proxyIndex.isValid())
			renameElement(toSource(proxyIndex));
	});

	// Enter/Return — open selected document
	auto *openShortcut = new QShortcut(QKeySequence(Qt::Key_Return), m_tree);
	openShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(openShortcut, &QShortcut::activated, [this]() {
		auto proxyIndex = m_tree->currentIndex();
		if (proxyIndex.isValid())
			onDoubleClicked(proxyIndex);
	});

	// Ctrl+F — focus the search bar
	auto *searchShortcut = new QShortcut(QKeySequence::Find, this);
	searchShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(searchShortcut, &QShortcut::activated, [this]() {
		m_search->setFocus();
		m_search->selectAll();
	});

	// Escape — clear search and return focus to tree
	auto *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_search);
	connect(escapeShortcut, &QShortcut::activated, [this]() {
		if (!m_search->text().isEmpty())
		{
			m_search->clear();
		}
		m_tree->setFocus();
	});
}

void FileManager::refresh()
{
	m_model->refresh();
	m_proxy->invalidate();

	if (m_search->text().isEmpty())
		expandRoots();
	else
		expandAll();
}

void FileManager::expandRoots()
{
	for (int i = 0; i < ProjectModel::ROOT_COUNT; i++)
	{
		auto sourceRoot = m_model->index(i, 0, QModelIndex());
		auto proxyRoot = toProxy(sourceRoot);
		if (proxyRoot.isValid())
			m_tree->expand(proxyRoot);

		// Also expand subdirectories that were marked expanded in the VFS.
		auto *dir = dynamic_cast<Directory *>(m_model->elementFromIndex(sourceRoot));
		if (dir && dir->expanded())
		{
			std::function<void(const QModelIndex &)> expandRecursive;
			expandRecursive = [&](const QModelIndex &proxyParent)
			{
				int rows = m_proxy->rowCount(proxyParent);
				for (int r = 0; r < rows; r++)
				{
					auto proxyChild = m_proxy->index(r, 0, proxyParent);
					auto sourceChild = toSource(proxyChild);
					auto *childDir = dynamic_cast<Directory *>(m_model->elementFromIndex(sourceChild));
					if (childDir && childDir->expanded())
					{
						m_tree->expand(proxyChild);
						expandRecursive(proxyChild);
					}
				}
			};
			expandRecursive(proxyRoot);
		}
	}
}

void FileManager::expandAll()
{
	m_tree->expandAll();
}

void FileManager::saveExpandedState(const QModelIndex &proxyParent)
{
	int rows = m_proxy->rowCount(proxyParent);
	for (int r = 0; r < rows; r++)
	{
		auto proxyChild = m_proxy->index(r, 0, proxyParent);
		if (m_tree->isExpanded(proxyChild))
		{
			auto sourceChild = toSource(proxyChild);
			auto *elem = m_model->elementFromIndex(sourceChild);
			if (elem)
				m_expanded_save.insert(elem);
			saveExpandedState(proxyChild);
		}
	}
}

void FileManager::restoreExpandedState(const QModelIndex &proxyParent)
{
	int rows = m_proxy->rowCount(proxyParent);
	for (int r = 0; r < rows; r++)
	{
		auto proxyChild = m_proxy->index(r, 0, proxyParent);
		auto sourceChild = toSource(proxyChild);
		auto *elem = m_model->elementFromIndex(sourceChild);
		if (elem && m_expanded_save.count(elem))
		{
			m_tree->expand(proxyChild);
			restoreExpandedState(proxyChild);
		}
	}
}

// ---------------------------------------------------------
//  Index mapping helpers
// ---------------------------------------------------------

QModelIndex FileManager::toSource(const QModelIndex &proxyIndex) const
{
	return m_proxy->mapToSource(proxyIndex);
}

QModelIndex FileManager::toProxy(const QModelIndex &sourceIndex) const
{
	return m_proxy->mapFromSource(sourceIndex);
}

// ---------------------------------------------------------
//  Slots
// ---------------------------------------------------------

void FileManager::onDoubleClicked(const QModelIndex &proxyIndex)
{
	if (!proxyIndex.isValid())
		return;

	auto sourceIndex = toSource(proxyIndex);
	auto *elem = m_model->elementFromIndex(sourceIndex);

	// Toggle expansion for directories.
	if (dynamic_cast<Directory *>(elem))
	{
		m_tree->setExpanded(proxyIndex, !m_tree->isExpanded(proxyIndex));
		return;
	}

	// Open documents.
	auto *doc = dynamic_cast<Document *>(elem);
	if (doc)
	{
		emit documentRequested(doc);
		return;
	}

	// Open bookmarks (navigate to the bookmarked location).
	auto *ts = dynamic_cast<TimeStamp *>(elem);
	if (ts)
	{
		emit bookmarkRequested(ts);
	}
}

void FileManager::onContextMenu(const QPoint &pos)
{
	auto proxyIndex = m_tree->indexAt(pos);
	if (!proxyIndex.isValid())
		return;

	auto sourceIndex = toSource(proxyIndex);
	auto *elem = m_model->elementFromIndex(sourceIndex);

	QMenu menu(this);

	if (m_model->isRootDirectory(sourceIndex))
	{
		buildRootContextMenu(menu, sourceIndex);
	}
	else if (dynamic_cast<Bookmark *>(elem))
	{
		buildBookmarkContextMenu(menu, sourceIndex);
	}
	else if (dynamic_cast<Directory *>(elem))
	{
		buildDirectoryContextMenu(menu, sourceIndex);
	}
	else if (dynamic_cast<Document *>(elem))
	{
		buildDocumentContextMenu(menu, sourceIndex);

		// ── Multi-selection: Bind annotation to sound ──
		auto proxyIndexes = m_tree->selectionModel()->selectedIndexes();
		if (proxyIndexes.size() == 2)
		{
			Annotation *annot = nullptr;
			Sound *sound = nullptr;

			for (auto &pi : proxyIndexes)
			{
				auto si = toSource(pi);
				auto *e = m_model->elementFromIndex(si);
				if (auto *a = dynamic_cast<Annotation *>(e))
					annot = a;
				else if (auto *s = dynamic_cast<Sound *>(e))
					sound = s;
			}

			if (annot && sound)
			{
				menu.addSeparator();
				menu.addAction(tr("Bind annotation to sound file"), [this, annot, sound]() {
					annot->set_sound(Handle<Sound>(sound));
					// Refresh the info panel by re-emitting the selection.
					onSelectionChanged();
				});
			}
		}

		// ── Single selection: Create annotation from sound ──
		if (proxyIndexes.size() == 1)
		{
			auto *sound = dynamic_cast<Sound *>(elem);
			if (sound)
			{
				menu.addSeparator();
				menu.addAction(tr("Create annotation"), [this, sound]() {
					// Use default constructor which sets m_type = Native.
					auto annot = make_handle<Annotation>();
					annot->set_sound(Handle<Sound>(sound));
					// Mark as modified so the save dialog will appear.
					annot->set_graph_modified(true);
					auto *raw = annot.get();
					// Place in the same folder as the sound file.
					auto *parent = sound->parent();
					if (!parent) parent = m_project->corpus().get();
					parent->append(std::move(annot), true);
					refresh();
					emit documentRequested(raw);
				});
			}
		}
	}
	else
	{
		return;
	}

	if (!menu.isEmpty())
		menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void FileManager::onSelectionChanged()
{
	QList<Document*> docs;
	auto proxyIndexes = m_tree->selectionModel()->selectedIndexes();

	for (auto &pi : proxyIndexes)
	{
		auto si = toSource(pi);
		auto *elem = m_model->elementFromIndex(si);
		if (auto *doc = dynamic_cast<Document *>(elem))
			docs.append(doc);
	}

	emit selectionChanged(docs);
}

void FileManager::onFilterTextChanged(const QString &text)
{
	m_proxy->setFilterText(text);

	if (text.isEmpty())
		expandRoots();
	else
		expandAll();
}

// ---------------------------------------------------------
//  Context menu builders
// ---------------------------------------------------------

void FileManager::buildRootContextMenu(QMenu &menu, const QModelIndex &sourceIndex)
{
	auto *dir = dynamic_cast<Directory *>(m_model->elementFromIndex(sourceIndex));
	if (!dir)
		return;

	bool isCorpus = (dir == m_project->corpus().get());
	bool isScripts = (dir == m_project->scripts().get());
	bool isData = (dir == m_project->data().get());
	bool isAnalyses = (dir == m_project->analyses().get());
	bool isQueries = (dir == m_project->queries().get());

	if (isCorpus || isScripts || isData || isAnalyses || isQueries)
	{
		menu.addAction(tr("Add files..."), [this, sourceIndex]() {
			addFilesToDirectory(sourceIndex);
		});
	}

	menu.addAction(tr("New folder"), [this, sourceIndex]() {
		addSubfolder(sourceIndex);
	});

	if (!dir->empty())
	{
		menu.addSeparator();

		auto proxyIndex = toProxy(sourceIndex);

		menu.addAction(tr("Expand all"), [this, proxyIndex]() {
			m_tree->expandRecursively(proxyIndex);
		});

		menu.addAction(tr("Collapse all"), [this, proxyIndex]() {
			m_tree->collapse(proxyIndex);
		});

		menu.addSeparator();

		menu.addAction(tr("Sort by name"), [this, dir]() {
			dir->sort();
			m_project->modify();
			refresh();
		});
	}
}

void FileManager::buildDirectoryContextMenu(QMenu &menu, const QModelIndex &sourceIndex)
{
	menu.addAction(tr("Add files..."), [this, sourceIndex]() {
		addFilesToDirectory(sourceIndex);
	});

	menu.addAction(tr("New folder"), [this, sourceIndex]() {
		addSubfolder(sourceIndex);
	});

	menu.addSeparator();

	menu.addAction(tr("Rename"), [this, sourceIndex]() {
		renameElement(sourceIndex);
	});

	menu.addSeparator();

	menu.addAction(tr("Remove"), [this, sourceIndex]() {
		auto *dir = dynamic_cast<Directory *>(m_model->elementFromIndex(sourceIndex));
		if (!dir)
			return;

		auto label = QString::fromUtf8(dir->label().data(), (int) dir->label().size());
		auto answer = QMessageBox::question(this, tr("Remove folder"),
			tr("Remove folder \"%1\" and all its contents from the project?").arg(label));

		if (answer == QMessageBox::Yes)
		{
			m_project->remove(Handle<Directory>(dir));
			m_project->modify();
			refresh();
		}
	});
}

void FileManager::buildDocumentContextMenu(QMenu &menu, const QModelIndex &sourceIndex)
{
	menu.addAction(tr("Open"), [this, sourceIndex]() {
		openDocument(sourceIndex);
	});

	auto *doc = dynamic_cast<Document *>(m_model->elementFromIndex(sourceIndex));

	if (auto *dt = dynamic_cast<DataTable *>(doc))
	{
		menu.addAction(tr("Analyze"), [this, dt]() {
			emit analysisRequested(dt);
		});
	}

	if (doc && doc->has_path())
	{
		menu.addAction(tr("Copy full path"), [doc]() {
			auto path = QString::fromUtf8(doc->path().data(), (int) doc->path().size());
			QApplication::clipboard()->setText(path);
		});
	}

	menu.addSeparator();

	menu.addAction(tr("Remove from project"), [this, sourceIndex]() {
		auto *doc = dynamic_cast<Document *>(m_model->elementFromIndex(sourceIndex));
		if (!doc)
			return;

		auto label = QString::fromUtf8(doc->label().data(), (int) doc->label().size());
		auto answer = QMessageBox::question(this, tr("Remove file"),
			tr("Remove \"%1\" from the project?\n(The file on disk is not affected.)").arg(label));

		if (answer == QMessageBox::Yes)
		{
			m_project->remove(Handle<Document>(doc));
			m_project->modify();
			refresh();
		}
	});
}

void FileManager::buildBookmarkContextMenu(QMenu &menu, const QModelIndex &sourceIndex)
{
	auto proxyIndex = toProxy(sourceIndex);

	menu.addAction(tr("Go to bookmark"), [this, proxyIndex]() {
		onDoubleClicked(proxyIndex);
	});

	menu.addSeparator();

	menu.addAction(tr("Remove"), [this, sourceIndex]() {
		auto *bookmark = dynamic_cast<Bookmark *>(m_model->elementFromIndex(sourceIndex));
		if (!bookmark)
			return;

		m_project->remove(Handle<Bookmark>(bookmark));
		m_project->modify();
		refresh();
	});
}

// ---------------------------------------------------------
//  Actions
// ---------------------------------------------------------

void FileManager::addFilesToDirectory(const QModelIndex &sourceParent)
{
	auto *dir = dynamic_cast<Directory *>(m_model->elementFromIndex(sourceParent));
	if (!dir)
		return;

	// Build the filter string based on which root directory we're under.
	QString filter = tr("All supported files (*)");

	bool isCorpus = (dir->toplevel() == m_project->corpus().get());
	bool isScripts = (dir->toplevel() == m_project->scripts().get());

	if (isCorpus)
	{
		filter = tr("Corpus files (*.wav *.flac *.ogg *.aiff *.phon-annot *.textgrid);;All files (*)");
	}
	else if (isScripts)
	{
		filter = tr("Scripts (*.phon);;All files (*)");
	}

	auto files = QFileDialog::getOpenFileNames(this, tr("Add files"), QString(), filter);
	if (files.isEmpty())
		return;

	for (auto &f : files)
	{
		auto bytes = f.toUtf8();
		auto path = String(bytes.constData(), bytes.size());

		FileType type = FileType::Any;
		if (dir->toplevel() == m_project->corpus().get())
			type = FileType::CorpusFile;
		else if (dir->toplevel() == m_project->scripts().get())
			type = FileType::Script;
		else if (dir->toplevel() == m_project->data().get())
			type = FileType::Dataset;
		else if (dir->toplevel() == m_project->queries().get())
			type = FileType::Query;

		auto handle = Handle<Directory>(dir);
		m_project->add_file(std::move(path), handle, type, true);
	}

	m_project->modify();
	refresh();
	auto proxyParent = toProxy(sourceParent);
	if (proxyParent.isValid())
		m_tree->expand(proxyParent);

	if (m_project->import_flag())
	{
		QMessageBox::information(this, tr("Import"),
			tr("Some files could not be imported (unsupported format or already in project)."));
		m_project->clear_import_flag();
	}
}

void FileManager::addSubfolder(const QModelIndex &sourceParent)
{
	bool ok;
	auto name = QInputDialog::getText(this, tr("New folder"), tr("Folder name:"),
		QLineEdit::Normal, tr("New folder"), &ok);

	if (ok && !name.isEmpty())
	{
		auto newSourceIndex = m_model->addSubfolder(name, sourceParent);
		auto proxyParent = toProxy(sourceParent);
		if (proxyParent.isValid())
			m_tree->expand(proxyParent);
		auto newProxyIndex = toProxy(newSourceIndex);
		if (newProxyIndex.isValid())
			m_tree->setCurrentIndex(newProxyIndex);
	}
}

void FileManager::removeSelectedElements()
{
	auto proxyIndexes = m_tree->selectionModel()->selectedIndexes();
	if (proxyIndexes.isEmpty())
		return;

	// Don't allow deleting root directories.
	std::vector<Element *> elements;
	for (auto &pi : proxyIndexes)
	{
		auto si = toSource(pi);
		if (si.isValid() && !m_model->isRootDirectory(si))
			elements.push_back(m_model->elementFromIndex(si));
	}

	if (elements.empty())
		return;

	QString msg = (elements.size() == 1)
		? tr("Remove the selected item from the project?\n(Files on disk are not affected.)")
		: tr("Remove the %1 selected items from the project?\n(Files on disk are not affected.)").arg(elements.size());

	auto answer = QMessageBox::question(this, tr("Remove"), msg);
	if (answer != QMessageBox::Yes)
		return;

	for (auto *elem : elements)
	{
		if (auto *dir = dynamic_cast<Directory *>(elem))
		{
			m_project->remove(Handle<Directory>(dir));
		}
		else if (auto *doc = dynamic_cast<Document *>(elem))
		{
			m_project->remove(Handle<Document>(doc));
		}
		else if (auto *bk = dynamic_cast<Bookmark *>(elem))
		{
			m_project->remove(Handle<Bookmark>(bk));
		}
	}

	m_project->modify();
	refresh();
}

void FileManager::openDocument(const QModelIndex &sourceIndex)
{
	auto *doc = dynamic_cast<Document *>(m_model->elementFromIndex(sourceIndex));
	if (doc)
		emit documentRequested(doc);
}

void FileManager::renameElement(const QModelIndex &sourceIndex)
{
	if (!sourceIndex.isValid() || m_model->isRootDirectory(sourceIndex))
		return;

	auto *elem = m_model->elementFromIndex(sourceIndex);
	if (!dynamic_cast<Directory *>(elem))
		return; // only directories can be renamed in the tree

	auto proxyIndex = toProxy(sourceIndex);
	if (proxyIndex.isValid())
		m_tree->edit(proxyIndex);
}

} // namespace phonometrica
