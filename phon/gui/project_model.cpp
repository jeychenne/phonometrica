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

#include <QApplication>
#include <QStyle>
#include <QIODevice>
#include <QDataStream>
#include <QFileIconProvider>
#include <phon/gui/project_model.hpp>
#include <phon/application/project.hpp>
#include <phon/application/analysis.hpp>

namespace phonometrica {

static constexpr const char *MIME_TYPE = "application/x-phonometrica-elements";


ProjectModel::ProjectModel(Project *project, QObject *parent) :
	QAbstractItemModel(parent), m_project(project)
{

}

// ---------------------------------------------------------
//  Core model interface
// ---------------------------------------------------------

QModelIndex ProjectModel::index(int row, int column, const QModelIndex &parent) const
{
	if (column != 0)
		return {};

	if (!parent.isValid())
	{
		// Children of the invisible root = the 5 root directories.
		if (row >= 0 && row < ROOT_COUNT)
			return createIndex(row, 0, rootAt(row));
		return {};
	}

	auto *elem = elementFromIndex(parent);
	auto *dir = dynamic_cast<Directory *>(elem);
	if (!dir || row < 0 || row >= (int) dir->size())
		return {};

	// Directory::get() is 1-based.
	auto *child = dir->get(row + 1).get();
	return createIndex(row, 0, child);
}

QModelIndex ProjectModel::parent(const QModelIndex &index) const
{
	if (!index.isValid())
		return {};

	auto *elem = elementFromIndex(index);
	auto *parentDir = elem->parent();

	if (!parentDir)
	{
		// This element is one of the 5 root directories — its parent is the invisible root.
		return {};
	}

	// Find the row of parentDir in *its* parent.
	int row = rowOfElement(parentDir);
	if (row < 0)
		return {};

	return createIndex(row, 0, parentDir);
}

int ProjectModel::rowCount(const QModelIndex &parent) const
{
	if (!parent.isValid())
		return ROOT_COUNT;

	auto *elem = elementFromIndex(parent);
	auto *dir = dynamic_cast<Directory *>(elem);

	return dir ? (int) dir->size() : 0;
}

int ProjectModel::columnCount(const QModelIndex &) const
{
	return 1;
}

QVariant ProjectModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid())
		return {};

	auto *elem = elementFromIndex(index);

	switch (role)
	{
	case Qt::DisplayRole:
	case Qt::EditRole:
	{
		auto label = elem->label();
		auto qlabel = QString::fromUtf8(label.data(), (int) label.size());
		auto *doc = dynamic_cast<Document *>(elem);
		if (doc && doc->modified())
			qlabel += QStringLiteral(" *");
		return qlabel;
	}
	case Qt::DecorationRole:
		return iconForElement(elem);

	case Qt::ToolTipRole:
	{
		auto *doc = dynamic_cast<Document *>(elem);
		if (doc && doc->has_path())
		{
			auto &path = doc->path();
			return QString::fromUtf8(path.data(), (int) path.size());
		}
		return {};
	}
	default:
		return {};
	}
}

bool ProjectModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
	if (role != Qt::EditRole || !index.isValid())
		return false;

	auto *elem = elementFromIndex(index);
	auto *dir = dynamic_cast<Directory *>(elem);

	// Only directories (non-root) can be renamed.
	if (!dir || isRootDirectory(index))
		return false;

	auto text = value.toString().toUtf8();
	dir->set_label(String(text.constData(), text.size()));

	if (m_project)
		m_project->modify();

	emit dataChanged(index, index, {Qt::DisplayRole});
	return true;
}

Qt::ItemFlags ProjectModel::flags(const QModelIndex &index) const
{
	if (!index.isValid())
		return Qt::ItemIsDropEnabled;

	Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

	auto *elem = elementFromIndex(index);

	// All items are draggable except root directories.
	if (!isRootDirectory(index))
		f |= Qt::ItemIsDragEnabled;

	// Directories accept drops.
	if (dynamic_cast<Directory *>(elem))
		f |= Qt::ItemIsDropEnabled;

	// Non-root directories are editable (rename).
	if (dynamic_cast<Directory *>(elem) && !isRootDirectory(index))
		f |= Qt::ItemIsEditable;

	return f;
}

