# =====================================================================
# statistics/bayesian/inla/_helpers.R
# =====================================================================
# Shared helpers for the INLA reference suite. Mirrors the role of
# bayesian/_helpers.R for the brms suite.
#
# Reference engine:
#   R-INLA with PC priors. Each per-chapter gen_reference_*.R script
#   fits its models with priors that match Phonometrica's auto-scaled
#   defaults (see scale_default_priors in phon/analysis/fitting.cpp),
#   then serialises posterior summaries to a JSON reference.
#
# Why INLA as a second reference engine:
#   The brms suite tests Phon's (Laplace + grid integration) against
#   HMC truth. The INLA suite tests it against the canonical
#   Laplace-style approximation. Disagreements with INLA point to
#   implementation differences; agreement with INLA but disagreement
#   with brms points to Laplace-vs-MCMC fundamentals.
#
# Prior matching (see README.md for the full table):
#   * β intercept: N(mean(y_link), max(2.5, 2.5·sd(y_link)))
#   * β slopes:    N(0,             max(2.5, 2.5·sd(y_link)))
#   * RE SDs:      PC(scale, 0.05)
#   * Residual:    PC(scale, 0.05) (gaussian only)
#   * Student-t: INLA defaults on ALL controls (β priors + family
#     hypers). Anything narrower destabilises INLA's inner GMRF
#     optimiser. See inla_defaults() and the README carve-out.
#   * NB θ, Beta φ: INLA defaults on family hypers; β stays matched.
#     Tolerances on these specific posteriors are loosened.
# =====================================================================

suppressPackageStartupMessages({
    library(jsonlite)
    library(INLA)
    library(brinla)
})

# --------------------------------------------------------------------
# compute_link_stats — exact mirror of bayesian/_helpers.R helper.
# Replicates the link-scale statistics computed by Phonometrica's
# scale_default_priors() in fitting.cpp.
# --------------------------------------------------------------------

compute_link_stats <- function(y, family) {
    y <- as.numeric(y)
    y <- y[is.finite(y)]
    stopifnot(length(y) >= 2)

    if (family %in% c("binomial", "beta")) {
        # logit link, with a clamp that matches the C++ side (0.001, 0.999)
        yc <- pmin(pmax(y, 0.001), 0.999)
        ly <- log(yc / (1 - yc))
    } else if (family %in% c("poisson", "negbin")) {
        ly <- log(y + 0.5)
    } else {
        # gaussian, student — identity link
        ly <- y
    }
    list(mean = mean(ly), sd = sd(ly))
}

# --------------------------------------------------------------------
# phon_inla_priors — produce the INLA prior controls matching Phon's
# auto-scaled defaults. Returns a list with components:
#   $control.fixed   — prior on β (intercept + slopes)
#   $control.family  — prior on family-level hyper (residual SD via
#                      PC for gaussian/student; INLA default otherwise
#                      — see the dispersion-prior carve-out in README)
#   $pc.prec.scale   — the PC prior scale; caller uses this for
#                      f(g, hyper = pc_re_hyper(scale)) on any RE term
# --------------------------------------------------------------------

phon_inla_priors <- function(y, family) {
    s <- compute_link_stats(y, family)
    scale <- max(2.5, 2.5 * s$sd)
    prec_beta <- 1.0 / (scale * scale)

    # control.fixed: one common prec on slopes (Phon also uses a single
    # scale across all non-intercept fixed effects); intercept gets its
    # own (mean, prec) targeting the raw intercept.
    ctrl.fixed <- list(
        mean.intercept = s$mean,
        prec.intercept = prec_beta,
        mean = 0,
        prec = prec_beta
    )

    # PC prior on the family-level SD applies ONLY to gaussian. For
    # Student-t, INLA's PC prior on the t-scale precision combined
    # with weakly-informative β priors makes the inner GMRF Q matrix
    # numerically non-PD (assertion in tabulate-Qfunc.c). brinla's
    # own family="T" fit uses INLA defaults — we follow that idiom.
    # See the "Carve-out" section in README.md.
    if (family == "gaussian") {
        ctrl.family <- list(hyper = list(prec = list(
            prior = "pc.prec",
            param = c(scale, 0.05)
        )))
    } else {
        ctrl.family <- list()
    }

    list(
        control.fixed  = ctrl.fixed,
        control.family = ctrl.family,
        pc.prec.scale  = scale
    )
}

