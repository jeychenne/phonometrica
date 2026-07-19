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
 * Created: 01/02/2021                                                                                                 *
 *                                                                                                                     *
 * Purpose: Base class for all queries. By default, searches for matches in a set of annotations. Subclasses can       *
 * additionally take phonetic measurements.                                                                            *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_QUERY_HPP
#define PHONOMETRICA_QUERY_HPP

#include <phon/application/annotation.hpp>
#include <phon/application/conc/metaconstraint.hpp>
#include <phon/application/conc/constraint.hpp>
#include <phon/application/conc/concordance.hpp>
#include <phon/regex.hpp>

namespace phonometrica {

class Query : public Document
{
public:

	String class_name() const override { return "Query"; }

	enum class Type
	{
		Text      = 1,
		Formant   = 2,
		Pitch     = 4,
		Intensity = 8,
		Duration  = 16,
		SpectralMoments = 32,
		VoiceQuality = 64,
		Acoustic  = Formant|Pitch|Intensity|Duration|SpectralMoments|VoiceQuality
	};

	using Context = Concordance::Context;

	Query(Directory *parent, String path);

	~Query() override = default;

	void add_metaconstraint(AutoMetaConstraint m, bool mutate = true);

	void add_constraint(Constraint c, bool mutate = true);

	String label() const override;

	String browser_label() const override;

	void set_label(String value, bool mutate);

	void set_selection(Array<Handle<Annotation>> files);

	virtual void clear();

	virtual Handle<Concordance> execute();

	// Note: subclasses must override this method and return false
	virtual bool is_text_query() const { return true; }

	virtual bool is_duration_query() const { return false; }

	virtual bool is_formant_query() const { return false; }

	virtual bool is_pitch_query() const { return false; }

	virtual bool is_intensity_query() const { return false; }

	virtual bool is_spectral_moments_query() const { return false; }

	virtual bool is_voice_quality_query() const { return false; }

	const Array<AutoMetaConstraint> &metaconstraints() const { return m_metaconstraints; }

	const Array<Handle<Annotation>> &selection() const { return selected_annotations; }

	intptr_t constraint_count() const { return m_constraints.size(); }

	const Constraint &get_constraint(intptr_t i) const { return m_constraints[i]; }

	virtual Handle<Query> copy() const;

	int context_length() const;

	void set_context_length(int context_length);

	Context context() const;

	void set_context(Context context);

	int reference_constraint() const;

	void set_reference_constraint(int value);

	bool include_duration() const { return m_include_duration; }

	void set_include_duration(bool value) { m_include_duration = value; }

	bool duration_in_ms() const { return m_duration_in_ms; }

	void set_duration_in_ms(bool value) { m_duration_in_ms = value; }

	bool empty();

    void request_cancel() { m_cancel_requested = true; }

	static void initialize(Runtime &rt);

    Signal<int, int> query_progress;  // current, total, cancel

protected:

	// Subclasses chain the public Query constructor. If path is non-empty, it calls load() —
	// but during base-class construction the vtable still points here, so subclasses should
	// pass an empty path and load themselves.
	void load() override;

	void write() override;

	void parse_metaconstraints_from_xml(xml_node root);

	void parse_constraints_from_xml(xml_node root);

	void parse_options_from_xml(xml_node root);

	Array<Handle<Annotation>> filter_annotations(Array<Handle<Annotation>> candidates) const;

	bool filter_metadata(const Document *file) const;

	Array<AutoMatch> search();

	Array<AutoMatch> search_annotation(const Handle<Annotation> &annot);

	Array <AutoMatch> find_matches(const Handle<Annotation> &annot, const Constraint &constraint, Array <AutoMatch> matches,
	                               Array<int> &blacklist, Constraint::Relation op, bool is_ref) const;

	Array <AutoMatch> find_matches(const Handle<Annotation> &annot, const Constraint &constraint, Array <AutoMatch> matches,
	                               intptr_t layer_index,
	                               Array<int> &seen, Constraint::Relation op, bool is_ref) const;

	std::unique_ptr<QueryMatch::Target>
    find_target(const Event &event, const Constraint &constraint, intptr_t layer_index, intptr_t &pos,
	            bool is_ref) const;

	// Constraints on the metadata
	Array<AutoMetaConstraint> m_metaconstraints;

	// Constraints on the data
	Array<Constraint> m_constraints;

	// If empty, use all the annotations from the project
	Array<Handle<Annotation>> selected_annotations;

	// Label set by the user
	String m_label;

	// Type of context for the reference constraint
	Context m_context = Context::None;

	// Reference constraint (1-based; defaults to first constraint)
	int m_ref_constraint = 1;

	// Context length, for KWIC mode
	int m_context_length = 0;

	// Whether to add duration column(s) to the concordance
	bool m_include_duration = false;

	// Whether durations are in milliseconds (true) or seconds (false)
	bool m_duration_in_ms = false;

    // Let the user cancel a query that takes too much time
    bool m_cancel_requested = false;
};


namespace traits {
template<> struct maybe_cyclic<Query> : std::false_type { };
template<> struct is_clonable<Query> : std::false_type { };
}

} // namespace phonometrica

#endif // PHONOMETRICA_QUERY_HPP
