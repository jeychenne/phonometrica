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
 * Created: 24/11/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: headless test of the import of CSV columns into a concordance (column_import.hpp): CSV parsing, separator  *
 * guessing, and the three ways of matching the file's lines with the concordance's rows (ID, line, key lookup),       *
 * with each policy for missing values. The concordance comes from a real query over throwaway TextGrids.              *
 *                                                                                                                     *
 * Usage: test_column_import                                                                                           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include <phon/runtime.hpp>
#include <phon/application/annotation.hpp>
#include <phon/application/conc/column_import.hpp>
#include <phon/application/conc/query.hpp>
#include <phon/application/project.hpp>
#include <phon/application/settings.hpp>
#include <phon/utils/file_system.hpp>

using namespace phonometrica;

static int g_failures = 0;

static void check(bool condition, const std::string &what)
{
	std::printf("%-80s %s\n", what.c_str(), condition ? "ok" : "FAILED");
	if (!condition) {
		++g_failures;
	}
}

// Run `f` and return the message of the exception it throws, or "" if it doesn't throw.
static std::string error_of(const std::function<void()> &f)
{
	try {
		f();
	}
	catch (std::exception &e) {
		return e.what();
	}
	return std::string();
}

static bool contains(const std::string &s, const char *needle)
{
	return s.find(needle) != std::string::npos;
}

