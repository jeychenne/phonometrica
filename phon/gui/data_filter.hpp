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
 * Purpose: Filter proxy model for data tables. Sits between a DatasetModel or ConcordanceModel and the QTableView,   *
 *          providing non-destructive row filtering based on user-defined column rules, plus free column sorting.       *
 *          Rules can be combined with AND (default) or OR logic.                                                      *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_DATA_FILTER_HPP
#define PHONOMETRICA_DATA_FILTER_HPP

#include <QSortFilterProxyModel>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

namespace phonometrica {

// ─── Filter rule ────────────────────────────────────────────────────

enum class FilterOp
{
	Eq,            // == (numeric or exact text)
	Ne,            // !=
	Lt,            // <  (numeric only)
	Le,            // <= (numeric only)
	Gt,            // >  (numeric only)
	Ge,            // >= (numeric only)
	Contains,      // text contains substring (case-insensitive)
	NotContains,   // text does not contain substring
	Regex,         // text matches regex
	InSet          // text is one of a set of values (factor levels)
};

// How multiple rules combine.
enum class FilterLogic
{
	And,   // row must pass every rule (default)
	Or     // row passes if any rule matches
};

struct FilterRule
{
	int column = 0;                    // 0-based column index
	FilterOp op = FilterOp::Eq;
	QString value;                     // threshold or pattern
	QStringList set_values;            // for InSet
	QRegularExpression regex;          // compiled regex (for Regex op)
	double numeric_value = 0.0;        // pre-parsed (for numeric ops)
	bool numeric_valid = false;        // whether numeric_value was parsed OK

	// Call after setting value/op to pre-compute derived fields.
	void prepare();
};

// ─── Proxy model ────────────────────────────────────────────────────

class DataFilterProxyModel final : public QSortFilterProxyModel
{
	Q_OBJECT

public:

	explicit DataFilterProxyModel(QObject *parent = nullptr);

	// Rule management.
	void addRule(const FilterRule &rule);
	void setRule(int index, const FilterRule &rule);
	void removeRule(int index);
	void clearRules();
	const QVector<FilterRule> &rules() const { return m_rules; }
	int ruleCount() const { return m_rules.size(); }

	// Enable / disable filtering (toggle button).
	void setFilterEnabled(bool enabled);
	bool isFilterEnabled() const { return m_enabled; }

	// Logic used to combine rules.
	void setLogic(FilterLogic logic);
	FilterLogic logic() const { return m_logic; }

	// Adjust rule column indices after structural column changes.
	// col is 0-based. Rules targeting the removed column are deleted.
	void adjustAfterColumnRemove(int col);
	// col is 0-based insertion point.
	void adjustAfterColumnInsert(int col);
	// from/to are 0-based; models a remove-at-from then insert-at-to.
	void adjustAfterColumnMove(int from, int to);

	// Number of rows passing the filter in the source model.
	int visibleRowCount() const;

	// Collect source-model row indices that pass the filter (for subset creation).
	// Returns 0-based source row indices.
	QVector<int> visibleSourceRows() const;

	// Evaluate the current filter rules against a source-model row,
	// ignoring the enabled flag. Returns true if the row passes under
	// the current logic. If there are no rules, returns true.
	// Used by the "Add boolean column" feature so the computed flag
	// reflects rule matching regardless of whether the filter is active.
	bool evaluateRow(int sourceRow) const;

signals:

	void filterChanged();
	void logicChanged(FilterLogic logic);

protected:

	bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

	// Enable numeric-aware sorting: if both values parse as doubles, compare numerically.
	bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:

	bool matchesRule(int sourceRow, const FilterRule &rule) const;

	// Evaluate all rules under the current logic (ignoring m_enabled).
	bool evaluateRules(int sourceRow) const;

	QVector<FilterRule> m_rules;
	bool m_enabled = true;
	FilterLogic m_logic = FilterLogic::And;
};

// Convert between FilterOp and serialization strings used by FilterRuleData.
// Strings: "==", "!=", "<", "<=", ">", ">=", "contains", "!contains", "matches", "in".
const char *filter_op_to_string(FilterOp op);
FilterOp string_to_filter_op(const char *s);

// Convert between FilterLogic and serialization strings ("and" / "or").
const char *filter_logic_to_string(FilterLogic logic);
FilterLogic string_to_filter_logic(const char *s);

} // namespace phonometrica

#endif // PHONOMETRICA_DATA_FILTER_HPP
