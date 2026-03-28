/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 08/02/2021                                                                                                 *
 *                                                                                                                     *
 * Purpose: a Concordance represents the result of a query.                                                            *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CONCORDANCE_HPP
#define PHONOMETRICA_CONCORDANCE_HPP

#include <phon/application/data_table.hpp>
#include <phon/application/conc/match.hpp>

namespace phonometrica {

class Concordance : public DataTable
{
public:

	enum class Context
	{
		None,   // no context
		Labels, // labels from surrounding events
		KWIC    // keyword in context
	};

	Concordance(Directory *parent, const String &path);

	Concordance(intptr_t target_count, Context ctx, intptr_t context_length, Array<AutoMatch> matches, Directory *parent,
				const String &path = String());

	Concordance(const Concordance &other);

	intptr_t target_count() const { return m_target_count; }

	String get_header(intptr_t j) const override;

	String get_cell(intptr_t i, intptr_t j) const override;

	void set_cell(intptr_t i, intptr_t j, const String &value) override;

	intptr_t row_count() const override;

	intptr_t column_count() const override;

	bool empty() const override;

	void find_context();

	bool has_context() const;

	bool is_target(intptr_t col) const;

	bool is_left_context(intptr_t col) const;

	bool is_right_context(intptr_t col) const;

	bool is_time(intptr_t col) const;

	bool is_layer(intptr_t col) const;

	Match &get_match(intptr_t i);

	bool is_file_info_column(intptr_t col) const;

	bool is_metadata_column(intptr_t col) const;

	bool is_measurement_column(intptr_t col) const;

	bool has_extra_columns() const { return !m_extra_headers.empty(); }

	intptr_t extra_column_count() const { return m_extra_headers.size(); }

	void set_extra_headers(Array<String> headers) { m_extra_headers = std::move(headers); }

	String label() const override;

	void set_label(String value, bool mutate);

	void modify();

	AutoMatch remove_match(intptr_t row);

	void restore_match(intptr_t row, AutoMatch m);

	Handle<Concordance> unite(const Concordance &other, const String &label) const;

	Handle<Concordance> intersect(const Concordance &other, const String &label) const;

	Handle<Concordance> complement(const Concordance &other, const String &label) const;

	bool update_match(intptr_t i, intptr_t target);

	void update_context(intptr_t i);

	std::pair<String, String> get_context(intptr_t i) const;

protected:

	void preload();

	void load() override;

	void write() override;

	void parse_options_from_xml(xml_node root);

	void parse_matches_from_xml(xml_node root);

	void find_labels_context();

	String get_left_context(intptr_t i) const;

	String get_right_context(intptr_t i) const;

	void find_kwic_context();

	std::pair<String, String> get_kwic_context(const Match &match, const String &sep) const;

	std::pair<String, String> get_labels_context(const Match &match) const;

	int match_region_size() const;

	int context_column_count() const;

	Array<AutoMatch> m_matches;

	// Left and right context
	Array<std::pair<String,String>> m_context;

	// Extra column headers for acoustic measurements (F1, F2, B1, etc.)
	Array<String> m_extra_headers;

	String m_label;

	int m_target_count = 0;

	int m_context_length = 0;

	Context m_context_type = Context::None;
};


namespace traits {
template<> struct maybe_cyclic<Concordance> : std::false_type { };
}

} // namespace phonometrica

#endif // PHONOMETRICA_CONCORDANCE_HPP
