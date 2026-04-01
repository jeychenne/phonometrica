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
 * Created: 27/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QColor>
#include <phon/gui/conc/concordance_model.hpp>

namespace phonometrica {

ConcordanceModel::ConcordanceModel(Handle<Concordance> conc, QObject *parent) :
	QAbstractTableModel(parent), m_conc(std::move(conc))
{
	m_bold_font.setBold(true);
}

int ConcordanceModel::rowCount(const QModelIndex &) const
{
	return (int) m_conc->row_count();
}

int ConcordanceModel::columnCount(const QModelIndex &) const
{
	return (int) m_conc->column_count();
}

QVariant ConcordanceModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid()) return {};

	// Concordance uses 1-based indexing.
	intptr_t row = index.row() + 1;
	intptr_t col = index.column() + 1;

	switch (role)
	{
		case Qt::DisplayRole:
		case Qt::EditRole:
		{
			auto text = m_conc->get_cell(row, col);
			return QString::fromUtf8(text.data(), (int) text.size());
		}
		case Qt::TextAlignmentRole:
		{
			if (m_conc->is_layer(col) || m_conc->is_target(col))
				return QVariant(Qt::AlignCenter);
			if (m_conc->is_time(col) || m_conc->is_left_context(col))
				return QVariant(Qt::AlignRight | Qt::AlignVCenter);
			return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
		}
		case Qt::FontRole:
		{
			if (m_conc->is_target(col))
				return m_bold_font;
			return {};
		}
		case Qt::ForegroundRole:
		{
			if (m_conc->is_target(col))
				return QColor(Qt::red);
			return {};
		}
		default:
			return {};
	}
}

bool ConcordanceModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
	if (!index.isValid() || role != Qt::EditRole) return false;

	intptr_t row = index.row() + 1;
	intptr_t col = index.column() + 1;

	if (!m_conc->is_editable_measurement(col)) return false;

	try
	{
		auto text = String(value.toString().toUtf8().constData());
		m_conc->set_cell(row, col, text);

		// Emit dataChanged for the whole row so that derived columns (ERB/Bark) update too.
		emit dataChanged(this->index(index.row(), 0),
		                 this->index(index.row(), columnCount() - 1));
		return true;
	}
	catch (...)
	{
		return false;
	}
}

QVariant ConcordanceModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation == Qt::Horizontal)
	{
		if (role == Qt::DisplayRole)
		{
			auto text = m_conc->get_header(section + 1);
			return QString::fromUtf8(text.data(), (int) text.size());
		}
		if (role == Qt::EditRole)
		{
			// Return the default (non-aliased) header for editing
			auto text = m_conc->get_default_header(section + 1);
			return QString::fromUtf8(text.data(), (int) text.size());
		}
	}

	if (orientation == Qt::Vertical && role == Qt::DisplayRole)
	{
		return section + 1;
	}

	return {};
}

bool ConcordanceModel::setHeaderData(int section, Qt::Orientation orientation, const QVariant &value, int role)
{
	if (orientation != Qt::Horizontal || role != Qt::EditRole) return false;

	auto default_hdr = m_conc->get_default_header(section + 1);
	auto new_name = String(value.toString().toUtf8().constData());

	if (new_name.empty() || new_name == default_hdr) {
		// Revert to default
		m_conc->clear_header_alias(default_hdr);
	}
	else {
		m_conc->set_header_alias(default_hdr, new_name);
	}

	emit headerDataChanged(Qt::Horizontal, section, section);
	return true;
}

Qt::ItemFlags ConcordanceModel::flags(const QModelIndex &index) const
{
	auto base = QAbstractTableModel::flags(index);
	if (!index.isValid()) return base;

	intptr_t col = index.column() + 1;

	if (m_conc->is_editable_measurement(col)) {
		return base | Qt::ItemIsEditable;
	}

	return base;
}

AutoMatch ConcordanceModel::removeMatch(int row)
{
	beginRemoveRows(QModelIndex(), row, row);
	auto m = m_conc->remove_match(row + 1);
	endRemoveRows();
	return m;
}

void ConcordanceModel::restoreMatch(int row, AutoMatch m)
{
	beginInsertRows(QModelIndex(), row, row);
	m_conc->restore_match(row + 1, std::move(m));
	endInsertRows();
}

void ConcordanceModel::refreshRow(int row)
{
	emit dataChanged(index(row, 0), index(row, columnCount() - 1));
}

void ConcordanceModel::refreshAll()
{
	beginResetModel();
	endResetModel();
}

} // namespace phonometrica
