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
	void datasetCreated(Handle<Dataset> ds);

private slots:

	void onDeleteRows();
	void onDeleteColumns();
	void onExportCsv();

	void onUnion();
	void onIntersection();
	void onComplement();

	void onContextMenu(const QPoint &pos);

private:

	void setupUi();
	void updateCountLabel();
	int selectedRow() const;
	int selectedColumn() const;
	QList<int> selectedRows() const;
	QList<int> selectedColumns() const;
	Handle<Dataset> pickDataset(const QString &title);

	DatasetModel *m_model = nullptr;
	QTableView *m_table = nullptr;
	QToolBar *m_toolbar = nullptr;
	QLabel *m_count_label = nullptr;

	Handle<Dataset> m_ds;
};

} // namespace phonometrica

#endif // PHONOMETRICA_DATASET_VIEW_HPP
