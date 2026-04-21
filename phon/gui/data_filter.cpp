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

void DataFilterProxyModel::setLogic(FilterLogic logic)
{
	if (m_logic == logic) return;
	m_logic = logic;
	invalidateFilter();
	emit logicChanged(m_logic);
	emit filterChanged();
}

void DataFilterProxyModel::adjustAfterColumnRemove(int col)
{
	bool changed = false;

	for (int i = m_rules.size() - 1; i >= 0; i--)
	{
		if (m_rules[i].column == col)
		{
			m_rules.removeAt(i);
			changed = true;
		}
		else if (m_rules[i].column > col)
		{
			m_rules[i].column--;
			changed = true;
		}
	}

	if (changed)
	{
		invalidateFilter();
		emit filterChanged();
	}
}

void DataFilterProxyModel::adjustAfterColumnInsert(int col)
{
	bool changed = false;

	for (auto &rule : m_rules)
	{
		if (rule.column >= col)
		{
			rule.column++;
			changed = true;
		}
	}

	if (changed)
	{
		invalidateFilter();
		emit filterChanged();
	}
}

void DataFilterProxyModel::adjustAfterColumnMove(int from, int to)
{
	// Model a remove-at-from then insert-at-to.
	for (auto &rule : m_rules)
	{
		if (rule.column == from)
		{
			// This rule's column was moved. After remove, indices shift down
			// for columns > from. Then insert at to shifts indices up for >= to.
			// The moved column lands at position to.
			rule.column = to;
		}
		else
		{
			// Simulate the remove: shift down if after the removed column.
			int c = rule.column;
			if (c > from) c--;
			// Simulate the insert: shift up if at or after the insertion point.
			if (c >= to) c++;
			rule.column = c;
		}
	}

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

bool DataFilterProxyModel::evaluateRow(int sourceRow) const
{
	// Ignore m_enabled here: the caller (e.g. the "Add boolean column"
	// feature) wants to know what the rules say regardless of whether
	// the filter toggle is currently on or off.
	return evaluateRules(sourceRow);
}

bool DataFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
	Q_UNUSED(sourceParent)

	if (!m_enabled || m_rules.isEmpty())
		return true;

	return evaluateRules(sourceRow);
}

bool DataFilterProxyModel::evaluateRules(int sourceRow) const
{
	if (m_rules.isEmpty())
		return true;

	if (m_logic == FilterLogic::Or)
	{
		// OR: the row passes if any rule matches.
		for (auto &rule : m_rules) {
			if (matchesRule(sourceRow, rule))
				return true;
		}
		return false;
	}

	// AND (default): every rule must pass.
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

// ─── FilterOp ↔ String conversions ──────────────────────────────────

const char *filter_op_to_string(FilterOp op)
{
	switch (op)
	{
	case FilterOp::Eq:          return "==";
	case FilterOp::Ne:          return "!=";
	case FilterOp::Lt:          return "<";
	case FilterOp::Le:          return "<=";
	case FilterOp::Gt:          return ">";
	case FilterOp::Ge:          return ">=";
	case FilterOp::Contains:    return "contains";
	case FilterOp::NotContains: return "!contains";
	case FilterOp::Regex:       return "matches";
	case FilterOp::InSet:       return "in";
	}
	return "==";
}

FilterOp string_to_filter_op(const char *s)
{
	std::string_view sv(s);
	if (sv == "==")         return FilterOp::Eq;
	if (sv == "!=")         return FilterOp::Ne;
	if (sv == "<")          return FilterOp::Lt;
	if (sv == "<=")         return FilterOp::Le;
	if (sv == ">")          return FilterOp::Gt;
	if (sv == ">=")         return FilterOp::Ge;
	if (sv == "contains")   return FilterOp::Contains;
	if (sv == "!contains")  return FilterOp::NotContains;
	if (sv == "matches")    return FilterOp::Regex;
	if (sv == "in")         return FilterOp::InSet;
	return FilterOp::Eq;
}

// ─── FilterLogic ↔ String conversions ───────────────────────────────

const char *filter_logic_to_string(FilterLogic logic)
{
	switch (logic)
	{
	case FilterLogic::And: return "and";
	case FilterLogic::Or:  return "or";
	}
	return "and";
}

FilterLogic string_to_filter_logic(const char *s)
{
	std::string_view sv(s);
	if (sv == "or") return FilterLogic::Or;
	return FilterLogic::And;
}

} // namespace phonometrica
