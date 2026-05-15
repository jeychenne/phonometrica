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

#include <phon/gui/file_dialog.hpp>
#include <QMessageBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QShortcut>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <phon/gui/file_manager.hpp>
#include <phon/gui/project_model.hpp>
#include <phon/gui/extract_layers_dialog.hpp>
#include <phon/gui/extract_slice_dialog.hpp>
#include <phon/gui/merge_annotations_dialog.hpp>
#include <phon/gui/concatenate_annotations_dialog.hpp>
#include <phon/gui/concatenate_sounds_dialog.hpp>
#include <phon/application/project.hpp>
#include <phon/application/bookmark.hpp>
#include <phon/application/data_table.hpp>
#include <phon/application/dataset.hpp>
#include <phon/application/annotation.hpp>
#include <phon/application/annotation_ops.hpp>
#include <phon/application/constants.hpp>
#include <phon/application/script.hpp>
#include <phon/application/praat.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

namespace {

// ---------------------------------------------------------------------------
//  Helpers for the annotation_ops context-menu actions.
//
//  The pattern is the same across every operation that produces a new file:
//    1. Build the source data into a staged Handle<> via annotation_ops.
//       The staged document is fully populated in memory (layers, properties,
//       description, sound binding) and has been written to disk.
//    2. Add the disk file to the project via Project::add_file. The project
//       constructs its own Annotation/Sound, which for native annotations
//       reads metadata from the file but for TextGrid annotations does not
//       (TextGrid has no slot for metadata).
//    3. Copy properties, description, and sound binding from the staged
//       Handle<> to the project's document. This is idempotent for native
//       (Document::add_property dedupes by category) and necessary for
//       TextGrid.
// ---------------------------------------------------------------------------

// Look up the document the project just constructed for `disk_path`.
Document *project_document_for(Project *proj, const String &disk_path)
{
	auto &files = proj->files();
	auto it = files.find(disk_path);
	if (it == files.end()) return nullptr;
	return it->second.get();
}

// Copy properties/description/sound binding from `staged` onto the project's
// in-memory document `target`. Safe to call regardless of underlying format.
void copy_annotation_metadata(const Annotation &staged, Annotation &target)
{
	for (auto &p : staged.properties()) {
		target.add_property(p, false);
	}
	if (!staged.description().empty()) {
		target.set_description(staged.description(), false);
	}
	if (staged.has_sound()) {
		target.set_sound(staged.sound(), false);
	}
}

// Register the staged annotation under `parent` in the project and copy its
// metadata to the resulting in-project document. Returns the in-project
// pointer, or nullptr if registration failed.
Annotation *register_staged_annotation(Project *proj,
                                       const Handle<Annotation> &staged,
                                       const Handle<Directory> &parent)
{
	String disk_path = staged->path();
	proj->add_file(String(staged->path()), parent, FileType::CorpusFile, true);
	auto *doc = project_document_for(proj, disk_path);
	auto *new_annot = dynamic_cast<Annotation *>(doc);
	if (new_annot) {
		copy_annotation_metadata(*staged, *new_annot);
	}
	proj->modify();
	return new_annot;
}

// Mirror of register_staged_annotation for sounds. Sounds carry no
// per-document metadata (properties live on Document but are not
// typically set on Sound; we still copy them through for parity).
Sound *register_staged_sound(Project *proj,
                             const Handle<Sound> &staged,
                             const Handle<Directory> &parent)
{
	String disk_path = staged->path();
	proj->add_file(String(staged->path()), parent, FileType::CorpusFile, true);
	auto *doc = project_document_for(proj, disk_path);
	auto *new_snd = dynamic_cast<Sound *>(doc);
	if (new_snd) {
		for (auto &p : staged->properties()) {
			new_snd->add_property(p, false);
		}
		if (!staged->description().empty()) {
			new_snd->set_description(staged->description(), false);
		}
	}
	proj->modify();
	return new_snd;
}

// Default extension to use when producing an annotation of the given type.
String annotation_ext_for(Annotation::Type t)
{
	return (t == Annotation::Type::TextGrid) ? String(".TextGrid")
	                                         : String(PHON_EXT_ANNOTATION);
}

// Build a sibling output path: `<source_dir>/<source_basename><suffix><ext>`,
// then make it non-colliding via annotation_ops::unique_path.
String sibling_output_path(const Document &source, const String &suffix, const String &ext)
{
	auto dir  = filesystem::directory_name(source.path());
	auto base = filesystem::strip_ext(filesystem::base_name(source.path()));
	String desired;
	desired.append(base);
	desired.append(suffix);
	desired.append(ext);
	return unique_path(filesystem::join(dir, desired));
}

} // anonymous namespace


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
	m_tree->setFrameShape(QFrame::NoFrame);

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
#if PHON_MACOS
	// Match the height of a native NSSearchField.
	m_search->setMinimumHeight(32);
