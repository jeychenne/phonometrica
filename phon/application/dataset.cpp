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
 * Created: 12/09/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <set>
#include <phon/application/dataset.hpp>
#include <phon/application/project.hpp>
#include <phon/utils/file_system.hpp>
#include <phon/utils/text.hpp>

namespace phonometrica {

Dataset::Column::~Column()
{

}

Dataset::Dataset(Directory *parent, String path) :
		DataTable(meta::get_class<Dataset>(), parent, std::move(path))
{

}

Dataset::Dataset(const Dataset &other) :
		DataTable(other.klass, other.parent(), String()), m_labels(other.m_labels)
{
	for (auto &col : other.m_columns) {
		m_columns.append(std::unique_ptr<Column>(col->clone()));
	}

	nrow = other.nrow;
	ncol = other.ncol;

	m_content_modified = true;
}

String Dataset::label() const
{
	return m_label.empty() ? Document::label() : m_label;
}

String Dataset::browser_label() const
{
	return m_label.empty() ? Document::browser_label() : m_label;
}

void Dataset::set_label(String value, bool mutate)
{
	m_label = std::move(value);
	if (mutate) m_content_modified = true;
}


void Dataset::load()
{
	// Clear existing data so load() is idempotent on reload.
	m_columns.clear();
	m_labels.clear();
	nrow = 0;
	ncol = 0;

	auto ext = filesystem::ext(m_path, true);

	if (ext == ".csv" || ext == ".tsv")
	{
		read_from_csv(m_separator.view());
	}
	else
	{
		throw error("Cannot load spreadsheet with '%' extension", ext);
	}
}

void Dataset::write()
{
	to_csv(m_path, m_separator);
}

void Dataset::set_separator(String sep)
{
	m_separator = std::move(sep);
	// Force a reload the next time the dataset is accessed.
	m_loaded = false;
}

bool Dataset::needs_metadata_node() const
{
	return m_separator != "\t" || DataTable::needs_metadata_node();
}

void Dataset::metadata_to_xml(xml_node meta_node)
{
	DataTable::metadata_to_xml(meta_node);

	if (m_separator != "\t")
	{
		auto sep_node = meta_node.append_child("Separator");
		// Store a human-readable name so the project XML remains legible.
		const char *name = (m_separator == ",")  ? "comma" :
		                   (m_separator == ";")  ? "semicolon" : "tab";
		sep_node.text().set(name);
	}
}

void Dataset::metadata_from_xml(xml_node meta_node)
{
	DataTable::metadata_from_xml(meta_node);

	static const std::string_view sep_tag("Separator");

	for (auto node = meta_node.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == sep_tag)
		{
			std::string_view val = node.text().get();
			if (val == "comma")          m_separator = ",";
			else if (val == "semicolon") m_separator = ";";
			else                         m_separator = "\t";
		}
	}
}

void Dataset::read_from_csv(std::string_view sep)
{
    assert(!m_path.empty());
    auto raw_data = utils::parse_csv(m_path, sep);
    if (raw_data.empty()) return;

    // Parse header.
    for (auto &label : raw_data.take_first())
    {
        if (label.ends_with(".num"))
        {
            label.remove_last(".num");
            m_columns.append(std::make_unique<TColumn<double>>());
        }
        else if (label.ends_with(".bool"))
        {
            label.remove_last(".bool");
            m_columns.append(std::make_unique<TColumn<bool>>());
        }
        else
        {
            if (label.ends_with(".text")) label.remove_last(".text");
            m_columns.append(std::make_unique<TColumn<String>>());
        }
        m_labels.append(std::move(label));
    }

    // Parse data.
    if (raw_data.empty()) return;
    nrow = raw_data.size();
    ncol = m_labels.size();
    for (auto &col : m_columns) col->resize(nrow);

    for (intptr_t i = 1; i <= nrow; i++)
    {
        auto &row = raw_data[i];

        for (intptr_t j = 1; j <= ncol; j++)
        {
            auto col = m_columns[j].get();

            switch (m_columns[j]->type())
            {
            case ColumnType::Numeric:
            {
                // Tolerant parse: missing-value sentinels become NaN, as do unparseable
                // cells (consistent with silent CSV import semantics).
                cast_num(col)->set(i, parse_numeric_cell(row[j].view()));
                break;
            }
            case ColumnType::Boolean:
            {
                bool value = row[j].to_bool();
                cast_bool(col)->set(i, value);
                break;
            }
            default:
            {
                cast_string(col)->set(i, std::move(row[j]));
            }
            }
        }
    }

    // Second pass: auto-detect column types for columns that had no type suffix.
    for (intptr_t j = 1; j <= ncol; j++)
    {
        auto col = m_columns[j].get();
        if (col->type() != ColumnType::Text) continue;

        auto *text_col = cast_string(col);
        bool all_numeric = true;
        bool all_boolean = true;

        for (intptr_t i = 1; i <= nrow; i++)
        {
            auto &val = text_col->get(i);
            // Missing-value sentinels (empty, "nan", "NaN", "NA", "undefined") are
            // compatible with a Numeric column but not with Boolean.
            if (is_missing_value_token(val.view()))
            {
                all_boolean = false;
                continue;
            }

            bool ok;
            val.to_float(&ok);
            if (!ok) all_numeric = false;

            if (val != "true" && val != "false") all_boolean = false;

            if (!all_numeric && !all_boolean) break;
        }

        if (all_numeric)
        {
            auto num_col = std::make_unique<TColumn<double>>(nrow);
            for (intptr_t i = 1; i <= nrow; i++)
            {
                auto &val = text_col->get(i);
                num_col->set(i, parse_numeric_cell(val.view()));
            }
            m_columns[j] = std::move(num_col);
        }
        else if (all_boolean)
        {
            auto bool_col = std::make_unique<TColumn<bool>>(nrow);
            for (intptr_t i = 1; i <= nrow; i++)
            {
                auto &val = text_col->get(i);
                bool_col->set(i, val == "true");
            }
            m_columns[j] = std::move(bool_col);
        }
    }
}

