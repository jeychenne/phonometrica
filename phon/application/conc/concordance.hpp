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
 * Created: 08/02/2021                                                                                                 *
 *                                                                                                                     *
 * Purpose: a Concordance represents the result of a query.                                                            *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CONCORDANCE_HPP
#define PHONOMETRICA_CONCORDANCE_HPP

#include <map>
#include <vector>
#include <phon/application/data_table.hpp>
#include <phon/application/conc/match.hpp>

namespace phonometrica {

// Forward declarations (full definitions in phon/application/protocol.hpp and protocol_apply.hpp).
class Protocol;
struct ProtocolApplyResult;

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

	enum class AuxColumnType
	{
		Text,          // generic text
		Numeric,       // generic number (no conversion)
		FormantHz,     // formant in Hz → can toggle ERB, Bark
		BandwidthHz,   // bandwidth in Hz → can toggle ERB, Bark
		PitchHz,       // F0 in Hz → can toggle semitones, ERB-rate
		IntensityDb    // intensity in dB → no derived toggles
	};

	struct AuxColumn
	{
		String header;
		AuxColumnType type = AuxColumnType::Text;
		Array<double> num_data;    // 1-based; used for Numeric and measurement types
		Array<String> text_data;   // 1-based; used for Text type
		double semitone_ref = 100; // semitone reference Hz (PitchHz only)
	};

	/// Snapshot of everything tied to a single match row, captured by remove_match()
	/// and consumed by restore_match() to support undo. Beyond the match itself we
	/// must save the context cache entry (if any) and the aux-column cell that lives
	/// in each auxiliary column at that row, since all of these are indexed in
	/// lock-step with m_matches.
	struct RemovedRow
	{
		AutoMatch match;
		bool had_context = false;
		std::pair<String, String> context;
		// One entry per aux column, in column order. Only the field matching the
		// column's type carries data; the other holds a default-constructed value.
		std::vector<String> aux_text;
		std::vector<double> aux_num;
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

	bool is_duration_column(intptr_t col) const;

	/// True if column `col` (1-based) is a measurement-time column (derived, non-editable).
	/// Applies to both Wide (per-point or midpoint) and Long (absolute Time) layouts.
	bool is_measurement_time_column(intptr_t col) const;

	/// True if column `col` (1-based) is a stored formant or bandwidth value that can be edited.
	bool is_editable_measurement(intptr_t col) const;

	/// True if column `col` (1-based) is the base (non-derived) display column of an auxiliary
	/// column added via merge() or apply_protocol(). Derived ERB/Bark/ST display columns are
	/// computed from the base value and remain read-only.
	bool is_editable_aux(intptr_t col) const;

	/// True if column `col` (1-based) is editable — either a measurement cell or an aux base cell.
	/// This is the unified predicate the GUI uses to decide whether a cell opens an editor.
	bool is_editable_cell(intptr_t col) const;

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

	/// True iff the concordance carries a trailing per-match "Max freq" column
	/// (set when FormantQuery is in manual mode with a per-property override active).
	/// Mutually exclusive with has_auto_params (which is true only in Weenink mode).
	bool has_per_match_max_freq() const { return m_has_per_match_max_freq; }

	/// True iff the concordance carries two trailing per-match columns "Min pitch"
	/// and "Max pitch" (set when PitchQuery has a per-property pitch-range override active).
	bool has_per_match_pitch_range() const { return m_has_per_match_pitch_range; }

	/// Set formant metadata. Called by FormantQuery::execute() when creating a new concordance.
	void set_formant_meta(int nformant, bool bandwidth, bool erb, bool bark, bool auto_params);

	/// Set the per-match Max freq flag (manual + override case).
	void set_has_per_match_max_freq(bool b) { m_has_per_match_max_freq = b; }

	/// Set the per-match pitch range columns flag (pitch override case).
	void set_has_per_match_pitch_range(bool b) { m_has_per_match_pitch_range = b; }

	/// Toggle ERB display columns (recomputed on the fly).
	void set_has_erb(bool b);

