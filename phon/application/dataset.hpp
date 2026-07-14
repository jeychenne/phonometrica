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

	String browser_label() const override;

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

	// Remove row i (0-based). Shifts subsequent rows up.
	void remove_row(intptr_t i);

	// Remove column j (0-based). Shifts subsequent columns left.
	void remove_column(intptr_t j);

	// Duplicate column src (0-based) and insert the copy at position dest (0-based).
	// dest may be column_count() to append at the end.
	void duplicate_column(intptr_t src, intptr_t dest);

	// Move column src (0-based) to position dest (0-based).
	// dest is interpreted as the target position in the final layout.
	void move_column(intptr_t src, intptr_t dest);

	Handle<Dataset> unite(const Dataset &other, const String &label) const;

	Handle<Dataset> intersect(const Dataset &other, const String &label) const;

	Handle<Dataset> complement(const Dataset &other, const String &label) const;

	/// Create a subset containing only the specified rows (0-based indices).
	/// The result is a new in-memory Dataset added to the project.
	Handle<Dataset> subset(const std::vector<int> &rows_0based, const String &label) const;

	/// Set the header (column name) for column j (0-based).
	void set_header(intptr_t j, const String &name);

	/// Append a new numeric column with the given header and values.
	/// The values vector must have exactly row_count() elements.
	void add_numeric_column(const String &header, const std::vector<double> &values);

	/// Append a new text column with the given header and values.
	/// The values vector must have exactly row_count() elements.
	void add_text_column(const String &header, const std::vector<String> &values);

	/// Factory: construct an empty in-memory Dataset with the given row count
	/// and zero columns. Use add_numeric_column / add_text_column to populate.
	/// Useful for building computed Datasets (e.g. predict() output) where
	/// there is no source table to clone from.
	static Handle<Dataset> create_empty(intptr_t nrow);

	/// Mark this Dataset as already in memory and not requiring a load() call.
	/// Use after constructing via the copy constructor (which leaves m_loaded
	/// at the default false), or any other in-memory construction path.
	/// Without this, a subsequent .ncol / .nrow access in scripting would
	/// trigger Document::open() → Dataset::load() with an empty m_path,
	/// failing with "Cannot load spreadsheet with '' extension".
	void mark_loaded() { m_loaded = true; }

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

	// ── Column storage (public for undo/redo support) ──

	struct Column
	{
		virtual ~Column();

        virtual ColumnType type() const = 0;

		virtual void resize(intptr_t size) = 0;

		virtual Column *clone() const = 0;

	protected:

        ColumnType find_type(const std::type_info &t) const;
	};

	using AutoColumn = std::unique_ptr<Column>;

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

	// ── Undo/redo support ────────────────────────────

	/// A typed cell value for row save/restore.
	struct CellValue
	{
		ColumnType type = ColumnType::Text;
		double num = 0;
		String str;
		bool flag = false;
	};

	/// A saved row: one CellValue per column.
	struct SavedRow
	{
		Array<CellValue> cells;
	};

	/// A saved column: header + polymorphic column data.
	struct SavedColumn
	{
		String label;
		AutoColumn data;
	};

	/// Remove row i (0-based) and return its data for undo.
	SavedRow extract_row(intptr_t i);

	/// Insert a previously extracted row at position i (0-based).
	void insert_row(intptr_t i, const SavedRow &row);

	/// Remove column j (0-based) and return it for undo.
	SavedColumn extract_column(intptr_t j);

	/// Insert a previously extracted column at position j (0-based).
	void insert_column(intptr_t j, SavedColumn col);

	// Separator used when reading this CSV file (stored in the project).
	// Default is tab; other common values are "," and ";".
	String separator() const { return m_separator; }
	void set_separator(String sep);

	void metadata_to_xml(xml_node meta_node) override;
	void metadata_from_xml(xml_node meta_node) override;
	bool needs_metadata_node() const override;

private:

	void read_from_csv(std::string_view sep = "\t");

	void load() override;

	void write() override;

	TColumn<double>* cast_num(Column *col) { return static_cast<TColumn<double>*>(col); }
	const TColumn<double>* cast_num(Column *col) const { return static_cast<TColumn<double>*>(col); }

	TColumn<bool>* cast_bool(Column *col) { return static_cast<TColumn<bool>*>(col); }
	const TColumn<bool>* cast_bool(Column *col) const { return static_cast<TColumn<bool>*>(col); }

	TColumn<String>* cast_string(Column *col) { return static_cast<TColumn<String>*>(col); }
	const TColumn<String>* cast_string(Column *col) const { return static_cast<TColumn<String>*>(col); }

	/// Build a tab-separated string key for row i (0-based), used for set membership.
	String row_key(intptr_t i) const;

	/// Append row i (0-based) from source into this dataset.
	/// Assumes compatible column structure.
	void append_row_from(const Dataset &source, intptr_t i);

	Array<String> m_labels;

	Array<AutoColumn> m_columns;

	String m_label;

	String m_separator = "\t";

	intptr_t nrow = 0;

	intptr_t ncol = 0;
};


namespace traits {
template<> struct maybe_cyclic<Dataset> : std::false_type { };
template<> struct is_clonable<Dataset> : std::false_type { };
}

} // namespace phonometrica

#endif // PHONOMETRICA_DATASET_HPP
