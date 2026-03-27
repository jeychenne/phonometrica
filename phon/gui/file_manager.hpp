/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 21/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: the file manager is the project browser panel. It displays the project's virtual file system as a tree    *
 * and provides context menus for common operations (add/remove files, create subfolders, etc.) as well as             *
 * drag-and-drop for reorganizing elements. A search bar at the top filters the tree in real time.                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FILE_MANAGER_HPP
#define PHONOMETRICA_FILE_MANAGER_HPP

#include <QWidget>
#include <QTreeView>
#include <QLineEdit>
#include <QMenu>
#include <QSortFilterProxyModel>
#include <QVBoxLayout>

namespace phonometrica {

class Project;
class ProjectModel;
class Element;
class Document;
class TimeStamp;


// A proxy model that filters the project tree using the VFS's quick_search mechanism.
// When a document matches, its ancestor directories are kept visible. When filtering is
// active, all visible directories are automatically expanded.
class ProjectFilterModel : public QSortFilterProxyModel
{
	Q_OBJECT

public:

	explicit ProjectFilterModel(QObject *parent = nullptr);

	void setFilterText(const QString &text);

protected:

	bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:

	// Recursively check whether any descendant of the given source index matches.
	bool hasMatchingDescendant(const QModelIndex &sourceIndex) const;

	QString m_filterText;
};


class FileManager : public QWidget
{
	Q_OBJECT

public:

	explicit FileManager(Project *project, QWidget *parent = nullptr);

	// Full refresh of the tree (e.g. after project open/close).
	void refresh();

	// Expand the root directories that should be expanded on startup.
	void expandRoots();

	// Expand all visible items (used when filtering).
	void expandAll();

	// Access the underlying model for external use.
	ProjectModel *model() const { return m_model; }

signals:

	// Emitted when the user requests to open a document (double-click or context menu).
	void documentRequested(Document *doc);

	// Emitted when the user double-clicks a bookmark.
	void bookmarkRequested(TimeStamp *bookmark);

	// Emitted when the selection changes.
	void selectionChanged(Element *elem);

protected:

	bool eventFilter(QObject *obj, QEvent *event) override;

private slots:

	void onDoubleClicked(const QModelIndex &proxyIndex);
	void onContextMenu(const QPoint &pos);
	void onSelectionChanged();
	void onFilterTextChanged(const QString &text);

private:

	void setupUi();
	void setupKeyboardShortcuts();

	// Map between proxy and source indexes.
	QModelIndex toSource(const QModelIndex &proxyIndex) const;
	QModelIndex toProxy(const QModelIndex &sourceIndex) const;

	// --- Context menu builders ---

	void buildRootContextMenu(QMenu &menu, const QModelIndex &sourceIndex);
	void buildDirectoryContextMenu(QMenu &menu, const QModelIndex &sourceIndex);
	void buildDocumentContextMenu(QMenu &menu, const QModelIndex &sourceIndex);
	void buildBookmarkContextMenu(QMenu &menu, const QModelIndex &sourceIndex);

	// --- Actions ---

	void addFilesToDirectory(const QModelIndex &sourceParent);
	void addSubfolder(const QModelIndex &sourceParent);
	void removeSelectedElements();
	void openDocument(const QModelIndex &sourceIndex);
	void renameElement(const QModelIndex &sourceIndex);

	Project *m_project;
	ProjectModel *m_model;
	ProjectFilterModel *m_proxy;
	QTreeView *m_tree;
	QLineEdit *m_search;
};

} // namespace phonometrica

#endif // PHONOMETRICA_FILE_MANAGER_HPP