// ---------------------------------------------------------
//  Drag and drop
// ---------------------------------------------------------

Qt::DropActions ProjectModel::supportedDropActions() const
{
	return Qt::MoveAction;
}

QStringList ProjectModel::mimeTypes() const
{
	return { MIME_TYPE };
}

QMimeData *ProjectModel::mimeData(const QModelIndexList &indexes) const
{
	auto *mime = new QMimeData;
	QByteArray encoded;
	QDataStream stream(&encoded, QIODevice::WriteOnly);

	for (auto &index : indexes)
	{
		if (index.isValid() && index.column() == 0)
			stream << (quintptr) elementFromIndex(index);
	}

	mime->setData(MIME_TYPE, encoded);
	return mime;
}

bool ProjectModel::canDropMimeData(const QMimeData *data, Qt::DropAction action,
                                   int /*row*/, int /*column*/, const QModelIndex &parent) const
{
	if (action != Qt::MoveAction || !data->hasFormat(MIME_TYPE))
		return false;

	// Must drop onto a directory.
	if (!parent.isValid())
		return false; // dropping onto the invisible root is not allowed

	auto *target = elementFromIndex(parent);
	return dynamic_cast<Directory *>(target) != nullptr;
}

bool ProjectModel::dropMimeData(const QMimeData *data, Qt::DropAction action,
                                int row, int /*column*/, const QModelIndex &parent)
{
	if (action != Qt::MoveAction || !data->hasFormat(MIME_TYPE))
		return false;

	auto *targetDir = dynamic_cast<Directory *>(elementFromIndex(parent));
	if (!targetDir)
		return false;

	QByteArray encoded = data->data(MIME_TYPE);
	QDataStream stream(&encoded, QIODevice::ReadOnly);

	// Collect all elements to move.
	std::vector<Element *> elements;
	while (!stream.atEnd())
	{
		quintptr ptr;
		stream >> ptr;
		elements.push_back(reinterpret_cast<Element *>(ptr));
	}

	// Process moves one by one.
	for (auto *elem : elements)
	{
		auto *srcDir = elem->parent();
		if (!srcDir)
			continue; // cannot move a root directory

		// Don't move into the same position.
		if (srcDir == targetDir)
			continue;

		int srcRow = rowOfElement(elem);
		if (srcRow < 0)
			continue;

		QModelIndex srcParent = indexFromElement(srcDir);

		// Keep the element alive during the move.
		Handle<Element> handle(elem);

		// Remove from source.
		beginRemoveRows(srcParent, srcRow, srcRow);
		srcDir->remove(handle);
		endRemoveRows();

		// Insert into target.
		int dstRow = (row >= 0 && row <= (int) targetDir->size()) ? row : (int) targetDir->size();
		QModelIndex dstParent = parent; // use the parent index we were given

		beginInsertRows(dstParent, dstRow, dstRow);
		// Insert at 1-based position; -1 means append.
		if (dstRow >= (int) targetDir->size())
			targetDir->append(std::move(handle));
		else
			targetDir->insert(dstRow + 1, std::move(handle));
		endInsertRows();
	}

	if (m_project)
		m_project->modify();

	return true;
}

// ---------------------------------------------------------
//  Helpers
// ---------------------------------------------------------

Element *ProjectModel::elementFromIndex(const QModelIndex &index) const
{
	if (!index.isValid())
		return nullptr;
	return static_cast<Element *>(index.internalPointer());
}

QModelIndex ProjectModel::indexFromElement(Element *elem) const
{
	if (!elem)
		return {};

	int row = rowOfElement(elem);
	if (row < 0)
		return {};

	return createIndex(row, 0, elem);
}

bool ProjectModel::isRootDirectory(const QModelIndex &index) const
{
	if (!index.isValid())
		return false;

	auto *elem = elementFromIndex(index);
	for (int i = 0; i < ROOT_COUNT; i++)
	{
		if (rootAt(i) == elem)
			return true;
	}
	return false;
}

void ProjectModel::refresh()
{
	beginResetModel();
	endResetModel();
}

