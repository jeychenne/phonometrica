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
 * Purpose: Compact filter bar widget for data table views. Displays one row per active filter rule with a column      *
 *          selector, operator selector, value field, and remove button. An "Add rule" button appends new rules.       *
 *          Reusable by DatasetView and ConcordanceView.                                                               *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FILTER_BAR_HPP
#define PHONOMETRICA_FILTER_BAR_HPP

#include <functional>
#include <QWidget>
#include <QVBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <phon/gui/data_filter.hpp>
#include <phon/gui/checkable_combo_box.hpp>

namespace phonometrica {

// Callback to test whether a column is numeric.
// Signature: bool(int column_0based) → true if numeric.
using ColumnTypeCallback = std::function<bool(int)>;

// Callback to retrieve unique text levels for a column.
// Signature: QStringList(int column_0based) → sorted unique values.
using ColumnLevelsCallback = std::function<QStringList(int)>;


class FilterBar final : public QWidget
{
	Q_OBJECT

public:

	explicit FilterBar(DataFilterProxyModel *proxy, QWidget *parent = nullptr);

	// Set column information. Call whenever the source model's columns change.
	void setColumns(const QStringList &headers, ColumnTypeCallback isNumeric,
	                ColumnLevelsCallback getLevels);

	// Rebuild the UI strips from the proxy's current rules (e.g. after clearRules).
	void rebuild();

	// Add a new empty filter rule strip.
	void appendStrip();

signals:

	void ruleCountChanged(int count);

private:

	struct RuleStrip
	{
		QWidget *container = nullptr;
		QComboBox *column_combo = nullptr;
		QComboBox *op_combo = nullptr;
		QLineEdit *value_edit = nullptr;
		CheckableComboBox *set_combo = nullptr;   // hidden unless InSet (multi-select)
		QToolButton *remove_button = nullptr;
		bool is_numeric = false;          // tracks current operator list type
		int set_column = -1;              // column the set_combo was populated for
	};

	RuleStrip createStrip(int ruleIndex);
	void removeStrip(int index);
	void onStripChanged(int index);
	void populateOperators(QComboBox *combo, bool numeric);
	FilterOp opFromIndex(int index, bool numeric) const;
	int opToIndex(FilterOp op, bool numeric) const;
	void updateStripValueWidget(RuleStrip &strip);

	DataFilterProxyModel *m_proxy;
	QVBoxLayout *m_strips_layout = nullptr;
	QPushButton *m_add_button = nullptr;
	QVector<RuleStrip> m_strips;

	QStringList m_headers;
	ColumnTypeCallback m_isNumeric;
	ColumnLevelsCallback m_getLevels;
};

} // namespace phonometrica

#endif // PHONOMETRICA_FILTER_BAR_HPP