	/// Toggle Bark display columns (recomputed on the fly).
	void set_has_bark(bool b);

	/// Number of raw fields stored per measurement point.
	/// Formant: nformant + (has_bandwidth ? nformant : 0). Pitch/Intensity: 1.
	/// Spectral moments: number of enabled moments.
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

	// ── Intensity metadata ──────────────────────────────────────────────
	// Only raw intensity in dB is stored per match. No computed columns.

	bool is_intensity() const { return m_is_intensity; }

	/// Set intensity metadata. Called by IntensityQuery::execute().
	void set_intensity_meta();

	// ── Spectral moments metadata ────────────────────────────────────────
	// COG, Spread, Skewness, Kurtosis are stored per match. No computed columns.

	bool is_spectral_moments() const { return m_is_spectral_moments; }

	/// Set spectral moments metadata. Called by SpectralMomentsQuery::execute().
	void set_spectral_moments_meta(bool cog, bool spread, bool skewness, bool kurtosis);

	// ── Voice quality metadata ───────────────────────────────────────────
	// Pulses, jitter, shimmer, HNR are stored per match. No computed columns.
	// Values are stored in display units (ms, %, µs, dB, Hz) so CSV exports
	// are immediately readable without per-column scaling.

	bool is_voice_quality() const { return m_is_voice_quality; }

	/// Set voice quality metadata. Called by VoiceQualityQuery::execute().
	void set_voice_quality_meta(bool num_pulses, bool mean_period, bool mean_f0,
	                            bool jitter_local, bool jitter_local_abs,
	                            bool jitter_rap, bool jitter_ppq5, bool jitter_ddp,
	                            bool shimmer_local, bool shimmer_local_db,
	                            bool shimmer_apq3, bool shimmer_apq5, bool shimmer_apq11,
	                            bool hnr);

	// ── Duration metadata ───────────────────────────────────────────────

	bool has_duration() const { return m_has_duration; }

	void set_has_duration(bool b) { m_has_duration = b; }

	bool duration_in_ms() const { return m_duration_in_ms; }

	void set_duration_in_ms(bool b) { m_duration_in_ms = b; }

	// ── Measurement time metadata ───────────────────────────────────────
	// Optional "measurement time" columns (absolute seconds). Derived from
	// the reference target's event boundaries and the measurement method
	// (midpoint or per-point percentages); no extra data stored per match.
	// Controlled by the query-level "Add measurement time" option and by
	// the display-menu toggle.

	bool has_time() const { return m_has_time; }

	/// Toggle measurement-time column(s). Rebuilds display headers.
	void set_has_time(bool b);

	int duration_column_count() const { return m_has_duration ? m_target_count : 0; }

	bool highlight_targets() const { return m_highlight_targets; }

	void set_highlight_targets(bool b) { m_highlight_targets = b; }

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

	String browser_label() const override;

	void set_label(String value, bool mutate);

	void modify();

	/// Remove the match at 1-based `row` along with its parallel context-cache
	/// entry and aux-column cells. Returns a RemovedRow that captures every part
	/// needed by restore_match() to put the row back exactly as it was.
	RemovedRow remove_match(intptr_t row);

	/// Reinsert a row previously captured by remove_match() at 1-based `row`,
	/// restoring the match, the context cache entry (if any), and the aux-column
	/// cells in each auxiliary column.
	void restore_match(intptr_t row, RemovedRow data);

	Handle<Concordance> unite(const Concordance &other, const String &label) const;

	Handle<Concordance> intersect(const Concordance &other, const String &label) const;

	Handle<Concordance> complement(const Concordance &other, const String &label) const;

	/// Create a subset containing only the specified rows (0-based indices).
	/// The result is a new in-memory Concordance added to the project.
	Handle<Concordance> subset(const std::vector<int> &rows_0based, const String &label) const;

	/// Append a new numeric auxiliary column with the given header and values.
	/// The values vector must have exactly row_count() elements.
	void add_numeric_column(const String &header, const std::vector<double> &values);

