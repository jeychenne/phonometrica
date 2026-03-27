/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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

QVariant ConcordanceModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role != Qt::DisplayRole) return {};

	if (orientation == Qt::Horizontal)
	{
		auto text = m_conc->get_header(section + 1);
		return QString::fromUtf8(text.data(), (int) text.size());
	}

	// Row numbers (1-based)
	return section + 1;
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
