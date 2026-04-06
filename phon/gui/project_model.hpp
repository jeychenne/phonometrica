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
 * Purpose: Qt item model wrapping the project's virtual file system. The model exposes the seven root directories     *
 * (Corpus, Queries, Data tables, Analyses, Scripts, Notes, Bookmarks) as top-level items, with their contents        *
 * Element* pointers are stored as internalPointer() in QModelIndex — no duplication of the tree structure.             *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PROJECT_MODEL_HPP
#define PHONOMETRICA_PROJECT_MODEL_HPP

#include <QAbstractItemModel>
#include <QMimeData>
#include <QIcon>
#include <phon/runtime/typed_object.hpp>
#include <phon/application/conc/query.hpp>

namespace phonometrica {

class Project;
class Element;
class Directory;
class Document;

class ProjectModel : public QAbstractItemModel
{
	Q_OBJECT

public:

	static constexpr int ROOT_COUNT = 7;

	explicit ProjectModel(Project *project, QObject *parent = nullptr);

	// --- QAbstractItemModel interface ---

	QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
	QModelIndex parent(const QModelIndex &index) const override;

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	int columnCount(const QModelIndex &parent = QModelIndex()) const override;

	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
	bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

	Qt::ItemFlags flags(const QModelIndex &index) const override;

	// --- Drag and drop ---

	Qt::DropActions supportedDropActions() const override;
	QStringList mimeTypes() const override;
	QMimeData *mimeData(const QModelIndexList &indexes) const override;
	bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
	                     const QModelIndex &parent) const override;
	bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
	                  const QModelIndex &parent) override;

	// --- Helpers ---

	Element *elementFromIndex(const QModelIndex &index) const;
	QModelIndex indexFromElement(Element *elem) const;

	// Returns true if the index corresponds to one of the 5 root directories.
	bool isRootDirectory(const QModelIndex &index) const;

	// Full model reset — call after project open/close.
	void refresh();

	// --- Mutations with proper begin/end signals ---

	// Append an element to the directory at parentIndex. Returns the index of the new element.
	QModelIndex appendElement(Handle<Element> elem, const QModelIndex &parentIndex);

	// Remove the element at the given index.
	void removeElement(const QModelIndex &index);

	// Add a subfolder to the directory at parentIndex.
	QModelIndex addSubfolder(const QString &name, const QModelIndex &parentIndex);

private:

	// Find the 0-based row of an element within its parent directory (or among the 5 roots).
	int rowOfElement(Element *elem) const;

	// Return the root directory at the given 0-based row (0..4).
	Directory *rootAt(int row) const;

	// Icon for a given element.
	QIcon iconForElement(Element *elem) const;

	Project *m_project;
};

} // namespace phonometrica

#endif // PHONOMETRICA_PROJECT_MODEL_HPP
