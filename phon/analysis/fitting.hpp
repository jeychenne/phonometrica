/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: fit a model from a Formula and a DataTable (Concordance or Dataset).                                       *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FITTING_HPP
#define PHONOMETRICA_FITTING_HPP

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
//! \param data      a DataTable (Concordance or Dataset) providing column access
//! \param formula   a parsed Formula object
//! \param family    family name: "gaussian" (default), "binomial", "poisson"
//! \return a fitted Model
//!
//! Rows containing missing values (NaN or empty) for any variable in the formula are
//! excluded (complete-case analysis). Throws if a variable name is not found, or if
//! there are not enough complete observations.
Model fit(const DataTable &data, const Formula &formula, const String &family = "gaussian");

} // namespace phonometrica::stats

#endif // PHONOMETRICA_FITTING_HPP
