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
 * Created: 02/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for computing distance metrics (z-score, modified z-score, etc.) on a numeric column with           *
 *          optional group-by, and optionally auto-creating a filter rule on the result.                               *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_OUTLIER_DIALOG_HPP
#define PHONOMETRICA_OUTLIER_DIALOG_HPP

#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <phon/analysis/column_metrics.hpp>
#include <phon/gui/checkable_combo_box.hpp>

class QStackedWidget;

namespace phonometrica {

class OutlierDialog final : public QDialog
{
	Q_OBJECT

public:

	/// @param numericColumns   Display names of numeric columns (for the column combo).
	/// @param numericIndices   Corresponding 1-based column indices in the dataset.
	/// @param textColumns      Display names of text/factor columns (for group-by combo).
	/// @param textIndices      Corresponding 1-based column indices in the dataset.
	OutlierDialog(const QStringList &numericColumns, const QVector<int> &numericIndices,
	              const QStringList &textColumns, const QVector<int> &textIndices,
	              QWidget *parent = nullptr);

	// Results (valid after accept).
	int selectedColumn() const;                  // 1-based dataset column (univariate)
	QVector<int> selectedColumns() const;        // 1-based dataset columns (multivariate)
	stats::ColumnMetric selectedMetric() const;
	QVector<int> groupByColumns() const;         // 1-based dataset columns (empty = no grouping)
	QString columnName() const;                  // name for the new column
	bool addFilter() const;                      // whether to auto-add a filter rule
	double filterThreshold() const;              // threshold value

private slots:

	void onColumnChanged(int index);
	void onMetricChanged(int index);

private:

	void setupUi(const QStringList &numericColumns, const QStringList &textColumns);
	void updateColumnName();

	QComboBox *m_column_combo = nullptr;
	CheckableComboBox *m_multi_column_combo = nullptr;
	QStackedWidget *m_column_stack = nullptr;
	QComboBox *m_metric_combo = nullptr;
	CheckableComboBox *m_group_combo = nullptr;
	QLineEdit *m_name_edit = nullptr;
	QCheckBox *m_filter_check = nullptr;
	QDoubleSpinBox *m_threshold_spin = nullptr;

	QStringList m_numeric_names;
	QStringList m_text_names;
	QVector<int> m_numeric_indices;
	QVector<int> m_text_indices;
};

} // namespace phonometrica

#endif // PHONOMETRICA_OUTLIER_DIALOG_HPP
