/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 31/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: View for displaying and interacting with tabular datasets (CSV files). Provides a toolbar with save,       *
 *          CSV export, delete rows, delete columns, and analysis actions. Data is displayed in a QTableView   *
 *          backed by DatasetModel.                                                                                    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_DATASET_VIEW_HPP
#define PHONOMETRICA_DATASET_VIEW_HPP

#include <QTableView>
#include <QToolBar>
#include <QLabel>
#include <phon/gui/view.hpp>
#include <phon/gui/dataset_model.hpp>

namespace phonometrica {

class DatasetView final : public View
{
	Q_OBJECT

public:

	explicit DatasetView(Handle<Dataset> ds, QWidget *parent = nullptr);

	QString label() const override;
	String path() const override;
	bool isModified() const override;
	bool save() override;
	void discardChanges() override;
	QString helpAnchor() const override { return QStringLiteral("dataset"); }

signals:

	void requestAnalysis(Handle<DataTable> source);

private slots:

	void onDeleteRows();
	void onDeleteColumns();
	void onExportCsv();

	void onContextMenu(const QPoint &pos);

private:

	void setupUi();
	void updateCountLabel();
	int selectedRow() const;
	int selectedColumn() const;
	QList<int> selectedRows() const;
	QList<int> selectedColumns() const;

	DatasetModel *m_model = nullptr;
	QTableView *m_table = nullptr;
	QToolBar *m_toolbar = nullptr;
	QLabel *m_count_label = nullptr;

	Handle<Dataset> m_ds;
};

} // namespace phonometrica

#endif // PHONOMETRICA_DATASET_VIEW_HPP
