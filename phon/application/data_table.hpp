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
 * Created: 28/02/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: Abstract base class for tabular datasets, where each column represents a variable and each row represents  *
 * an observation. Derived classes are Dataset, which represents a CSV file, and Concordance, which is the base        *
 * for all the types of concordances available in Phonometrica.                                                        *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_DATA_TABLE_HPP
#define PHONOMETRICA_DATA_TABLE_HPP

#include <phon/application/vfs.hpp>

namespace phonometrica {

class Runtime;


/// Lightweight description of a filter rule, stored in the application layer
/// (no Qt dependency). Serialized in the project XML as view metadata.
struct FilterRuleData
{
	String column;          // column header name
	String op;              // ==, !=, <, <=, >, >=, contains, !contains, matches, in
	String value;           // threshold or pattern (unused for "in")
	Array<String> set_values;  // factor levels (only for "in")
};


class DataTable : public Document
{
public:

	explicit DataTable(Class *klass, Directory *parent, String path = String());

	void from_xml(xml_node root, const String &project_dir);

	virtual String get_header(intptr_t j) const = 0;

	virtual String get_cell(intptr_t i, intptr_t j) const = 0;

	virtual void set_cell(intptr_t i, intptr_t j, const String &value) = 0;

	virtual intptr_t row_count() const = 0;

	virtual intptr_t column_count() const = 0;

	virtual bool empty() const = 0;

	virtual void to_csv(const String &path, const String &sep);

	/// Find the 1-based column index whose header matches `name` (case-sensitive).
	/// Returns 0 if not found.
	intptr_t find_column(const String &name) const;

	// ── Saved filter rules ───────────────────────────────────────────

	const Array<FilterRuleData> &filter_rules() const { return m_filter_rules; }
	bool filter_enabled() const { return m_filter_enabled; }

	/// Replace the saved filter rules. Does NOT mark the document as modified
	/// (filters are view metadata, not data changes).
	void set_filter_rules(Array<FilterRuleData> rules, bool enabled);

	/// Clear all saved filter rules.
	void clear_filter_rules();

	static void initialize(Runtime &rt);

protected:

	void metadata_to_xml(xml_node meta_node) override;

	void metadata_from_xml(xml_node meta_node) override;

	bool needs_metadata_node() const override;

private:

	void save_metadata() override;

	bool uses_external_metadata() const override;

	Array<FilterRuleData> m_filter_rules;

	bool m_filter_enabled = true;
};


namespace traits {
template<> struct maybe_cyclic<DataTable> : std::false_type { };
}

} // namespace phonometrica

#endif // PHONOMETRICA_DATA_TABLE_HPP
