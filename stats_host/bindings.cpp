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
 * Purpose: script bindings of the headless statistics host (MIGRATION_NOTES step 4b). Registers the app classes       *
 * (DataTable, Dataset, Model, Prior) and the statistics natives on the NEW engine via the typed rt.add_function /     *
 * rt.add_class / rt.add_field surface — the same registrations the full cutover will use. Ported from the old         *
 * registrations in phon/application/data_table.cpp (fit/priors/model fields) and project.cpp (load).                  *
 *                                                                                                                     *
 * Conventions established here for the cutover branch:                                                                *
 *   - Analysis-layer throws (the formatted `error(...)` std::runtime_error) are converted to catchable script errors  *
 *     at the binding: every native that can throw takes a leading `Isolate &` and wraps its body in                   *
 *     `catch (std::exception &) -> iso.raise(...)`. The engine deliberately converts only RuntimeError/ScriptError.   *
 *   - Model field access is rt.add_field (step-4a gap G1, now an engine feature) — `model.loglik` works as before.    *
 *   - Script indices (get_cell row/col) are 1-based with negative-from-the-end support, converted here exactly like   *
 *     the old dim_index_from_script.                                                                                  *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <cstdio>
#include <string>

#include <phon/runtime.hpp>

#include <phon/application/data_table.hpp> // the shim's headless table

#include <phon/analysis/fitting.hpp>
#include <phon/analysis/model.hpp>
#include <phon/analysis/prior.hpp>

#include "printers.hpp"

