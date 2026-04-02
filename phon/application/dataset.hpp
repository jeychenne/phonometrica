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
 * Purpose: tabular dataset (e.g. CSV file).                                                                           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_DATASET_HPP
#define PHONOMETRICA_DATASET_HPP

#include <vector>
#include <phon/application/data_table.hpp>

namespace phonometrica {

class Dataset final : public DataTable
{
public:

    enum class ColumnType
    {
        Boolean,
        Numeric,
        Text
    };

	Dataset(Directory *parent, String path = String());

	Dataset(const Dataset &other);

	String label() const override;

	void set_label(String value, bool mutate);

	String get_header(intptr_t j) const override;

	String get_cell(intptr_t i, intptr_t j) const override;

	void set_cell(intptr_t i, intptr_t j, const String &value) override;

	intptr_t row_count() const override { return nrow; }

	intptr_t column_count() const override { return ncol; }

	bool empty() const override { return nrow == 0; }


    ColumnType column_type(intptr_t j) const;

    bool is_numeric(intptr_t j) const;

    bool is_text(intptr_t j) const;

    bool is_boolean(intptr_t j) const;

    std::span<const double> numeric_column(intptr_t j) const;

    std::span<const String> text_column(intptr_t j) const;

    std::span<const bool> boolean_column(intptr_t j) const;

    // Get the unique values in a text column (for factor levels).
    Array<String> get_levels(intptr_t j) const;

	// Remove row i (1-based). Shifts subsequent rows up.
	void remove_row(intptr_t i);

	// Remove column j (1-based). Shifts subsequent columns left.
	void remove_column(intptr_t j);

	Handle<Dataset> unite(const Dataset &other, const String &label) const;

	Handle<Dataset> intersect(const Dataset &other, const String &label) const;

	Handle<Dataset> complement(const Dataset &other, const String &label) const;

	/// Create a subset containing only the specified rows (0-based indices).
	/// The result is a new in-memory Dataset added to the project.
	Handle<Dataset> subset(const std::vector<int> &rows_0based, const String &label) const;

	/// Append a new numeric column with the given header and values.
	/// The values vector must have exactly row_count() elements.
	void add_numeric_column(const String &header, const std::vector<double> &values);

	/// Check that this dataset has the same columns (count and names) as `other`.
	/// Throws an error with a descriptive message if the columns are incompatible.
	void check_columns_compatible(const Dataset &other) const;

	/// Horizontal merge: add columns from `other` to a copy of this dataset.
	/// `columns_to_add`: (header_name, B_column_index) pairs for new columns.
	/// `columns_to_overwrite`: (A_column_index, B_column_index) pairs where B overwrites A.
	Handle<Dataset> merge(const DataTable &other, const String &label,
	                      const Array<std::pair<String, intptr_t>> &columns_to_add,
	                      const Array<std::pair<intptr_t, intptr_t>> &columns_to_overwrite) const;

	static void initialize(Runtime &rt);

private:

	struct Column
	{
		virtual ~Column();

        virtual ColumnType type() const = 0;

		virtual void resize(intptr_t size) = 0;

		virtual Column *clone() const = 0;

	protected:

        ColumnType find_type(const std::type_info &t) const;
	};

	template<class T>
	struct TColumn : public Column
	{
		TColumn() = default;

		TColumn(intptr_t size, const T &value = T()) :
			data(size, value)
		{ }

		TColumn(const Array<T> &d) : data(d) { }

        ColumnType type() const override { return find_type(typeid(T)); }

		const T &get(intptr_t i) const { return data[i]; }

		void set(intptr_t i, T value) { data[i] = std::move(value); }

		void resize(intptr_t size) override { data.resize(size); }

		Column *clone() const override { return new TColumn<T>(data); }

		Array<T> data;
	};

	void read_from_csv(std::string_view sep = ",");

	void load() override;

	void write() override;

	TColumn<double>* cast_num(Column *col) { return static_cast<TColumn<double>*>(col); }
	const TColumn<double>* cast_num(Column *col) const { return static_cast<TColumn<double>*>(col); }

	TColumn<bool>* cast_bool(Column *col) { return static_cast<TColumn<bool>*>(col); }
	const TColumn<bool>* cast_bool(Column *col) const { return static_cast<TColumn<bool>*>(col); }

	TColumn<String>* cast_string(Column *col) { return static_cast<TColumn<String>*>(col); }
	const TColumn<String>* cast_string(Column *col) const { return static_cast<TColumn<String>*>(col); }

	using AutoColumn = std::unique_ptr<Column>;

	/// Build a tab-separated string key for row i (1-based), used for set membership.
	String row_key(intptr_t i) const;

	/// Append row i (1-based) from source into this dataset.
	/// Assumes compatible column structure.
	void append_row_from(const Dataset &source, intptr_t i);

	Array<String> m_labels;

	Array<AutoColumn> m_columns;

	String m_label;

	intptr_t nrow = 0;

	intptr_t ncol = 0;
};


namespace traits {
template<> struct maybe_cyclic<Dataset> : std::false_type { };
}

} // namespace phonometrica

#endif // PHONOMETRICA_DATASET_HPP
