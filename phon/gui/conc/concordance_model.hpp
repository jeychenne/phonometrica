/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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

	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

	// Remove a match and notify the view. Returns the removed match for undo support.
	AutoMatch removeMatch(int row);

	// Restore a previously removed match.
	void restoreMatch(int row, AutoMatch m);

	// Refresh a single row (e.g. after editing an event).
	void refreshRow(int row);

	// Refresh all data (e.g. after a set operation).
	void refreshAll();

	Handle<Concordance> concordance() const { return m_conc; }

private:

	Handle<Concordance> m_conc;
	QFont m_bold_font;
};

} // namespace phonometrica

#endif // PHONOMETRICA_CONCORDANCE_MODEL_HPP