static std::string str(const String &s)
{
	return std::string(s.data(), size_t(s.size()));
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

static CsvTable csv(const char *text, const char *sep = ",", bool header = true)
{
	return parse_csv_table(text, sep, header);
}

// All the cells of a column, as displayed.
static std::vector<std::string> column(const Concordance &conc, const char *name)
{
	std::vector<std::string> cells;
	intptr_t j = conc.find_column(String(name));
	if (j < 0) return cells;
	for (intptr_t i = 0; i < conc.row_count(); i++) {
		cells.push_back(str(conc.get_cell(i, j)));
	}
	return cells;
}

static bool is_numeric_column(Concordance &conc, const char *name)
{
	intptr_t j = conc.find_column(String(name));
	intptr_t c = conc.resolve_aux_column(j);
	if (c < 0) return false;
	// There is no read-only accessor for an aux column: take it out and put it back.
	auto col = conc.extract_aux_column(c);
	bool numeric = (col.type == Concordance::AuxColumnType::Numeric);
	conc.restore_aux_column(c, std::move(col));
	return numeric;
}

int main(int argc, char **argv)
{
	(void) argc;

	// ── CSV parsing ──────────────────────────────────────────────────────────────────

	{
		// BOM, CRLF, quoted separator, doubled quotes, line break inside quotes, blank lines, separator-only line.
		auto t = csv("\xEF\xBB\xBFWord, Freq ,Gloss\r\n"
		             "cat,12,\"feline, small\"\r\n"
		             "\r\n"
		             "dog,7,\"says \"\"woof\"\"\"\r\n"
		             "owl,3,\"two\nlines\"\r\n"
		             ",,\r\n");
		check(t.column_count() == 3 && t.row_count() == 3, "parse: 3 columns and 3 rows (blank lines skipped)");
		check(str(t.headers[0]) == "Word" && str(t.headers[1]) == "Freq", "parse: BOM removed, headers trimmed");
		check(str(t.rows[0][2]) == "feline, small", "parse: quoted field keeps the separator");
		check(str(t.rows[1][2]) == "says \"woof\"", "parse: doubled quotes are unescaped");
		check(str(t.rows[2][2]) == "two\nlines", "parse: quoted field keeps the line break");
		check(t.line_numbers[0] == 2 && t.line_numbers[1] == 4 && t.line_numbers[2] == 5,
		      "parse: line numbers of the rows");

		auto e = error_of([] { csv("a,b\n1,2\n3\n"); });
		check(contains(e, "Line 3") && contains(e, "expected 2 fields but found 1"),
		      "parse: a short row is reported with its line number");

		e = error_of([] { csv("a,b\n1,\"open\n"); });
		check(contains(e, "Line 2") && contains(e, "not closed"), "parse: an unclosed quote is reported");

		auto t2 = csv("x\ty\n1\t2\n", "\t", false);
		check(t2.row_count() == 2 && str(t2.headers[1]) == "Column 2", "parse: no header row gives Column n names");

		auto t3 = csv("a||b\n1||2\n", "||");
		check(t3.column_count() == 2 && str(t3.rows[0][1]) == "2", "parse: multi-character separator");

		check(str(guess_csv_separator("a\tb\tc\n1,2\t3")) == "\t", "guess: tab");
		check(str(guess_csv_separator("\n\na;b;c\n")) == ";", "guess: semicolon after blank lines");
		check(str(guess_csv_separator("\"x;y;z\",b\n")) == ",", "guess: separators inside quotes are ignored");
		check(str(guess_csv_separator("word\n")) == "\t", "guess: tab when there is a single column");
	}

	// ── A concordance from a real query ──────────────────────────────────────────────

	Runtime rt;
	Project::preinitialize(rt);
	Project::create(rt);
	{
		Table phon_namespace;
		phon_namespace.set(Variant::make(String("settings")), Variant::make(Table()));
		rt.add_global("phon", Variant::make(phon_namespace));
	}
	Settings::initialize(&rt, String(argv[0]));
	Settings::reset_query();

	String dir = filesystem::join(filesystem::temp_directory(), "phon_test_column_import");
	if (!filesystem::exists(dir)) {
		filesystem::create_directory(dir);
	}
	String p1 = filesystem::join(dir, "one.TextGrid");
	String p2 = filesystem::join(dir, "two.TextGrid");
	write_textgrid(p1, {"the", "cat", "sat"});
	write_textgrid(p2, {"a", "Cat", "naps"});

	Array<Handle<Annotation>> annotations;
	annotations.append(Handle<Annotation>::make(nullptr, p1));
	annotations.append(Handle<Annotation>::make(nullptr, p2));

	Constraint c;
	c.op = Constraint::Operator::Matches;
	c.relation = Constraint::Relation::None;
	c.case_sensitive = true;
	c.layer_index = 0;
	c.target = "^(cat|sat|a|Cat|naps)$";

	auto query = Handle<Query>::make(nullptr, String());
	query->add_constraint(c, false);
	query->set_selection(annotations);
	auto conc = query->execute();

	// The match column: the first target column.
	intptr_t target = -1;
	for (intptr_t j = 0; j < conc->column_count() && target < 0; j++) {
		if (conc->is_target(j)) target = j;
	}

	std::vector<std::string> words;
	for (intptr_t i = 0; i < conc->row_count(); i++) words.push_back(str(conc->get_cell(i, target)));
	std::string shown;
	for (auto &w : words) shown += w + " ";
	std::printf("   matches: %s\n", shown.c_str());
	check(words == std::vector<std::string>({"cat", "sat", "a", "Cat", "naps"}), "query gives the expected matches");
	if (words.size() != 5) return 1;

	const intptr_t ncol = conc->column_count();

	// ── Lookup ───────────────────────────────────────────────────────────────────────

	auto lexicon = csv("Word,Freq,POS\n"
	                   "cat,120,N\n"
	                   "sat,30,V\n"
	                   "naps,5,V\n"
	                   "dog,80,N\n");

	ColumnImportOptions lookup;
	lookup.mode = ColumnImportMode::Lookup;
	lookup.conc_key_column = target;
	lookup.csv_key_column = 0;
	lookup.columns = { {String("Freq"), 1}, {String("POS"), 2} };

	{
		auto plan = plan_column_import(*conc, lexicon, lookup);
		check(plan.matched == 3 && plan.unmatched == 2, "lookup: case-sensitive keys match 3 rows out of 5");

		lookup.ignore_case = true;
		plan = plan_column_import(*conc, lexicon, lookup);
		check(plan.matched == 4 && plan.unmatched == 1 && plan.unused_rows == 0,
		      "lookup: ignoring case matches 4 rows; unused lexicon lines are not counted");
		check(plan.examples.size() == 1 && contains(str(plan.examples[0]), "Row 3: \"a\" was not found"),
		      "lookup: the missing row is described");

		lookup.missing = MissingValuePolicy::Error;
		auto e = error_of([&] { import_columns(*conc, lexicon, lookup); });
		check(contains(e, "some values are missing") && contains(e, "Row 3"), "lookup: Error policy refuses to import");
		check(conc->column_count() == ncol, "lookup: a refused import leaves the concordance unchanged");

		lookup.missing = MissingValuePolicy::Empty;
		import_columns(*conc, lexicon, lookup);
		check(conc->column_count() == ncol + 2, "lookup: two columns are added");
		check(column(*conc, "Freq") == std::vector<std::string>({"120", "30", "nan", "120", "5"}),
		      "lookup: numeric values copied to every row with the key (missing = nan)");
		check(column(*conc, "POS") == std::vector<std::string>({"N", "V", "", "N", "V"}),
		      "lookup: text values copied (missing = empty)");
		check(is_numeric_column(*conc, "Freq"), "lookup: an all-number column is numeric");

		e = error_of([&] { import_columns(*conc, lexicon, lookup); });
		check(contains(e, "already a column named \"Freq\""), "lookup: importing a column twice is refused");
	}

	{
		ColumnImportOptions opts = lookup;
		opts.columns = { {String("Freq2"), 1} };
		opts.missing = MissingValuePolicy::Custom;
		opts.custom_value = String("0");
		import_columns(*conc, lexicon, opts);
		check(column(*conc, "Freq2") == std::vector<std::string>({"120", "30", "0", "120", "5"}),
		      "custom value: a number keeps the column numeric");

		opts.columns = { {String("Freq3"), 1} };
		opts.custom_value = String("unknown");
		import_columns(*conc, lexicon, opts);
		check(column(*conc, "Freq3")[2] == "unknown" && !is_numeric_column(*conc, "Freq3"),
		      "custom value: text makes the column a text column");

		opts.columns = { {String("Freq4"), 1} };
		opts.missing = MissingValuePolicy::NaN;
		auto with_na = csv("Word,Freq\ncat,NA\nsat,\nnaps,5\na,2\n");
		auto plan = plan_column_import(*conc, with_na, opts);
		check(plan.matched == 5 && plan.missing_cells == 3, "missing cells: NA and empty cells are missing");
		import_columns(*conc, with_na, opts);
		check(column(*conc, "Freq4") == std::vector<std::string>({"nan", "nan", "2", "nan", "5"}),
		      "missing cells: NaN policy");
	}

	{
		// A key on two lines is fine if the lines agree on the imported columns, and an error otherwise.
		ColumnImportOptions opts = lookup;
		opts.columns = { {String("POS2"), 2} };
		auto dup_same = csv("Word,Freq,POS\ncat,1,N\ncat,2,N\n");
		check(error_of([&] { plan_column_import(*conc, dup_same, opts); }).empty(),
		      "duplicates: identical imported values are not ambiguous");

		opts.columns = { {String("Freq5"), 1} };
		auto e = error_of([&] { plan_column_import(*conc, dup_same, opts); });
		check(contains(e, "\"cat\" occurs on several lines of the file (lines 2 and 3)"),
		      "duplicates: different values are ambiguous");

		auto dup_unused = csv("Word,Freq\ndog,1\ndog,2\n");
		check(error_of([&] { plan_column_import(*conc, dup_unused, opts); }).empty(),
		      "duplicates: a duplicated key that no row uses is ignored");
	}

	// ── By ID ────────────────────────────────────────────────────────────────────────

	{
		conc->add_text_column(String("ID"), { String("m1"), String("m2"), String("m3"), String("m4"), String("m5") });
		intptr_t id_col = conc->find_column(String("ID"));

		auto ids = csv("Speaker;ID\nS3;m3\nS1; m1 \nS9;m9\n", ";");
		ColumnImportOptions opts;
		opts.mode = ColumnImportMode::ById;
		opts.conc_key_column = id_col;
		opts.csv_key_column = 1;
		opts.columns = { {String("Speaker"), 0} };

		auto plan = plan_column_import(*conc, ids, opts);
		check(plan.matched == 2 && plan.unmatched == 3 && plan.unused_rows == 1,
		      "by ID: 2 rows matched, 3 unmatched, 1 line of the file unused (keys trimmed)");

		import_columns(*conc, ids, opts);
		check(column(*conc, "Speaker") == std::vector<std::string>({"S1", "", "S3", "", ""}),
		      "by ID: values go to the rows with the same ID");

		opts.conc_key_column = target;
		opts.ignore_case = true;
		opts.columns = { {String("Speaker2"), 0} };
		auto e = error_of([&] { plan_column_import(*conc, ids, opts); });
		check(contains(e, "The ID \"Cat\" occurs more than once in the concordance (rows 1 and 4)"),
		      "by ID: repeated IDs in the concordance are refused");
	}

	// ── By line ──────────────────────────────────────────────────────────────────────

	{
		ColumnImportOptions opts;
		opts.mode = ColumnImportMode::ByLine;
		opts.columns = { {String("Duration"), 0}, {String("Note"), 1} };

		auto e = error_of([&] { plan_column_import(*conc, csv("d,n\n1,x\n2,y\n"), opts); });
		check(contains(e, "The file has 2 data rows but the concordance has 5 matches"),
		      "by line: a different number of lines is refused");

		import_columns(*conc, csv("d\tn\n0.1\tx\n0.2\t\n0.3\tz\n0.4\tw\n0.5\tv\n", "\t"), opts);
		check(column(*conc, "Duration") == std::vector<std::string>({"0.1", "0.2", "0.3", "0.4", "0.5"}),
		      "by line: lines go to the rows in order");
		check(column(*conc, "Note")[1].empty(), "by line: an empty cell stays empty");
	}

	{
		// Two new columns with the same name are refused before anything is added.
		intptr_t before = conc->column_count();
		ColumnImportOptions opts;
		opts.mode = ColumnImportMode::ByLine;
		opts.columns = { {String("X"), 0}, {String("X"), 1} };
		auto e = error_of([&] { import_columns(*conc, csv("a,b\n1,1\n2,2\n3,3\n4,4\n5,5\n"), opts); });
		check(contains(e, "used more than once") && conc->column_count() == before,
		      "names: duplicate new names are refused, nothing added");
	}

	if (g_failures > 0) {
		std::printf("\n%d check(s) FAILED\n", g_failures);
		return 1;
	}
	std::printf("\nall checks passed\n");
	return 0;
}