#endif
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

		// ── Multi-selection: Merge annotations ──
		// Show "Merge annotations..." when 2+ annotations (and nothing else)
		// are selected. The dialog handles picking the base, the output path,
		// and the format.
		if (proxyIndexes.size() >= 2)
		{
			QList<Annotation *> selected_annots;
			bool homogeneous = true;
			for (auto &pi : proxyIndexes)
			{
				auto si = toSource(pi);
				auto *e = m_model->elementFromIndex(si);
				if (auto *a = dynamic_cast<Annotation *>(e)) {
					selected_annots.append(a);
				}
				else {
					homogeneous = false;
					break;
				}
			}

			if (homogeneous && selected_annots.size() >= 2)
			{
				menu.addSeparator();
				menu.addAction(tr("Merge annotations..."), [this, selected_annots]() {
					// Make sure all sources are loaded before showing the dialog
					// (so the dialog can report their layer counts).
					for (auto *a : selected_annots) {
						try { a->open(); }
						catch (std::exception &e) {
							QMessageBox::warning(this, tr("Merge annotations"),
								QString::fromUtf8(e.what()));
							return;
						}
					}

					MergeAnnotationsDialog dlg(this, selected_annots);
					if (dlg.exec() != QDialog::Accepted)
						return;

					auto *base = dlg.baseAnnotation();
					auto others_qlist = dlg.otherAnnotations();
					auto out_path = dlg.outputPath();
					auto out_format = dlg.outputFormat();
					if (!base || others_qlist.isEmpty() || out_path.empty())
						return;

					// Wrap others in Handles for the span-of-Handle API.
					std::vector<Handle<Annotation>> others;
					others.reserve(others_qlist.size());
					for (auto *a : others_qlist) others.emplace_back(a);

					Handle<Annotation> staged;
					try {
						staged = merge_annotations(*base,
							std::span<const Handle<Annotation>>(others.data(), others.size()),
							out_path, out_format);
					}
					catch (std::exception &e) {
						QMessageBox::warning(this, tr("Merge annotations"),
							QString::fromUtf8(e.what()));
						return;
					}

					auto *parent_dir = base->parent();
					if (!parent_dir) parent_dir = m_project->corpus().get();
					register_staged_annotation(m_project, staged, Handle<Directory>(parent_dir));
					refresh();
				});

				menu.addAction(tr("Concatenate annotations..."), [this, selected_annots]() {
					// Sources are already loaded (the Merge block did it before this
					// menu was built — but we run open() defensively to handle the
					// case where the user opens this action directly without Merge
					// having been listed for whatever reason).
					for (auto *a : selected_annots) {
						try { a->open(); }
						catch (std::exception &e) {
							QMessageBox::warning(this, tr("Concatenate annotations"),
								QString::fromUtf8(e.what()));
							return;
						}
					}

					ConcatenateAnnotationsDialog dlg(this, selected_annots);
					if (dlg.exec() != QDialog::Accepted)
						return;

					auto order = dlg.orderedSources();
					auto durations = dlg.orderedDurations();
					auto out_path = dlg.outputPath();
					auto out_format = dlg.outputFormat();
					if (order.size() < 2 || out_path.empty())
						return;

					std::vector<Handle<Annotation>> srcs;
					srcs.reserve(order.size());
					for (auto *a : order) srcs.emplace_back(a);

					Handle<Annotation> staged;
					try {
						staged = concatenate_annotations(
							std::span<const Handle<Annotation>>(srcs.data(), srcs.size()),
							std::span<const double>(durations.data(), durations.size()),
							out_path, out_format);
					}
					catch (std::exception &e) {
						QMessageBox::warning(this, tr("Concatenate annotations"),
							QString::fromUtf8(e.what()));
						return;
					}

					// Anchor the new file in the first source's directory.
					auto *parent_dir = order.first()->parent();
					if (!parent_dir) parent_dir = m_project->corpus().get();
					register_staged_annotation(m_project, staged, Handle<Directory>(parent_dir));
					refresh();
				});
			}
		}

		// ── Multi-selection: Concatenate sounds ──
		if (proxyIndexes.size() >= 2)
		{
			QList<Sound *> selected_sounds;
			bool homogeneous = true;
			for (auto &pi : proxyIndexes)
			{
				auto si = toSource(pi);
				auto *e = m_model->elementFromIndex(si);
				if (auto *s = dynamic_cast<Sound *>(e)) {
					selected_sounds.append(s);
				}
				else {
					homogeneous = false;
					break;
				}
			}

			if (homogeneous && selected_sounds.size() >= 2)
			{
				menu.addSeparator();
				menu.addAction(tr("Concatenate sounds..."), [this, selected_sounds]() {
					ConcatenateSoundsDialog dlg(this, selected_sounds);
					if (dlg.exec() != QDialog::Accepted)
						return;

					auto order = dlg.orderedSources();
					auto out_path = dlg.outputPath();
					auto fmt = dlg.outputFormat();
					if (order.size() < 2 || out_path.empty())
						return;

					std::vector<Handle<Sound>> srcs;
					srcs.reserve(order.size());
					for (auto *s : order) srcs.emplace_back(s);

					Handle<Sound> staged;
					try {
						staged = concatenate_sounds(
							std::span<const Handle<Sound>>(srcs.data(), srcs.size()),
							out_path, fmt);
					}
					catch (std::exception &e) {
						QMessageBox::warning(this, tr("Concatenate sounds"),
							QString::fromUtf8(e.what()));
						return;
					}

					auto *parent_dir = order.first()->parent();
					if (!parent_dir) parent_dir = m_project->corpus().get();
					register_staged_sound(m_project, staged, Handle<Directory>(parent_dir));
					refresh();
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
	bool isNotes = (dir == m_project->notes().get());

	if (isNotes)
	{
		menu.addAction(tr("New note"), [this, sourceIndex]() {
			// Emit a signal so MainWindow can create a new NoteView.
			auto *dir = dynamic_cast<Directory *>(m_model->elementFromIndex(sourceIndex));
			if (dir)
				emit noteRequested(dir);
		});

		menu.addAction(tr("Add files..."), [this, sourceIndex]() {
			addFilesToDirectory(sourceIndex);
		});
	}
	else if (isCorpus || isScripts || isData || isAnalyses || isQueries)
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

	// ── Open in Praat ────────────────────────────────────────────────────
	if (praat::available() && doc && doc->has_path())
	{
		if (auto *snd = dynamic_cast<Sound *>(doc))
		{
			menu.addAction(tr("Open in Praat"), [snd]() {
				try { praat::open_sound(snd->path()); }
				catch (...) {}
			});
		}
		else if (auto *annot = dynamic_cast<Annotation *>(doc))
		{
			if (annot->is_textgrid())
			{
				menu.addAction(tr("Open in Praat"), [annot]() {
					try {
						String snd_path;
						if (annot->has_sound())
							snd_path = annot->sound()->path();
						praat::open_textgrid(annot->path(), snd_path);
					}
					catch (...) {}
				});
			}
		}
	}
	menu.addSeparator();

	// ── Export native annotation to TextGrid ──────────────────────────────
	if (auto *annot = dynamic_cast<Annotation *>(doc))
	{
		if (annot->is_native() && annot->has_path())
		{
			menu.addAction(tr("Export to Praat TextGrid..."), [this, annot]() {
				// Build a default filename: same base name with .TextGrid extension.
				auto base = filesystem::strip_ext(filesystem::base_name(annot->path()));
				auto default_name = QString::fromUtf8(base.data(), (int) base.size())
				                    + QStringLiteral(".TextGrid");

				auto qpath = getSaveFileName(this, tr("Export to TextGrid"),
				                             tr("Praat TextGrid (*.TextGrid)"), default_name);
				if (qpath.isEmpty())
					return;

				auto bytes = qpath.toUtf8();
				auto export_path = String(bytes.constData(), bytes.size());

				try {
					// write_as_textgrid resets m_modified — preserve the original flag.
					bool was_modified = annot->graph_modified();
					annot->write_as_textgrid(export_path);
					if (was_modified)
						annot->set_graph_modified(true);
				}
				catch (std::exception &e) {
					QMessageBox::warning(this, tr("Export failed"), QString::fromUtf8(e.what()));
					return;
				}

				// Ask whether to import the exported TextGrid into the project.
				auto answer = QMessageBox::question(this, tr("Import TextGrid"),
					tr("The annotation was exported successfully.\n\n"
					   "Do you want to import the TextGrid into the project?"));

				if (answer != QMessageBox::Yes)
					return;

				auto *parent_dir = annot->parent();
				auto parent_handle = Handle<Directory>(parent_dir);

				// Keep a copy of the path before add_file moves it.
				String lookup_path = export_path;
				m_project->add_file(std::move(export_path), parent_handle, FileType::CorpusFile, true);

				// Find the newly imported annotation and copy metadata from the original.
				auto &files = m_project->files();
				auto it = files.find(lookup_path);
				if (it != files.end())
				{
					auto new_annot = dynamic_cast<Annotation *>(it->second.get());
					if (new_annot)
					{
						// Copy properties.
						for (auto &prop : annot->properties())
							new_annot->add_property(prop, false);

						// Copy description.
						if (!annot->description().empty())
							new_annot->set_description(annot->description(), false);

						// Bind to the same sound.
						if (annot->has_sound())
							new_annot->set_sound(annot->sound(), false);
					}
				}

				m_project->modify();
				refresh();
			});
		}
	}

	// ── Export TextGrid annotation to native format ───────────────────────
	if (auto *annot = dynamic_cast<Annotation *>(doc))
	{
		if (annot->is_textgrid() && annot->has_path())
		{
			menu.addAction(tr("Export to Phonometrica annotation..."), [this, annot]() {
				auto base = filesystem::strip_ext(filesystem::base_name(annot->path()));
				auto default_name = QString::fromUtf8(base.data(), (int) base.size())
				                    + QStringLiteral(".phon-annot");

				auto qpath = getSaveFileName(this, tr("Export to Phonometrica annotation"),
				                             tr("Phonometrica annotation (*.phon-annot)"), default_name);
				if (qpath.isEmpty())
					return;

				auto bytes = qpath.toUtf8();
				auto export_path = String(bytes.constData(), bytes.size());

				try {
					bool was_modified = annot->graph_modified();
					annot->write_as_native(export_path);
					if (was_modified)
						annot->set_graph_modified(true);
				}
				catch (std::exception &e) {
					QMessageBox::warning(this, tr("Export failed"), QString::fromUtf8(e.what()));
					return;
				}

				auto answer = QMessageBox::question(this, tr("Import annotation"),
					tr("The annotation was exported successfully.\n\n"
					   "Do you want to import it into the project?"));

				if (answer != QMessageBox::Yes)
					return;

				auto *parent_dir = annot->parent();
				auto parent_handle = Handle<Directory>(parent_dir);

				String lookup_path = export_path;
				m_project->add_file(std::move(export_path), parent_handle, FileType::CorpusFile, true);

				// The native format embeds metadata, but TextGrid annotations
				// store metadata externally — copy it to the new annotation
				// so it is available immediately without re-opening.
				auto &files = m_project->files();
				auto it = files.find(lookup_path);
				if (it != files.end())
				{
					auto new_annot = dynamic_cast<Annotation *>(it->second.get());
					if (new_annot)
					{
						for (auto &prop : annot->properties())
							new_annot->add_property(prop, false);

						if (!annot->description().empty())
							new_annot->set_description(annot->description(), false);

						if (annot->has_sound())
							new_annot->set_sound(annot->sound(), false);
					}
				}

				m_project->modify();
				refresh();
			});
		}
	}

	if (auto *script = dynamic_cast<Script *>(doc))
	{
		if (script->has_path())
		{
			menu.addAction(tr("Run"), [this, script]() {
				auto &p = script->path();
				emit scriptRunRequested(QString::fromUtf8(p.data(), (int) p.size()));
			});
		}
	}

	if (auto *dt = dynamic_cast<DataTable *>(doc))
	{
		menu.addAction(tr("Analyze"), [this, dt]() {
			emit analysisRequested(dt);
		});
	}

	// CSV separator picker — only for Dataset backed by a .csv file.
	if (auto *ds = dynamic_cast<Dataset *>(doc))
	{
		if (ds->has_path())
		{
			auto ext = filesystem::ext(ds->path(), true);
			if (ext == ".csv" || ext == ".tsv")
			{
				auto *sepMenu = menu.addMenu(tr("Separator"));

				struct SepOption { QString label; String sep; };
				const SepOption options[] = {
					{ tr("Tab (default)"), "\t" },
					{ tr("Comma (,)"),     ","  },
					{ tr("Semicolon (;)"), ";"  },
				};

				for (auto &opt : options)
				{
					auto *act = sepMenu->addAction(opt.label, [this, ds, sep = opt.sep]() {
						ds->set_separator(sep);
						m_project->modify();
						// Re-open so the DatasetView reloads with the new separator.
						emit documentRequested(ds);
					});
					act->setCheckable(true);
					act->setChecked(ds->separator() == opt.sep);
				}
			}
		}
	}

	// ── Annotation transformations (annotation_ops) ───────────────────────
	if (auto *annot = dynamic_cast<Annotation *>(doc))
	{
		if (annot->has_path())
		{
			// "Duplicate" — produces a sibling file with a "_copy" suffix and
			// inherits format, properties, description and sound binding from
			// the source.
			menu.addAction(tr("Duplicate"), [this, annot]() {
				auto ext = annotation_ext_for(annot->is_textgrid()
				                              ? Annotation::Type::TextGrid
				                              : Annotation::Type::Native);
				String out_path = sibling_output_path(*annot, "_copy", ext);

				Handle<Annotation> staged;
				try {
					staged = duplicate_annotation(*annot, out_path);
				}
				catch (std::exception &e) {
					QMessageBox::warning(this, tr("Duplicate annotation"),
						QString::fromUtf8(e.what()));
					return;
				}

				auto *parent_dir = annot->parent();
				if (!parent_dir) parent_dir = m_project->corpus().get();
				register_staged_annotation(m_project, staged, Handle<Directory>(parent_dir));
				refresh();
			});

			// "Extract layers..." — opens a dialog to pick which layers to
			// keep, the output path, and (if the user wants to convert
			// formats) the on-disk format.
			menu.addAction(tr("Extract layers..."), [this, annot]() {
				try { annot->open(); }
				catch (std::exception &e) {
					QMessageBox::warning(this, tr("Extract layers"),
						QString::fromUtf8(e.what()));
					return;
				}

				ExtractLayersDialog dlg(this, *annot);
				if (dlg.exec() != QDialog::Accepted)
					return;

				auto indices = dlg.selectedLayers();
				auto out_path = dlg.outputPath();
				auto out_format = dlg.outputFormat();
				if (indices.empty() || out_path.empty())
					return;

				Handle<Annotation> staged;
				try {
					staged = extract_layers(*annot,
						std::span<const intptr_t>(indices.data(), indices.size()),
						out_path, out_format);
				}
				catch (std::exception &e) {
					QMessageBox::warning(this, tr("Extract layers"),
						QString::fromUtf8(e.what()));
					return;
				}

				auto *parent_dir = annot->parent();
				if (!parent_dir) parent_dir = m_project->corpus().get();
				register_staged_annotation(m_project, staged, Handle<Directory>(parent_dir));
				refresh();
			});

			// "Extract slice..." — opens a dialog to pick a time range and,
			// when the annotation has a bound sound, whether to slice the
			// annotation, the sound, or both. The "both" mode automatically
			// binds the new annotation to the new sound.
			menu.addAction(tr("Extract slice..."), [this, annot]() {
				try { annot->open(); }
				catch (std::exception &e) {
					QMessageBox::warning(this, tr("Extract slice"),
						QString::fromUtf8(e.what()));
					return;
				}

				ExtractSliceDialog dlg(this, annot, /*sound=*/nullptr);
				if (dlg.exec() != QDialog::Accepted)
					return;

				auto *parent_dir = annot->parent();
				if (!parent_dir) parent_dir = m_project->corpus().get();
				auto parent_handle = Handle<Directory>(parent_dir);

				double t1 = dlg.startTime();
				double t2 = dlg.endTime();
				auto mode = dlg.mode();

				Sound *new_sound_in_project = nullptr;
				Annotation *new_annot_in_project = nullptr;

				// Sound side first so that a successful "Both" run binds the
				// project-managed sound (not the staged Handle) to the
				// project-managed annotation below.
				if (mode != ExtractSliceDialog::Mode::AnnotationOnly)
				{
					Handle<Sound> staged_snd;
					try {
						staged_snd = extract_sound_slice(
							*annot->sound(), t1, t2,
							dlg.soundOutputPath(),
							dlg.soundOutputFormat());
					}
					catch (std::exception &e) {
						QMessageBox::warning(this, tr("Extract slice"),
							QString::fromUtf8(e.what()));
						return;
					}
					new_sound_in_project = register_staged_sound(
						m_project, staged_snd, parent_handle);
				}

				if (mode != ExtractSliceDialog::Mode::SoundOnly)
				{
					Handle<Annotation> staged_annot;
					try {
						staged_annot = extract_annotation_slice(
							*annot, t1, t2, dlg.clipPartial(),
							dlg.annotationOutputPath(),
							dlg.annotationOutputFormat());
					}
					catch (std::exception &e) {
						QMessageBox::warning(this, tr("Extract slice"),
							QString::fromUtf8(e.what()));
						return;
					}
					new_annot_in_project = register_staged_annotation(
						m_project, staged_annot, parent_handle);
				}

				// "Both" mode: bind the project's in-memory annotation to the
				// project's in-memory sound. mutate=true so the binding is
				// persisted when the project is next saved.
				if (mode == ExtractSliceDialog::Mode::Both
				    && new_annot_in_project && new_sound_in_project)
				{
					new_annot_in_project->set_sound(Handle<Sound>(new_sound_in_project));
				}

				refresh();
			});
			menu.addSeparator();
		}
	}

	// ── Sound transformations (annotation_ops) ────────────────────────────
	// Single-sound "Extract slice..." entry. The annotation-side counterpart
	// above already covers annotations with bound sound; this entry handles
	// a sound right-click independently.
	if (auto *snd = dynamic_cast<Sound *>(doc))
	{
		if (snd->has_path())
		{
			menu.addSeparator();
			menu.addAction(tr("Extract slice..."), [this, snd]() {
				ExtractSliceDialog dlg(this, /*annot=*/nullptr, snd);
				if (dlg.exec() != QDialog::Accepted)
					return;

				Handle<Sound> staged;
				try {
					staged = extract_sound_slice(
						*snd, dlg.startTime(), dlg.endTime(),
						dlg.soundOutputPath(),
						dlg.soundOutputFormat());
				}
				catch (std::exception &e) {
					QMessageBox::warning(this, tr("Extract slice"),
						QString::fromUtf8(e.what()));
					return;
				}

				auto *parent_dir = snd->parent();
				if (!parent_dir) parent_dir = m_project->corpus().get();
				register_staged_sound(m_project, staged, Handle<Directory>(parent_dir));
				refresh();
			});
		}
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

		auto label = QString::fromUtf8(doc->browser_label().data(), (int) doc->browser_label().size());
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
	else if (dir->toplevel() == m_project->notes().get())
	{
		filter = tr("Research notes (*.phon-note);;All files (*)");
	}

	auto files = getOpenFileNames(this, tr("Add files"), filter);
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
		else if (dir->toplevel() == m_project->notes().get())
			type = FileType::Note;

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
