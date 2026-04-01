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
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FITTING_HPP
#define PHONOMETRICA_FITTING_HPP

#include <map>
#include <functional>
#include <phon/analysis/model.hpp>
#include <phon/analysis/formula.hpp>
#include <phon/application/data_table.hpp>

namespace phonometrica::stats {

// Progress callback for model fitting: receives (current_step, max_steps).
using FittingCallback = std::function<void(int, int)>;

//! Fit a model to data using a formula.
//!
//! \param progress  optional callback for reporting fitting progress
Model fit(const DataTable &data, const Formula &formula, const String &family = "gaussian",
          const std::map<String, String> &reference_levels = {},
          FittingCallback progress = nullptr);

} // namespace phonometrica::stats

#endif // PHONOMETRICA_FITTING_HPP
