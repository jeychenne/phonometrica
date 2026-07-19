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
 * Purpose: model summary/comparison printers for the headless statistics host (MIGRATION_NOTES step 4b). Ported       *
 * verbatim from phon/application/data_table.cpp (print_model_summary + the compare_models callback body) with         *
 * rt.printf -> std::printf — the old Runtime's console redirection is the GUI's concern; the headless host prints     *
 * to stdout. At the full cutover these printers move back next to the registrations, on the engine's output hook      *
 * (step-4a gap G4).                                                                                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <cstdio>
#include <vector>

#include <phon/analysis/model_comparison.hpp>
#include <phon/error.hpp>

#include "printers.hpp"

namespace phonometrica::stats_host {

// First-column width helper for printf-style summary tables.
//
// Returns max(min_w, longest_name + pad).  All summary tables
// (fixed effects, hyperparameters, smooth terms, random effects, …)
// use this so a long term name like
// "cor(Intercept,man.dist:subsystem[vowels]|language)" no longer
// pushes value columns out of alignment.
static int summary_column_width(const Array<String> &names, int min_w, int pad = 2)
{
	int w = min_w;
	for (intptr_t i = 0; i < names.size(); i++) {
		int len = (int)names[i].size() + pad;
		if (len > w) w = len;
	}
	return w;
}

void print_model_summary(const stats::Model &m)
{
	const char *family_display = m.family.data();
	if (m.is_negbin()) family_display = "Negative binomial";
	if (m.is_beta()) family_display = "Beta";
	if (m.is_student()) family_display = "Student t (robust)";

	std::printf("\nFamily: %s (%s)\n", family_display, m.link.data());
	if (m.is_negbin()) {
		std::printf("Theta (overdispersion): %.4f\n", m.theta);
	}
	if (m.is_beta()) {
		std::printf("Phi (precision): %.4f\n", m.phi);
	}
	if (m.is_student()) {
		std::printf("Sigma (scale): %.4f\n", m.sigma);
		std::printf("Nu (df): %.4f\n", m.nu);
		if (!m.laplace_method.empty()) {
			const char *method_label =
				(m.laplace_method == "exact")
				    ? "exact"
				    : "Fisher-information (robust fallback)";
			std::printf("Laplace correction: %s\n", method_label);
		}
	}
	std::printf("Formula: %s\n", m.formula.data());
	if (m.is_bayesian()) {
		std::printf("Estimation: Bayesian (Gaussian approximation)\n");
	} else if (m.method == stats::Method::REML) {
		std::printf("Estimation: Frequentist (restricted maximum likelihood)\n");
	} else {
		std::printf("Estimation: Frequentist (maximum likelihood)\n");
	}
	std::printf("Observations: %ld\n", (long)m.nobs);

	// Experimental notice: smooth terms are not yet at production parity.
	// Printed once per summary so users inspecting GAM output are aware
	// that EDF and effective penalty values may differ from mgcv.
	if (m.smooth_terms.size() > 0) {
		std::printf("\nNote: GAM support (s() smooth terms) is experimental in this release.\n"
		          "      Fitted curves and inference are qualitatively reliable, but smooth\n"
		          "      EDF and lambda values may differ numerically from reference\n"
		          "      implementations such as R's mgcv\n");
	}

	if (m.is_bayesian())
	{
		auto prior_str = stats::format_prior_summary(m.priors, m.family);
		std::printf("\n%s", prior_str.c_str());
	}

	if (!m.response_levels.empty())
	{
		std::printf("Response levels: %s = 0, %s = 1\n",
		          m.response_levels[0].data(), m.response_levels[1].data());
	}

	std::printf("\n");

	if (m.is_bayesian())
	{
		// ── Bayesian summary ────────────────────────────────────
		std::printf("Fixed effects (posterior):\n");

		bool has_mode = !m.posterior_mode.empty();
		bool has_median = !m.posterior_median.empty();

		int name_w = summary_column_width(m.coef_names, 24);
		std::string lbl_fmt = "%-" + std::to_string(name_w) + "s";

		if (has_mode && has_median)
		{
			std::printf((lbl_fmt + " %12s %12s %12s %12s %12s %12s %8s\n").c_str(),
			          "", "Post.Mean", "Post.Mode", "Post.Median",
			          "Post.SD", "CI.lower", "CI.upper", "pd");
		}
		else
		{
			std::printf((lbl_fmt + " %12s %12s %12s %12s %8s\n").c_str(),
			          "", "Post.Mean", "Post.SD", "CI.lower", "CI.upper", "pd");
		}

		std::string row_full = lbl_fmt + " %12.4f %12.4f %12.4f %12.4f %12.4f %12.4f %8s%s\n";
		std::string row_brief = lbl_fmt + " %12.4f %12.4f %12.4f %12.4f %8s%s\n";

		for (intptr_t i = 0; i < m.nfixed; i++)
		{
			const char *name = (i < m.coef_names.size()) ? m.coef_names[i].data() : "?";

			double pd_val = (i < m.pd.size()) ? m.pd[i] : 0.0;
			char pdbuf[16];
			snprintf(pdbuf, sizeof(pdbuf), "%.4f", pd_val);

			const char *stars = "";
			if (pd_val > 0.999) stars = " ***";
			else if (pd_val > 0.99) stars = " **";
			else if (pd_val > 0.975) stars = " *";
			else if (pd_val > 0.95) stars = " .";

			if (has_mode && has_median)
			{
				std::printf(row_full.c_str(),
				          name,
				          m.posterior_mean[i], m.posterior_mode[i], m.posterior_median[i],
				          m.posterior_sd[i],
				          m.ci_lower[i], m.ci_upper[i],
				          pdbuf, stars);
			}
			else
			{
				std::printf(row_brief.c_str(),
				          name,
				          m.posterior_mean[i], m.posterior_sd[i],
				          m.ci_lower[i], m.ci_upper[i],
				          pdbuf, stars);
			}
		}

		std::printf("---\n");
		std::printf("pd thresholds: 0.999 '***' 0.99 '**' 0.975 '*' 0.95 '.' (two-sided equivalents)\n\n");

		// Hyperparameters
		if (!m.hyper_names.empty())
		{
			bool has_hyper_sd = !m.hyper_posterior_sd.empty()
			                 && m.hyper_posterior_sd.size() == m.hyper_names.size()
			                 && !std::isnan(m.hyper_posterior_sd[0]);

			int hyper_w = summary_column_width(m.hyper_names, 30);
			std::string hyper_fmt = "%-" + std::to_string(hyper_w) + "s";

			if (has_hyper_sd)
			{
				std::printf("Hyperparameters (posterior):\n");
				std::printf((hyper_fmt + " %12s %12s %12s %12s\n").c_str(),
				          "", "Post.Mean", "Post.SD", "CI.lower", "CI.upper");

				std::string hyper_row = hyper_fmt + " %12.4f %12.4f %12.4f %12.4f\n";
				for (intptr_t i = 0; i < m.hyper_names.size(); i++)
				{
					std::printf(hyper_row.c_str(),
					          m.hyper_names[i].data(),
					          m.hyper_posterior_mean[i], m.hyper_posterior_sd[i],
					          m.hyper_ci_lower[i], m.hyper_ci_upper[i]);
				}
			}
			else
			{
				std::printf("Hyperparameters (posterior):\n");
				std::printf((hyper_fmt + " %12s\n").c_str(), "", "Post.Mean");

				std::string hyper_row = hyper_fmt + " %12.4f\n";
				for (intptr_t i = 0; i < m.hyper_names.size(); i++)
				{
					std::printf(hyper_row.c_str(),
					          m.hyper_names[i].data(), m.hyper_posterior_mean[i]);
				}
			}
			std::printf("\n");
		}
	}
	else
	{
		// ── Frequentist summary ─────────────────────────────────
		const char *stat_label = (m.is_gaussian() || m.is_student()) ? "t value" : "z value";
		std::printf("Fixed effects:\n");

		int name_w = summary_column_width(m.coef_names, 24);
		std::string lbl_fmt = "%-" + std::to_string(name_w) + "s";

		std::printf((lbl_fmt + " %12s %12s %12s %12s\n").c_str(),
		          "", "Estimate", "Std.Error", stat_label, "Pr(>|t|)");

		std::string row_fmt = lbl_fmt + " %12.4f %12.4f %12.3f %12s%s\n";

		for (intptr_t i = 0; i < m.nfixed; i++)
		{
			const char *name = (i < m.coef_names.size()) ? m.coef_names[i].data() : "?";
			char pbuf[16];
			if (m.p[i] < 0.001) snprintf(pbuf, sizeof(pbuf), "< 0.001");
			else snprintf(pbuf, sizeof(pbuf), "%.4f", m.p[i]);

			const char *stars = "";
			if (m.p[i] < 0.001) stars = " ***";
			else if (m.p[i] < 0.01) stars = " **";
			else if (m.p[i] < 0.05) stars = " *";
			else if (m.p[i] < 0.1) stars = " .";

			std::printf(row_fmt.c_str(),
			          name, m.beta[i], m.se[i], m.stat[i], pbuf, stars);
		}

		std::printf("---\n");
		std::printf("Signif. codes: 0 '***' 0.001 '**' 0.01 '*' 0.05 '.' 0.1 ' ' 1\n\n");
	}

	// ── Smooth terms (GAM / penalized regression) ───────────────
	// Mirrors the smooth-terms table shown in the GUI analysis view.
	// Applies to both frequentist and Bayesian GAMs; empty for plain
	// linear, GLM, and mixed-model fits.
	if (m.has_smooth_terms())
	{
		// Build labels first so we can both size the column and reuse them.
		// mgcv-style labels: s(x), s(x, bs=re), s(group):slope for
		// random slopes s(group, by=x, bs=re).
		auto build_label = [](const auto &sm) -> String {
			String label("s(");
			label.append(sm.variable);
			if (sm.basis == "re") {
				if (!sm.by.empty()) {
					label.append("):");
					label.append(sm.by);
				} else {
					label.append(", bs=re)");
				}
			} else {
				label.append(")");
			}
			return label;
		};

		Array<String> labels;
		for (intptr_t i = 0; i < m.smooth_terms.size(); i++) {
			labels.append(build_label(m.smooth_terms[i]));
		}

		int name_w = summary_column_width(labels, 24);
		std::string lbl_fmt = "%-" + std::to_string(name_w) + "s";

		std::printf("Approximate significance of smooth terms:\n");
		std::printf((lbl_fmt + " %8s %8s %10s %12s\n").c_str(),
		          "", "edf", "Ref.df", "F", "p-value");

		std::string row_fmt = lbl_fmt + " %8.3f %8.3f %10.2f %12s%s\n";

		for (intptr_t i = 0; i < m.smooth_terms.size(); i++)
		{
			auto &sm = m.smooth_terms[i];

			char pbuf[16];
			if (sm.p_value < 0.001) snprintf(pbuf, sizeof(pbuf), "< 0.001");
			else snprintf(pbuf, sizeof(pbuf), "%.4f", sm.p_value);

			const char *stars = "";
			if (sm.p_value < 0.001) stars = " ***";
			else if (sm.p_value < 0.01) stars = " **";
			else if (sm.p_value < 0.05) stars = " *";
			else if (sm.p_value < 0.1) stars = " .";

			std::printf(row_fmt.c_str(),
			          labels[i].data(), sm.edf, sm.ref_df, sm.F_stat, pbuf, stars);
		}
		std::printf("---\n");
		std::printf("Signif. codes: 0 '***' 0.001 '**' 0.01 '*' 0.05 '.' 0.1 ' ' 1\n\n");
	}

	if (m.has_random_effects())
	{
		// Show a "Corr" column when any group has q > 1 (random slopes).
		// For q = 1 all the way through we keep the legacy header.
		bool show_corr = false;
		for (intptr_t g = 0; g < m.random_effects.size(); g++) {
			if (m.random_effects[g].term_names.size() > 1) {
				show_corr = true;
				break;
			}
		}

		// cov_chol is the packed lower-triangular raw Cholesky factor L (NOT
		// log-diagonal) stored row by row. Element (r, c) with
		// 0-indexed r ≥ c lives at cov_chol[r*(r+1)/2 + c].
		// Covariance  Σ(s, t) = Σ_{k ≤ min(s,t)} L(s,k) · L(t,k).
		auto chol_at = [](const Array<double> &cc, intptr_t r0, intptr_t c0) -> double {
			intptr_t idx = r0 * (r0 + 1) / 2 + c0;
			return (idx < cc.size()) ? cc[idx] : 0.0;
		};
		auto cov_st = [&](const Array<double> &cc, intptr_t s0, intptr_t t0) -> double {
			if (s0 > t0) std::swap(s0, t0);
			double sum = 0.0;
			for (intptr_t k = 0; k <= s0; k++) {
				sum += chol_at(cc, s0, k) * chol_at(cc, t0, k);
			}
			return sum;
		};

		// Compute first-column width.  Two kinds of labels share this slot:
		// the (un-indented) group name and the (indented by 2) term names.
		// We need w ≥ max(min, longest_group + pad, 2 + longest_term + pad)
		// so values stay aligned regardless of which row holds the longest
		// label.  "Residual" (8 chars) trivially fits any min ≥ 10.
		constexpr int re_min_w = 20;
		constexpr int re_pad = 2;
		constexpr int re_indent = 2;
		int name_w = re_min_w;
		for (intptr_t g = 0; g < m.random_effects.size(); g++)
		{
			auto &re = m.random_effects[g];
			int gw = (int)re.group_name.size() + re_pad;
			if (gw > name_w) name_w = gw;
			for (intptr_t t = 0; t < re.term_names.size(); t++) {
				int tw = re_indent + (int)re.term_names[t].size() + re_pad;
				if (tw > name_w) name_w = tw;
			}
		}
		int term_w = name_w - re_indent;
		std::string grp_fmt  = "%-" + std::to_string(name_w) + "s";
		std::string term_fmt = "%-" + std::to_string(term_w) + "s";

		std::printf("Random effects:\n");
		if (show_corr) {
			std::printf((grp_fmt + " %12s %12s %8s   %s\n").c_str(),
				"Group", "Variance", "Std.Dev.", "Levels", "Corr");
		} else {
			std::printf((grp_fmt + " %12s %12s %8s\n").c_str(),
				"Group", "Variance", "Std.Dev.", "Levels");
		}

		std::string grp_row  = grp_fmt + " %12.4f %12.4f %8ld\n";
		std::string term_row = "  " + term_fmt + " %12.4f %12.4f %8s";

		for (intptr_t g = 0; g < m.random_effects.size(); g++)
		{
			auto &re = m.random_effects[g];
			intptr_t q = re.term_names.size();
			for (intptr_t t = 0; t < q; t++)
			{
				double var = (t < re.variance.size()) ? re.variance[t] : 0.0;
				double sd = std::sqrt(std::max(var, 0.0));

				if (t == 0) {
					std::printf(grp_row.c_str(),
						re.group_name.data(), var, sd, (long)re.nlevels);
				} else {
					// Indented term name, blank Levels slot, then one corr per
					// previous term in the same group.
					std::printf(term_row.c_str(),
						re.term_names[t].data(), var, sd, "");
					for (intptr_t s = 0; s < t; s++) {
						double var_s = (s < re.variance.size()) ? re.variance[s] : 0.0;
						double denom = std::sqrt(std::max(var_s, 1e-30)
						                       * std::max(var,   1e-30));
						double corr  = cov_st(re.cov_chol, s, t) / denom;
						std::printf(" %+7.4f", corr);
					}
					std::printf("\n");
				}
			}
		}

		if (m.is_gaussian()) {
			std::string res_fmt = grp_fmt + " %12.4f %12.4f\n";
			std::printf(res_fmt.c_str(), "Residual", m.rse * m.rse, m.rse);
		}
		std::printf("\n");
	}
	else if (m.is_gaussian() && !m.is_bayesian())
	{
		std::printf("Residual standard error: %.4f on %ld degrees of freedom\n", m.rse, (long)m.df_residual);
		std::printf("R-squared: %.4f, Adjusted R-squared: %.4f\n", m.r2, m.adj_r2);
	}

	if (!m.is_bayesian())
	{
		std::printf("AIC: %.1f  BIC: %.1f  logLik: %.1f\n", m.aic, m.bic, m.loglik);
	}
	else
	{
		if (!std::isnan(m.log_marginal))
			std::printf("Log-marginal likelihood: %.2f  logLik: %.1f\n", m.log_marginal, m.loglik);
		else
			std::printf("logLik: %.1f\n", m.loglik);

		if (!std::isnan(m.waic))
			std::printf("WAIC: %.1f  p_WAIC: %.1f\n", m.waic, m.p_waic);
		if (!std::isnan(m.loo_ic))
			std::printf("LOO-IC: %.1f  p_LOO: %.1f\n", m.loo_ic, m.p_loo);

		// Pareto k diagnostic summary.
		if (!m.pareto_k.empty())
		{
			int n_good = 0, n_ok = 0, n_bad = 0, n_verybad = 0;
			for (intptr_t j = 0; j < m.pareto_k.size(); j++)
			{
				double k = m.pareto_k[j];
				if (k < 0.5)      n_good++;
				else if (k < 0.7) n_ok++;
				else if (k < 1.0) n_bad++;
				else              n_verybad++;
			}
			if (n_bad == 0 && n_verybad == 0 && n_ok == 0)
				std::printf("Pareto k: all < 0.5 (good)\n");
			else if (n_bad == 0 && n_verybad == 0)
				std::printf("Pareto k: %d/%ld > 0.5 (ok, LOO-IC reliable)\n",
				          n_ok, (long)m.pareto_k.size());
			else
				std::printf("Pareto k: %d/%ld > 0.7 (LOO-IC may be unreliable; consider WAIC)\n",
				          n_bad + n_verybad, (long)m.pareto_k.size());
		}
	}

	if (m.niter > 0)
	{
		const char *opt_suffix = "";
		if (m.optimizer == "newton")     opt_suffix = " (Newton)";
		else if (m.optimizer == "lbfgs") opt_suffix = " (L-BFGS)";

		if (m.converged)
			std::printf("Converged in %d iterations%s\n", m.niter, opt_suffix);
		else
			std::printf("WARNING: did not converge after %d iterations%s\n", m.niter, opt_suffix);
	}

	// Identifiability diagnostic.  Independent of the optimizer's
	// convergence flag: a model can converge to a well-defined mode
	// of the prior-regularized objective while still being weakly
	// identified by the data (flat joint Hessian, pinned variance
	// component, etc.).  Only printed when the fit set the flag.
	if (!m.well_identified && !m.fit_warning.empty())
	{
		std::printf("Note: %s\n", m.fit_warning.data());
	}

	// Prior-scale diagnostic (Bayesian only).  Separate from the
	// identifiability warning: the fit can be well-identified yet still
	// have its posterior driven by a prior whose scale does not match
	// the response scale (see Model::prior_warning).
	if (!m.prior_warning.empty())
	{
		std::printf("Warning (prior scale): %s\n", m.prior_warning.data());
	}

	std::printf("\n");
}

void print_model_comparison(const stats::Model &m1, const stats::Model &m2)
{
	// Both models must use the same estimation method.
	if (m1.estimation != m2.estimation)
	{
		throw error("Cannot compare a % model with a % model. "
		            "Both models must use the same estimation method.",
		            stats::estimation_name(m1.estimation),
		            stats::estimation_name(m2.estimation));
	}

	std::vector<const stats::Model *> models = { &m1, &m2 };

	if (m1.is_bayesian())
	{
		// ── Bayesian comparison ────────────────────────────────
		auto result = stats::bayesian_compare(models);

		for (auto &w : result.warnings)
			std::printf("Warning: %s\n", w.data());

		// Summary table.
		bool show_marginal = result.has_bayes_factors;
		bool show_waic = !std::isnan(m1.waic) || !std::isnan(m2.waic);
		bool show_loo = !std::isnan(m1.loo_ic) || !std::isnan(m2.loo_ic);

		// Header.
		std::printf("\n%-8s %6s %12s", "Model", "npar", "logLik");
		if (show_marginal) std::printf(" %14s", "log p(y|M)");
		if (show_waic)     std::printf(" %10s %8s", "WAIC", "p_WAIC");
		if (show_loo)      std::printf(" %10s %8s", "LOO-IC", "p_LOO");
		std::printf("\n");

		for (size_t i = 0; i < result.rows.size(); i++)
		{
			auto &r = result.rows[i];
			std::printf("%-8d %6ld %12.1f", (int)r.original_index + 1, (long)r.npar, r.loglik);
			if (show_marginal)
				std::printf(" %14.2f", r.log_marginal);
			if (show_waic) {
				if (std::isnan(r.waic)) std::printf(" %10s %8s", "--", "--");
				else std::printf(" %10.1f %8.1f", r.waic, r.p_waic);
			}
			if (show_loo) {
				if (std::isnan(r.loo_ic)) std::printf(" %10s %8s", "--", "--");
				else std::printf(" %10.1f %8.1f", r.loo_ic, r.p_loo);
			}
			std::printf("\n");
		}

		// Bayes factors.
		if (show_marginal)
		{
			std::printf("\nPairwise log Bayes factors:\n");
			for (auto &p : result.pairs)
			{
				if (std::isnan(p.log_bf)) continue;
				std::printf("  1 vs 2:  log BF = %.2f", p.log_bf);
				if (p.log_bf > 0)      std::printf("  (favours model 1)");
				else if (p.log_bf < 0) std::printf("  (favours model 2)");
				std::printf("\n");
			}
		}

		// Pairwise IC differences.
		if (show_waic || show_loo)
		{
			std::printf("\nPairwise information criteria (negative favours model 1):\n");
			for (auto &p : result.pairs)
			{
				if (show_waic && !std::isnan(p.delta_waic))
					std::printf("  WAIC:    delta = %10.1f  SE = %10.1f\n", p.delta_waic, p.se_diff);
				if (show_loo && !std::isnan(p.delta_loo))
					std::printf("  LOO-IC:  delta = %10.1f  SE = %10.1f\n", p.delta_loo, p.se_loo_diff);
			}
		}

		std::printf("\n");
	}
	else
	{
		// ── Frequentist comparison ─────────────────────────────
		auto result = stats::anova_compare(models);

		for (auto &w : result.warnings)
			std::printf("Warning: %s\n", w.data());

		std::printf("\n%-8s %6s %12s %12s %12s %12s\n", "Model", "npar", "logLik", "AIC", "BIC", "deviance");
		for (size_t i = 0; i < result.rows.size(); i++) {
			auto &r = result.rows[i];
			std::printf("%-8d %6ld %12.4f %12.4f %12.4f %12.4f\n",
			          r.original_index + 1, (long)r.npar, r.loglik, r.aic, r.bic, r.deviance);
		}

		std::printf("\n%-12s %8s %12s %12s\n", "Comparison", "df", "Chi-sq", "Pr(>Chisq)");
		for (auto &p : result.pairs) {
			char pbuf[16];
			if (std::isnan(p.p_value)) snprintf(pbuf, sizeof(pbuf), "NA");
			else if (p.p_value < 0.001) snprintf(pbuf, sizeof(pbuf), "< 0.001");
			else snprintf(pbuf, sizeof(pbuf), "%.6f", p.p_value);

			// Label in complex-vs-simpler order: a significant test favours
			// the model named first.
			char label[32];
			snprintf(label, sizeof(label), "%d vs %d",
			         result.rows[p.index_b].original_index + 1,
			         result.rows[p.index_a].original_index + 1);
			std::printf("%-12s %8ld %12.4f %12s\n", label, (long)p.df_diff,
			          std::isnan(p.chisq) ? 0.0 : p.chisq, pbuf);
		}
		std::printf("---\nNote: a significant test favours the more complex model (named first).\n\n");
	}
}

} // namespace phonometrica::stats_host
