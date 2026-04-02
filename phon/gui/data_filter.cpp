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

#include <phon/gui/data_filter.hpp>

namespace phonometrica {

// ─── FilterRule ─────────────────────────────────────────────────────

void FilterRule::prepare()
{
	if (op == FilterOp::Regex) {
		regex = QRegularExpression(value, QRegularExpression::CaseInsensitiveOption);
	}

	// Pre-parse the numeric value for comparison operators.
	numeric_valid = false;
	if (op == FilterOp::Eq || op == FilterOp::Ne ||
	    op == FilterOp::Lt || op == FilterOp::Le ||
	    op == FilterOp::Gt || op == FilterOp::Ge)
	{
		bool ok;
		numeric_value = value.toDouble(&ok);
		numeric_valid = ok;
	}
}

// ─── DataFilterProxyModel ───────────────────────────────────────────

DataFilterProxyModel::DataFilterProxyModel(QObject *parent) :
	QSortFilterProxyModel(parent)
{
	setDynamicSortFilter(true);
}

void DataFilterProxyModel::addRule(const FilterRule &rule)
{
	m_rules.append(rule);
	m_rules.last().prepare();
	invalidateFilter();
	emit filterChanged();
}

void DataFilterProxyModel::setRule(int index, const FilterRule &rule)
{
	if (index < 0 || index >= m_rules.size()) return;
	m_rules[index] = rule;
	m_rules[index].prepare();
	invalidateFilter();
	emit filterChanged();
}

void DataFilterProxyModel::removeRule(int index)
{
	if (index < 0 || index >= m_rules.size()) return;
	m_rules.removeAt(index);
	invalidateFilter();
	emit filterChanged();
}

void DataFilterProxyModel::clearRules()
{
	if (m_rules.isEmpty()) return;
	m_rules.clear();
	invalidateFilter();
	emit filterChanged();
}

void DataFilterProxyModel::setFilterEnabled(bool enabled)
{
	if (m_enabled == enabled) return;
	m_enabled = enabled;
	invalidateFilter();
	emit filterChanged();
}

int DataFilterProxyModel::visibleRowCount() const
{
	return rowCount();
}

QVector<int> DataFilterProxyModel::visibleSourceRows() const
{
	QVector<int> rows;
	rows.reserve(rowCount());

	for (int i = 0; i < rowCount(); i++) {
		rows.append(mapToSource(index(i, 0)).row());
	}

	return rows;
}

bool DataFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
	Q_UNUSED(sourceParent)

	if (!m_enabled || m_rules.isEmpty())
		return true;

	// AND logic: every rule must pass.
	for (auto &rule : m_rules) {
		if (!matchesRule(sourceRow, rule))
			return false;
	}

	return true;
}

bool DataFilterProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
	auto left_data = sourceModel()->data(left, Qt::DisplayRole).toString();
	auto right_data = sourceModel()->data(right, Qt::DisplayRole).toString();

	// Try numeric comparison first.
	bool left_ok, right_ok;
	double lv = left_data.toDouble(&left_ok);
	double rv = right_data.toDouble(&right_ok);

	if (left_ok && right_ok) {
		return lv < rv;
	}

	// Fall back to case-insensitive string comparison.
	return QString::compare(left_data, right_data, Qt::CaseInsensitive) < 0;
}

bool DataFilterProxyModel::matchesRule(int sourceRow, const FilterRule &rule) const
{
	// An empty value is a no-op: the rule passes all rows.
	// This prevents hiding everything when the user hasn't typed anything yet.
	// For InSet, nothing checked also means no filtering (show all).
	if (rule.op != FilterOp::InSet && rule.value.isEmpty())
		return true;
	if (rule.op == FilterOp::InSet && rule.set_values.isEmpty())
		return true;

	auto idx = sourceModel()->index(sourceRow, rule.column);
	auto cell = sourceModel()->data(idx, Qt::DisplayRole).toString();

	switch (rule.op)
	{
	case FilterOp::Eq:
	{
		// Try numeric comparison if the rule value parsed OK.
		if (rule.numeric_valid) {
			bool ok;
			double cv = cell.toDouble(&ok);
			if (ok) return cv == rule.numeric_value;
		}
		return cell.compare(rule.value, Qt::CaseInsensitive) == 0;
	}
	case FilterOp::Ne:
	{
		if (rule.numeric_valid) {
			bool ok;
			double cv = cell.toDouble(&ok);
			if (ok) return cv != rule.numeric_value;
		}
		return cell.compare(rule.value, Qt::CaseInsensitive) != 0;
	}
	case FilterOp::Lt:
	{
		bool ok;
		double cv = cell.toDouble(&ok);
		return ok && cv < rule.numeric_value;
	}
	case FilterOp::Le:
	{
		bool ok;
		double cv = cell.toDouble(&ok);
		return ok && cv <= rule.numeric_value;
	}
	case FilterOp::Gt:
	{
		bool ok;
		double cv = cell.toDouble(&ok);
		return ok && cv > rule.numeric_value;
	}
	case FilterOp::Ge:
	{
		bool ok;
		double cv = cell.toDouble(&ok);
		return ok && cv >= rule.numeric_value;
	}
	case FilterOp::Contains:
		return cell.contains(rule.value, Qt::CaseInsensitive);

	case FilterOp::NotContains:
		return !cell.contains(rule.value, Qt::CaseInsensitive);

	case FilterOp::Regex:
		return rule.regex.isValid() && rule.regex.match(cell).hasMatch();

	case FilterOp::InSet:
	{
		for (auto &v : rule.set_values) {
			if (cell.compare(v, Qt::CaseInsensitive) == 0) return true;
		}
		return false;
	}
	}

	return true;
}

} // namespace phonometrica
