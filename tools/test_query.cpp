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
 * Created: 26/07/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Headless acceptance test for the query pipeline. It exists mainly to pin the ordering guarantees the       *
 * search loop relies on now that reading files has been separated from scanning them, since those are invisible in    *
 * the concordance a user sees: every annotation is open before any scanning starts, only the sound files behind       *
 * actual matches are read, and they are read before any measurement runs. The parallel search planned on top of this  *
 * breaks silently if any of those stops holding.                                                                      *
 *                                                                                                                     *
 * Usage: test_query <path-to-test/data>                                                                               *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <any>
#include <cmath>
#include <cstdint>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <phon/runtime.hpp>
#include <phon/application/annotation.hpp>
#include <phon/application/conc/formant_query.hpp>
#include <phon/application/conc/intensity_query.hpp>
#include <phon/application/conc/pitch_query.hpp>
#include <phon/application/conc/spectral_moments_query.hpp>
#include <phon/application/conc/voice_quality_query.hpp>
#include <phon/application/conc/query.hpp>
#include <phon/application/conc/query_pool.hpp>
#include <phon/application/project.hpp>
#include <phon/application/property.hpp>
#include <phon/application/settings.hpp>
#include <phon/application/sound.hpp>
#include <phon/utils/file_system.hpp>

using namespace phonometrica;

static int g_failures = 0;

static void check(bool condition, const std::string &what)
{
	std::printf("%-72s %s\n", what.c_str(), condition ? "ok" : "FAILED");
	if (!condition) {
		++g_failures;
	}
}

// One progress event, as the GUI would see it.
struct ProgressEvent
{
	Query::Stage stage;
	int current;
	int total;
};

static const char *stage_name(Query::Stage s)
{
	switch (s)
	{
		case Query::Stage::LoadingAnnotations: return "LoadingAnnotations";
		case Query::Stage::Searching:          return "Searching";
		case Query::Stage::LoadingSounds:      return "LoadingSounds";
		case Query::Stage::Measuring:          return "Measuring";
	}
	return "?";
}

// The order in which stages were entered, with repeats collapsed.
static std::vector<Query::Stage> stage_order(const std::vector<ProgressEvent> &events)
{
	std::vector<Query::Stage> result;
	for (auto &e : events) {
		if (result.empty() || result.back() != e.stage) {
			result.push_back(e.stage);
		}
	}
	return result;
}

static std::string describe(const std::vector<Query::Stage> &stages)
{
	std::string s;
	for (auto stage : stages) {
		if (!s.empty()) s += " -> ";
		s += stage_name(stage);
	}
	return s;
}

// A minimal TextGrid with a single interval tier, one interval per label.
static void write_textgrid(const String &path, const std::vector<std::string> &labels)
{
	double duration = double(labels.size()) * 0.1;
	FILE *f = std::fopen(path.data(), "w");
	if (!f) {
		std::fprintf(stderr, "cannot write %s\n", path.data());
		std::exit(1);
	}
	std::fprintf(f, "File type = \"ooTextFile\"\nObject class = \"TextGrid\"\n\n");
	std::fprintf(f, "xmin = 0\nxmax = %f\ntiers? <exists>\nsize = 1\nitem []:\n", duration);
	std::fprintf(f, "    item [1]:\n        class = \"IntervalTier\"\n        name = \"words\"\n");
	std::fprintf(f, "        xmin = 0\n        xmax = %f\n        intervals: size = %d\n",
	             duration, (int) labels.size());
	for (size_t i = 0; i < labels.size(); i++)
	{
		std::fprintf(f, "        intervals [%d]:\n            xmin = %f\n            xmax = %f\n"
		                "            text = \"%s\"\n",
		             (int) i + 1, double(i) * 0.1, double(i + 1) * 0.1, labels[i].c_str());
	}
	std::fclose(f);
}

