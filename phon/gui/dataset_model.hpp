/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 31/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: QAbstractTableModel adapter around a Dataset object. Provides free sorting/selection through QTableView.   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_DATASET_MODEL_HPP
#define PHONOMETRICA_DATASET_MODEL_HPP

#include <QAbstractTableModel>
#include <phon/application/dataset.hpp>

namespace phonometrica {

class DatasetModel final : public QAbstractTableModel
{
	Q_OBJECT

public:

	explicit DatasetModel(Handle<Dataset> ds, QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;

	int columnCount(const QModelIndex &parent = QModelIndex()) const override;

	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

	bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

	Qt::ItemFlags flags(const QModelIndex &index) const override;

	// Remove a row and notify the view.
	void removeRow(int row);

	// Remove a column and notify the view.
	void removeColumn(int col);

	// Refresh all data (e.g. after structural changes).
	void refreshAll();

	Handle<Dataset> dataset() const { return m_ds; }

private:

	Handle<Dataset> m_ds;
};

} // namespace phonometrica

#endif // PHONOMETRICA_DATASET_MODEL_HPP