String Dataset::get_header(intptr_t j) const
{
	return m_labels[j];
}

String Dataset::get_cell(intptr_t i, intptr_t j) const
{
	auto col = m_columns[j].get();

	switch (col->type())
	{
        case ColumnType::Numeric:
		{
			double val = cast_num(col)->get(i);
			if (std::isnan(val)) return String("nan");
			if (std::isfinite(val) && val == std::floor(val))
				return String::convert(intptr_t(val));
			return String::convert(val);
		}
        case ColumnType::Boolean:
			return String::convert(cast_bool(col)->get(i));
		default:
			return cast_string(col)->get(i);
	}
}

void Dataset::set_cell(intptr_t i, intptr_t j, const String &value)
{
	auto col = m_columns[j].get();

	switch (col->type())
	{
        case ColumnType::Numeric:
		{
			if (is_missing_value_token(value.view())) {
				cast_num(col)->set(i, std::nan(""));
			}
			else {
				bool ok;
				double result = value.to_float(&ok);
				if (!ok) {
					throw error("Invalid numeric value in cell (%, %)", i, j);
				}
				cast_num(col)->set(i, result);
			}
		}
		break;
        case ColumnType::Boolean:
		{
			cast_bool(col)->set(i, value.to_bool());
		}
		break;
		default:
			cast_string(col)->set(i, value);
	}
}

void Dataset::initialize(Runtime &)
{

}

Dataset::ColumnType Dataset::Column::find_type(const std::type_info &t) const
{
	if (t == typeid(String))
        return ColumnType::Text;
	if (t == typeid(double))
        return ColumnType::Numeric;
	assert(t == typeid(bool));
    return ColumnType::Boolean;
}

Dataset::ColumnType Dataset::column_type(intptr_t j) const
{
    return m_columns[j]->type();
}

bool Dataset::is_numeric(intptr_t j) const
{
    return m_columns[j]->type() == ColumnType::Numeric;
}

bool Dataset::is_text(intptr_t j) const
{
    return m_columns[j]->type() == ColumnType::Text;
}

bool Dataset::is_boolean(intptr_t j) const
{
    return m_columns[j]->type() == ColumnType::Boolean;
}

std::span<const double> Dataset::numeric_column(intptr_t j) const
{
    auto col = m_columns[j].get();
    if (col->type() != ColumnType::Numeric) {
        throw error("Column '%' is not numeric", m_labels[j]);
    }
    auto &data = cast_num(col)->data;
    return { data.begin(), data.end() };
}

std::span<const String> Dataset::text_column(intptr_t j) const
{
    auto col = m_columns[j].get();
    if (col->type() != ColumnType::Text) {
        throw error("Column '%' is not text", m_labels[j]);
    }
    auto &data = cast_string(col)->data;
    return { data.begin(), data.end() };
}

std::span<const bool> Dataset::boolean_column(intptr_t j) const
{
    auto col = m_columns[j].get();
    if (col->type() != ColumnType::Boolean) {
        throw error("Column '%' is not boolean", m_labels[j]);
    }
    auto &data = cast_bool(col)->data;
    return { data.begin(), data.end() };
}

