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
 * Created: 28/02/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/file.hpp>
#include <phon/runtime/runtime.hpp>
#include <phon/application/project.hpp>
#include <phon/application/data_table.hpp>
#include <phon/utils/file_system.hpp>
#include <phon/analysis/fitting.hpp>

namespace phonometrica {


DataTable::DataTable(Class *klass, Directory *parent, String path) :
		Document(klass, parent, std::move(path))
{

}

void DataTable::from_xml(xml_node root, const String &project_dir)
{
	static const std::string_view path_tag("Path");

	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == path_tag)
		{
			String path(node.text().get());
			Project::interpolate(path, project_dir);
			m_path = std::move(path);
		}
	}
}

void DataTable::save_metadata()
{
	// Metadata is now embedded in the project file for all file types.
	if (uses_external_metadata()) {
		Document::save_metadata();
	}
}

bool DataTable::uses_external_metadata() const
{
	// TODO: native datasets won't use external metadata
	return is<Dataset>();
}

void DataTable::to_csv(const String &path, const String &sep)
{
	File file(path, File::Write);
	auto nrow = this->row_count();
	auto ncol = this->column_count();

	for (intptr_t j = 1; j <= ncol; j++)
	{
		file.write(get_header(j));
		if (j == ncol) file.write('\n');
		else file.write(sep);
	}

	for (intptr_t i = 1; i <= nrow; i++)
	{
		for (intptr_t j = 1; j <= ncol; j++)
		{
			file.write(get_cell(i, j));
			if (j == ncol) file.write('\n');
			else file.write(sep);
		}
	}
}


// =====================================================================
// Summarize a fitted model: format and print to console.
// =====================================================================

static void print_model_summary(Runtime &rt, const stats::Model &m)
{
	// Family display name
	const char *family_display = m.family.data();
	if (m.is_negbin()) family_display = "Negative binomial";

	rt.printf("\nFamily: %s (%s)\n", family_display, m.link.data());
	if (m.is_negbin()) {
		rt.printf("Theta (overdispersion): %.4f\n", m.theta);
	}
	rt.printf("Formula: %s\n", m.formula.data());
	rt.printf("Observations: %ld\n", (long)m.nobs);

	if (!m.response_levels.empty())
	{
		rt.printf("Response levels: %s = 0, %s = 1\n",
		          m.response_levels[1].data(), m.response_levels[2].data());
	}

	rt.printf("\n");

	// Fixed effects table header
	const char *stat_label = m.is_gaussian() ? "t value" : "z value";
	rt.printf("Fixed effects:\n");
	rt.printf("%-24s %12s %12s %12s %12s\n", "", "Estimate", "Std.Error", stat_label, "Pr(>|t|)");

	for (intptr_t i = 1; i <= m.nfixed; i++)
	{
		// Coefficient name
		const char *name = (i <= m.coef_names.size()) ? m.coef_names[i].data() : "?";

		// Format p-value
		char pbuf[16];
		if (m.p[i] < 0.001) {
			snprintf(pbuf, sizeof(pbuf), "< 0.001");
		} else {
			snprintf(pbuf, sizeof(pbuf), "%.4f", m.p[i]);
		}

		// Significance stars
		const char *stars = "";
		if (m.p[i] < 0.001) stars = " ***";
		else if (m.p[i] < 0.01) stars = " **";
		else if (m.p[i] < 0.05) stars = " *";
		else if (m.p[i] < 0.1) stars = " .";

		rt.printf("%-24s %12.4f %12.4f %12.3f %12s%s\n",
		          name, m.beta[i], m.se[i], m.stat[i], pbuf, stars);
	}

	rt.printf("---\n");
	rt.printf("Signif. codes: 0 '***' 0.001 '**' 0.01 '*' 0.05 '.' 0.1 ' ' 1\n\n");

	// Random effects
	if (m.has_random_effects())
	{
		rt.printf("Random effects:\n");
		rt.printf("%-20s %12s %12s %8s\n", "Group", "Variance", "Std.Dev.", "Levels");

		for (intptr_t g = 1; g <= m.random_effects.size(); g++)
		{
			auto &re = m.random_effects[g];

			for (intptr_t t = 1; t <= re.term_names.size(); t++)
			{
				double var = (t <= re.variance.size()) ? re.variance[t] : 0.0;
				double sd = std::sqrt(std::max(var, 0.0));

				if (t == 1) {
					rt.printf("%-20s %12.4f %12.4f %8ld\n",
					          re.group_name.data(), var, sd, (long)re.nlevels);
				} else {
					rt.printf("  %-18s %12.4f %12.4f\n",
					          re.term_names[t].data(), var, sd);
				}
			}
		}

		if (m.is_gaussian()) {
			rt.printf("%-20s %12.4f %12.4f\n", "Residual", m.rse * m.rse, m.rse);
		}

		rt.printf("\n");
	}
	else if (m.is_gaussian())
	{
		rt.printf("Residual standard error: %.4f on %ld degrees of freedom\n",
		          m.rse, (long)m.df_residual);
		rt.printf("R-squared: %.4f, Adjusted R-squared: %.4f\n", m.r2, m.adj_r2);
	}

	// Information criteria
	rt.printf("AIC: %.1f  BIC: %.1f  logLik: %.1f\n", m.aic, m.bic, m.loglik);

	// Convergence info for iterative methods
	if (m.niter > 0)
	{
		if (m.converged) {
			rt.printf("Converged in %d iterations\n", m.niter);
		} else {
			rt.printf("WARNING: did not converge after %d iterations\n", m.niter);
		}
	}

	rt.printf("\n");
}


