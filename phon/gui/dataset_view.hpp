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
 *          CSV export, delete rows, delete columns, filtering, subset creation, and analysis actions. Data is          *
 *          displayed in a QTableView backed by DatasetModel with a DataFilterProxyModel for sorting and filtering.    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_DATASET_VIEW_HPP
#define PHONOMETRICA_DATASET_VIEW_HPP

#include <QTableView>
#include <QToolBar>
#include <QLabel>
#include <QAction>
#include <phon/gui/view.hpp>
#include <phon/gui/dataset_model.hpp>
#include <phon/gui/data_filter.hpp>
#include <phon/gui/filter_bar.hpp>
#include <phon/gui/outlier_dialog.hpp>
#include <phon/application/conc/concordance.hpp>

namespace phonometrica {

class DatasetView final : public View
{
	Q_OBJECT

public:

	explicit DatasetView(Handle<Dataset> ds, QWidget *parent = nullptr);

	QString label() const override;
	String path() const override;
	Document* document() const override { return m_ds.get(); }
	bool isModified() const override;
	bool save() override;
	void discardChanges() override;
	QString helpAnchor() const override { return QStringLiteral("dataset"); }

signals:

	void requestAnalysis(Handle<DataTable> source);
	void datasetCreated(Handle<Dataset> ds);
	void concordanceCreated(Handle<Concordance> conc);

private slots:

	void onDeleteRows();
	void onDeleteColumns();
	void onExportCsv();

	void onUnion();
	void onIntersection();
	void onComplement();
	void onMerge();

	void onToggleFilter();
	void onClearFilters();
	void onCreateSubset();
	void onAddMetricColumn();

	void onContextMenu(const QPoint &pos);
	void onRenameColumn(int section);
	void onRecodeColumn(int section);
	void onTransformColumn(int section);
	void onDuplicateColumn(int section);
	void onMoveColumn(int section);

private:

	void setupUi();
	void updateCountLabel();
	void setupFilterBar();
	int selectedRow() const;
	int selectedColumn() const;
	QList<int> selectedRows() const;
	QList<int> selectedColumns() const;
	Handle<Dataset> pickDataset(const QString &title);
	Handle<DataTable> pickDataTable(const QString &title);

	DatasetModel *m_model = nullptr;
	DataFilterProxyModel *m_proxy = nullptr;
	FilterBar *m_filter_bar = nullptr;
	QTableView *m_table = nullptr;
	QToolBar *m_toolbar = nullptr;
	QLabel *m_count_label = nullptr;
	QAction *m_filter_action = nullptr;
	QAction *m_clear_filter_action = nullptr;
	QAction *m_subset_action = nullptr;

	Handle<Dataset> m_ds;
};

} // namespace phonometrica

#endif // PHONOMETRICA_DATASET_VIEW_HPP
