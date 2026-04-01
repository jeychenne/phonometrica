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
 * Created: 27/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: QAbstractTableModel adapter around a Concordance object. This avoids the manual wxGrid synchronisation     *
 *          of the wx version and gives us free sorting/selection through QTableView.                                  *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CONCORDANCE_MODEL_HPP
#define PHONOMETRICA_CONCORDANCE_MODEL_HPP

#include <QAbstractTableModel>
#include <QFont>
#include <phon/application/conc/concordance.hpp>

namespace phonometrica {

class ConcordanceModel final : public QAbstractTableModel
{
	Q_OBJECT

public:

	explicit ConcordanceModel(Handle<Concordance> conc, QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;

	int columnCount(const QModelIndex &parent = QModelIndex()) const override;

	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

	bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

	bool setHeaderData(int section, Qt::Orientation orientation, const QVariant &value, int role = Qt::EditRole) override;

	Qt::ItemFlags flags(const QModelIndex &index) const override;

	// Remove a match and notify the view. Returns the removed match for undo support.
	AutoMatch removeMatch(int row);

	// Restore a previously removed match.
	void restoreMatch(int row, AutoMatch m);

	// Refresh a single row (e.g. after editing an event).
	void refreshRow(int row);

	// Refresh all data (e.g. after a set operation or column structure change).
	void refreshAll();

	Handle<Concordance> concordance() const { return m_conc; }

private:

	Handle<Concordance> m_conc;
	QFont m_bold_font;
};

} // namespace phonometrica

#endif // PHONOMETRICA_CONCORDANCE_MODEL_HPP
