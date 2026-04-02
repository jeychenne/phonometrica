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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QStackedWidget>
#include <phon/gui/outlier_dialog.hpp>

namespace phonometrica {

OutlierDialog::OutlierDialog(const QStringList &numericColumns, const QVector<int> &numericIndices,
                             const QStringList &textColumns, const QVector<int> &textIndices,
                             QWidget *parent) :
	QDialog(parent),
	m_numeric_names(numericColumns),
	m_text_names(textColumns),
	m_numeric_indices(numericIndices),
	m_text_indices(textIndices)
{
	setWindowTitle(tr("Add metric column"));
	setMinimumWidth(400);
	setupUi(numericColumns, textColumns);
}

void OutlierDialog::setupUi(const QStringList &numericColumns, const QStringList &textColumns)
{
	auto *layout = new QVBoxLayout(this);

	auto *form = new QFormLayout;
	form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

	// Metric selector (placed first so column widgets can react to it).
	m_metric_combo = new QComboBox;
	m_metric_combo->addItem(tr("Z-score"),
		(int) stats::ColumnMetric::ZScore);
	m_metric_combo->addItem(tr("Modified z-score (robust)"),
		(int) stats::ColumnMetric::ModifiedZScore);
	m_metric_combo->addItem(tr("Absolute z-score"),
		(int) stats::ColumnMetric::AbsZScore);
	m_metric_combo->addItem(tr("Absolute modified z-score (robust)"),
		(int) stats::ColumnMetric::AbsModifiedZScore);
	m_metric_combo->addItem(tr("Percentile rank"),
		(int) stats::ColumnMetric::Percentile);
	m_metric_combo->addItem(tr("Euclidean distance"),
		(int) stats::ColumnMetric::EuclideanDistance);
	m_metric_combo->addItem(tr("Mahalanobis distance"),
		(int) stats::ColumnMetric::MahalanobisDistance);
	// Default to absolute modified z-score (most useful for phonetics).
	m_metric_combo->setCurrentIndex(3);
	form->addRow(tr("Metric:"), m_metric_combo);

	// Column selector: stacked widget switches between single and multi mode.
	m_column_combo = new QComboBox;
	m_column_combo->addItems(numericColumns);

	m_multi_column_combo = new CheckableComboBox;
	m_multi_column_combo->setItems(numericColumns);

	m_column_stack = new QStackedWidget;
	m_column_stack->addWidget(m_column_combo);       // index 0: univariate
	m_column_stack->addWidget(m_multi_column_combo);  // index 1: multivariate
	m_column_stack->setCurrentIndex(0);
	form->addRow(tr("Column(s):"), m_column_stack);

	// Group-by selector (multi-select: nothing checked = no grouping).
	m_group_combo = new CheckableComboBox;
	m_group_combo->setItems(textColumns);
	form->addRow(tr("Group by:"), m_group_combo);

	// Column name.
	m_name_edit = new QLineEdit;
	form->addRow(tr("New column:"), m_name_edit);

	layout->addLayout(form);

	// ── Filter section ──
	layout->addSpacing(8);

	auto *filter_row = new QHBoxLayout;
	m_filter_check = new QCheckBox(tr("Add filter:  value \u2264"));
	m_filter_check->setChecked(true);
	filter_row->addWidget(m_filter_check);

	m_threshold_spin = new QDoubleSpinBox;
	m_threshold_spin->setRange(0.1, 100.0);
	m_threshold_spin->setDecimals(2);
	m_threshold_spin->setSingleStep(0.5);
	filter_row->addWidget(m_threshold_spin);

	filter_row->addStretch();
	layout->addLayout(filter_row);

	connect(m_filter_check, &QCheckBox::toggled, m_threshold_spin, &QWidget::setEnabled);

	// ── Buttons ──
	layout->addSpacing(8);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	// ── Initialize ──
	connect(m_column_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &OutlierDialog::onColumnChanged);
	connect(m_multi_column_combo, &CheckableComboBox::checkedItemsChanged,
	        this, [this](const QStringList &) { updateColumnName(); });
	connect(m_metric_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &OutlierDialog::onMetricChanged);

	updateColumnName();
	onMetricChanged(m_metric_combo->currentIndex());
}

void OutlierDialog::onColumnChanged(int)
{
	updateColumnName();
}

void OutlierDialog::onMetricChanged(int)
{
	auto metric = selectedMetric();
	m_threshold_spin->setValue(stats::default_threshold(metric));

	bool multi = stats::is_multivariate(metric);
	m_column_stack->setCurrentIndex(multi ? 1 : 0);

	updateColumnName();
}

void OutlierDialog::updateColumnName()
{
	auto metric = selectedMetric();
	auto suffix = stats::metric_suffix(metric);

	if (stats::is_multivariate(metric)) {
		auto checked = m_multi_column_combo->checkedItems();
		QString base;
		if (checked.size() == 1) {
			base = checked.first();
		}
		else if (checked.size() <= 3) {
			base = checked.join(QStringLiteral("_"));
		}
		else {
			base = QStringLiteral("multi_%1col").arg(checked.size());
		}
		if (base.isEmpty()) base = QStringLiteral("distance");
		m_name_edit->setText(base + QStringLiteral("_") + QString::fromLatin1(suffix));
	}
	else {
		auto col_name = m_column_combo->currentText();
		m_name_edit->setText(col_name + QStringLiteral("_") + QString::fromLatin1(suffix));
	}
}

int OutlierDialog::selectedColumn() const
{
	int idx = m_column_combo->currentIndex();
	if (idx < 0 || idx >= m_numeric_indices.size()) return 0;
	return m_numeric_indices[idx];
}

QVector<int> OutlierDialog::selectedColumns() const
{
	QVector<int> result;
	auto checked = m_multi_column_combo->checkedItems();
	for (auto &name : checked) {
		int idx = m_numeric_names.indexOf(name);
		if (idx >= 0 && idx < m_numeric_indices.size()) {
			result.append(m_numeric_indices[idx]);
		}
	}
	return result;
}

stats::ColumnMetric OutlierDialog::selectedMetric() const
{
	return (stats::ColumnMetric) m_metric_combo->currentData().toInt();
}

QVector<int> OutlierDialog::groupByColumns() const
{
	QVector<int> result;
	auto checked = m_group_combo->checkedItems();
	for (auto &name : checked) {
		int idx = m_text_names.indexOf(name);
		if (idx >= 0 && idx < m_text_indices.size()) {
			result.append(m_text_indices[idx]);
		}
	}
	return result;
}

QString OutlierDialog::columnName() const
{
	return m_name_edit->text().trimmed();
}

bool OutlierDialog::addFilter() const
{
	return m_filter_check->isChecked();
}

double OutlierDialog::filterThreshold() const
{
	return m_threshold_spin->value();
}

} // namespace phonometrica
