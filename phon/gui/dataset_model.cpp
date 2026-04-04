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
 * Created: 31/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/gui/dataset_model.hpp>

namespace phonometrica {

DatasetModel::DatasetModel(Handle<Dataset> ds, QObject *parent) :
	QAbstractTableModel(parent), m_ds(std::move(ds))
{
}

int DatasetModel::rowCount(const QModelIndex &) const
{
	return (int) m_ds->row_count();
}

int DatasetModel::columnCount(const QModelIndex &) const
{
	return (int) m_ds->column_count();
}

QVariant DatasetModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid()) return {};

	// Dataset uses 1-based indexing.
	intptr_t row = index.row() + 1;
	intptr_t col = index.column() + 1;

	switch (role)
	{
		case Qt::DisplayRole:
		case Qt::EditRole:
		{
			auto text = m_ds->get_cell(row, col);
			return QString::fromUtf8(text.data(), (int) text.size());
		}
		case Qt::TextAlignmentRole:
		{
			if (m_ds->is_numeric(col))
				return QVariant(Qt::AlignRight | Qt::AlignVCenter);
			return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
		}
		default:
			return {};
	}
}

bool DatasetModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
	if (!index.isValid() || role != Qt::EditRole) return false;

	intptr_t row = index.row() + 1;
	intptr_t col = index.column() + 1;

	try
	{
		auto text = String(value.toString().toUtf8().constData());
		m_ds->set_cell(row, col, text);
		m_ds->set_content_modified(true);
		Document::file_modified();
		emit dataChanged(this->index(index.row(), 0),
		                 this->index(index.row(), columnCount() - 1));
		return true;
	}
	catch (...)
	{
		return false;
	}
}

QVariant DatasetModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation == Qt::Horizontal && (role == Qt::DisplayRole || role == Qt::EditRole))
	{
		auto text = m_ds->get_header(section + 1);
		return QString::fromUtf8(text.data(), (int) text.size());
	}

	if (orientation == Qt::Vertical && role == Qt::DisplayRole)
	{
		return section + 1;
	}

	return {};
}

bool DatasetModel::setHeaderData(int section, Qt::Orientation orientation, const QVariant &value, int role)
{
	if (orientation != Qt::Horizontal || role != Qt::EditRole) return false;

	auto new_name = value.toString().trimmed();
	if (new_name.isEmpty()) return false;

	m_ds->set_header(section + 1, String(new_name.toUtf8().constData()));
	m_ds->set_content_modified(true);
	Document::file_modified();
	emit headerDataChanged(Qt::Horizontal, section, section);
	return true;
}

Qt::ItemFlags DatasetModel::flags(const QModelIndex &index) const
{
	auto base = QAbstractTableModel::flags(index);
	if (!index.isValid()) return base;

	return base | Qt::ItemIsEditable;
}

void DatasetModel::removeRow(int row)
{
	beginRemoveRows(QModelIndex(), row, row);
	m_ds->remove_row(row + 1); // 1-based
	endRemoveRows();
}

void DatasetModel::removeColumn(int col)
{
	beginRemoveColumns(QModelIndex(), col, col);
	m_ds->remove_column(col + 1); // 1-based
	endRemoveColumns();
}

Dataset::SavedRow DatasetModel::extractRow(int row)
{
	beginRemoveRows(QModelIndex(), row, row);
	auto saved = m_ds->extract_row(row + 1); // 1-based
	endRemoveRows();
	return saved;
}

void DatasetModel::insertRow(int row, const Dataset::SavedRow &data)
{
	beginInsertRows(QModelIndex(), row, row);
	m_ds->insert_row(row + 1, data); // 1-based
	endInsertRows();
}

Dataset::SavedColumn DatasetModel::extractColumn(int col)
{
	beginRemoveColumns(QModelIndex(), col, col);
	auto saved = m_ds->extract_column(col + 1); // 1-based
	endRemoveColumns();
	return saved;
}

void DatasetModel::insertColumn(int col, Dataset::SavedColumn data)
{
	beginInsertColumns(QModelIndex(), col, col);
	m_ds->insert_column(col + 1, std::move(data)); // 1-based
	endInsertColumns();
}

void DatasetModel::refreshAll()
{
	beginResetModel();
	endResetModel();
}

} // namespace phonometrica
