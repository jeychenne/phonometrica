/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any      *
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
#include <phon/application/dataset.hpp>
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


void Dataset::load()
{
	auto ext = filesystem::ext(m_path, true);

	if (ext == ".csv")
	{
		read_from_csv("\t");
	}
	else
	{
		throw error("Cannot load spreadsheet with '%' extension", ext);
	}
}

void Dataset::write()
{

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
                double value = row[j].to_float();
                cast_num(col)->set(i, value);
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
            if (val.empty())
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
                num_col->set(i, val.empty() ? std::nan("") : val.to_float());
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
			bool ok;
			double result = value.to_float(&ok);
			if (!ok) {
				throw error("Invalid numeric value in cell (%, %)", i, j);
			}
			cast_num(col)->set(i, result);
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
} // namespace phonometrica
