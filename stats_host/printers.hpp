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
 * Created: 19/07/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: model summary/comparison printers of the headless statistics host (see printers.cpp).                      *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_STATS_HOST_PRINTERS_HPP
#define PHONOMETRICA_STATS_HOST_PRINTERS_HPP

#include <phon/analysis/model.hpp>

namespace phonometrica {
class Isolate;
}

namespace phonometrica::stats_host {

// The `summarize(model)` output, ported from the app's print_model_summary. Output
// goes through the engine's redirectable sink (Isolate::write_output, roadmap E3).
void print_model_summary(Isolate &iso, const stats::Model &m);

// The `compare(m1, m2)` output (ANOVA / WAIC / Bayes factors), ported from the
// app's compare_models callback. Throws on estimation-method mismatch.
void print_model_comparison(Isolate &iso, const stats::Model &m1, const stats::Model &m2);

} // namespace phonometrica::stats_host

#endif // PHONOMETRICA_STATS_HOST_PRINTERS_HPP
