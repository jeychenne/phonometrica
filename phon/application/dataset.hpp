/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 12/09/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: tabular dataset (e.g. CSV file).                                                                           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_DATASET_HPP
#define PHONOMETRICA_DATASET_HPP

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

	Array<String> m_labels;

	Array<AutoColumn> m_columns;

	intptr_t nrow = 0;

	intptr_t ncol = 0;
};


namespace traits {
template<> struct maybe_cyclic<Dataset> : std::false_type { };
}

} // namespace phonometrica

#endif // PHONOMETRICA_DATASET_HPP
