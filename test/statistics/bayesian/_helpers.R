# =====================================================================
# statistics/bayesian/_helpers.R
# =====================================================================
# Shared helpers for the Bayesian validation suite. Mirrors the role
# that frequentist/_helpers.R plays for the frequentist suite.
#
# Reference engine:
#   brms 2.20+ with Stan via rstan/cmdstanr. Each per-family
#   gen_reference_<family>.R script fits 5 models with explicit priors
#   that match Phonometrica's data-dependent auto-scaled defaults
#   (see scale_default_priors in phon/analysis/fitting.cpp), then
#   serialises posterior summaries to a JSON reference.
#
# Why explicit-prior matching:
#   brms' built-in defaults are flat on slopes and student_t(3, m, s)
#   on the intercept — they don't match Phonometrica's
#   N(mean(y_link), 2.5·sd(y_link)) intercept and N(0, 2.5·sd(y_link))
#   slope priors. To make the comparison meaningful we replicate
#   Phonometrica's priors on the brms side.
#
# Intercept handling:
#   Phonometrica places its intercept prior on the *raw* intercept.
#   brms' default centres predictors and puts the prior on the
#   centred intercept, which is a different parameter. We use the
#   `0 + Intercept + ...` formula trick to expose the raw intercept
#   as a regular `b` coefficient that `prior(class = b, coef = "Intercept")`
#   can target directly.
# =====================================================================

suppressPackageStartupMessages({
    library(jsonlite)
    library(brms)
    library(posterior)
    library(loo)
})

# Sampling defaults. Override by setting these env vars before
# running gen_reference_*.R, e.g.:
#   PHON_BAYES_ITER=8000 Rscript gen_reference_gaussian.R
.PHON_BAYES_ITER     <- as.integer(Sys.getenv("PHON_BAYES_ITER",     "4000"))
.PHON_BAYES_WARMUP   <- as.integer(Sys.getenv("PHON_BAYES_WARMUP",   "1000"))
.PHON_BAYES_CHAINS   <- as.integer(Sys.getenv("PHON_BAYES_CHAINS",   "4"))
.PHON_BAYES_CORES    <- as.integer(Sys.getenv("PHON_BAYES_CORES",    "4"))
.PHON_BAYES_SEED     <- as.integer(Sys.getenv("PHON_BAYES_SEED",     "42"))
.PHON_BAYES_ADAPT_DELTA <- as.numeric(Sys.getenv("PHON_BAYES_ADAPT_DELTA", "0.95"))

# --------------------------------------------------------------------
# compute_link_stats — replicate the link-scale statistics computed
# by scale_default_priors() in fitting.cpp. The mean and SD on the
# link scale are what determine the data-dependent prior scale.
# --------------------------------------------------------------------

compute_link_stats <- function(y, family) {
    # Coerce; drop NAs and non-finite values to match the C++ pass.
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
        # gaussian, student_t — identity link
        ly <- y
    }
    list(mean = mean(ly), sd = sd(ly))
}

# --------------------------------------------------------------------
# phon_default_priors — produce a brms prior block that matches
# Phonometrica's auto-scaled defaults bit-for-bit:
#
#   Intercept (raw):  N(mean(y_link), max(2.5, 2.5·sd(y_link)))
#   Slopes:           N(0,            max(2.5, 2.5·sd(y_link)))
#   RE SDs:           PC(scale, 0.05) ≡ exponential(-log(0.05) / scale)
#   Residual SD:      same exponential, class = sigma  (gaussian only)
#   RE correlation:   LKJ(1) — uniform; brms default but set explicitly
#
# Returns a `brmsprior` object.
# --------------------------------------------------------------------

