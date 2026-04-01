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
#include <phon/analysis/model.hpp>
#include <phon/analysis/formula.hpp>
#include <phon/application/data_table.hpp>

namespace phonometrica::stats {

//! Fit a model to data using a formula.
//!
//! This is the main entry point for model fitting. It parses column names from the formula,
//! extracts the data from the DataTable, builds design matrices (with treatment contrasts for
//! categorical variables), and calls the appropriate regression function.
//!
//! \param data             a DataTable (Concordance or Dataset) providing column access
//! \param formula          a parsed Formula object
//! \param family           family name: "gaussian" (default), "binomial", "poisson"
//! \param reference_levels maps variable names to their chosen reference level for treatment
//!                         contrasts; variables not in the map use the default (first
//!                         alphabetically sorted level)
//! \return a fitted Model
//!
//! Rows containing missing values (NaN or empty) for any variable in the formula are
//! excluded (complete-case analysis). Throws if a variable name is not found, or if
//! there are not enough complete observations.
Model fit(const DataTable &data, const Formula &formula, const String &family = "gaussian",
          const std::map<String, String> &reference_levels = {});

} // namespace phonometrica::stats

#endif // PHONOMETRICA_FITTING_HPP