	/// Append a new text auxiliary column with the given header and values.
	/// The values vector must have exactly row_count() elements.
	void add_text_column(const String &header, const std::vector<String> &values);

	/// Apply a coding protocol to the text of column `source_col` (1-based), appending one new
	/// text aux column per protocol field. Cells are read via get_cell() so any text column is
	/// accepted (targets, aux columns, file info, context, metadata); measurement (numeric)
	/// columns are rejected. When `translate` is true (default), raw captures are replaced by
	/// the protocol's human-readable labels; when false, raw captures are kept. The source
	/// column is not modified or hidden — column visibility is a view-layer concern.
	/// Rows that fail to match the protocol yield empty cells across all new columns and are
	/// reported via the returned ProtocolApplyResult's `failed_rows` list.
	ProtocolApplyResult apply_protocol(intptr_t source_col, const Protocol &protocol, bool translate = true);

	/// Check that this concordance has the same columns (count and names) as `other`.
	/// Throws an error with a descriptive message if the columns are incompatible.
	void check_columns_compatible(const Concordance &other) const;

	/// Check that this concordance has the same matches as `other`.
	bool matches_equal(const Concordance &other) const;

	/// Horizontal merge: add columns from `other` to a copy of this concordance.
	/// `columns_to_add` is a list of (display_header, B_column_index) pairs; types are auto-detected.
	Handle<Concordance> merge(const DataTable &other, const String &label,
	                          const Array<std::pair<String, intptr_t>> &columns_to_add) const;

	intptr_t aux_stored_count() const { return m_aux_columns.size(); }
	intptr_t aux_display_column_count() const;

	/// Returns the number of display columns produced by aux column c (1-based).
	int aux_col_display_width(intptr_t c) const;

	/// Given a 1-based display column index, return the 1-based stored aux column index
	/// if it falls in the aux region, or 0 if it does not.
	intptr_t resolve_aux_column(intptr_t display_col) const;

	/// Remove the stored aux column at 1-based index c.
	void remove_aux_column(intptr_t c);

	/// Extract an aux column, removing it from the concordance and returning it for undo storage.
	AuxColumn extract_aux_column(intptr_t c);

	/// Restore a previously extracted aux column at position c (1-based).
	void restore_aux_column(intptr_t c, AuxColumn col);

	/// True if any aux column of the given measurement type exists.
	bool has_aux_pitch() const;
	bool has_aux_formant() const;

	// ── Aux toggle flags ────────────────────────────────────────────────
	bool aux_pitch_semitones() const { return m_aux_pitch_st; }
	bool aux_pitch_erb() const { return m_aux_pitch_erb; }
	bool aux_formant_erb() const { return m_aux_formant_erb; }
	bool aux_formant_bark() const { return m_aux_formant_bark; }

	void set_aux_pitch_semitones(bool b);
	void set_aux_pitch_erb(bool b);
	void set_aux_formant_erb(bool b);
	void set_aux_formant_bark(bool b);

	// ── Measurement column introspection (for merge) ────────────────────
	/// True if col is a stored (non-derived) measurement column.
	bool is_stored_measurement(intptr_t col) const;

	/// Returns the type tag for a stored measurement column.
	AuxColumnType get_measurement_type(intptr_t col) const;

	/// Returns the raw stored double for a measurement column, NaN otherwise.
	double get_raw_measurement_value(intptr_t row, intptr_t col) const;

	bool update_match(intptr_t i, intptr_t target);

	void update_context(intptr_t i);

	std::pair<String, String> get_context(intptr_t i) const;

	/// Compute the default (non-aliased) header for column j (1-based).
	String get_default_header(intptr_t j) const;

	/// Returns a unique concordance number (1, 2, 3...) for default naming.
	static int next_id();

protected:

	/// Copy all measurement/display metadata from this concordance to `target`.
	void copy_metadata_to(Concordance &target) const;

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

	/// Return the display header for the k-th display sub-column (1-based) of aux column c (1-based).
	String aux_display_header(intptr_t c, intptr_t k) const;