Array<String> Dataset::get_levels(intptr_t j) const
{
    auto col = m_columns[j].get();
    if (col->type() != ColumnType::Text) {
        throw error("Column '%' is not text", m_labels[j]);
    }
    auto &data = cast_string(col)->data;
    std::set<String> unique;
    for (auto &val : data) {
        unique.insert(val);
    }
    Array<String> levels;
    for (auto &val : unique) {
        levels.append(val);
    }
    return levels;
}

void Dataset::remove_row(intptr_t i)
{
    assert(i >= 1 && i <= nrow);

    for (intptr_t j = 1; j <= ncol; j++)
    {
        auto col = m_columns[j].get();

        switch (col->type())
        {
        case ColumnType::Numeric:
            cast_num(col)->data.remove_at(i);
            break;
        case ColumnType::Boolean:
            cast_bool(col)->data.remove_at(i);
            break;
        default:
            cast_string(col)->data.remove_at(i);
            break;
        }
    }

    nrow--;
}

void Dataset::remove_column(intptr_t j)
{
    assert(j >= 1 && j <= ncol);
    m_labels.remove_at(j);
    m_columns.remove_at(j);
    ncol--;
}

void Dataset::duplicate_column(intptr_t src, intptr_t dest)
{
    assert(src >= 1 && src <= ncol);
    assert(dest >= 1 && dest <= ncol + 1);

    auto cloned = std::unique_ptr<Column>(m_columns[src]->clone());
    auto label = m_labels[src];

    if (dest > ncol) {
        m_labels.append(std::move(label));
        m_columns.append(std::move(cloned));
    }
    else {
        m_labels.insert(dest, std::move(label));
        m_columns.insert(dest, std::move(cloned));
    }
    ncol++;
    m_content_modified = true;
}

void Dataset::move_column(intptr_t src, intptr_t dest)
{
    assert(src >= 1 && src <= ncol);
    assert(dest >= 1 && dest <= ncol);
    if (src == dest) return;

    // Extract the column and label from the source position.
    auto col = std::move(m_columns[src]);
    auto label = m_labels[src];
    m_labels.remove_at(src);
    m_columns.remove_at(src);

    // After removal the array has ncol-1 elements. Inserting at the
    // original dest position produces the correct final ordering because
    // Array::insert appends when pos > size.
    if (dest > ncol - 1) {
        m_labels.append(std::move(label));
        m_columns.append(std::move(col));
    }
    else {
        m_labels.insert(dest, std::move(label));
        m_columns.insert(dest, std::move(col));
    }
    m_content_modified = true;
}


// ── Undo/redo support ─────────────────────────────────────

Dataset::SavedRow Dataset::extract_row(intptr_t i)
{
	assert(i >= 1 && i <= nrow);

	SavedRow row;
	row.cells.resize(ncol);

	for (intptr_t j = 1; j <= ncol; j++)
	{
		auto col = m_columns[j].get();
		CellValue &cv = row.cells[j];
		cv.type = col->type();

		switch (cv.type)
		{
		case ColumnType::Numeric:
			cv.num = cast_num(col)->get(i);
			break;
		case ColumnType::Boolean:
			cv.flag = cast_bool(col)->get(i);
			break;
		default:
			cv.str = cast_string(col)->get(i);
			break;
		}
	}

	// Now remove the row (shifts subsequent rows up).
	remove_row(i);
	return row;
}

void Dataset::insert_row(intptr_t i, const SavedRow &row)
{
	assert(i >= 1 && i <= nrow + 1);
	assert(row.cells.size() == ncol);

	for (intptr_t j = 1; j <= ncol; j++)
	{
		auto col = m_columns[j].get();
		auto &cv = row.cells[j];

		switch (cv.type)
		{
		case ColumnType::Numeric:
			cast_num(col)->data.insert(i, cv.num);
			break;
		case ColumnType::Boolean:
			cast_bool(col)->data.insert(i, cv.flag);
			break;
		default:
			cast_string(col)->data.insert(i, cv.str);
			break;
		}
	}

	nrow++;
}

Dataset::SavedColumn Dataset::extract_column(intptr_t j)
{
	assert(j >= 1 && j <= ncol);
	SavedColumn sc;
	sc.label = m_labels[j];
	sc.data = std::move(m_columns[j]);
	m_labels.remove_at(j);
	m_columns.remove_at(j);
	ncol--;
	return sc;
}