namespace phonometrica::stats_host {

namespace {

// ── conversions ──────────────────────────────────────────────────────

NumArray to_numarray(const Array<double> &a)
{
	NumArray out = NumArray::make_1d(a.size());
	double *d = out.detach();
	for (intptr_t i = 0; i < a.size(); ++i)
		d[i] = a[i];
	return out;
}

List to_string_list(const Array<String> &a)
{
	List out;
	for (intptr_t i = 0; i < a.size(); ++i)
		out.append(Variant::make(a[i]));
	return out;
}

// Convert a script exception message: analysis-layer throws become catchable
// script errors (the engine only auto-converts RuntimeError/ScriptError).
[[noreturn]] void raise_from(Isolate &iso, const std::exception &e)
{
	iso.raise(String(e.what()), 0);
}

// 1-based script index (negative counts from the end) -> 0-based, bounds-checked.
// The old engine's dim_index_from_script semantics.
intptr_t script_index(Isolate &iso, intptr_t i, intptr_t n, const char *what)
{
	intptr_t idx = (i > 0) ? i - 1 : (i < 0 ? n + i : -1);
	if (idx < 0 || idx >= n)
		iso.raise(String::format("[Index error] Invalid %s index: %ld", what, (long) i), 0);
	return idx;
}

// GAM notice printed by every fit overload (parity with the old callbacks).
void note_gam(Isolate &iso, const stats::Model &model)
{
	if (model.smooth_terms.size() > 0)
		iso.write_output("Note: GAM support (s() smooth terms) is experimental in this release.\n");
}

// The options-Table parser of the old fit_opts callbacks: strict key/value
// validation ("fit_method" = ML | REML, case-insensitive).
stats::FitOptions parse_fit_options(Isolate &iso, const Table &tab)
{
	stats::FitOptions opts;
	List keys = tab.keys();
	for (intptr_t i = 0; i < keys.size(); ++i)
	{
		Variant k = keys.get(i);
		String key;
		try
		{
			key = k.to<String>();
		}
		catch (std::exception &)
		{
			iso.raise(String("[Type error] fit() options table: keys must be strings"), 0);
		}
		if (key == "fit_method")
		{
			String val = tab.get(k).to<String>();
			String upper = val.to_upper();
			if (upper == "ML")
				opts.method = stats::Method::ML;
			else if (upper == "REML")
				opts.method = stats::Method::REML;
			else
				iso.raise(String::format("[Value error] fit() options: \"fit_method\" must be "
				                         "\"ML\" or \"REML\" (got \"%s\")",
				                         val.data()),
				          0);
		}
		else
		{
			iso.raise(String::format("[Value error] fit() options: unknown key \"%s\". "
			                         "Supported keys: \"fit_method\".",
			                         key.data()),
			          0);
		}
	}
	return opts;
}

// ── registration groups ──────────────────────────────────────────────

void register_classes(Runtime &rt)
{
	Class *object = rt.get_class("Object");
	Class *dt = rt.add_class<DataTable>("DataTable", object);
	rt.add_class<Dataset>("Dataset", dt);
	rt.add_class<stats::Model>("Model", object);
	// Script name "PriorSpec", NOT "Prior": a registered class name resolves at
	// compile time, so `Prior()` in a script would parse as (forbidden) construction
	// of a builtin instead of a call to the factory generic below. The old engine
	// hung a constructor on the class; the new-engine idiom is class "PriorSpec" +
	// factory function "Prior" — old scripts' `Prior()` syntax is unchanged.
	rt.add_class<stats::PriorSpec>("PriorSpec", object);
}

void register_data_natives(Runtime &rt)
{
	// load(path) -> Dataset. The old native lives in project.cpp:1505 and also
	// imports the file into the project; headless there is no project.
	rt.add_function("load", [](Isolate &iso, const String &path) -> Handle<Dataset> {
		try
		{
			return Handle<Dataset>::make(path);
		}
		catch (std::exception &e)
		{
			raise_from(iso, e);
		}
	});

	rt.add_function("get_cell",
	                [](Isolate &iso, DataTable &t, intptr_t row, intptr_t col) -> String {
		                intptr_t i = script_index(iso, row, t.row_count(), "row");
		                intptr_t j = script_index(iso, col, t.column_count(), "column");
		                return t.get_cell(i, j);
	                });

	rt.add_function("get_header", [](Isolate &iso, DataTable &t, intptr_t col) -> String {
		intptr_t j = script_index(iso, col, t.column_count(), "column");
		return t.get_header(j);
	});

	// get_column(table, column): NumArray for a numeric column, else a List of
	// String. Registered for a 1-based index and for a column name.
	auto column_value = [](DataTable &t, intptr_t j) -> Variant {
		if (t.is_numeric(j))
		{
			NumArray col = NumArray::make_1d(t.row_count());
			double *d = col.detach();
			for (intptr_t i = 0; i < t.row_count(); ++i)
			{
				const String cell = t.get_cell(i, j);
				bool ok = false;
				double val = cell.to_float(&ok);
				d[i] = ok ? val : std::nan("");
			}
			return Variant::make(col);
		}
		List items;
		for (intptr_t i = 0; i < t.row_count(); ++i)
			items.append(Variant::make(t.get_cell(i, j)));
		return Variant::make(items);
	};
	rt.add_function("get_column",
	                [column_value](Isolate &iso, DataTable &t, intptr_t col) -> Variant {
		                intptr_t j = script_index(iso, col, t.column_count(), "column");
		                return column_value(t, j);
	                });
	rt.add_function("get_column",
	                [column_value](Isolate &iso, DataTable &t, const String &name) -> Variant {
		                for (intptr_t j = 0; j < t.column_count(); ++j)
		                {
			                if (t.get_header(j) == name)
				                return column_value(t, j);
		                }
		                iso.raise(
		                    String::format("[Index error] No column named \"%s\"", name.data()),
		                    0);
	                });

	// add_column(table, values, name): the old `append(table, ...)` column natives
	// (data_table.cpp:2576-2577). RENAMED for the new engine: `append`'s stdlib
	// methods mark parameter 1 as a write-back ref, and ref-masks are uniform per
	// generic — a mask-0 DataTable& overload is rejected (step-4b engine gap G6).
	rt.add_function("add_column",
	                [](Isolate &iso, DataTable &t, const List &values, const String &name) {
		                if (values.size() != t.row_count())
			                iso.raise(String::format(
			                              "add_column: list has %ld elements but table has %ld rows",
			                              (long) values.size(), (long) t.row_count()),
			                          0);
		                Array<String> cells;
		                for (intptr_t i = 0; i < values.size(); ++i)
			                cells.append(values.get(i).to<String>());
		                t.add_text_column(name, cells);
	                });
	rt.add_function("add_column",
	                [](Isolate &iso, DataTable &t, const NumArray &arr, const String &name) {
		                if (arr.size() != t.row_count())
			                iso.raise(String::format(
			                              "add_column: array has %ld elements but table has %ld rows",
			                              (long) arr.size(), (long) t.row_count()),
			                          0);
		                NumArray flat = arr.contiguous();
		                t.add_numeric_column(name, flat.data() + flat.offset(), flat.size());
	                });

	// Table fields (the old dataset_get_field surface that makes sense headless).
	rt.add_field<DataTable>("path", [](const DataTable &t) { return t.path(); });
	rt.add_field<DataTable>("nrow", [](const DataTable &t) { return t.row_count(); });
	rt.add_field<DataTable>("length", [](const DataTable &t) { return t.row_count(); });
	rt.add_field<DataTable>("ncol", [](const DataTable &t) { return t.column_count(); });
	rt.add_field<DataTable>("empty", [](const DataTable &t) { return t.row_count() == 0; });
	rt.add_field<DataTable>("headers", [](const DataTable &t) {
		List out;
		for (intptr_t j = 0; j < t.column_count(); ++j)
			out.append(Variant::make(t.get_header(j)));
		return out;
	});
}

void register_fit(Runtime &rt)
{
	// fit(formula, data) — Gaussian.
	rt.add_function(
	    "fit", [](Isolate &iso, const String &formula, DataTable &data) -> Handle<stats::Model> {
		    try
		    {
			    auto model = stats::fit(data, stats::Formula::parse(formula), "gaussian");
			    model.compute_pseudo_r2();
			    note_gam(iso, model);
			    return Handle<stats::Model>::make(std::move(model));
		    }
		    catch (std::exception &e)
		    {
			    raise_from(iso, e);
		    }
	    });

	// fit(formula, data, family).
	rt.add_function("fit",
	                [](Isolate &iso, const String &formula, DataTable &data,
	                   const String &family) -> Handle<stats::Model> {
		                try
		                {
			                auto model = stats::fit(data, stats::Formula::parse(formula), family);
			                model.compute_pseudo_r2();
			                note_gam(iso, model);
			                return Handle<stats::Model>::make(std::move(model));
		                }
		                catch (std::exception &e)
		                {
			                raise_from(iso, e);
		                }
	                });

	// fit(formula, data, options) — Gaussian + options.
	rt.add_function("fit",
	                [](Isolate &iso, const String &formula, DataTable &data,
	                   const Table &options) -> Handle<stats::Model> {
		                auto opts = parse_fit_options(iso, options);
		                try
		                {
			                auto model =
			                    stats::fit(data, stats::Formula::parse(formula), "gaussian", opts);
			                model.compute_pseudo_r2();
			                note_gam(iso, model);
			                return Handle<stats::Model>::make(std::move(model));
		                }
		                catch (std::exception &e)
		                {
			                raise_from(iso, e);
		                }
	                });

	// fit(formula, data, family, options).
	rt.add_function("fit",
	                [](Isolate &iso, const String &formula, DataTable &data, const String &family,
	                   const Table &options) -> Handle<stats::Model> {
		                auto opts = parse_fit_options(iso, options);
		                try
		                {
			                auto model =
			                    stats::fit(data, stats::Formula::parse(formula), family, opts);
			                model.compute_pseudo_r2();
			                note_gam(iso, model);
			                return Handle<stats::Model>::make(std::move(model));
		                }
		                catch (std::exception &e)
		                {
			                raise_from(iso, e);
		                }
	                });

	// fit(formula, data, prior) — Bayesian, Gaussian.
	rt.add_function("fit",
	                [](Isolate &iso, const String &formula, DataTable &data,
	                   const stats::PriorSpec &priors) -> Handle<stats::Model> {
		                try
		                {
			                auto model =
			                    stats::fit(data, stats::Formula::parse(formula), "gaussian", priors);
			                model.compute_pseudo_r2();
			                note_gam(iso, model);
			                return Handle<stats::Model>::make(std::move(model));
		                }
		                catch (std::exception &e)
		                {
			                raise_from(iso, e);
		                }
	                });

	// fit(formula, data, family, prior) — Bayesian.
	rt.add_function("fit",
	                [](Isolate &iso, const String &formula, DataTable &data, const String &family,
	                   const stats::PriorSpec &priors) -> Handle<stats::Model> {
		                try
		                {
			                auto model =
			                    stats::fit(data, stats::Formula::parse(formula), family, priors);
			                model.compute_pseudo_r2();
			                note_gam(iso, model);
			                return Handle<stats::Model>::make(std::move(model));
		                }
		                catch (std::exception &e)
		                {
			                raise_from(iso, e);
		                }
	                });

	rt.add_function("get_coef",
	                [](const stats::Model &m) -> NumArray { return to_numarray(m.beta); });

	rt.add_function("summarize",
	                [](Isolate &iso, const stats::Model &m) { print_model_summary(iso, m); });

	rt.add_function("compare", [](Isolate &iso, const stats::Model &m1, const stats::Model &m2) {
		try
		{
			print_model_comparison(iso, m1, m2);
		}
		catch (std::exception &e)
		{
			raise_from(iso, e);
		}
	});
}

void register_priors(Runtime &rt)
{
	// Prior() was an add_constructor on the old engine; new-engine classes are not
	// script-constructible, so the same call syntax is provided by a factory generic.
	rt.add_function("Prior", []() -> Handle<stats::PriorSpec> {
		return Handle<stats::PriorSpec>::make(stats::PriorSpec::default_spec());
	});

	rt.add_function("set_fixed", [](stats::PriorSpec &prior, double mean, double sd) {
		prior.fixed_effects.mean = mean;
		prior.fixed_effects.sd = sd;
		prior.fixed_auto = false;
	});

	rt.add_function("set_fixed",
	                [](stats::PriorSpec &prior, const String &name, double mean, double sd) {
		                stats::NormalPrior np;
		                np.mean = mean;
		                np.sd = sd;
		                prior.coefficient_priors[name] = np;
	                });

	auto parse_variance_type = [](Isolate &iso, const String &s) -> stats::VariancePriorType {
		if (s == "pc")
			return stats::VariancePriorType::PC;
		if (s == "half_cauchy")
			return stats::VariancePriorType::HalfCauchy;
		if (s == "half_normal")
			return stats::VariancePriorType::HalfNormal;
		iso.raise(String::format("Unknown variance prior type: \"%s\". Expected \"pc\", "
		                         "\"half_cauchy\", or \"half_normal\"",
		                         s.data()),
		          0);
	};

	rt.add_function("set_variance", [parse_variance_type](Isolate &iso, stats::PriorSpec &prior,
	                                                      const String &type, double param1) {
		prior.variance_components.type = parse_variance_type(iso, type);
		prior.variance_components.param1 = param1;
		prior.variance_auto = false;
	});

	rt.add_function("set_variance",
	                [parse_variance_type](Isolate &iso, stats::PriorSpec &prior,
	                                      const String &type, double param1, double param2) {
		                prior.variance_components.type = parse_variance_type(iso, type);
		                prior.variance_components.param1 = param1;
		                prior.variance_components.param2 = param2;
		                prior.variance_auto = false;
	                });

	rt.add_function("set_residual", [parse_variance_type](Isolate &iso, stats::PriorSpec &prior,
	                                                      const String &type, double param1) {
		prior.residual.type = parse_variance_type(iso, type);
		prior.residual.param1 = param1;
		prior.residual_auto = false;
	});

	rt.add_function("set_residual",
	                [parse_variance_type](Isolate &iso, stats::PriorSpec &prior,
	                                      const String &type, double param1, double param2) {
		                prior.residual.type = parse_variance_type(iso, type);
		                prior.residual.param1 = param1;
		                prior.residual.param2 = param2;
		                prior.residual_auto = false;
	                });

	rt.add_function("set_negbin_theta", [](stats::PriorSpec &prior, double shape, double rate) {
		prior.negbin_theta.shape = shape;
		prior.negbin_theta.rate = rate;
	});

	rt.add_function("set_beta_phi", [](stats::PriorSpec &prior, double shape, double rate) {
		prior.beta_phi.shape = shape;
		prior.beta_phi.rate = rate;
	});

	rt.add_function("set_lkj", [](Isolate &iso, stats::PriorSpec &prior, double eta) {
		if (!(eta > 0.0))
			iso.raise(String::format("set_lkj: eta must be strictly positive (got %g)", eta), 0);
		prior.lkj_eta = eta;
	});
}

// The full model_get_field surface (old data_table.cpp:1080) as typed field
// getters — `model.loglik` etc. work exactly as on the old engine.
void register_model_fields(Runtime &rt)
{
	using stats::Model;

	rt.add_field<Model>("formula", [](const Model &m) { return m.formula; });
	rt.add_field<Model>("family", [](const Model &m) { return m.family; });
	rt.add_field<Model>("link", [](const Model &m) { return m.link; });
	rt.add_field<Model>("nobs", [](const Model &m) { return m.nobs; });
	rt.add_field<Model>("aic", [](const Model &m) { return m.aic; });
	rt.add_field<Model>("bic", [](const Model &m) { return m.bic; });
	rt.add_field<Model>("loglik", [](const Model &m) { return m.loglik; });
	rt.add_field<Model>("deviance", [](const Model &m) { return m.deviance; });
	rt.add_field<Model>("r2", [](const Model &m) { return m.r2; });
	rt.add_field<Model>("adj_r2", [](const Model &m) { return m.adj_r2; });
	rt.add_field<Model>("r2_marginal", [](const Model &m) { return m.r2_marginal; });
	rt.add_field<Model>("r2_conditional", [](const Model &m) { return m.r2_conditional; });
	rt.add_field<Model>("rse", [](const Model &m) { return m.rse; });
	rt.add_field<Model>("df", [](const Model &m) { return m.df_residual; });
	rt.add_field<Model>("theta", [](const Model &m) { return m.theta; });
	rt.add_field<Model>("phi", [](const Model &m) { return m.phi; });
	rt.add_field<Model>("sigma", [](const Model &m) { return m.sigma; });
	rt.add_field<Model>("nu", [](const Model &m) { return m.nu; });
	rt.add_field<Model>("converged", [](const Model &m) { return m.converged; });
	rt.add_field<Model>("niter", [](const Model &m) { return intptr_t(m.niter); });
	rt.add_field<Model>("optimizer", [](const Model &m) { return m.optimizer; });
	rt.add_field<Model>("well_identified", [](const Model &m) { return m.well_identified; });
	rt.add_field<Model>("warning", [](const Model &m) { return m.fit_warning; });
	rt.add_field<Model>("prior_warning", [](const Model &m) { return m.prior_warning; });
	rt.add_field<Model>("fitted", [](const Model &m) { return to_numarray(m.fitted); });
	rt.add_field<Model>("residuals", [](const Model &m) { return to_numarray(m.residuals); });
	rt.add_field<Model>("estimation",
	                    [](const Model &m) { return String(stats::estimation_name(m.estimation)); });
	rt.add_field<Model>("fit_method", [](const Model &m) {
		return String(m.method == stats::Method::REML ? "REML" : "ML");
	});
	rt.add_field<Model>("log_marginal", [](const Model &m) { return m.log_marginal; });
	rt.add_field<Model>("waic", [](const Model &m) { return m.waic; });
	rt.add_field<Model>("p_waic", [](const Model &m) { return m.p_waic; });
	rt.add_field<Model>("lppd", [](const Model &m) { return m.lppd; });
	rt.add_field<Model>("se_waic", [](const Model &m) { return m.se_waic; });
	rt.add_field<Model>("loo_ic", [](const Model &m) { return m.loo_ic; });
	rt.add_field<Model>("p_loo", [](const Model &m) { return m.p_loo; });
	rt.add_field<Model>("se_loo", [](const Model &m) { return m.se_loo; });
	rt.add_field<Model>("pareto_k", [](const Model &m) { return to_numarray(m.pareto_k); });
	rt.add_field<Model>("posterior_mean",
	                    [](const Model &m) { return to_numarray(m.posterior_mean); });
	rt.add_field<Model>("posterior_mode",
	                    [](const Model &m) { return to_numarray(m.posterior_mode); });
	rt.add_field<Model>("posterior_median",
	                    [](const Model &m) { return to_numarray(m.posterior_median); });
	rt.add_field<Model>("posterior_sd", [](const Model &m) { return to_numarray(m.posterior_sd); });
	rt.add_field<Model>("ci_lower", [](const Model &m) { return to_numarray(m.ci_lower); });
	rt.add_field<Model>("ci_upper", [](const Model &m) { return to_numarray(m.ci_upper); });
	rt.add_field<Model>("pd", [](const Model &m) { return to_numarray(m.pd); });
	rt.add_field<Model>("se", [](const Model &m) { return to_numarray(m.se); });
	rt.add_field<Model>("stat", [](const Model &m) { return to_numarray(m.stat); });
	rt.add_field<Model>("p", [](const Model &m) { return to_numarray(m.p); });
	rt.add_field<Model>("coef_names", [](const Model &m) { return to_string_list(m.coef_names); });
	rt.add_field<Model>("hyper_names",
	                    [](const Model &m) { return to_string_list(m.hyper_names); });
	rt.add_field<Model>("hyper_posterior_mean",
	                    [](const Model &m) { return to_numarray(m.hyper_posterior_mean); });
	rt.add_field<Model>("hyper_posterior_sd",
	                    [](const Model &m) { return to_numarray(m.hyper_posterior_sd); });
	rt.add_field<Model>("hyper_ci_lower",
	                    [](const Model &m) { return to_numarray(m.hyper_ci_lower); });
	rt.add_field<Model>("hyper_ci_upper",
	                    [](const Model &m) { return to_numarray(m.hyper_ci_upper); });

	// Random-effects summary: flat "sd(term|group)" layout parallel to hyper_*,
	// plus a final "sd(residual)" for Gaussian mixed models (old dispatcher parity).
	rt.add_field<Model>("ranef_names", [](const Model &m) {
		List items;
		for (intptr_t g = 0; g < m.random_effects.size(); g++)
		{
			auto &re = m.random_effects[g];
			for (intptr_t t = 0; t < re.term_names.size(); t++)
			{
				std::string name = "sd(" +
				                   std::string(re.term_names[t].data(),
				                               static_cast<size_t>(re.term_names[t].size())) +
				                   "|" +
				                   std::string(re.group_name.data(),
				                               static_cast<size_t>(re.group_name.size())) +
				                   ")";
				items.append(Variant::make(String(name.data(), (intptr_t) name.size())));
			}
		}
		if (m.is_gaussian() && m.has_random_effects())
			items.append(Variant::make(String("sd(residual)")));
		return items;
	});
	rt.add_field<Model>("ranef_sd", [](const Model &m) {
		Array<double> sds;
		for (intptr_t g = 0; g < m.random_effects.size(); g++)
		{
			auto &re = m.random_effects[g];
			for (intptr_t t = 0; t < re.term_names.size(); t++)
			{
				double var = (t < re.variance.size()) ? re.variance[t] : 0.0;
				sds.append(std::sqrt(std::max(var, 0.0)));
			}
		}
		if (m.is_gaussian() && m.has_random_effects())
			sds.append(m.rse);
		return to_numarray(sds);
	});

	// Smooth terms (GAM): parallel arrays, one entry per s() term.
	rt.add_field<Model>("smooth_names", [](const Model &m) {
		List items;
		for (intptr_t i = 0; i < m.smooth_terms.size(); i++)
		{
			auto &sm = m.smooth_terms[i];
			String label("s(");
			label.append(sm.variable);
			if (!sm.by.empty())
			{
				label.append("):");
				label.append(sm.by);
			}
			else
				label.append(")");
			items.append(Variant::make(label));
		}
		return items;
	});
	rt.add_field<Model>("smooth_edf", [](const Model &m) {
		Array<double> edfs;
		for (intptr_t i = 0; i < m.smooth_terms.size(); i++)
			edfs.append(m.smooth_terms[i].edf);
		return to_numarray(edfs);
	});
	rt.add_field<Model>("smooth_F", [](const Model &m) {
		Array<double> Fs;
		for (intptr_t i = 0; i < m.smooth_terms.size(); i++)
			Fs.append(m.smooth_terms[i].F_stat);
		return to_numarray(Fs);
	});
	rt.add_field<Model>("smooth_p", [](const Model &m) {
		Array<double> ps;
		for (intptr_t i = 0; i < m.smooth_terms.size(); i++)
			ps.append(m.smooth_terms[i].p_value);
		return to_numarray(ps);
	});
	rt.add_field<Model>("smooth_log_lambda", [](const Model &m) {
		Array<double> lls;
		for (intptr_t i = 0; i < m.smooth_log_lambda.size(); i++)
			lls.append(m.smooth_log_lambda[i]);
		return to_numarray(lls);
	});
	rt.add_field<Model>("n_smooth", [](const Model &m) { return m.smooth_terms.size(); });
}

} // namespace

void register_bindings(Runtime &rt)
{
	register_classes(rt);
	register_data_natives(rt);
	register_fit(rt);
	register_priors(rt);
	register_model_fields(rt);
}

} // namespace phonometrica::stats_host