# --------------------------------------------------------------------
# inla_defaults — return a `pri` object that asks fit_inla to use
# INLA's own defaults for everything. Used for families where any
# narrower configuration destabilises INLA's optimiser (Student-t on
# the current INLA `devel` build, confirmed via brinla/blr.R).
#
# The fit_inla helper drops empty control.* fields before calling
# inla(), so passing this is equivalent to the brinla idiom:
#     inla(formula, family = "T", data = ..., control.compute = ...)
#
# Trade-off: the β posteriors are no longer matched to Phon's
# auto-scaled defaults. For n in the dozens with weakly-informative
# priors on either side, posteriors are likelihood-dominated and the
# numerical disagreement is small (mean shift typically < 5% of the
# parameter scale). The corresponding test in test_blr.phon
# (tol_b3) already accommodates this.
# --------------------------------------------------------------------

inla_defaults <- function() {
    list(
        control.fixed  = list(),
        control.family = list(),
        pc.prec.scale  = NA_real_
    )
}

# --------------------------------------------------------------------
# pc_re_hyper — INLA hyper-spec for an f(g, ...) RE term with PC prior
# matching Phon's set_variance(p, "pc", scale, 0.05). Caller embeds
# this in the formula:
#     f(g, model = "iid", hyper = pc_re_hyper(scale))
# --------------------------------------------------------------------

pc_re_hyper <- function(scale) {
    list(prec = list(prior = "pc.prec", param = c(scale, 0.05)))
}

# --------------------------------------------------------------------
# fit_inla — wrap inla() with matched priors and the compute toggles
# we need for downstream extraction. Caller passes formula, data,
# family, and any extra args (e.g. offset, Ntrials).
#
# control.compute:
#   * dic, waic, cpo : extracted by extract_fit_inla
#   * return.marginals.predictor : disabled (we don't currently
#     compare fitted values; saves memory on n>1000 models)
# control.predictor:
#   * link = 1 ensures linear predictor is reported on the link scale
#     even when no NA rows are present (defensive, no effect on the fit)
# --------------------------------------------------------------------

fit_inla <- function(formula_obj, data, family, pri, ...) {
    extra <- list(...)
    base_args <- list(
        formula        = formula_obj,
        data           = data,
        family         = family,
        control.compute = list(dic = TRUE, waic = TRUE, cpo = TRUE,
                               return.marginals = TRUE)
    )
    # Force inla.mode = "classic" for Student-t. INLA's current default
    # "compact" mode (and the "experimental" alternative) both crash
    # on family="T" via a Q-matrix non-PD assertion in the inner GMRF
    # init (tabulate-Qfunc.c:121). Bisected with bayesian/inla/probe_b3.R
    # on INLA devel; classic mode produces clean fits without warnings.
    # Caller may override via `...` if a future INLA release fixes it.
    if (family == "T" && !("inla.mode" %in% names(extra))) {
        extra$inla.mode <- "classic"
    }
    # Skip empty control.* fields so INLA falls back to its own
    # defaults. Passing list() explicitly is NOT the same as omitting
    # the argument — INLA's check for "user-supplied prior" hinges on
    # the argument being absent. inla_defaults() relies on this.
    if (length(pri$control.fixed) > 0L) {
        base_args$control.fixed <- pri$control.fixed
    }
    if (length(pri$control.family) > 0L) {
        base_args$control.family <- pri$control.family
    }
    inla_args <- c(base_args, extra)
    do.call(inla, inla_args)
}

# --------------------------------------------------------------------
# inla_sd_summary — given an INLA precision marginal, return a list of
# (post_mean, post_sd, ci_lower, ci_upper) on the *SD* scale. Used for
# residual SD (gaussian, student) and all RE SDs.
#
# The "post_sd" output here is the posterior SD of σ itself (a
# scalar), computed as sqrt(E[σ²] - E[σ]²) on the transformed
# marginal. Not to be confused with "the SD" σ as a parameter.
# --------------------------------------------------------------------

inla_sd_summary <- function(prec_marg) {
    sd_marg <- inla.tmarginal(function(x) 1.0 / sqrt(x), prec_marg)
    qs <- inla.qmarginal(c(0.025, 0.975), sd_marg)
    mu  <- inla.emarginal(function(x) x,       sd_marg)
    mu2 <- inla.emarginal(function(x) x * x,   sd_marg)
    list(
        post_mean = mu,
        post_sd   = sqrt(max(mu2 - mu * mu, 0.0)),
        ci_lower  = qs[1],
        ci_upper  = qs[2]
    )
}

# --------------------------------------------------------------------
# inla_xform_summary — apply an arbitrary monotone transform fn(x) to
# an INLA marginal and summarise. Used for NB size→θ inversion
# (transform = 1/x) when INLA's hyperparameter is the inverse of what
# Phonometrica reports.
# --------------------------------------------------------------------