void Dataset::insert_column(intptr_t j, SavedColumn col)
{
	assert(j >= 1 && j <= ncol + 1);

	if (j > ncol) {
		m_labels.append(std::move(col.label));
		m_columns.append(std::move(col.data));
	}
	else {
		m_labels.insert(j, std::move(col.label));
		m_columns.insert(j, std::move(col.data));
	}
	ncol++;
}


// ── Set operations ─────────────────────────────────────────

String Dataset::row_key(intptr_t i) const
{
	String key;
	for (intptr_t j = 1; j <= ncol; j++) {
		if (j > 1) key.append('\t');
		key.append(get_cell(i, j));
	}
	return key;
}

void Dataset::append_row_from(const Dataset &source, intptr_t i)
{
	for (intptr_t j = 1; j <= ncol; j++)
	{
		auto *dst = m_columns[j].get();
		auto *src = source.m_columns[j].get();

		switch (dst->type())
		{
		case ColumnType::Numeric:
			cast_num(dst)->data.append(cast_num(src)->get(i));
			break;
		case ColumnType::Boolean:
			cast_bool(dst)->data.append(cast_bool(src)->get(i));
			break;
		default:
			cast_string(dst)->data.append(cast_string(src)->get(i));
			break;
		}
	}
	nrow++;
}

void Dataset::check_columns_compatible(const Dataset &other) const
{
	if (ncol != other.ncol) {
		throw error("Cannot combine datasets: they have different numbers of columns (% vs %)", ncol, other.ncol);
	}
	for (intptr_t j = 1; j <= ncol; j++) {
		if (m_labels[j] != other.m_labels[j]) {
			throw error("Cannot combine datasets: column % has different names (\"%\" vs \"%\")", j, m_labels[j], other.m_labels[j]);
		}
		if (m_columns[j]->type() != other.m_columns[j]->type()) {
			throw error("Cannot combine datasets: column \"%\" has different types", m_labels[j]);
		}
	}
}

Handle<Dataset> Dataset::unite(const Dataset &other, const String &label) const
{
	check_columns_compatible(other);

	// Start with a copy of this dataset.
	auto result = make_handle<Dataset>(*this);
	result->m_loaded = true;
	result->set_label(label, false);

	// Build a set of row keys from this dataset.
	std::set<String> keys;
	for (intptr_t i = 1; i <= nrow; i++) {
		keys.insert(row_key(i));
	}

	// Add rows from other that are not already present.
	for (intptr_t i = 1; i <= other.nrow; i++) {
		auto key = other.row_key(i);
		if (keys.find(key) == keys.end()) {
			keys.insert(std::move(key));
			result->append_row_from(other, i);
		}
	}

	auto parent = Project::get()->data().get();
	parent->append(result, false);

	return result;
}

Handle<Dataset> Dataset::intersect(const Dataset &other, const String &label) const
{
	check_columns_compatible(other);

	// Build a set of row keys from B.
	std::set<String> other_keys;
	for (intptr_t i = 1; i <= other.nrow; i++) {
		other_keys.insert(other.row_key(i));
	}

	// Create an empty result with the same column structure.
	auto result = make_handle<Dataset>(nullptr);
	result->m_labels = m_labels;
	result->ncol = ncol;
	result->m_loaded = true;
	result->m_content_modified = true;
	result->set_label(label, false);

	for (intptr_t j = 1; j <= ncol; j++) {
		result->m_columns.append(std::unique_ptr<Column>(m_columns[j]->clone()));
	}
	// Columns were cloned with all rows — reset them to empty.
	for (intptr_t j = 1; j <= ncol; j++) {
		result->m_columns[j]->resize(0);
	}

	// Keep rows from A that also appear in B.
	for (intptr_t i = 1; i <= nrow; i++) {
		if (other_keys.count(row_key(i))) {
			result->append_row_from(*this, i);
		}
	}

	auto parent = Project::get()->data().get();
	parent->append(result, false);

	return result;
}

Handle<Dataset> Dataset::complement(const Dataset &other, const String &label) const
{
	check_columns_compatible(other);

	// Build a set of row keys from B.
	std::set<String> other_keys;
	for (intptr_t i = 1; i <= other.nrow; i++) {
		other_keys.insert(other.row_key(i));
	}

	// Create an empty result with the same column structure.
	auto result = make_handle<Dataset>(nullptr);
	result->m_labels = m_labels;
	result->ncol = ncol;
	result->m_loaded = true;
	result->m_content_modified = true;
	result->set_label(label, false);

	for (intptr_t j = 1; j <= ncol; j++) {
		result->m_columns.append(std::unique_ptr<Column>(m_columns[j]->clone()));
	}
	for (intptr_t j = 1; j <= ncol; j++) {
		result->m_columns[j]->resize(0);
	}

	// Keep rows from A that do NOT appear in B.
	for (intptr_t i = 1; i <= nrow; i++) {
		if (!other_keys.count(row_key(i))) {
			result->append_row_from(*this, i);
		}
	}

	auto parent = Project::get()->data().get();
	parent->append(result, false);

	return result;
}