QModelIndex ProjectModel::appendElement(Handle<Element> elem, const QModelIndex &parentIndex)
{
	auto *dir = dynamic_cast<Directory *>(elementFromIndex(parentIndex));
	if (!dir)
		return {};

	int row = (int) dir->size();

	beginInsertRows(parentIndex, row, row);
	dir->append(std::move(elem));
	endInsertRows();

	if (m_project)
		m_project->modify();

	return index(row, 0, parentIndex);
}

void ProjectModel::removeElement(const QModelIndex &index)
{
	if (!index.isValid() || isRootDirectory(index))
		return;

	auto *elem = elementFromIndex(index);
	auto *parentDir = elem->parent();
	if (!parentDir)
		return;

	int row = index.row();
	QModelIndex parentIndex = parent(index);

	// Keep alive during removal.
	Handle<Element> handle(elem);

	beginRemoveRows(parentIndex, row, row);
	parentDir->remove(handle);
	endRemoveRows();

	// Null the parent pointer so the element is fully detached.
	elem->set_parent(nullptr, false);

	if (m_project)
		m_project->modify();
}

QModelIndex ProjectModel::addSubfolder(const QString &name, const QModelIndex &parentIndex)
{
	auto *dir = dynamic_cast<Directory *>(elementFromIndex(parentIndex));
	if (!dir)
		return {};

	auto bytes = name.toUtf8();
	auto label = String(bytes.constData(), bytes.size());
	auto folder = make_handle<Directory>(dir, std::move(label));

	return appendElement(std::move(folder), parentIndex);
}

int ProjectModel::rowOfElement(Element *elem) const
{
	if (!elem)
		return -1;

	// Check if it's one of the 5 roots.
	for (int i = 0; i < ROOT_COUNT; i++)
	{
		if (rootAt(i) == elem)
			return i;
	}

	// Otherwise, find it in its parent.
	auto *parentDir = elem->parent();
	if (!parentDir)
		return -1;

	for (intptr_t i = 1; i <= parentDir->size(); i++)
	{
		if (parentDir->get(i).get() == elem)
			return (int)(i - 1); // convert 1-based to 0-based
	}

	return -1;
}

Directory *ProjectModel::rootAt(int row) const
{
	if (!m_project)
		return nullptr;

	switch (row)
	{
	case 0: return m_project->corpus().get();
	case 1: return m_project->queries().get();
	case 2: return m_project->scripts().get();
	case 3: return m_project->data().get();
	case 4: return m_project->analyses().get();
	case 5: return m_project->bookmarks().get();
	default: return nullptr;
	}
}

QIcon ProjectModel::iconForElement(Element *elem) const
{
    static QFileIconProvider iconProvider;

    // Root directories get their own icons.
    if (auto *dir = dynamic_cast<Directory *>(elem))
    {
        if (dir == rootAt(0)) return QIcon(":/icons/database.svg");
        if (dir == rootAt(1)) return QIcon(":/icons/search.svg");
        if (dir == rootAt(2)) return QIcon(":/icons/square-terminal.svg");
        if (dir == rootAt(3)) return QIcon(":/icons/sheet.svg");
        if (dir == rootAt(4)) return QIcon(":/icons/statistics.svg");
        if (dir == rootAt(5)) return QIcon(":/icons/book-marked.svg");

        return iconProvider.icon(QFileIconProvider::Folder);
    }

    if (dynamic_cast<Sound *>(elem))
    	return QIcon(":/icons/file-volume.svg");

    if (dynamic_cast<Annotation *>(elem))
    	return QIcon(":/icons/file-text.svg");

    if (dynamic_cast<Script *>(elem))
        return QIcon(":/icons/file-terminal.svg");

    if (dynamic_cast<Bookmark *>(elem))
        return QIcon(":/icons/file-bookmarked.svg");

    if (dynamic_cast<Dataset *>(elem))
        return QIcon(":/icons/file-spreadsheet.svg");

    if (dynamic_cast<Analysis *>(elem))
        return QIcon(":/icons/statistics.svg");

    if (dynamic_cast<Query *>(elem))
        return QIcon(":/icons/file-search-corner.svg");

    if (dynamic_cast<Concordance *>(elem))
        return QIcon(":/icons/scan-eye.svg");

	return iconProvider.icon(QFileInfo("/dummy/file.txt"));
}


} // namespace phonometrica