phon_default_priors <- function(y, family, has_random_effects = TRUE,
                                has_correlated_random = FALSE) {
    s <- compute_link_stats(y, family)
    scale <- max(2.5, 2.5 * s$sd)
    # PC(scale, alpha) on σ has density λ exp(-λσ) with λ = -log(alpha)/scale.
    lambda <- -log(0.05) / scale

    pri <- c(
        # Raw intercept (use `0 + Intercept` formula syntax in the model)
        prior_string(
            sprintf("normal(%.10g, %.10g)", s$mean, scale),
            class = "b", coef = "Intercept"
        ),
        # All other slopes
        prior_string(
            sprintf("normal(0, %.10g)", scale),
            class = "b"
        )
    )

    if (has_random_effects) {
        pri <- c(pri, prior_string(sprintf("exponential(%.10g)", lambda),
                                    class = "sd"))
    }
    if (family == "gaussian") {
        pri <- c(pri, prior_string(sprintf("exponential(%.10g)", lambda),
                                    class = "sigma"))
    }
    if (has_correlated_random) {
        # LKJ(1): uniform over correlation matrices. Equivalent to
        # Phonometrica's eta=1.0 default. Set explicitly so the
        # reference is reproducible across brms versions whose
        # default may differ.
        pri <- c(pri, prior_string("lkj(1)", class = "cor"))
    }
    pri
}

# --------------------------------------------------------------------
# add_explicit_intercept — rewrite "y ~ x + (1|g)" as
# "y ~ 0 + Intercept + x + (1|g)" so the raw intercept is exposed as
# a `b` coefficient that prior(coef = "Intercept") can target.
#
# Random-effect terms must keep their `1` (intercept-in-RE) — only
# the population-level intercept moves.
# --------------------------------------------------------------------

add_explicit_intercept <- function(formula_str) {
    parts <- strsplit(formula_str, "~", fixed = TRUE)[[1]]
    stopifnot(length(parts) == 2L)
    lhs <- trimws(parts[1])
    rhs <- trimws(parts[2])
    paste0(lhs, " ~ 0 + Intercept + ", rhs)
}

# --------------------------------------------------------------------
# detect_correlated_random — true if the RHS has a `(1 + something | g)`
# term (correlated random intercept + slope). When true, a `cor`
# prior is applicable.
# --------------------------------------------------------------------

detect_correlated_random <- function(formula_str) {
    grepl("\\(\\s*1\\s*\\+", formula_str)
}

# --------------------------------------------------------------------
# fit_brms — wrap brm() with sampling defaults, prior matching, and
# a Stan-model file cache. The cache lives under stan_cache/ next to
# the R script; safe to delete to force a recompile.
# --------------------------------------------------------------------

fit_brms <- function(formula_str, data, family, file_id) {
    correlated <- detect_correlated_random(formula_str)
    has_re <- grepl("\\(.*\\|.*\\)", formula_str)

    pri <- phon_default_priors(
        data[[trimws(strsplit(formula_str, "~", fixed = TRUE)[[1]][1])]],
        family,
        has_random_effects   = has_re,
        has_correlated_random = correlated
    )

    f_str <- add_explicit_intercept(formula_str)

    fam_obj <- switch(family,
        "gaussian" = gaussian(),
        "binomial" = bernoulli(),       # 0/1 response → bernoulli
        stop(sprintf("fit_brms: unsupported family '%s'", family))
    )

    cache_dir <- file.path(script_dir(), "stan_cache")
    dir.create(cache_dir, showWarnings = FALSE, recursive = TRUE)
    cache_file <- file.path(cache_dir, paste0(file_id, ".rds"))

    fit <- brm(
        formula  = as.formula(f_str),
        data     = data,
        family   = fam_obj,
        prior    = pri,
        iter     = .PHON_BAYES_ITER,
        warmup   = .PHON_BAYES_WARMUP,
        chains   = .PHON_BAYES_CHAINS,
        cores    = .PHON_BAYES_CORES,
        seed     = .PHON_BAYES_SEED,
        control  = list(adapt_delta = .PHON_BAYES_ADAPT_DELTA),
        file     = cache_file,
        file_refit = "on_change",
        refresh  = 0
    )
    fit
}

# --------------------------------------------------------------------
# check_convergence_brms — Rhat-based convergence flag. Threshold of
# 1.05 is Vehtari et al. 2021's recommended value; we don't gate on
# ESS because some hyperparameters with thick posterior tails have
# legitimately low ESS that doesn't indicate non-convergence.
# --------------------------------------------------------------------

check_convergence_brms <- function(fit, rhat_threshold = 1.05) {
    s <- posterior::summarise_draws(as_draws(fit), default_convergence_measures())
    rhats <- s$rhat[is.finite(s$rhat)]
    if (length(rhats) == 0L) return(FALSE)
    all(rhats < rhat_threshold)
}