inla_xform_summary <- function(marg, fn) {
    xf_marg <- inla.tmarginal(fn, marg)
    qs <- inla.qmarginal(c(0.025, 0.975), xf_marg)
    mu  <- inla.emarginal(function(x) x,       xf_marg)
    mu2 <- inla.emarginal(function(x) x * x,   xf_marg)
    list(
        post_mean = mu,
        post_sd   = sqrt(max(mu2 - mu * mu, 0.0)),
        ci_lower  = qs[1],
        ci_upper  = qs[2]
    )
}

# --------------------------------------------------------------------
# inla_id_summary — pass-through summary of an INLA marginal (no
# transform). Used for hyperparameters whose INLA-reported value is
# already what Phonometrica reports (e.g. Student-t dof ν).
# --------------------------------------------------------------------

inla_id_summary <- function(marg) {
    qs <- inla.qmarginal(c(0.025, 0.975), marg)
    mu  <- inla.emarginal(function(x) x,       marg)
    mu2 <- inla.emarginal(function(x) x * x,   marg)
    list(
        post_mean = mu,
        post_sd   = sqrt(max(mu2 - mu * mu, 0.0)),
        ci_lower  = qs[1],
        ci_upper  = qs[2]
    )
}

# --------------------------------------------------------------------
# extract_fixef_inla — fixed-effect posterior summaries with pd.
# Names are emitted in INLA's native paste0(var, level) format, which
# matches R's convention used elsewhere in the suite.
# bayes_assert's r_to_phon_name handles the translation to Phon names.
#
# INLA names the raw intercept "(Intercept)" — strip the parens to
# match brms' "Intercept" convention so r_to_phon_name does the right
# thing (it special-cases bare "Intercept").
#
# pd is computed from each fixed-effect marginal directly:
#   pd_i = max( P(β_i ≤ 0), P(β_i > 0) ).
# --------------------------------------------------------------------

extract_fixef_inla <- function(fit) {
    sf <- fit$summary.fixed
    nm <- rownames(sf)
    nm[nm == "(Intercept)"] <- "Intercept"

    n <- length(nm)
    pd <- numeric(n)
    for (i in seq_len(n)) {
        p_neg <- inla.pmarginal(0, fit$marginals.fixed[[i]])
        pd[i] <- max(p_neg, 1 - p_neg)
    }

    list(
        names     = I(nm),
        post_mean = I(unname(sf[, "mean"])),
        post_sd   = I(unname(sf[, "sd"])),
        ci_lower  = I(unname(sf[, "0.025quant"])),
        ci_upper  = I(unname(sf[, "0.975quant"])),
        pd        = I(pd)
    )
}

# --------------------------------------------------------------------
# build_hyper_inla — explicit, per-model hyper-block builder.
#
# `specs` is a list of entries, each:
#   list(phon_name = "sd(residual)", inla_idx = 1, transform = "sd")
# where transform is one of:
#   "sd"  — sqrt(1/x);  for precision marginals
#   "inv" — 1/x;        for INLA's NB "size" → θ
#   "id"  — pass-through; for already-meaningful marginals (e.g. ν)
#
# This is called per-model from each gen_reference_*.R, since the
# mapping from INLA's anonymous hyperpar indices to Phon-format names
# is model-specific. The Phon harness matches hyper rows by name, not
# index, so the order in `specs` is free to follow whatever reads
# best in the generator.
# --------------------------------------------------------------------

build_hyper_inla <- function(fit, specs) {
    if (length(specs) == 0L) return(NULL)

    names_out <- character(0)
    pmean <- numeric(0); psd <- numeric(0)
    lo    <- numeric(0); hi    <- numeric(0)

    for (s in specs) {
        marg <- fit$marginals.hyperpar[[s$inla_idx]]
        summ <- switch(s$transform,
            "sd"  = inla_sd_summary(marg),
            "inv" = inla_xform_summary(marg, function(x) 1.0 / x),
            "id"  = inla_id_summary(marg),
            stop(sprintf("build_hyper_inla: unknown transform '%s'",
                         s$transform))
        )
        names_out <- c(names_out, s$phon_name)
        pmean <- c(pmean, summ$post_mean)
        psd   <- c(psd,   summ$post_sd)
        lo    <- c(lo,    summ$ci_lower)
        hi    <- c(hi,    summ$ci_upper)
    }

    list(
        names     = I(names_out),
        post_mean = I(pmean),
        post_sd   = I(psd),
        ci_lower  = I(lo),
        ci_upper  = I(hi)
    )
}

