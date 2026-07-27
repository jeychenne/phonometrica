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

#include <atomic>
#include <functional>
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

	// The successive stages a run goes through, reported through query_progress. The two
	// Loading stages read files from disk; the other two compute. A front end can therefore
	// show I/O and computation on separate progress bars without knowing what kind of query
	// it is running. A text query only ever reports the first two.
	enum class Stage
	{
		LoadingAnnotations,
		Searching,
		LoadingSounds,
		Measuring
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

	// Ask the running query to stop at the next item boundary. Called from the GUI thread
	// while the query runs; atomic because the search and measurement loops that read the
	// flag are about to move onto worker threads.
	void request_cancel() { m_cancel_requested.store(true, std::memory_order_relaxed); }

	static void initialize(Runtime &rt);

	// stage, current, total. `current` counts items finished, so it runs from 0 to `total`.
	Signal<Stage, int, int> query_progress;

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

	// Run the front half of the query: resolve the annotation set, read it from disk, scan it,
	// and (for a query that measures audio) read the sound files the matches will need. Every
	// subclass calls this as the first step of execute().
	Array<AutoMatch> search();

	// The annotations this query will scan: the explicit selection if there is one, otherwise
	// every annotation in the project, filtered through the metaconstraints. Metadata is known
	// from project load time, so this needs no file to be open.
	Array<Handle<Annotation>> resolve_annotations() const;

	// Open every annotation, reporting Stage::LoadingAnnotations. Returns false if the user
	// cancelled part way through.
	//
	// Loading is deliberately separated from scanning. Parsing an annotation registers its
	// properties in Property's global tables, which are plain statics with no locking, and it
	// fires the file-loading signals the GUI is wired to. Neither is safe once the scan runs on
	// worker threads, so the whole set is read here, on the calling thread, before any scanning
	// begins. search_annotations() may then assume every annotation it is given is open.
	bool load_annotations(const Array<Handle<Annotation>> &annotations);

	// Open the sound bound to each annotation that produced at least one match, reporting
	// Stage::LoadingSounds. Returns false if the user cancelled part way through.
	//
	// Only matched annotations are loaded: Sound::load() reads the whole file into memory, so a
	// query that matches three files out of five hundred must not read five hundred sound files.
	// This is why sounds are loaded after the scan rather than alongside the annotations.
	bool load_sounds(const Array<AutoMatch> &matches);

	// Scan every annotation for matches, reporting Stage::Searching. Every annotation must
	// already be open (see load_annotations). Dispatches to one of the two below.
	Array<AutoMatch> search_annotations(const Array<Handle<Annotation>> &annotations);

	// Whether `count` work items are worth spreading over the query pool. False when the user
	// turned parallel queries off, when the pool has no workers, or when there are too few items
	// for the coordination to pay for itself. Used for both phases: an item is an annotation
	// while scanning, a match while measuring.
	static bool use_parallel(intptr_t count);

	// Measure every match, reporting Stage::Measuring, on the query pool when it pays. This is
	// the whole second half of an acoustic query, shared by all five subclasses.
	//
	// `measure_one` runs concurrently on several matches at once. What it may touch:
	//   - the match it is given (each one belongs to a single worker), and
	//   - anything on the query that is read-only for the duration.
	// What it may not touch, because these are shared across workers:
	//   - any counter or cache on the query — make it atomic or accumulate it per match;
	//   - the Handle in `match.annotation()` or the Sound behind it. Bind those by reference
	//     (`auto &`), never by value: copying a Handle updates a refcount that is non-atomic,
	//     and two matches from one annotation share both cells.
	// It must not throw: a measurement that fails fills its row with NaN, which is what the
	// serial version always did.
	void measure_matches(Array<AutoMatch> &matches, const std::function<void(QueryMatch &)> &measure_one);

	// Freeze the metadata behind `matches` so measurement workers may read it concurrently. Called
	// by measure_matches; a subclass that measures on its own schedule must call it itself.
	void freeze_match_metadata(const Array<AutoMatch> &matches);

	Array<AutoMatch> search_annotations_serially(const Array<Handle<Annotation>> &annotations);

	// Scan on the query pool. Produces exactly the same matches, in the same order, as the serial
	// version: each annotation's hits go into its own bucket and the buckets are concatenated in
	// annotation order afterwards, so the result does not depend on how the work was scheduled.
	Array<AutoMatch> search_annotations_in_parallel(const Array<Handle<Annotation>> &annotations);

	// Scanning one annotation touches nothing shared and mutates no part of the query, which is
	// what lets several of these run at once. Keep it const so that stays true.
	Array<AutoMatch> search_annotation(const Handle<Annotation> &annot) const;

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
	std::atomic<bool> m_cancel_requested{false};
};



} // namespace phonometrica

#endif // PHONOMETRICA_QUERY_HPP
