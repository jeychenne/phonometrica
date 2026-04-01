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
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: an Analysis holds a reference to a DataTable (concordance or dataset) and a collection of fitted models.   *
 *          Serialized as XML (.phon-analysis). The source DataTable is referenced by path.                            *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_ANALYSIS_HPP
#define PHONOMETRICA_ANALYSIS_HPP

#include <vector>
#include <map>
#include <phon/application/data_table.hpp>
#include <phon/analysis/model.hpp>
#include <phon/analysis/formula.hpp>
#include <phon/analysis/fitting.hpp>

namespace phonometrica {

class Analysis : public Document
{
public:

	// Create a new analysis from a data source (no path yet — must be saved).
	Analysis(Directory *parent, Handle<DataTable> source);

	// Load an existing analysis from file.
	Analysis(Directory *parent, const String &path);

	// The source data (may be null if source file is missing).
	DataTable *data() { return m_source.get(); }
	const DataTable *data() const { return m_source.get(); }
	Handle<DataTable> source() const { return m_source; }
	bool has_source() const { return m_source != nullptr; }

	// The source path (always available, even if source is missing).
	const String &source_path() const { return m_source_path; }

	// Fit a model using the given formula and family. Adds it to the model list.
	// Returns the index (0-based) of the newly fitted model.
	// Throws if source is unavailable.
	int fit(const String &formula_str, const String &family = "gaussian",
	        stats::FittingCallback progress = nullptr);

	// Number of fitted models.
	int model_count() const { return (int)m_models.size(); }

	// Access a model by index (0-based).
	const stats::Model &model(int index) const { return m_models[index]; }
	stats::Model &model(int index) { return m_models[index]; }

	// Remove a model by index.
	void remove_model(int index);

	// All available column headers from the source data.
	// Returns empty array if source is unavailable.
	Array<String> column_names() const;

	// ── Reference levels for treatment contrasts ────────────────────

	// Set a custom reference level for a categorical variable.
	void set_reference_level(const String &variable, const String &level);

	// Remove the custom reference level for a variable (revert to default alphabetical).
	void clear_reference_level(const String &variable);

	// Return the custom reference level for a variable, or empty string if default.
	String reference_level(const String &variable) const;

	// All user-specified reference levels.
	const std::map<String, String> &reference_levels() const { return m_reference_levels; }

protected:

	void load() override;
	void write() override;
	bool content_modified() const override;

private:

	// Resolve the source path to a Handle<DataTable> via the project.
	void resolve_source();

	Handle<DataTable> m_source;
	String m_source_path;
	std::vector<stats::Model> m_models;
	std::map<String, String> m_reference_levels;
	bool m_modified = false;
};


namespace traits {
template<> struct maybe_cyclic<Analysis> : std::false_type { };
}

} // namespace phonometrica

#endif // PHONOMETRICA_ANALYSIS_HPP
