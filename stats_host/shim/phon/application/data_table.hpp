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
 * Created: 19/07/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: headless stand-in for the application's DataTable (MIGRATION_NOTES step 4b). This header SHADOWS           *
 * phon/application/data_table.hpp in the phon_stats build (the shim include directory precedes the repository root),  *
 * so the analysis layer compiles against the NEW engine without dragging in the old-engine Document/VFS hierarchy.    *
 * It provides exactly the surface phon/analysis consumes — get_cell / row_count / column_count / get_header, all      *
 * 0-based, cells as strings — backed by a separator-delimited text file (tab by default, like the real Dataset).      *
 * Column-type suffixes (.num/.bool/.text) are stripped from headers for name parity; cells keep their raw file text   *
 * (the real Dataset re-stringifies parsed numerics, which only differs for cosmetic forms like "2.0" vs "2";          *
 * the fitting layer re-parses numerics from text either way). Boolean columns are NOT normalized to "true"/"false"    *
 * — none of the statistics test data uses .bool columns.                                                              *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_HEADLESS_DATA_TABLE_HPP
#define PHONOMETRICA_HEADLESS_DATA_TABLE_HPP

#include <phon/array.hpp>
#include <phon/string.hpp>

namespace phonometrica {

class DataTable
{
public:
	DataTable() = default;

	// Load `path` immediately (tab-separated unless the extension is ".csv2"-style
	// comma data; the statistics test data is tab-separated with a .csv extension,
	// exactly like the real Dataset's default separator).
	explicit DataTable(String path, String separator = String("\t"));

	intptr_t row_count() const { return m_rows.size(); }

	intptr_t column_count() const { return m_labels.size(); }

	String get_header(intptr_t j) const { return m_labels.at(j); }

	String get_cell(intptr_t i, intptr_t j) const { return m_rows.at(i).at(j); }

	const String &path() const { return m_path; }

	// Lazy-load parity with the real Document API: data is already in memory.
	void open() {}

	// True if every non-missing cell in column `j` parses as a finite double
	// (the same rule the fitting layer applies row-wise). Used by get_column.
	bool is_numeric(intptr_t j) const;

	// Append a column (the old add_text_column/add_numeric_column surface used by
	// the script-level add_column native). Sizes are checked by the caller.
	void add_text_column(const String &name, const Array<String> &cells);
	// Numeric cells serialize like the real Dataset::get_cell: NaN -> "nan",
	// integral -> integer form, else the double's default conversion.
	void add_numeric_column(const String &name, const double *values, intptr_t n);

private:
	String m_path;
	Array<String> m_labels;
	Array<Array<String>> m_rows;
};

// The script-visible subtype `load()` returns (the real app's CSV-backed table).
// Single non-virtual inheritance so the boxed payload upcasts to DataTable at the
// dispatch boundary.
struct Dataset final : DataTable
{
	using DataTable::DataTable;
};

} // namespace phonometrica

#endif // PHONOMETRICA_HEADLESS_DATA_TABLE_HPP
