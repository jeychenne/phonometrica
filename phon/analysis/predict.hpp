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
 * Created: 05/05/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: predict() — produce fitted values, standard errors, and confidence/credible intervals from a fitted Model  *
 *          on training rows or new data.                                                                              *
 *                                                                                                                     *
 *          Supported scope:                                                                                           *
 *            - Fixed-effects models, all six families.                                                                *
 *            - Mixed-effects models: population-level prediction (re_form = "none", default).                         *
 *              Conditional prediction (re_form = "all", using BLUPs) is deferred.                                     *
 *            - GAM smooths via persisted basis_data.                                                                  *
 *            - Both Frequentist and Bayesian estimation. For Bayesian models, the same X·β / x'Vx arithmetic          *
 *              produces posterior mean and posterior SD because model.beta and model.vcov are populated with          *
 *              the posterior mean and posterior covariance (set by bayesian_adjust / mixture summaries). The          *
 *              "CI" interval is interpreted as a credible interval; the column names ("Fit", "SE fit", "CI lower",   *
 *              "CI upper") stay the same so script consumers don't need to branch on estimation type.                 *
 *            - Confidence/credible intervals only (no prediction intervals yet).                                      *
 *                                                                                                                     *
 *          Documented refusals (clear errors when invoked):                                                           *
 *            - re_form = "all" on a mixed-effects model: conditional prediction not yet implemented.                  *
 *            - By-factor smooths (s(x, by=f)) and re-smooths (s(g, bs="re")): not yet supported.                      *
 *            - type = "pi" or "both": prediction intervals deferred to a later release.                               *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PREDICT_HPP
#define PHONOMETRICA_PREDICT_HPP

#include <phon/array.hpp>
#include <phon/string.hpp>
#include <phon/analysis/model.hpp>

namespace phonometrica {

class DataTable; // forward

namespace stats {

struct PredictOptions
{
	String type = "ci";          // "ci" only for now ("pi"/"both" deferred)
	String scale = "response";   // "response" | "link"
	bool   bare = false;         // if true, drop echoed input columns
	double ci_level = 0.95;      // coverage probability for the CI

	// Mixed-effects: how to treat the random-effects contribution.
	//   "none" (default) — population-level prediction: η = X·β with u=0.
	//                      Variance from V_β alone. Matches what
	//                      ggpredict / predict.glmmTMB(re.form = NA) return.
	//   "all"            — conditional on the BLUPs: deferred. Will refuse.
	String re_form = "none";

	// allow_new_levels: deferred (mixed-effects path is refused regardless)
};

struct PredictResult
{
	bool ok = false;
	String error;

	// All vectors are length n_new (the number of rows in newdata, or
	// n_train when called without newdata). NaN at row i indicates a row
	// that could not be predicted (e.g. missing or non-parseable predictor).
	Array<double> fit;       // mean prediction (response or link scale per opts.scale)
	Array<double> se_fit;    // SE on the link scale
	Array<double> ci_lower;  // CI lower bound (transformed to response scale if requested)
	Array<double> ci_upper;  // CI upper bound (transformed to response scale if requested)
};

// Predict at training rows. Uses model.fitted, model.X, and model.vcov directly.
// Available only in the same session that fit the model: model.X is not
// persisted to .phon-analysis, so predict_at_training() returns an error
// after a save/reload round trip. Use predict_at(model, newdata, opts) instead.
PredictResult predict_at_training(const Model &model, const PredictOptions &opts);

// Predict at new data rows. Replays the design (categorical lookup against
// saved variable_info, smooth basis evaluation against saved basis_data).
PredictResult predict_at(const Model &model, const DataTable &newdata,
                         const PredictOptions &opts);

} // namespace stats
} // namespace phonometrica

#endif // PHONOMETRICA_PREDICT_HPP