// =====================================================================
// Scripting bindings
// =====================================================================

void DataTable::initialize(Runtime &rt)
{
	// fit(data, formula_string) → Model (gaussian by default)
	auto fit2 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &formula_str = cast<String>(args[0]);
		auto &data = cast<DataTable>(args[1]);
		data.open();

		auto formula = stats::Formula::parse(formula_str);
		auto model = stats::fit(data, formula, "gaussian");

		return make_handle<stats::Model>(std::move(model));
	};

	// fit(data, formula_string, family_string) → Model
	auto fit3 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &formula_str = cast<String>(args[0]);
		auto &data = cast<DataTable>(args[1]);
		auto &family_str = cast<String>(args[2]);
		data.open();

		auto formula = stats::Formula::parse(formula_str);
		auto model = stats::fit(data, formula, family_str);

		return make_handle<stats::Model>(std::move(model));
	};

	// summarize(model) → prints summary to console
	auto summarize_model = [](Runtime &rt, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		print_model_summary(rt, model);
		return Variant();
	};

	// coef(model) → Array of coefficients
	auto coef_model = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		return make_handle<Array<double>>(model.beta);
	};

	// nobs(model) → number of observations
	auto nobs_model = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		return model.nobs;
	};

	// aic(model) → AIC
	auto aic_model = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		return model.aic;
	};

	// bic(model) → BIC
	auto bic_model = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		return model.bic;
	};

	// loglik(model) → log-likelihood
	auto loglik_model = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		return model.loglik;
	};

	// fitted(model) → fitted values array
	auto fitted_model = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		return make_handle<Array<double>>(model.fitted);
	};

	// residuals(model) → residuals array
	auto residuals_model = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		return make_handle<Array<double>>(model.residuals);
	};

	// Model field access: m.formula, m.family, m.aic, m.bic, m.r2, etc.
	auto model_get_field = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		auto &key = cast<String>(args[1]);

		if (key == "formula") return model.formula;
		if (key == "family") return model.family;
		if (key == "link") return model.link;
		if (key == "nobs") return model.nobs;
		if (key == "aic") return model.aic;
		if (key == "bic") return model.bic;
		if (key == "loglik") return model.loglik;
		if (key == "deviance") return model.deviance;
		if (key == "r2") return model.r2;
		if (key == "adj_r2") return model.adj_r2;
		if (key == "rse") return model.rse;
		if (key == "df") return model.df_residual;
		if (key == "theta") return model.theta;
		if (key == "converged") return model.converged;
		if (key == "niter") return intptr_t(model.niter);

		throw error("[Index error] Model type has no member named \"%\"", key);
	};

#define CLS(T) phonometrica::get_class<T>()

	// Register fit() with DataTable as first argument — works for both Dataset and Concordance
	rt.add_global("fit", fit2, { CLS(String), CLS(DataTable) });
	rt.add_global("fit", fit3, { CLS(String), CLS(DataTable), CLS(String) });

	// Register model functions
	rt.add_global("summarize", summarize_model, { CLS(stats::Model) });
	rt.add_global("coef", coef_model, { CLS(stats::Model) });
	rt.add_global("nobs", nobs_model, { CLS(stats::Model) });
	rt.add_global("aic", aic_model, { CLS(stats::Model) });
	rt.add_global("bic", bic_model, { CLS(stats::Model) });
	rt.add_global("loglik", loglik_model, { CLS(stats::Model) });
	rt.add_global("fitted", fitted_model, { CLS(stats::Model) });
	rt.add_global("residuals", residuals_model, { CLS(stats::Model) });

	// Register Model field access
	auto model_cls = CLS(stats::Model);
	model_cls->add_method(rt.get_field_string, model_get_field, { CLS(stats::Model), CLS(String) });

#undef CLS
}

} // namespace phonometrica
