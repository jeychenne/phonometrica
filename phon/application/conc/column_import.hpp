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
 * Created: 24/11/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: import columns from a CSV file into a concordance. Rows of the file are matched to the concordance's       *
 * matches by a unique ID, by line order, or by looking up a key (e.g. a word in a lexicon).                           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_COLUMN_IMPORT_HPP
#define PHONOMETRICA_COLUMN_IMPORT_HPP

#include <string_view>
#include <phon/application/conc/concordance.hpp>

namespace phonometrica {

/// A delimited text file held in memory: column names and data rows, all of the same width.
struct CsvTable
{
	Array<String> headers;
	Array<Array<String>> rows;
	Array<intptr_t> line_numbers;  // 1-based line of the file on which each data row starts (for messages)

	intptr_t row_count() const { return rows.size(); }
	intptr_t column_count() const { return headers.size(); }
};

/// Parse delimited text. Fields may be quoted with double quotes, in which case they can contain the separator, line
/// breaks and doubled quotes (""). Blank lines are skipped and a leading UTF-8 byte order mark is ignored. If
/// `has_header` is false, the columns are named "Column 1", "Column 2", etc. Header names are trimmed, and an empty
/// one is replaced by "Column n". Throws if a row does not have as many fields as the first one.
CsvTable parse_csv_table(std::string_view text, std::string_view separator, bool has_header);

/// Read and parse a delimited text file (see parse_csv_table()).
CsvTable read_csv_table(const String &path, std::string_view separator, bool has_header);

/// Guess the separator of delimited text: whichever of tab, semicolon and comma occurs most often (outside quotes) in
/// the first non-blank line. Ties and lines with none of them yield a tab.
String guess_csv_separator(std::string_view text);


enum class ColumnImportMode
{
	ById,    // one-to-one: a column of unique IDs, both in the concordance and in the file
	ByLine,  // the n-th data row of the file goes to the n-th match
	Lookup   // many-to-one: a key that may repeat in the concordance (e.g. a word) is looked up in the file
};

/// What to put in a cell when the value is missing: the match has no row in the file, or the file's cell is empty or
/// a missing-value token (NA, nan...; see DataTable::is_missing_value_token()).
enum class MissingValuePolicy
{
	Error,   // refuse to import anything
	Empty,   // leave the cell empty
	NaN,     // the numeric missing value "nan"
	Custom   // a value chosen by the user
};

struct ColumnImportOptions
{
	ColumnImportMode mode = ColumnImportMode::ById;

	// Key columns (ById and Lookup only), 0-based. The concordance's key is read as displayed (get_cell()).
	intptr_t conc_key_column = -1;
	intptr_t csv_key_column = -1;

	// Compare keys case-insensitively. Keys are always compared after trimming surrounding white space.
	bool ignore_case = false;

	// Columns to import: (name of the new column, 0-based column in the file).
	Array<std::pair<String, intptr_t>> columns;

	MissingValuePolicy missing = MissingValuePolicy::Empty;
	String custom_value;  // used with MissingValuePolicy::Custom
};

/// How the concordance's matches map onto the file's rows, computed by plan_column_import() without changing anything.
struct ColumnImportPlan
{
	Array<intptr_t> csv_rows;       // for each match: 0-based data row of the file, or -1 if there is none
	intptr_t matched = 0;           // matches that have a row in the file
	intptr_t unmatched = 0;         // matches that have no row in the file
	intptr_t missing_cells = 0;     // missing cells among the imported columns of matched rows
	intptr_t unused_rows = 0;       // rows of the file that no match uses (ById and ByLine only; 0 for Lookup)
	Array<String> examples;         // a few missing values, described for the user (row numbers are 1-based)

	bool has_missing() const { return unmatched > 0 || missing_cells > 0; }
};

/// Work out which row of the file goes with each match. Throws if the mapping cannot be built: invalid columns, a
/// line count that differs from the number of matches (ByLine), an ID that occurs twice in the concordance (ById),
/// or a key that a match uses and that occurs on several rows of the file with different values in the imported
/// columns. Rows whose values are identical in every imported column are not ambiguous, and the first one is used.
ColumnImportPlan plan_column_import(const Concordance &conc, const CsvTable &csv, const ColumnImportOptions &opts);

/// Append the selected columns of the file to the concordance, as auxiliary columns. A column whose values are all
/// numbers (or missing) becomes numeric; any other column is text. Either all the columns are added or, if anything
/// goes wrong (see plan_column_import(), name clashes, missing values with MissingValuePolicy::Error), none is and an
/// error is thrown. Returns the plan that was applied, for reporting.
ColumnImportPlan import_columns(Concordance &conc, const CsvTable &csv, const ColumnImportOptions &opts);

} // namespace phonometrica

#endif // PHONOMETRICA_COLUMN_IMPORT_HPP