# --------------------------------------------------------------------
# extract_fixef_brms — fixed-effect posterior summaries.
# Returns names in R's paste0(var, level) format; the phon-side
# harness translates via lib/assert.phon's r_to_phon_name. The
# special "Intercept" name (no parens, from `0 + Intercept` syntax)
# round-trips through the translator unchanged.
# --------------------------------------------------------------------

extract_fixef_brms <- function(fit) {
    draws_df <- as_draws_df(fit)
    # brms exposes population-level coefficients as "b_<name>".
    b_cols <- grep("^b_", colnames(draws_df), value = TRUE)
    if (length(b_cols) == 0L) {
        return(list(names = character(0)))
    }

    raw_names <- sub("^b_", "", b_cols)

    nm <- length(raw_names)
    post_mean <- numeric(nm)
    post_sd   <- numeric(nm)
    ci_lo     <- numeric(nm)
    ci_hi     <- numeric(nm)
    pd        <- numeric(nm)

    for (i in seq_along(b_cols)) {
        x <- draws_df[[b_cols[i]]]
        post_mean[i] <- mean(x)
        post_sd[i]   <- sd(x)
        qs <- quantile(x, c(0.025, 0.975), names = FALSE)
        ci_lo[i] <- qs[1]
        ci_hi[i] <- qs[2]
        # Probability of direction: max(P(x > 0), P(x < 0)).
        pd[i] <- max(mean(x > 0), mean(x < 0))
    }

    list(
        names     = I(raw_names),
        post_mean = I(post_mean),
        post_sd   = I(post_sd),
        ci_lower  = I(ci_lo),
        ci_upper  = I(ci_hi),
        pd        = I(pd)
    )
}

# --------------------------------------------------------------------
# extract_hyper_brms — hyperparameter posteriors. Names are formatted
# to match Phonometrica's hyper_names format:
#
#   sd(<term>|<group>)             (one per RE term, e.g. sd(Intercept|s))
#   sd(residual)                    (Gaussian residual SD; appended last)
#
# brms exposes RE SDs as "sd_<group>__<term>" and the residual SD as
# "sigma". The translation is purely syntactic.
#
# Returns NULL when there are no random effects AND no Gaussian
# residual SD (i.e. binomial/poisson fixed-effects-only models).
# --------------------------------------------------------------------

extract_hyper_brms <- function(fit, family) {
    draws_df <- as_draws_df(fit)

    sd_cols <- grep("^sd_", colnames(draws_df), value = TRUE)
    has_sigma <- (family == "gaussian") && ("sigma" %in% colnames(draws_df))

    if (length(sd_cols) == 0L && !has_sigma) {
        return(NULL)
    }

    names_out <- character(0)
    cols_out  <- character(0)

    for (col in sd_cols) {
        # Format: "sd_<group>__<term>", e.g. "sd_s__Intercept" or
        # "sd_speaker__styleformal".
        rest <- sub("^sd_", "", col)
        spl <- strsplit(rest, "__", fixed = TRUE)[[1]]
        if (length(spl) != 2L) next
        gname <- spl[1]
        tname <- spl[2]
        # brms uses "Intercept" (no parens) for RE intercepts. Both
        # formats round-trip through r_to_phon_ranef_name unchanged
        # since "Intercept" is already in Phonometrica's format.
        names_out <- c(names_out, sprintf("sd(%s|%s)", tname, gname))
        cols_out  <- c(cols_out,  col)
    }

    if (has_sigma) {
        names_out <- c(names_out, "sd(residual)")
        cols_out  <- c(cols_out,  "sigma")
    }

    n <- length(cols_out)
    post_mean <- numeric(n)
    post_sd   <- numeric(n)
    ci_lo     <- numeric(n)
    ci_hi     <- numeric(n)
    for (i in seq_len(n)) {
        x <- draws_df[[cols_out[i]]]
        post_mean[i] <- mean(x)
        post_sd[i]   <- sd(x)
        qs <- quantile(x, c(0.025, 0.975), names = FALSE)
        ci_lo[i] <- qs[1]
        ci_hi[i] <- qs[2]
    }

    list(
        names     = I(names_out),
        post_mean = I(post_mean),
        post_sd   = I(post_sd),
        ci_lower  = I(ci_lo),
        ci_upper  = I(ci_hi)
    )
}