Handle<Dataset> Dataset::merge(const DataTable &other, const String &label,
                               const Array<std::pair<String, intptr_t>> &columns_to_add,
                               const Array<std::pair<intptr_t, intptr_t>> &columns_to_overwrite) const
{
	if (nrow != other.row_count()) {
		throw error("Cannot merge: tables have different numbers of rows (% vs %)", nrow, other.row_count());
	}

	// Start with a copy of this dataset.
	auto result = make_handle<Dataset>(*this);
	result->m_loaded = true;
	result->set_label(label, false);

	// Overwrite columns: replace A's column data with B's cell values.
	for (auto &[a_col, b_col] : columns_to_overwrite)
	{
		auto *col = result->m_columns[a_col].get();
		for (intptr_t i = 1; i <= nrow; i++)
		{
			auto val = other.get_cell(i, b_col);
			switch (col->type())
			{
			case ColumnType::Numeric:
			{
				bool ok;
				double d = val.to_float(&ok);
				result->cast_num(col)->set(i, ok ? d : std::nan(""));
				break;
			}
			case ColumnType::Boolean:
				result->cast_bool(col)->set(i, val.to_bool());
				break;
			default:
				result->cast_string(col)->set(i, val);
				break;
			}
		}
	}

	// Add new columns from B as text columns.
	for (auto &[hdr, b_col] : columns_to_add)
	{
		auto new_col = std::make_unique<TColumn<String>>(nrow);
		for (intptr_t i = 1; i <= nrow; i++) {
			new_col->set(i, other.get_cell(i, b_col));
		}
		result->m_labels.append(hdr);
		result->m_columns.append(std::move(new_col));
		result->ncol++;
	}

	auto parent = Project::get()->data().get();
	parent->append(result, false);

	return result;
}

Handle<Dataset> Dataset::subset(const std::vector<int> &rows_0based, const String &label) const
{
	// Create an empty result with the same column structure.
	auto result = make_handle<Dataset>(nullptr);
	result->m_labels = m_labels;
	result->ncol = ncol;
	result->m_loaded = true;
	result->m_content_modified = true;
	result->set_label(label, false);

	for (intptr_t j = 1; j <= ncol; j++) {
		result->m_columns.append(std::unique_ptr<Column>(m_columns[j]->clone()));
	}
	// Columns were cloned with all rows — reset them to empty.
	for (intptr_t j = 1; j <= ncol; j++) {
		result->m_columns[j]->resize(0);
	}

	// Append selected rows (convert 0-based to 1-based).
	for (int row : rows_0based) {
		result->append_row_from(*this, row + 1);
	}

	auto parent = Project::get()->data().get();
	parent->append(result, false);

	return result;
}

void Dataset::set_header(intptr_t j, const String &name)
{
	if (j < 1 || j > ncol) {
		throw error("Column index % is out of range", j);
	}
	m_labels[j] = name;
	m_content_modified = true;
}

void Dataset::add_numeric_column(const String &header, const std::vector<double> &values)
{
	if ((intptr_t)values.size() != nrow) {
		throw error("Cannot add column '%': expected % values, got %", header, nrow, (intptr_t)values.size());
	}

	auto new_col = std::make_unique<TColumn<double>>(nrow);
	for (intptr_t i = 0; i < nrow; i++) {
		new_col->set(i + 1, values[i]);
	}

	m_labels.append(header);
	m_columns.append(std::move(new_col));
	ncol++;
	m_content_modified = true;
}

void Dataset::add_text_column(const String &header, const std::vector<String> &values)
{
	if ((intptr_t)values.size() != nrow) {
		throw error("Cannot add column '%': expected % values, got %", header, nrow, (intptr_t)values.size());
	}

	auto new_col = std::make_unique<TColumn<String>>(nrow);
	for (intptr_t i = 0; i < nrow; i++) {
		new_col->set(i + 1, values[i]);
	}

	m_labels.append(header);
	m_columns.append(std::move(new_col));
	ncol++;
	m_content_modified = true;
}

} // namespace phonometrica
