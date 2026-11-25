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
 * Purpose: see column_import.hpp.                                                                                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <phon/file.hpp>
#include <phon/application/conc/column_import.hpp>

namespace phonometrica {

namespace {

constexpr std::string_view utf8_bom("\xEF\xBB\xBF");

bool separator_at(std::string_view text, size_t i, std::string_view sep)
{
	return text.compare(i, sep.size(), sep) == 0;
}

String trimmed(const String &s)
{
	String t = s;
	t.trim();
	return t;
}

String default_column_name(intptr_t j)
{
	String name("Column ");
	name.append(String::convert(j + 1).view());
	return name;
}

// Keys are compared after trimming, and optionally case-insensitively.
std::string normalize_key(const String &s, bool ignore_case)
{
	String key = trimmed(s);
	if (ignore_case) key = key.to_lower();
	return std::string(key.data(), size_t(key.size()));
}

bool is_missing_cell(const String &trimmed_value)
{
	return DataTable::is_missing_value_token(trimmed_value.view());
}

} // namespace


CsvTable parse_csv_table(std::string_view text, std::string_view sep, bool has_header)
{
	if (sep.empty()) {
		throw error("The separator cannot be empty");
	}
	if (sep.find_first_of("\"\r\n") != std::string_view::npos) {
		throw error("The separator cannot contain quotes or line breaks");
	}

	CsvTable table;
	const size_t n = text.size();
	size_t i = text.starts_with(utf8_bom) ? utf8_bom.size() : 0;
	intptr_t line = 1;
	intptr_t width = -1;
	bool header_pending = has_header;

	auto at_field_end = [&](size_t k) {
		return text[k] == '\n' || text[k] == '\r' || separator_at(text, k, sep);
	};

	while (i < n)
	{
		const intptr_t record_line = line;
		Array<String> record;
		bool any_quoted = false;

		for (;;)
		{
			String field;

			if (i < n && text[i] == '"')
			{
				any_quoted = true;
				const intptr_t quote_line = line;
				i++;

				for (;;)
				{
					size_t q = text.find('"', i);
					if (q == std::string_view::npos) {
						throw error("Line %: a quoted field is not closed", quote_line);
					}
					auto chunk = text.substr(i, q - i);
					line += std::count(chunk.begin(), chunk.end(), '\n');
					field.append(chunk);
					i = q + 1;

					// A doubled quote stands for a literal quote.
					if (i < n && text[i] == '"') {
						field.push_back('"');
						i++;
					}
					else {
						break;
					}
				}
			}

			// Unquoted field, or (malformed but harmless) text after a closing quote.
			size_t start = i;
			while (i < n && !at_field_end(i)) i++;
			field.append(text.substr(start, i - start));
			record.append(std::move(field));

			if (i < n && separator_at(text, i, sep))
			{
				i += sep.size();
				continue;
			}

			// End of record: line break (LF, CRLF or CR) or end of text.
			if (i < n && text[i] == '\r') i++;
			if (i < n && text[i] == '\n') i++;
			line++;
			break;
		}

		// Skip blank lines, including lines made of separators only (spreadsheets sometimes write them at the end).
		if (!any_quoted)
		{
			bool blank = true;
			for (auto &f : record)
			{
				if (!trimmed(f).empty()) {
					blank = false;
					break;
				}
			}
			if (blank) continue;
		}

		if (width < 0) {
			width = record.size();
		}
		else if (record.size() != width) {
			throw error("Line %: expected % fields but found % (is the separator correct?)",
			            record_line, width, record.size());
		}

		if (header_pending)
		{
			header_pending = false;
			for (intptr_t j = 0; j < record.size(); j++)
			{
				String name = trimmed(record[j]);
				table.headers.append(name.empty() ? default_column_name(j) : name);
			}
			continue;
		}

		table.rows.append(std::move(record));
		table.line_numbers.append(record_line);
	}

	if (!has_header)
	{
		for (intptr_t j = 0; j < width; j++) {
			table.headers.append(default_column_name(j));
		}
	}

	return table;
}

CsvTable read_csv_table(const String &path, std::string_view separator, bool has_header)
{
	String text = File::read_all(path);
	return parse_csv_table(text.view(), separator, has_header);
}

String guess_csv_separator(std::string_view text)
{
	intptr_t tabs = 0, semicolons = 0, commas = 0;
	bool in_quotes = false, seen_content = false;
	size_t i = text.starts_with(utf8_bom) ? utf8_bom.size() : 0;

	for (; i < text.size(); i++)
	{
		char c = text[i];
		if (c == '"') {
			in_quotes = !in_quotes;
			seen_content = true;
			continue;
		}
		if (in_quotes) continue;

		if (c == '\n' || c == '\r')
		{
			if (seen_content) break;
			continue;
		}
		if (c == '\t') tabs++;
		else if (c == ';') semicolons++;
		else if (c == ',') commas++;
		if (c != ' ') seen_content = true;
	}

	if (tabs >= semicolons && tabs >= commas) return String("\t");
	return (semicolons >= commas) ? String(";") : String(",");
}


ColumnImportPlan plan_column_import(const Concordance &conc, const CsvTable &csv, const ColumnImportOptions &opts)
{
	const intptr_t nmatch = conc.match_count();
	const intptr_t nrow = csv.row_count();

	for (auto &col : opts.columns)
	{
		if (col.second < 0 || col.second >= csv.column_count()) {
			throw error("Invalid column number % in the file", col.second + 1);
		}
	}

	ColumnImportPlan plan;
	plan.csv_rows.resize(nmatch);
	for (intptr_t mi = 0; mi < nmatch; mi++) {
		plan.csv_rows[mi] = -1;
	}

	// Row numbers shown to the user: the match's first display row, 1-based (rows differ from matches in long layout).
	auto row_number = [&](intptr_t mi) { return conc.first_row_for_match(mi) + 1; };
	auto add_example = [&](String msg) {
		if (plan.examples.size() < 5) plan.examples.append(std::move(msg));
	};

	// Keys of the matches, as displayed (ById and Lookup only), for messages.
	std::vector<String> match_keys;

	if (opts.mode == ColumnImportMode::ByLine)
	{
		if (nrow != nmatch)
		{
			throw error("The file has % data rows but the concordance has % matches: importing by line requires "
			            "exactly one row per match", nrow, nmatch);
		}
		for (intptr_t mi = 0; mi < nmatch; mi++) {
			plan.csv_rows[mi] = mi;
		}
	}
	else
	{
		if (opts.conc_key_column < 0 || opts.conc_key_column >= conc.column_count()) {
			throw error("Invalid key column in the concordance");
		}
		if (opts.csv_key_column < 0 || opts.csv_key_column >= csv.column_count()) {
			throw error("Invalid key column in the file");
		}

		// Index the file's rows by key. Rows with an empty key can't be matched.
		std::unordered_map<std::string, std::vector<intptr_t>> index;
		index.reserve(size_t(nrow));
		for (intptr_t r = 0; r < nrow; r++)
		{
			auto key = normalize_key(csv.rows[r][opts.csv_key_column], opts.ignore_case);
			if (!key.empty()) index[key].push_back(r);
		}

		// A key that occurs on several rows is only a problem if those rows disagree on an imported column.
		std::unordered_set<std::string> checked_keys;
		auto check_duplicates = [&](const std::string &key, const std::vector<intptr_t> &rows, const String &shown) {
			if (!checked_keys.insert(key).second) return;
			auto &first = csv.rows[rows[0]];
			for (size_t k = 1; k < rows.size(); k++)
			{
				auto &other = csv.rows[rows[k]];
				for (auto &col : opts.columns)
				{
					if (trimmed(first[col.second]) != trimmed(other[col.second]))
					{
						throw error("\"%\" occurs on several lines of the file (lines % and %) with different values "
						            "in column \"%\", so it is not clear which line to use",
						            shown, csv.line_numbers[rows[0]], csv.line_numbers[rows[k]],
						            csv.headers[col.second]);
					}
				}
			}
		};

		std::unordered_map<std::string, intptr_t> seen_ids;  // ById: first match carrying each ID
		std::vector<char> used(size_t(nrow), 0);
		match_keys.reserve(size_t(nmatch));

		for (intptr_t mi = 0; mi < nmatch; mi++)
		{
			String shown = trimmed(conc.get_cell(conc.first_row_for_match(mi), opts.conc_key_column));
			match_keys.push_back(shown);
			auto key = normalize_key(shown, opts.ignore_case);
			if (key.empty()) continue;

			if (opts.mode == ColumnImportMode::ById)
			{
				auto [it, inserted] = seen_ids.emplace(key, mi);
				if (!inserted)
				{
					throw error("The ID \"%\" occurs more than once in the concordance (rows % and %). IDs must be "
					            "unique: to copy the same values to several rows, look up by key instead",
					            shown, row_number(it->second), row_number(mi));
				}
			}

			auto it = index.find(key);
			if (it == index.end()) continue;

			auto &rows = it->second;
			if (rows.size() > 1) check_duplicates(key, rows, shown);
			plan.csv_rows[mi] = rows[0];
			for (auto r : rows) used[size_t(r)] = 1;
		}

		if (opts.mode == ColumnImportMode::ById)
		{
			for (intptr_t r = 0; r < nrow; r++)
			{
				if (!used[size_t(r)] && !normalize_key(csv.rows[r][opts.csv_key_column], false).empty()) {
					plan.unused_rows++;
				}
			}
		}
	}

	// Count (and describe a few) missing values.
	for (intptr_t mi = 0; mi < nmatch; mi++)
	{
		intptr_t r = plan.csv_rows[mi];

		if (r < 0)
		{
			plan.unmatched++;
			if (match_keys[size_t(mi)].empty()) {
				add_example(String::format("Row %d: the key is empty", int(row_number(mi))));
			}
			else
			{
				String msg = String::format("Row %d: \"", int(row_number(mi)));
				msg.append(match_keys[size_t(mi)].view());
				msg.append("\" was not found in the file");
				add_example(std::move(msg));
			}
			continue;
		}

		plan.matched++;
		for (auto &col : opts.columns)
		{
			if (is_missing_cell(trimmed(csv.rows[r][col.second])))
			{
				plan.missing_cells++;
				String msg = String::format("Row %d: column \"", int(row_number(mi)));
				msg.append(csv.headers[col.second].view());
				msg.append(String::format("\" has no value on line %d of the file", int(csv.line_numbers[r])).view());
				add_example(std::move(msg));
			}
		}
	}

	return plan;
}

ColumnImportPlan import_columns(Concordance &conc, const CsvTable &csv, const ColumnImportOptions &opts)
{
	if (opts.columns.empty()) {
		throw error("Cannot import columns: no column was selected");
	}

	{
		Array<String> names;
		for (auto &col : opts.columns) names.append(col.first);
		conc.check_new_column_names(names, "Cannot import columns");
	}

	auto plan = plan_column_import(conc, csv, opts);

	if (opts.missing == MissingValuePolicy::Error && plan.has_missing())
	{
		String msg("Cannot import columns because some values are missing:");
		if (plan.unmatched > 0) {
			msg.append(String::format("\n- %d row(s) of the concordance have no matching line in the file",
			                          int(plan.unmatched)).view());
		}
		if (plan.missing_cells > 0) {
			msg.append(String::format("\n- %d cell(s) are empty or NA in the file", int(plan.missing_cells)).view());
		}
		msg.append("\n\nFor example:");
		for (auto &ex : plan.examples)
		{
			msg.append("\n");
			msg.append(ex.view());
		}
		throw error(msg.data());
	}

	String fill;
	switch (opts.missing)
	{
	case MissingValuePolicy::NaN:    fill = String("nan"); break;
	case MissingValuePolicy::Custom: fill = opts.custom_value; break;
	default:                         break;  // empty
	}

	const intptr_t nmatch = conc.match_count();
	Array<Concordance::AuxColumn> columns;

	for (auto &col : opts.columns)
	{
		std::vector<String> cells;
		cells.reserve(size_t(nmatch));

		for (intptr_t mi = 0; mi < nmatch; mi++)
		{
			intptr_t r = plan.csv_rows[mi];
			if (r < 0) {
				cells.push_back(fill);
				continue;
			}
			String value = trimmed(csv.rows[r][col.second]);
			cells.push_back(is_missing_cell(value) ? fill : value);
		}

		// The column is numeric if every non-missing value is a number.
		bool any_value = false, numeric = true;
		for (auto &v : cells)
		{
			if (DataTable::is_missing_value_token(v.view())) continue;
			any_value = true;
			bool ok = false;
			String::to_float(v.view(), &ok);
			if (!ok) {
				numeric = false;
				break;
			}
		}

		Concordance::AuxColumn aux;
		aux.header = col.first;

		if (any_value && numeric)
		{
			aux.type = Concordance::AuxColumnType::Numeric;
			aux.num_data.resize(nmatch);
			for (intptr_t mi = 0; mi < nmatch; mi++) {
				aux.num_data[mi] = DataTable::parse_numeric_cell(cells[size_t(mi)].view());
			}
		}
		else
		{
			aux.type = Concordance::AuxColumnType::Text;
			aux.text_data.resize(nmatch);
			for (intptr_t mi = 0; mi < nmatch; mi++) {
				aux.text_data[mi] = std::move(cells[size_t(mi)]);
			}
		}

		columns.append(std::move(aux));
	}

	conc.append_aux_columns(std::move(columns));

	return plan;
}

} // namespace phonometrica