# --------------------------------------------------------------------
# extract_fit_brms — top-level fit summaries: nobs, WAIC, LOO, and
# diagnostic metadata (max Pareto k).
#
# WAIC and LOO are computed via the loo package, which brms re-exports.
# They're slow on large models — runs ~5-30s on M5-class fits.
# --------------------------------------------------------------------

extract_fit_brms <- function(fit) {
    # WAIC: log-likelihood-based; relatively cheap.
    w <- tryCatch(waic(fit), error = function(e) NULL)
    # LOO: PSIS-LOO; slower but more reliable.
    l <- tryCatch(loo(fit, moment_match = FALSE), error = function(e) NULL)

    out <- list(
        nobs = nobs(fit)
    )
    if (!is.null(w) && !is.null(w$estimates)) {
        out$waic    <- unname(w$estimates["waic",    "Estimate"])
        out$p_waic  <- unname(w$estimates["p_waic",  "Estimate"])
        out$se_waic <- unname(w$estimates["waic",    "SE"])
    }
    if (!is.null(l) && !is.null(l$estimates)) {
        out$loo_ic <- unname(l$estimates["looic", "Estimate"])
        out$p_loo  <- unname(l$estimates["p_loo", "Estimate"])
        out$se_loo <- unname(l$estimates["looic", "SE"])
        # Max Pareto k for diagnostic — informational, not asserted.
        if (!is.null(l$diagnostics$pareto_k)) {
            out$max_pareto_k <- max(l$diagnostics$pareto_k, na.rm = TRUE)
        }
    }
    out
}

# --------------------------------------------------------------------
# build_model_entry_brms — single dispatch point for one model.
# --------------------------------------------------------------------

build_model_entry_brms <- function(fit, formula_str, family) {
    list(
        formula   = formula_str,
        engine    = "brms",
        family    = family,
        converged = check_convergence_brms(fit),
        fit       = extract_fit_brms(fit),
        fixef     = extract_fixef_brms(fit),
        hyper     = extract_hyper_brms(fit, family)
    )
}

# --------------------------------------------------------------------
# write_reference_brms — serialise to JSON. Self-describing header
# captures the brms / rstan / R versions and the sampling settings,
# so a re-run later can be compared apples-to-apples.
# --------------------------------------------------------------------

write_reference_brms <- function(family, models, dataset, path) {
    ref <- list(
        family       = family,
        dataset      = dataset,
        engine       = "brms",
        generated_at = format(Sys.time(), "%Y-%m-%d %H:%M:%S %Z"),
        R_version    = R.version.string,
        brms_version = as.character(packageVersion("brms")),
        rstan_version = if (requireNamespace("rstan", quietly = TRUE))
                            as.character(packageVersion("rstan")) else NA,
        sampling = list(
            iter        = .PHON_BAYES_ITER,
            warmup      = .PHON_BAYES_WARMUP,
            chains      = .PHON_BAYES_CHAINS,
            seed        = .PHON_BAYES_SEED,
            adapt_delta = .PHON_BAYES_ADAPT_DELTA
        ),
        prior_spec = list(
            note = paste(
                "Auto-scaled defaults matching Phonometrica's",
                "scale_default_priors():",
                "intercept ~ N(mean(y_link), 2.5·sd(y_link));",
                "slopes ~ N(0, 2.5·sd(y_link));",
                "sd ~ PC(2.5·sd(y_link), 0.05) ≡ exponential(-log(0.05)/scale);",
                "sigma ~ same exponential (Gaussian only);",
                "cor ~ LKJ(1)."
            )
        ),
        models = models
    )
    json <- toJSON(ref, auto_unbox = TRUE, null = "null", na = "null",
                   pretty = TRUE, digits = 12)
    writeLines(json, path)
    cat(sprintf("Wrote %s (%d model(s))\n", path, length(models)))
}

# --------------------------------------------------------------------
# script_dir / resolve_data_path — same idiom as frequentist/_helpers.R
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
    file.path(script_dir(), "..", "..", "data", rel)
}

resolve_output_path <- function(rel) {
    file.path(script_dir(), rel)
}
