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
 * Purpose: implementation of the headless DataTable (see shim/phon/application/data_table.hpp). Mirrors               *
 * utils::parse_csv + Dataset::read_from_csv: skip empty lines, strip a trailing '\r', enforce rectangularity,         *
 * strip .num/.bool/.text header suffixes.                                                                             *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>

#include <phon/application/data_table.hpp>

namespace phonometrica {

static bool is_missing_cell(const String &cell)
{
	return cell.empty() || cell == "nan" || cell == "NaN" || cell == "NA";
}

DataTable::DataTable(String path, String separator) : m_path(std::move(path))
{
	std::ifstream infile(std::string(m_path.data(), static_cast<size_t>(m_path.size())));
	if (!infile.is_open())
		throw std::runtime_error(std::string("Cannot open data file: ") +
		                         std::string(m_path.data(), static_cast<size_t>(m_path.size())));

	std::string line;
	bool have_header = false;
	intptr_t ncol = 0;
	intptr_t lineno = 0;

	while (std::getline(infile, line))
	{
		++lineno;
		// Strip a UTF-8 BOM (the real Dataset reads through File, whose encoding
		// sniffing consumes it; several shipped data files carry one).
		if (lineno == 1 && line.size() >= 3 && line[0] == '\xEF' && line[1] == '\xBB' &&
		    line[2] == '\xBF')
			line.erase(0, 3);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty())
			continue;

		String row_text(line.data(), static_cast<intptr_t>(line.size()));
		Array<String> fields = row_text.split(separator);

		if (!have_header)
		{
			// Strip the real Dataset's column-type suffixes (ASCII, so grapheme
			// count == byte count and left() is exact).
			auto strip_suffix = [](const String &label, Substring suffix) -> String {
				if (label.ends_with(suffix))
					return label.left(label.length() - static_cast<intptr_t>(suffix.size()));
				return label;
			};
			for (String &label : fields)
			{
				String clean = strip_suffix(label, ".num");
				clean = strip_suffix(clean, ".bool");
				clean = strip_suffix(clean, ".text");
				m_labels.append(std::move(clean));
			}
			ncol = m_labels.size();
			have_header = true;
			continue;
		}

		if (fields.size() != ncol)
			throw std::runtime_error(
			    std::string("Inconsistent number of columns in data file on line ") +
			    std::to_string(lineno) + " (expected " + std::to_string(ncol) + ", got " +
			    std::to_string(fields.size()) + ")");
		m_rows.append(std::move(fields));
	}
}

void DataTable::add_text_column(const String &name, const Array<String> &cells)
{
	m_labels.append(name);
	for (intptr_t i = 0; i < m_rows.size(); ++i)
		m_rows[i].append(cells[i]);
}

void DataTable::add_numeric_column(const String &name, const double *values, intptr_t n)
{
	m_labels.append(name);
	for (intptr_t i = 0; i < n; ++i)
	{
		double val = values[i];
		String cell;
		if (std::isnan(val))
			cell = String("nan");
		else if (std::isfinite(val) && val == std::floor(val))
			cell = String::convert(static_cast<intptr_t>(val));
		else
			cell = String::convert(val);
		m_rows[i].append(std::move(cell));
	}
}

bool DataTable::is_numeric(intptr_t j) const
{
	for (intptr_t i = 0; i < m_rows.size(); ++i)
	{
		const String &cell = m_rows[i][j];
		if (is_missing_cell(cell))
			continue;
		bool ok = false;
		double val = cell.to_float(&ok);
		if (!ok || !std::isfinite(val))
			return false;
	}
	return true;
}

} // namespace phonometrica