// A TextGrid whose single tier is a point tier ("TextTier"). Voice quality is the one measurement
// that keeps a counter of matches landing on an *instant* rather than an interval, and that counter
// is the only shared state the measurement phase writes — so exercising it needs point targets.
static void write_point_textgrid(const String &path, const std::vector<std::string> &labels)
{
	double duration = double(labels.size() + 1) * 0.1;
	FILE *f = std::fopen(path.data(), "w");
	if (!f) {
		std::fprintf(stderr, "cannot write %s\n", path.data());
		std::exit(1);
	}
	std::fprintf(f, "File type = \"ooTextFile\"\nObject class = \"TextGrid\"\n\n");
	std::fprintf(f, "xmin = 0\nxmax = %f\ntiers? <exists>\nsize = 1\nitem []:\n", duration);
	std::fprintf(f, "    item [1]:\n        class = \"TextTier\"\n        name = \"marks\"\n");
	std::fprintf(f, "        xmin = 0\n        xmax = %f\n        points: size = %d\n",
	             duration, (int) labels.size());
	for (size_t i = 0; i < labels.size(); i++)
	{
		std::fprintf(f, "        points [%d]:\n            number = %f\n            mark = \"%s\"\n",
		             (int) i + 1, double(i + 1) * 0.1, labels[i].c_str());
	}
	std::fclose(f);
}

