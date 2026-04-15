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
 * Purpose: fit a model from a Formula and a DataTable (Concordance or Dataset).                                       *
 *                                                                                                                     *
 * Note: The core architecture and integration logic were designed and authored by Julien Eychenne. Portions of the    *
 * statistical estimation logic in this file were developed with the assistance of Claude Opus 4.6 (Anthropic), based  *
 * on published statistical literature and reference R implementations.                                                *
 * All AI-assisted logic has been manually audited, refactored, and validated against a diverse suite of datasets and  *
 * reference R packages to ensure mathematical accuracy and implementation integrity.                                  *
 * While every effort has been made to ensure reliability, this software is provided without a guarantee of being      *
 * bug-free. In the event that discrepancies or errors are discovered, the author will do his best to address them.    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FITTING_HPP
#define PHONOMETRICA_FITTING_HPP

#include <map>
#include <functional>
#include <phon/analysis/model.hpp>
#include <phon/analysis/formula.hpp>
#include <phon/analysis/prior.hpp>
#include <phon/application/data_table.hpp>

namespace phonometrica::stats {

// Progress callback for model fitting: receives (current_step, max_steps).
using FittingCallback = std::function<void(int, int)>;

//! Fit a model to data using a formula (frequentist).
//!
//! \param progress  optional callback for reporting fitting progress
Model fit(const DataTable &data, const Formula &formula, const String &family = "gaussian",
          const std::map<String, String> &reference_levels = {},
          FittingCallback progress = nullptr,
          int max_iter = 200);

//! Fit a model to data using a formula (Bayesian).
//! When a PriorSpec is provided, the model is fitted using INLA-style
//! approximate Bayesian inference. A default-constructed PriorSpec gives
//! weakly informative priors.
//!
//! \param progress  optional callback for reporting fitting progress
Model fit(const DataTable &data, const Formula &formula, const String &family,
          const PriorSpec &priors,
          const std::map<String, String> &reference_levels = {},
          FittingCallback progress = nullptr,
          int max_iter = 200);

} // namespace phonometrica::stats

#endif // PHONOMETRICA_FITTING_HPP
