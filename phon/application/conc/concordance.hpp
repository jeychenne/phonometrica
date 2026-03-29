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

#include <map>
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

	enum class Layout
	{
		Wide,  // one row per match; per-point data in separate columns
		Long   // one row per time point; Step and Time columns added
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

	/// True if column `col` (1-based) is a stored formant or bandwidth value that can be edited.
	bool is_editable_measurement(intptr_t col) const;

	// ── Wide-mode extra columns (formant measurements) ───────────────────

	bool has_extra_columns() const { return !m_extra_headers.empty(); }

	intptr_t extra_column_count() const { return m_extra_headers.size(); }

	// ── Formant metadata ─────────────────────────────────────────────────
	// ERB and Bark values are computed on the fly from stored formant values.
	// Only raw formants (F) and bandwidths (B) are stored in match.measurements.

	int nformant() const { return m_nformant; }
	bool has_bandwidth() const { return m_has_bandwidth; }
	bool has_erb() const { return m_has_erb; }
	bool has_bark() const { return m_has_bark; }
	bool has_auto_params() const { return m_has_auto_params; }

	/// Set formant metadata. Called by FormantQuery::execute() when creating a new concordance.
	void set_formant_meta(int nformant, bool bandwidth, bool erb, bool bark, bool auto_params);

	/// Toggle ERB display columns (recomputed on the fly).
	void set_has_erb(bool b);

	/// Toggle Bark display columns (recomputed on the fly).
	void set_has_bark(bool b);

	/// Number of raw fields stored per measurement point.
	/// Formant: nformant + (has_bandwidth ? nformant : 0). Pitch: 1.
	int stored_fields_per_point() const;

	/// Number of display fields per measurement point: stored + computed (ERB, Bark, semitones).
	int display_fields_per_point() const;

	/// Rebuild m_extra_headers and m_base_headers from measurement metadata.
	/// Must be called after changing display flags or measurement metadata.
	void rebuild_extra_headers();

	// ── Pitch metadata ──────────────────────────────────────────────────
	// Semitones and ERB-rate are computed on the fly from stored F0 values.
	// Only raw F0 in Hz is stored in match.measurements.

	bool is_pitch() const { return m_is_pitch; }
	bool has_semitones() const { return m_has_semitones; }
	bool has_pitch_erb() const { return m_has_pitch_erb; }
	double semitone_reference() const { return m_semitone_ref; }

	/// Set pitch metadata. Called by PitchQuery::execute() when creating a new concordance.
	void set_pitch_meta(bool semitones, double st_ref, bool erb);

	/// Toggle semitone display columns (recomputed on the fly).
	void set_has_semitones(bool b);

	/// Toggle pitch ERB display columns (recomputed on the fly).
	void set_has_pitch_erb(bool b);

	// ── Layout toggle (wide/long) ────────────────────────────────────────

	Layout layout() const { return m_layout; }

	void set_layout(Layout l) { m_layout = l; }

	// True if this concordance has n-point measurement data (can toggle wide/long).
	bool has_measurement_data() const { return !m_measurement_points.empty(); }

	/// Set measurement points and average flag (called by FormantQuery::execute()).
	void set_measurement_info(Array<double> points, bool has_average);

	void set_has_series(bool b) { m_has_series = b; }

	const Array<double> &measurement_points() const { return m_measurement_points; }

	// ── Column aliases ───────────────────────────────────────────────────
	// Users can rename any column header. The system remembers the default header
	// name internally and uses it for identification.

	/// Set a display alias for the column whose default header is `default_header`.
	void set_header_alias(const String &default_header, const String &alias);

	/// Remove the alias for the given default header, reverting to the default name.
	void clear_header_alias(const String &default_header);

	/// Get the alias map (for serialization / UI).
	const std::map<String, String> &header_aliases() const { return m_header_aliases; }

	// ── Other ────────────────────────────────────────────────────────────

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

	/// Compute the default (non-aliased) header for column j (1-based).
	String get_default_header(intptr_t j) const;

	/// Returns a unique concordance number (1, 2, 3...) for default naming.
	static int next_id();

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

	// Number of extra columns in the current layout.
	intptr_t effective_extra_count() const;

	// In long mode, map display row i (1-based) to match index and point index.
	intptr_t match_for_row(intptr_t i) const;  // 1-based match index
	intptr_t point_for_row(intptr_t i) const;  // 0-based point index

	/// Resolve a display column within a measurement group to a double value.
	/// `meas` is the match's measurement vector; `stored_base` is the offset for this group;
	/// `within_group` is the 0-based index within the display group.
	double resolve_group_value(const std::vector<double> &meas, int stored_base, int within_group) const;

	/// Format a measurement value for display based on its column type within a group.
	/// Formants/bandwidths use the global "formant_display/hz_decimals" setting.
	/// ERB/Bark use 2 additional decimal places.
	String format_measurement(double val, int within_group) const;

	/// Return the stored measurement index for an editable column, or -1 if not editable.
	/// `extra_j` is 1-based index within extra columns; `row` is 1-based display row.
	intptr_t stored_index_for_column(intptr_t extra_j, intptr_t row) const;

	/// Detect formant metadata from old-format headers and migrate measurement vectors.
	void normalize_after_load();

	Array<AutoMatch> m_matches;

	// Left and right context
	Array<std::pair<String,String>> m_context;

	// Extra column headers for acoustic measurements — wide mode
	// (rebuilt dynamically from formant metadata by rebuild_extra_headers())
	Array<String> m_extra_headers;

	// Measurement metadata for wide/long toggle
	Array<double> m_measurement_points;    // percentages (e.g. 25, 50, 75)
	Array<String> m_base_headers;          // un-suffixed names for one point (rebuilt dynamically)

	// ── Formant metadata ─────────────────────────────────────────────────
	// ERB/Bark are computed on the fly. Only F and B values are stored per match.

	int m_nformant = 0;               // number of formants (e.g. 3 for F1, F2, F3)
	bool m_has_bandwidth = false;     // whether bandwidth (B) columns are stored
	bool m_has_erb = false;           // whether ERB display columns are active
	bool m_has_bark = false;          // whether Bark display columns are active
	bool m_has_auto_params = false;   // whether auto LPC params are stored (2 values at end)
	bool m_has_series = true;         // NPoint: per-point series data present
	bool m_has_average = false;       // NPoint: average group present

	// ── Pitch metadata ──────────────────────────────────────────────────
	// Semitones and ERB-rate are computed on the fly. Only F0 in Hz is stored per match.

	bool m_is_pitch = false;          // true if this concordance holds pitch data
	bool m_has_semitones = false;     // whether semitone display columns are active
	double m_semitone_ref = 100;      // Hz (reference for semitone conversion)
	bool m_has_pitch_erb = false;     // whether ERB-rate display columns are active

	// ── Column aliases ───────────────────────────────────────────────────

	std::map<String, String> m_header_aliases;   // default_header → user display name

	String m_label;

	int m_target_count = 0;

	int m_context_length = 0;

	Context m_context_type = Context::None;

	Layout m_layout = Layout::Wide;
};


namespace traits {
template<> struct maybe_cyclic<Concordance> : std::false_type { };
}

} // namespace phonometrica

#endif // PHONOMETRICA_CONCORDANCE_HPP