// A 16-bit mono PCM WAV, written by hand so the test can synthesize a signal whose pitch is known
// analytically without depending on the sound-writing path it is not testing.
static void write_wav(const String &path, const std::vector<double> &samples, int rate)
{
	auto u32 = [](FILE *f, uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [](FILE *f, uint16_t v) { std::fwrite(&v, 2, 1, f); };

	FILE *f = std::fopen(path.data(), "wb");
	if (!f) {
		std::fprintf(stderr, "cannot write %s\n", path.data());
		std::exit(1);
	}
	uint32_t data_bytes = (uint32_t) (samples.size() * 2);
	std::fwrite("RIFF", 1, 4, f); u32(f, 36 + data_bytes); std::fwrite("WAVE", 1, 4, f);
	std::fwrite("fmt ", 1, 4, f); u32(f, 16); u16(f, 1); u16(f, 1);
	u32(f, (uint32_t) rate); u32(f, (uint32_t) rate * 2); u16(f, 2); u16(f, 16);
	std::fwrite("data", 1, 4, f); u32(f, data_bytes);
	for (double s : samples)
	{
		double clipped = s < -1.0 ? -1.0 : (s > 1.0 ? 1.0 : s);
		u16(f, (uint16_t) (int16_t) std::lround(clipped * 30000.0));
	}
	std::fclose(f);
}

// A case-sensitive "contains" constraint is not just a variant worth covering: it is the branch of
// find_target that copies `constraint.target` itself into every match it produces, rather than
// building a fresh string out of the event's text. That copy is a refcount update on a string
// shared by every worker, so it is the case-sensitive path — not the default one — that exercises
// whether compile() froze the pattern.
static Constraint make_constraint(const char *target, bool case_sensitive = false)
{
	Constraint c;
	c.op = Constraint::Operator::Contains;
	c.relation = Constraint::Relation::None;
	c.case_sensitive = case_sensitive;
	c.layer_index = 0; // search every layer
	c.target = target;

	return c;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <path-to-test/data>\n", argv[0]);
		return 2;
	}
	String data_dir(argv[1]);
	String sound_path = filesystem::join(data_dir, "vowel_f500_1500_2500.wav");

	if (!filesystem::exists(sound_path)) {
		std::fprintf(stderr, "missing fixture: %s\n", sound_path.data());
		return 2;
	}

	Runtime rt;
	Project::preinitialize(rt);
	Project::create(rt);
	Sound::set_sound_formats();

	// Settings live in the `phon.settings` script table, which the application normally fills by
	// reading the user's profile. Seed an empty one instead: the tests below flip the
	// parallel-search switch, and going through the real profile would both depend on and clobber
	// the user's own preferences. Settings::set_value only touches this in-memory table — nothing
	// short of Settings::write() reaches the filesystem.
	{
		Table phon_namespace;
		phon_namespace.set(Variant::make(String("settings")), Variant::make(Table()));
		rt.add_global("phon", Variant::make(phon_namespace));
	}
	Settings::initialize(&rt);
	Settings::reset_query();

	String dir = filesystem::join(filesystem::temp_directory(), "phon_test_query");
	if (!filesystem::exists(dir)) {
		filesystem::create_directory(dir);
	}

	// Three annotations, of which the first and third contain the target. The middle one is
	// the control: nothing about it must be read beyond the annotation itself.
	String p1 = filesystem::join(dir, "one.TextGrid");
	String p2 = filesystem::join(dir, "two.TextGrid");
	String p3 = filesystem::join(dir, "three.TextGrid");
	write_textgrid(p1, {"the", "cat", "sat"});
	write_textgrid(p2, {"dog", "ran"});
	write_textgrid(p3, {"a", "cat", "naps"});

	// ── Text query ───────────────────────────────────────────────────────────────────

	{
		Array<Handle<Annotation>> annotations;
		annotations.append(Handle<Annotation>::make(nullptr, p1));
		annotations.append(Handle<Annotation>::make(nullptr, p2));
		annotations.append(Handle<Annotation>::make(nullptr, p3));

		auto query = Handle<Query>::make(nullptr, String());
		query->add_constraint(make_constraint("cat"), false);
		query->set_selection(annotations);

		std::vector<ProgressEvent> events;
		// Number of annotations still closed when each event fires, so we can tell exactly when
		// loading finished relative to the stage that assumes it did.
		std::vector<int> unloaded_at_event;

		ScopedConnection conn = query->query_progress.connect(
				[&](Query::Stage stage, int current, int total) {
					events.push_back({stage, current, total});
					int unloaded = 0;
					for (auto &a : annotations) {
						if (!a->loaded()) ++unloaded;
					}
					unloaded_at_event.push_back(unloaded);
				});

		check(!annotations[1]->loaded(), "annotation is closed before the query runs");

		auto conc = query->execute();

		auto stages = stage_order(events);
		std::printf("   stages: %s\n", describe(stages).c_str());

		check(stages.size() == 2
		      && stages[0] == Query::Stage::LoadingAnnotations
		      && stages[1] == Query::Stage::Searching,
		      "text query reports LoadingAnnotations then Searching");

		// The invariant the parallel search will depend on: nothing is left to open once
		// scanning starts, so a worker thread never has to parse a file.
		bool all_open_when_searching = true;
		for (size_t i = 0; i < events.size(); i++) {
			if (events[i].stage == Query::Stage::Searching && unloaded_at_event[i] != 0) {
				all_open_when_searching = false;
			}
		}
		check(all_open_when_searching, "every annotation is open before scanning starts");

		// Two matches, and they keep the order the annotations were given in.
		check(conc->row_count() == 2, "text query finds both occurrences of \"cat\"");
	}

	// ── Formant query ────────────────────────────────────────────────────────────────

	{
		// Each annotation gets its own Sound object over the same file, so "how many sounds were
		// read" distinguishes loading only what the matches need from loading everything.
		Array<Handle<Annotation>> annotations;
		Array<Handle<Sound>> sounds;
		for (auto &path : {p1, p2, p3})
		{
			auto sound = Handle<Sound>::make(nullptr, sound_path);
			auto annot = Handle<Annotation>::make(nullptr, path);
			annot->set_sound(sound, false);
			sounds.append(sound);
			annotations.append(annot);
		}

		auto query = Handle<FormantQuery>::make(nullptr, String());
		query->add_constraint(make_constraint("cat"), false);
		query->set_selection(annotations);

		std::vector<ProgressEvent> events;
		std::vector<int> loaded_sounds_at_event;

		ScopedConnection conn = query->query_progress.connect(
				[&](Query::Stage stage, int current, int total) {
					events.push_back({stage, current, total});
					int loaded = 0;
					for (auto &s : sounds) {
						if (s->loaded()) ++loaded;
					}
					loaded_sounds_at_event.push_back(loaded);
				});

		auto conc = query->execute();

		auto stages = stage_order(events);
		std::printf("   stages: %s\n", describe(stages).c_str());

		check(stages.size() == 4
		      && stages[0] == Query::Stage::LoadingAnnotations
		      && stages[1] == Query::Stage::Searching
		      && stages[2] == Query::Stage::LoadingSounds
		      && stages[3] == Query::Stage::Measuring,
		      "formant query loads sounds between scanning and measuring");

		// No sound is touched by the scan: the sounds are read only once the matches are known.
		bool no_sound_during_search = true;
		int sounds_to_load = -1;
		for (size_t i = 0; i < events.size(); i++)
		{
			if (events[i].stage == Query::Stage::Searching && loaded_sounds_at_event[i] != 0) {
				no_sound_during_search = false;
			}
			if (events[i].stage == Query::Stage::LoadingSounds && sounds_to_load < 0) {
				sounds_to_load = events[i].total;
			}
		}
		check(no_sound_during_search, "no sound file is read while scanning");

		// Two of the three annotations match, so the third one's sound must stay closed.
		check(sounds_to_load == 2, "only the sounds behind matches are read (2 of 3)");
		check(!sounds[1]->loaded(), "the unmatched annotation's sound is never read");

		// Every sound the measurement needs is in memory before measurement starts.
		bool sounds_ready_when_measuring = true;
		for (size_t i = 0; i < events.size(); i++) {
			if (events[i].stage == Query::Stage::Measuring && loaded_sounds_at_event[i] != 2) {
				sounds_ready_when_measuring = false;
			}
		}
		check(sounds_ready_when_measuring, "every needed sound is in memory before measuring");

		check(conc->row_count() == 2, "formant query measures both matches");
	}

	// ── Cancellation ─────────────────────────────────────────────────────────────────

	{
		Array<Handle<Annotation>> annotations;
		annotations.append(Handle<Annotation>::make(nullptr, p1));
		annotations.append(Handle<Annotation>::make(nullptr, p2));
		annotations.append(Handle<Annotation>::make(nullptr, p3));

		auto query = Handle<Query>::make(nullptr, String());
		query->add_constraint(make_constraint("cat"), false);
		query->set_selection(annotations);

		// Cancel as soon as scanning begins: the run must stop instead of producing a concordance
		// of everything.
		ScopedConnection conn = query->query_progress.connect(
				[&](Query::Stage stage, int, int) {
					if (stage == Query::Stage::Searching) {
						query->request_cancel();
					}
				});

		auto conc = query->execute();
		check(conc->row_count() == 0, "cancelling during the scan stops the query");

		// A cancelled run must not poison the next one.
		conn.disconnect();
		auto again = query->execute();
		check(again->row_count() == 2, "a query re-runs normally after being cancelled");
	}

	// ── The query pool itself ────────────────────────────────────────────────────────

	{
		// Every index runs exactly once, whatever the scheduling.
		const intptr_t n = 500;
		std::vector<std::atomic<int>> visits(n);
		for (auto &v : visits) v.store(0);

		std::vector<intptr_t> ticks;
		QueryPool::get().apply_settings();
		QueryPool::get().run(n,
				[&](intptr_t i) { visits[(size_t) i].fetch_add(1); },
				[&](intptr_t done) { ticks.push_back(done); });

		bool once = true;
		for (auto &v : visits) {
			if (v.load() != 1) once = false;
		}
		check(once, "the pool runs every index exactly once");
		check(!ticks.empty() && ticks.back() == n, "the pool's last tick reports every item done");

		bool monotonic = true;
		for (size_t i = 1; i < ticks.size(); i++) {
			if (ticks[i] < ticks[i - 1]) monotonic = false;
		}
		check(monotonic, "the pool's progress count never goes backwards");
	}

	{
		// The reported failure is the lowest-indexed one, not whichever worker lost the race.
		std::string message;
		try
		{
			QueryPool::get().run(200,
					[](intptr_t i) {
						if (i == 7 || i == 30 || i == 150) {
							throw error("failed at %", (int) i);
						}
					},
					[](intptr_t) {});
		}
		catch (std::exception &e)
		{
			message = e.what();
		}
		check(message == "failed at 7", "the pool reports the lowest-indexed failure");
	}

	{
		// A pool with no workers still runs everything, on the calling thread.
		QueryPool serial_pool(0);
		intptr_t sum = 0;
		serial_pool.run(10, [&](intptr_t i) { sum += i; }, [](intptr_t) {});
		check(sum == 45, "a pool with no workers runs every item on the caller");
	}

	// ── Parallel scan: same answer as the serial scan ────────────────────────────────

	{
		// Enough annotations to cross the parallel threshold, with wildly uneven sizes so that
		// handing out work dynamically actually matters: a static split would leave whichever
		// worker drew the big one running alone.
		const int corpus_size = 40;
		Array<Handle<Annotation>> annotations;
		for (int i = 0; i < corpus_size; i++)
		{
			std::vector<std::string> labels;
			int intervals = (i == 17) ? 20000 : 40;
			for (int k = 0; k < intervals; k++)
			{
				// A third of the intervals match, so every annotation contributes several hits and
				// the merge order is actually observable.
				labels.push_back((k % 3 == 0) ? "cat" : (k % 3 == 1 ? "dog" : "concat"));
			}
			String path = filesystem::join(dir, String::format("corpus_%d.TextGrid", i));
			write_textgrid(path, labels);
			annotations.append(Handle<Annotation>::make(nullptr, path));
		}

		// One string per concordance row, holding every cell, so a difference in match content,
		// target order or row order all show up. Rows and columns are 0-based, as in the Qt model.
		auto dump_rows = [](const Handle<Concordance> &conc) -> std::vector<std::string>
		{
			std::vector<std::string> rows;
			for (intptr_t i = 0; i < conc->row_count(); i++)
			{
				std::string row;
				for (intptr_t j = 0; j < conc->column_count(); j++)
				{
					auto cell = conc->get_cell(i, j);
					row.append(cell.data(), (size_t) cell.size());
					row += '\x1f';
				}
				rows.push_back(std::move(row));
			}
			return rows;
		};

		auto run_query = [&](bool parallel, std::vector<intptr_t> *search_ticks,
		                     bool cancel_on_search, bool case_sensitive = false) -> std::vector<std::string>
		{
			Settings::set_value("query", "parallel", Variant::make(parallel));

			auto query = Handle<Query>::make(nullptr, String());
			query->add_constraint(make_constraint("cat", case_sensitive), false);
			query->set_selection(annotations);

			ScopedConnection conn = query->query_progress.connect(
					[&](Query::Stage stage, int current, int) {
						if (stage != Query::Stage::Searching) {
							return;
						}
						if (search_ticks) {
							search_ticks->push_back(current);
						}
						if (cancel_on_search) {
							query->request_cancel();
						}
					});

			return dump_rows(query->execute());
		};

		std::vector<intptr_t> serial_ticks, parallel_ticks;
		auto serial = run_query(false, &serial_ticks, false);
		auto parallel = run_query(true, &parallel_ticks, false);

		check(!serial.empty(), "the corpus produces a non-empty concordance");
		check(parallel == serial, "the parallel scan produces exactly the serial concordance");

		// Run it twice more: the merge must not depend on how the work was scheduled.
		auto again = run_query(true, nullptr, false);
		auto once_more = run_query(true, nullptr, false);
		check(again == serial && once_more == serial, "repeated parallel scans are identical");

		// The same comparison on the case-sensitive path, where every match holds a copy of the
		// shared pattern string. Repeated so that the refcount traffic on that one cell is heavy
		// enough for a lost update to turn into a use-after-free a sanitizer can see.
		auto cs_serial = run_query(false, nullptr, false, true);
		check(!cs_serial.empty(), "the case-sensitive constraint matches something");

		bool cs_stable = true;
		for (int attempt = 0; attempt < 6; attempt++)
		{
			if (run_query(true, nullptr, false, true) != cs_serial) cs_stable = false;
		}
		check(cs_stable, "parallel scans on the case-sensitive path match the serial one");

		// The point of keeping the submitting thread out of the work is that it can report
		// progress while the workers run. One tick would mean it never got a chance.
		check(parallel_ticks.size() > 1, "progress is reported while the parallel scan runs");
		check(!parallel_ticks.empty() && parallel_ticks.back() == corpus_size,
		      "the parallel scan reports every annotation done");

		// ── Cancelling a parallel scan ───────────────────────────────────────────────

		// How much of the scan a cancellation catches is a matter of timing — workers check the
		// flag between annotations, and by the time the first progress tick reaches the caller
		// several of them may already be done. What must hold regardless is that the rows which
		// *were* produced are the complete result with some of them missing: same rows, same
		// relative order, nothing duplicated or reordered. That is the bucket merge surviving a
		// partially finished scan, which a timing assertion would not actually test.
		auto cancelled = run_query(true, nullptr, true);

		check(cancelled.size() <= serial.size(), "a cancelled scan yields no more than the whole");

		size_t k = 0;
		for (auto &row : serial)
		{
			if (k < cancelled.size() && cancelled[k] == row) ++k;
		}
		check(k == cancelled.size(), "a cancelled parallel scan is a subsequence of the whole");
	}

	// ── Parallel measurement (IntensityQuery) ────────────────────────────────────────

	{
		// Six matches per annotation, all in the middle of the one-second fixture so the analysis
		// window never runs off either end. Several matches per annotation is the point: they all
		// measure through the *same* Sound, concurrently, which is the sharing that measurement
		// parallelism introduces and the scan never had.
		const std::vector<std::string> labels =
			{"x", "x", "cat", "cat", "cat", "cat", "cat", "cat", "x", "x"};

		Array<Handle<Annotation>> annotations;
		auto shared_sound = Handle<Sound>::make(nullptr, sound_path);
		for (int i = 0; i < 6; i++)
		{
			String path = filesystem::join(dir, String::format("meas_%d.TextGrid", i));
			write_textgrid(path, labels);
			auto annot = Handle<Annotation>::make(nullptr, path);
			// The first two annotations deliberately share one Sound object, so the run also covers
			// two *different* annotations measuring through a single Sound at the same time.
			annot->set_sound(i < 2 ? shared_sound : Handle<Sound>::make(nullptr, sound_path), false);
			annotations.append(annot);
		}

		auto run_intensity = [&](bool parallel, bool npoint) -> std::vector<std::string>
		{
			Settings::set_value("query", "parallel", Variant::make(parallel));

			auto query = Handle<IntensityQuery>::make(nullptr, String());
			query->add_constraint(make_constraint("cat"), false);
			query->set_selection(annotations);
			if (npoint)
			{
				// The n-point method measures several times per match, so each work item does more
				// and touches the Sound repeatedly.
				query->set_method(IntensityQuery::Method::NPoint);
				Array<double> points;
				points.append(20.0); points.append(50.0); points.append(80.0);
				query->set_measurement_points(std::move(points));
				query->set_output_series(true);
				query->set_output_average(true);
			}

			auto conc = query->execute();
			std::vector<std::string> rows;
			for (intptr_t i = 0; i < conc->row_count(); i++)
			{
				std::string row;
				for (intptr_t j = 0; j < conc->column_count(); j++)
				{
					auto cell = conc->get_cell(i, j);
					row.append(cell.data(), (size_t) cell.size());
					row += '\x1f';
				}
				rows.push_back(std::move(row));
			}
			return rows;
		};

		for (bool npoint : {false, true})
		{
			const char *what = npoint ? "n-point" : "midpoint";

			auto serial = run_intensity(false, npoint);
			check(serial.size() == 36, std::string("intensity ") + what + ": 36 matches measured");

			// A measured value must actually be a number: if every cell came back NaN the
			// comparison below would pass while measuring nothing.
			bool has_number = false;
			for (auto &row : serial) {
				if (row.find("nan") == std::string::npos && row.find('.') != std::string::npos) has_number = true;
			}
			check(has_number, std::string("intensity ") + what + ": produced real measurements");

			bool stable = true;
			for (int attempt = 0; attempt < 4; attempt++)
			{
				if (run_intensity(true, npoint) != serial) stable = false;
			}
			check(stable, std::string("intensity ") + what + ": parallel measurement matches serial");
		}
	}

	// ── Parallel measurement, the other four query types ─────────────────────────────

	{
		const std::vector<std::string> labels =
			{"x", "x", "cat", "cat", "cat", "cat", "cat", "cat", "x", "x"};

		// Half the annotations carry one value of the "Speaker" property and half the other, so a
		// query with per-property parameter overrides resolves a different level per annotation —
		// which is what makes the workers read metadata concurrently.
		Array<Handle<Annotation>> annotations;
		for (int i = 0; i < 6; i++)
		{
			String path = filesystem::join(dir, String::format("acou_%d.TextGrid", i));
			write_textgrid(path, labels);
			auto annot = Handle<Annotation>::make(nullptr, path);
			annot->set_sound(Handle<Sound>::make(nullptr, sound_path), false);
			annot->add_property(Property(String("Speaker"),
			                             std::any(String(i % 2 == 0 ? "spk-a" : "spk-b"))), false);
			annotations.append(annot);
		}

		auto dump = [](const Handle<Concordance> &conc) {
			std::vector<std::string> rows;
			for (intptr_t i = 0; i < conc->row_count(); i++)
			{
				std::string row;
				for (intptr_t j = 0; j < conc->column_count(); j++)
				{
					auto cell = conc->get_cell(i, j);
					row.append(cell.data(), (size_t) cell.size());
					row += '\x1f';
				}
				rows.push_back(std::move(row));
			}
			return rows;
		};

		// Build, run and dump a query of type T, with `configure` applied before execution.
		auto check_query = [&](const char *what, auto make, auto configure)
		{
			auto run = [&](bool parallel) {
				Settings::set_value("query", "parallel", Variant::make(parallel));
				auto query = make();
				query->add_constraint(make_constraint("cat"), false);
				query->set_selection(annotations);
				configure(query);
				return dump(query->execute());
			};

			auto serial = run(false);
			check(serial.size() == 36, std::string(what) + ": 36 matches measured");

			bool has_number = false;
			for (auto &row : serial) {
				if (row.find('.') != std::string::npos) has_number = true;
			}
			check(has_number, std::string(what) + ": produced real measurements");

			bool stable = true;
			for (int attempt = 0; attempt < 3; attempt++) {
				if (run(true) != serial) stable = false;
			}
			check(stable, std::string(what) + ": parallel matches serial");
		};

		auto nothing = [](auto &) {};

		// The shipped default (Praat) returns a two-element pitch contour for a 50 ms window on
		// this fixture, which is exactly the case Sound::get_pitch's interpolation used to get
		// wrong, so measuring it here is worth more than measuring an easier tracker.
		check_query("pitch (default tracker)",
		            [] { return Handle<PitchQuery>::make(nullptr, String()); }, nothing);
		check_query("pitch (Rapt)", [] { return Handle<PitchQuery>::make(nullptr, String()); },
		            [](auto &q) { q->set_algorithm(speech::PitchTracker::Rapt); });
		check_query("spectral moments",
		            [] { return Handle<SpectralMomentsQuery>::make(nullptr, String()); }, nothing);
		check_query("voice quality",
		            [] { return Handle<VoiceQualityQuery>::make(nullptr, String()); }, nothing);
		check_query("formant", [] { return Handle<FormantQuery>::make(nullptr, String()); }, nothing);

		// Per-property parameter override: every worker looks the annotation's "Speaker" value up
		// in the override table, which copies Strings out of the property. This is the path
		// freeze_match_metadata exists for.
		check_query("formant + property override",
		            [] { return Handle<FormantQuery>::make(nullptr, String()); },
		            [](auto &q) {
			            q->set_override_category(String("Speaker"));
			            FormantQuery::LevelOverride a; a.max_freq = 5000;
			            FormantQuery::LevelOverride b; b.max_freq = 5500;
			            q->set_override_level(String("spk-a"), a);
			            q->set_override_level(String("spk-b"), b);
		            });

		// The consensus path is a different shape: two passes over the matches with a corpus-level
		// EM between them, and it groups matches into (speaker x vowel) cells, so it reads metadata
		// on the workers too.
		check_query("formant + consensus",
		            [] { return Handle<FormantQuery>::make(nullptr, String()); },
		            [](auto &q) {
			            q->set_automatic(true);
			            q->set_auto_method(FormantQuery::AutoMethod::Intrinsic);
			            q->set_consensus(true);
			            q->set_speaker_property(String("Speaker"));
		            });

		// ── The one counter the measurement phase shares ─────────────────────────────

		// Matches on point targets, so VoiceQualityQuery::measure_match increments its
		// instant-target counter for each. That counter is written by every worker at once, which
		// is why it is atomic; a plain increment loses updates and the total comes out short.
		Array<Handle<Annotation>> point_annotations;
		for (int i = 0; i < 6; i++)
		{
			String path = filesystem::join(dir, String::format("points_%d.TextGrid", i));
			write_point_textgrid(path, {"cat", "cat", "cat", "cat", "cat", "cat", "cat", "cat"});
			auto annot = Handle<Annotation>::make(nullptr, path);
			annot->set_sound(Handle<Sound>::make(nullptr, sound_path), false);
			point_annotations.append(annot);
		}

		auto count_instants = [&](bool parallel) -> intptr_t {
			Settings::set_value("query", "parallel", Variant::make(parallel));
			auto query = Handle<VoiceQualityQuery>::make(nullptr, String());
			query->add_constraint(make_constraint("cat"), false);
			query->set_selection(point_annotations);
			query->execute();
			return query->instant_target_count();
		};

		intptr_t serial_instants = count_instants(false);
		check(serial_instants == 48, "voice quality: all 48 point targets counted (serial)");

		bool counter_stable = true;
		for (int attempt = 0; attempt < 5; attempt++) {
			if (count_instants(true) != serial_instants) counter_stable = false;
		}
		check(counter_stable, "voice quality: instant-target count survives parallel measurement");
	}

	// ── Sound::get_pitch interpolation ───────────────────────────────────────────────

	{
		// The pitch contour is sampled at the requested time by interpolating the tracker's
		// frames. Getting that index arithmetic wrong shifts every measurement in time, which is
		// invisible on steady pitch and only shows up as an error proportional to how fast F0 is
		// moving — so it is checked here against a signal whose F0 is known at every instant: a
		// linear sweep, where the true value is a straight line between its endpoints.
		const double f_lo = 100.0, f_hi = 200.0, T = 1.0;
		const int rate = 16000;
		String sweep = filesystem::join(dir, "sweep.wav");

		// A few harmonics so every tracker locks onto the fundamental.
		{
			std::vector<double> samples((size_t)(rate * T), 0.0);
			double phase = 0;
			for (size_t i = 0; i < samples.size(); i++)
			{
				double t = double(i) / rate;
				phase += 2 * M_PI * (f_lo + (f_hi - f_lo) * t / T) / rate;
				double s = 0;
				for (int k = 1; k <= 5; k++) s += std::sin(k * phase) / k;
				samples[i] = s / 2.2;
			}
			write_wav(sweep, samples, rate);
		}

		auto snd = Handle<Sound>::make(nullptr, sweep);
		snd->open();

		// Away from the file edges the window is centred and the interpolation should land on the
		// requested time. Praat is the shipped default and the one that returns the shortest
		// contour, so it is the strictest case.
		double worst = 0;
		for (double t : {0.15, 0.30, 0.50, 0.70, 0.85})
		{
			double truth = f_lo + (f_hi - f_lo) * t / T;
			double v = snd->get_pitch(1, speech::PitchTracker::Praat, t, 75, 600, 0.45,
			                          0.35, 0.14, 0.03, 0.01, false);
			worst = std::max(worst, std::abs(v - truth));
		}
		std::printf("   worst pitch error on a known sweep: %.3f Hz\n", worst);

		// The old arithmetic was half a frame (5 ms) early, which on this sweep is a 0.5 Hz error.
		// Anything approaching that means the off-by-one is back.
		check(worst < 0.1, "pitch interpolation lands on the requested time (< 0.1 Hz)");

		// The two clipped-window cases: these used to index with size_t(-1) or one past the end.
		bool edges_ok = true;
		for (double t : {0.0, 0.002, 0.998, 1.0})
		{
			try
			{
				double v = snd->get_pitch(1, speech::PitchTracker::Praat, t, 75, 600, 0.45,
				                          0.35, 0.14, 0.03, 0.01, false);
				if (std::isnan(v) || v < 0 || v > 600) edges_ok = false;
			}
			catch (std::exception &)
			{
				// A tracker refusing a degenerate window is fine; reading out of bounds is not.
			}
		}
		check(edges_ok, "pitch at the very start and end of a file stays in range");
	}

	std::printf("\n%s\n", g_failures == 0 ? "ALL OK" : "FAILURES");

	return g_failures == 0 ? 0 : 1;
}