	/// Return the display value for the k-th display sub-column (1-based) of aux column c, at row mi (1-based).
	String aux_display_value(intptr_t c, intptr_t mi, intptr_t k) const;

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
	bool m_has_per_match_max_freq = false; // manual + override: single Max freq column at end
	bool m_has_per_match_pitch_range = false; // pitch override: Min pitch + Max pitch columns at end
	bool m_has_series = true;         // NPoint: per-point series data present
	bool m_has_average = false;       // NPoint: average group present

	// ── Pitch metadata ──────────────────────────────────────────────────
	// Semitones and ERB-rate are computed on the fly. Only F0 in Hz is stored per match.

	bool m_is_pitch = false;          // true if this concordance holds pitch data
	bool m_has_semitones = false;     // whether semitone display columns are active
	double m_semitone_ref = 100;      // Hz (reference for semitone conversion)
	bool m_has_pitch_erb = false;     // whether ERB-rate display columns are active

	// ── Intensity metadata ──────────────────────────────────────────────

	bool m_is_intensity = false;      // true if this concordance holds intensity data

	// ── Spectral moments metadata ────────────────────────────────────────

	bool m_is_spectral_moments = false;   // true if this concordance holds spectral moments data
	bool m_sm_cog = true;                 // COG column enabled
	bool m_sm_spread = true;              // Spread column enabled
	bool m_sm_skewness = true;            // Skewness column enabled
	bool m_sm_kurtosis = true;            // Kurtosis column enabled

	// ── Voice quality metadata ───────────────────────────────────────────

	bool m_is_voice_quality = false;      // true if this concordance holds voice quality data
	bool m_vq_num_pulses       = false;   // Number of pulses
	bool m_vq_mean_period      = false;   // Mean period (ms)
	bool m_vq_mean_f0          = false;   // Mean F0 (Hz)
	bool m_vq_jitter_local     = false;   // Jitter local (%)
	bool m_vq_jitter_local_abs = false;   // Jitter local absolute (µs)
	bool m_vq_jitter_rap       = false;   // RAP (%)
	bool m_vq_jitter_ppq5      = false;   // PPQ5 (%)
	bool m_vq_jitter_ddp       = false;   // DDP (%)
	bool m_vq_shimmer_local    = false;   // Shimmer local (%)
	bool m_vq_shimmer_local_db = false;   // Shimmer local (dB)
	bool m_vq_shimmer_apq3     = false;   // APQ3 (%)
	bool m_vq_shimmer_apq5     = false;   // APQ5 (%)
	bool m_vq_shimmer_apq11    = false;   // APQ11 (%)
	bool m_vq_hnr              = false;   // HNR (dB)

	// ── Duration metadata ───────────────────────────────────────────────

	bool m_has_duration = false;       // true if duration column(s) are present
	bool m_duration_in_ms = false;     // true if durations are in milliseconds
	bool m_highlight_targets = true;   // bold red targets in the view

	// ── Measurement time metadata ───────────────────────────────────────

	bool m_has_time = false;           // true if measurement-time column(s) are present

	// ── Column aliases ───────────────────────────────────────────────────

	std::map<String, String> m_header_aliases;   // default_header → user display name

	// ── Auxiliary columns from horizontal merge ──────────────────────────

	Array<AuxColumn> m_aux_columns;     // 1-based typed aux columns

	bool m_aux_pitch_st = false;        // show semitone columns for merged F0
	bool m_aux_pitch_erb = false;       // show ERB-rate columns for merged F0
	bool m_aux_formant_erb = false;     // show ERB columns for merged formants
	bool m_aux_formant_bark = false;    // show Bark columns for merged formants

	String m_label;

	int m_target_count = 0;

	int m_context_length = 0;

	Context m_context_type = Context::None;

	Layout m_layout = Layout::Wide;
};


namespace traits {
template<> struct maybe_cyclic<Concordance> : std::false_type { };
template<> struct is_clonable<Concordance> : std::false_type { };
}

} // namespace phonometrica

#endif // PHONOMETRICA_CONCORDANCE_HPP