# --------------------------------------------------------------------
# extract_fit_inla — top-level fit summaries: nobs and WAIC.
#
# WAIC: INLA gives fit$waic$waic (total) and fit$waic$p.eff (effective
# parameters). Pointwise terms in fit$waic$local$waic allow computing
# an SE estimate via Vehtari et al. (2017):
#   SE(WAIC) = sqrt(n · var(local_waic_terms))
# Absent on older INLA versions, in which case se_waic is omitted and
# bayes_assert uses a flat absolute tolerance.
#
# LOO-IC is deliberately NOT computed. INLA's CPO/LPML is related but
# not PSIS-LOO. bayes_assert.check_fit skips loo_ic when it's absent.
# --------------------------------------------------------------------

extract_fit_inla <- function(fit, data, response_col) {
    n_obs <- sum(!is.na(data[[response_col]]))
    out <- list(nobs = n_obs)

    if (!is.null(fit$waic)) {
        out$waic   <- fit$waic$waic
        out$p_waic <- fit$waic$p.eff
        loc <- fit$waic$local$waic
        if (!is.null(loc) && length(loc) > 1L) {
            out$se_waic <- sqrt(length(loc) * var(loc))
        }
    }
    out
}

# --------------------------------------------------------------------
# check_convergence_inla — the practical check is that INLA's internal
# optimiser reached a mode and the marginals are non-degenerate.
# fit$mode$mode.status is the optimiser exit code (0 = converged).
# Falls back to "marginals.fixed is non-null" on older INLA versions.
# --------------------------------------------------------------------

check_convergence_inla <- function(fit) {
    if (is.null(fit$mode) || is.null(fit$mode$mode.status)) {
        return(!is.null(fit$marginals.fixed))
    }
    fit$mode$mode.status == 0
}

# --------------------------------------------------------------------
# build_model_entry_inla — single dispatch point for one model entry
# in the reference JSON. `response_col` is the response column name in
# `data` (used only for nobs). `hyper_specs` is passed straight to
# build_hyper_inla; pass list() for fixed-effects-only models.
# --------------------------------------------------------------------

build_model_entry_inla <- function(fit, formula_str, family,
                                   data, response_col, hyper_specs) {
    list(
        formula   = formula_str,
        engine    = "inla",
        family    = family,
        converged = check_convergence_inla(fit),
        fit       = extract_fit_inla(fit, data, response_col),
        fixef     = extract_fixef_inla(fit),
        hyper     = build_hyper_inla(fit, hyper_specs)
    )
}

# --------------------------------------------------------------------
# write_reference_inla — same JSON layout as the brms reference, with
# "engine": "inla" so future debuggers know which reference engine
# produced each entry. Self-describing header captures the R / INLA /
# brinla versions for re-run apples-to-apples comparison.
# --------------------------------------------------------------------

write_reference_inla <- function(chapter, models, dataset, path) {
    ref <- list(
        chapter        = chapter,
        dataset        = dataset,
        engine         = "inla",
        generated_at   = format(Sys.time(), "%Y-%m-%d %H:%M:%S %Z"),
        R_version      = R.version.string,
        INLA_version   = as.character(packageVersion("INLA")),
        brinla_version = as.character(packageVersion("brinla")),
        prior_spec = list(
            note = paste(
                "Matched to Phonometrica's scale_default_priors():",
                "intercept ~ N(mean(y_link), max(2.5, 2.5*sd(y_link)));",
                "slopes ~ N(0, max(2.5, 2.5*sd(y_link)));",
                "RE SD and gaussian/student residual SD ~ PC(scale, 0.05).",
                "Carve-out: INLA default priors on NB size, beta",
                "precision, and student dof. Tolerances on those",
                "specific posteriors are loosened — see test_*.phon."
            )
        ),
        models = models
    )
    json <- toJSON(ref, auto_unbox = TRUE, null = "null", na = "null",
                   pretty = TRUE, digits = 12)
    writeLines(json, path)
    cat(sprintf("Wrote %s (%d models)\n", path, length(models)))
}

# --------------------------------------------------------------------
# script_dir / resolve_data_path / resolve_output_path
# This file lives in bayesian/inla/, so test/data/ is THREE levels up.
# --------------------------------------------------------------------

script_dir <- function() {
    args <- commandArgs(trailingOnly = FALSE)
    file_arg <- grep("^--file=", args, value = TRUE)
    if (length(file_arg) > 0L) {
        return(normalizePath(dirname(sub("^--file=", "", file_arg[1]))))
    }
    for (i in seq_len(sys.nframe())) {
        ofile <- sys.frame(i)$ofile
        if (!is.null(ofile)) return(normalizePath(dirname(ofile)))
    }
    getwd()
}

resolve_data_path <- function(rel) {
    file.path(script_dir(), "..", "..", "..", "data", rel)
}

resolve_output_path <- function(rel) {
    file.path(script_dir(), rel)
}
