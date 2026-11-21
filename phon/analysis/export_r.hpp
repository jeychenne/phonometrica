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
 * Created: 21/11/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: export the frequentist models of an analysis to R. Writes a self-contained folder holding the data the     *
 * models were fitted on (data.csv), a runnable R script that fits each equivalent model (fit.R), Phonometrica's own   *
 * estimates (phonometrica_estimates.csv) and a README. When run, the script prints a side-by-side comparison of the   *
 * two programs' estimates for each model. Models that cannot be exported are listed with the reason.                  *
 *                                                                                                                     *
 * This module is Qt-free and depends only on the analysis layer and on the DataTable read surface shared by the       *
 * application and the headless statistics host (row_count, column_count, get_header, get_cell), so that phon_stats   *
 * can drive it for the self-test in test/statistics/export_r.                                                         *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_EXPORT_R_HPP
#define PHONOMETRICA_EXPORT_R_HPP

#include <map>
#include <vector>
#include <phon/string.hpp>
#include <phon/array.hpp>
#include <phon/analysis/model.hpp>
#include <phon/analysis/formula.hpp>
#include <phon/application/data_table.hpp>

namespace phonometrica::stats {

struct ExportROptions
{
	// Shown in the header of fit.R and in README.txt. Purely informational.
	String analysis_path;

	// User-set reference levels (Analysis::reference_levels()). Needed to
	// recover the level order of categorical variables that only appear as
	// random slopes, which Model::variable_info does not cover.
	std::map<String, String> reference_levels;

	// Write phonometrica_estimates.csv and end fit.R with the comparison block.
	bool include_comparison = true;
};

// One model to export, with the label shown in the script and the comparison
// (e.g. the analysis view's "Model 3"; empty means "Model <position>").
struct ExportRModel
{
	const Model *model = nullptr;
	String label;
};

struct ExportRSkipped
{
	String label;
	String reason;
};

struct ExportRResult
{
	String directory;
	Array<String> files;                 // full paths of the files written, in write order
	Array<String> exported;              // labels of the models in fit.R
	std::vector<ExportRSkipped> skipped; // models left out, with the reason
};

// Empty string when the model can be exported; otherwise a user-facing
// reason. This is the single definition of which models the export covers:
// the GUI uses it for the context-menu tooltip and the scripting native for
// its error message.
String r_export_blocker(const Model &model);

// A filesystem-safe folder name "<analysis>_R" (no directory part).
String r_export_folder_name(const String &analysis_name);

// Write the export folder `dest_dir` (created if needed; existing files with
// the same names are overwritten) for all the models that can be exported.
// `data` must be the table the models were fitted on. Models that cannot be
// exported -- see r_export_blocker, or a model whose data has changed since it
// was fitted -- are left out and reported in the result (and in fit.R and the
// README). Throws std::runtime_error with a user-facing message if no model
// can be exported.
ExportRResult export_to_r(const std::vector<ExportRModel> &models, const DataTable &data, const String &dest_dir,
                          const ExportROptions &options = {});

// Single-model convenience: throws with the reason if the model cannot be exported.
ExportRResult export_to_r(const Model &model, const DataTable &data, const String &dest_dir,
                          const ExportROptions &options = {});

} // namespace phonometrica::stats

#endif // PHONOMETRICA_EXPORT_R_HPP
