# =====================================================================
# statistics/frequentist/_helpers.R
# =====================================================================
# Shared helpers used by every gen_reference_<family>.R script.
#
# Goals:
#   * Match Phonometrica's `m.ranef_names` format exactly:
#       sd(<term>|<group>)                — per random-effect term
#       sd(residual)                       — appended for Gaussian mixed
#     Phonometrica builds these names in phon/application/data_table.cpp
#     under model_get_field("ranef_names"). The R-side names must match
#     verbatim for the lookup-by-name in lib/assert.phon to succeed.
#
#   * Produce JSON that round-trips through Phonometrica's load_json,
#     which is implemented as do_string on the JSON text. Phonometrica's
#     scripting engine is a JSON superset, so jsonlite output is parsed
#     directly. Two settings are non-negotiable:
#       auto_unbox = TRUE   — scalars stay scalar (nobs is 1800 not [1800])
#       null       = "null" — NULL ranef appears as JSON null, not omitted
#                             (assert.phon's check_fit branches on this)
#
#   * One canonical model entry for every engine. Callers don't worry
#     about dispatch: build_model_entry handles lm vs glmmTMB by argument.
# =====================================================================

suppressPackageStartupMessages({
    library(jsonlite)
    library(glmmTMB)
})

# --------------------------------------------------------------------
# Fit-summary extractors. One per (engine, family) cell as needed.
# --------------------------------------------------------------------

extract_fit_lm <- function(m) {
    s <- summary(m)
    list(
        nobs   = unname(nobs(m)),
        loglik = as.numeric(logLik(m)),
        aic    = AIC(m),
        bic    = BIC(m),
        rse    = unname(s$sigma),
        r2     = unname(s$r.squared),
        adj_r2 = unname(s$adj.r.squared)
    )
}

# Family-aware glmmTMB extractor. The dispersion parameter and its
# accessor differ across families:
#   gaussian   : sigma(m) is the residual SD
#   negbin     : sigma(m) is theta (nbinom2 size; see ?nbinom2)
#   beta       : sigma(m) is phi (precision)
#   student    : sigma(m) is the scale, nu lives in the dispersion model
#   binomial   : no dispersion to report
#   poisson    : no dispersion to report
extract_fit_glmmtmb <- function(m, family) {
    out <- list(
        nobs   = unname(nobs(m)),
        loglik = as.numeric(logLik(m)),
        aic    = AIC(m),
        bic    = BIC(m)
    )
    if (family == "gaussian") {
        out$rse <- unname(sigma(m))
    } else if (family == "negbin") {
        out$theta <- unname(sigma(m))
    } else if (family == "beta") {
        out$phi <- unname(sigma(m))
    } else if (family == "student") {
        out$sigma <- unname(sigma(m))
        s <- summary(m)
        # t_family puts log(nu) as the (Intercept) of the dispersion
        # submodel. dispformula = ~1 ⇒ a single-row table.
        disp <- s$coefficients$disp
        if (!is.null(disp) && nrow(disp) >= 1L) {
            out$nu <- exp(unname(disp[1, "Estimate"]))
        }
    }
    out
}

# --------------------------------------------------------------------
# Phonometrica name translation. R's model.matrix names categorical
# coefficients as paste0(var, level), e.g. "voweli", and the intercept
# as "(Intercept)". Phonometrica's coef_names use:
#
#   "Intercept"          (no parentheses)
#   "vowel[i]"           (level in square brackets)
#   "vowel[i]:gender[M]" (interaction, brackets preserved)
#   "sd(Intercept|speaker)"
#   "sd(vowel[i]|speaker)"
#   "sd(residual)"       (already matches; left alone)
#
# We do the translation here, on the R side, so the JSON reference
# contains names in Phonometrica's format. The phon-side harness then
# just looks up by name without knowing R's conventions. This also
# matches what `summarize(model)` prints, which is what the user sees.
#
# Translation is a per-name string transform that needs to know the
# variable names and their non-reference levels. We extract those
# from the model frame.
# --------------------------------------------------------------------

build_cat_levels <- function(m) {
    # Returns a named list: var -> non-reference levels (character).
    # For glmmTMB, model.frame() includes random-effect grouping
    # variables too, but those don't appear in fixed-effect names so
    # the extra entries are harmless.
    mf <- tryCatch(model.frame(m), error = function(e) NULL)
    if (is.null(mf)) return(list())
    out <- list()
    for (nm in names(mf)) {
        v <- mf[[nm]]
        if (is.factor(v)) {
            lvls <- levels(v)
            if (length(lvls) >= 2L) {
                out[[nm]] <- lvls[-1]   # drop reference level
            }
        }
    }
    out
}

translate_term <- function(term, cat_levels) {
    # Translate a single coefficient name fragment (no colons).
    # "(Intercept)" -> "Intercept"
    # "voweli"      -> "vowel[i]"     (when "vowel" is a factor with level "i")
    # "x"           -> "x"            (continuous variable, unchanged)
    if (term == "(Intercept)") {
        return("Intercept")
    }
    # Try every (variable, level) pair. The longest-matching variable
    # wins to handle the rare case of nested-prefix names.
    best <- NULL
    best_var_len <- -1L
    for (var in names(cat_levels)) {
        for (lev in cat_levels[[var]]) {
            cat_name <- paste0(var, lev)
            if (term == cat_name && nchar(var) > best_var_len) {
                best <- paste0(var, "[", lev, "]")
                best_var_len <- nchar(var)
            }
        }
    }
    if (!is.null(best)) return(best)
    term  # not a categorical-level name; pass through
}

translate_fixef_name <- function(name, cat_levels) {
    # Split on ":" for interactions, translate each component, rejoin.
    parts <- strsplit(name, ":", fixed = TRUE)[[1]]
    paste(vapply(parts, translate_term, character(1), cat_levels = cat_levels),
          collapse = ":")
}

translate_ranef_name <- function(group_name, term_name, cat_levels) {
    # term_name is the inner name (e.g. "(Intercept)" or "voweli").
    # Translate it and wrap as Phonometrica formats it.
    translated <- translate_fixef_name(term_name, cat_levels)
    sprintf("sd(%s|%s)", translated, group_name)
}

extract_fixef_lm <- function(m) {
    ct <- summary(m)$coefficients
    cat_levels <- build_cat_levels(m)
    raw_names <- unname(rownames(ct))
    list(
        names    = vapply(raw_names, translate_fixef_name,
                          character(1), cat_levels = cat_levels,
                          USE.NAMES = FALSE),
        estimate = unname(ct[, "Estimate"]),
        se       = unname(ct[, "Std. Error"])
    )
}

extract_fixef_glmmtmb <- function(m) {
    ct <- summary(m)$coefficients$cond
    cat_levels <- build_cat_levels(m)
    raw_names <- unname(rownames(ct))
    list(
        names    = vapply(raw_names, translate_fixef_name,
                          character(1), cat_levels = cat_levels,
                          USE.NAMES = FALSE),
        estimate = unname(ct[, "Estimate"]),
        se       = unname(ct[, "Std. Error"])
    )
}

# --------------------------------------------------------------------
# Random-effect SDs. Format names to match Phonometrica's
# sd(<term>|<group>) convention. For Gaussian mixed models append a
# final sd(residual) entry — this mirrors what
# model_get_field("ranef_names") does on the C++ side.
#
# Returns NULL when the model has no random effects — the importing
# test branches on this.
# --------------------------------------------------------------------

extract_ranef_glmmtmb <- function(m, family) {
    vc <- VarCorr(m)$cond
    if (is.null(vc) || length(vc) == 0L) {
        return(NULL)
    }
    cat_levels <- build_cat_levels(m)
    names_out <- character(0)
    sd_out    <- numeric(0)
    for (gname in names(vc)) {
        blk <- vc[[gname]]
        sds <- attr(blk, "stddev")
        for (tnm in names(sds)) {
            # tnm is the R-side term name: "(Intercept)" for a random
            # intercept, "voweli" for a level-coded random slope. We
            # translate to Phonometrica's convention before formatting:
            #   "(Intercept)" -> "Intercept"  -> sd(Intercept|speaker)
            #   "voweli"      -> "vowel[i]"   -> sd(vowel[i]|speaker)
            phon_term <- translate_fixef_name(tnm, cat_levels)
            names_out <- c(names_out, sprintf("sd(%s|%s)", phon_term, gname))
            sd_out    <- c(sd_out,    unname(sds[[tnm]]))
        }
    }
    if (family == "gaussian") {
        # sd(residual) format is identical between R and Phonometrica.
        names_out <- c(names_out, "sd(residual)")
        sd_out    <- c(sd_out,    unname(sigma(m)))
    }
    # I() (AsIs) prevents jsonlite::toJSON(auto_unbox = TRUE) from
    # collapsing length-1 vectors to scalars.
    list(names = I(names_out), sd = I(sd_out))
}

# --------------------------------------------------------------------
# build_model_entry — single dispatch point.
# `engine` is "lm" or "glmmTMB"; family is one of
#   gaussian, binomial, poisson, negbin, beta, student.
# --------------------------------------------------------------------

build_model_entry <- function(m, formula_str, engine, family) {
    if (engine == "lm") {
        return(list(
            formula = formula_str,
            engine  = "lm",
            family  = "gaussian",
            converged = TRUE,
            fit     = extract_fit_lm(m),
            fixef   = extract_fixef_lm(m),
            ranef   = NULL
        ))
    }
    if (engine != "glmmTMB") {
        stop(sprintf("Unknown engine: %s", engine))
    }
    fit_obj <- m$fit
    converged <- !is.null(fit_obj) && isTRUE(fit_obj$convergence == 0L)
    list(
        formula   = formula_str,
        engine    = "glmmTMB",
        family    = family,
        converged = converged,
        fit       = extract_fit_glmmtmb(m, family),
        fixef     = extract_fixef_glmmtmb(m),
        ranef     = extract_ranef_glmmtmb(m, family)
    )
}

# --------------------------------------------------------------------
# write_reference — serialize and persist. The header fields make a
# generated reference self-describing: re-running the suite later, you
# can tell at a glance which R + glmmTMB versions produced the file.
# --------------------------------------------------------------------

write_reference <- function(family, models, dataset, path) {
    ref <- list(
        family          = family,
        dataset         = dataset,
        generated_at    = format(Sys.time(), "%Y-%m-%d %H:%M:%S %Z"),
        R_version       = R.version.string,
        glmmTMB_version = as.character(packageVersion("glmmTMB")),
        models          = models
    )
    json <- toJSON(
        ref,
        auto_unbox = TRUE,
        null       = "null",
        na         = "null",
        pretty     = TRUE,
        digits     = 12
    )
    writeLines(json, path)
    cat(sprintf("Wrote %s (%d model(s))\n", path, length(models)))
}

# --------------------------------------------------------------------
# fit_or_skip — guarded glmmTMB call. Reports failure to stderr and
# returns NULL so the caller can record the model as nonconverged
# rather than aborting the whole reference generation.
# --------------------------------------------------------------------

fit_or_skip <- function(label, expr) {
    tryCatch(
        eval(substitute(expr), envir = parent.frame()),
        error = function(e) {
            message(sprintf("[%s] glmmTMB failed: %s", label, conditionMessage(e)))
            NULL
        }
    )
}

# --------------------------------------------------------------------
# resolve_data_path — helper so the per-family R scripts can be run
# from any working directory. Resolves relative to the directory the
# script itself lives in.
# --------------------------------------------------------------------

script_dir <- function() {
    # When sourced via Rscript, commandArgs(trailingOnly=FALSE) carries
    # --file=path; when source()d, sys.frames helps. We try both.
    args <- commandArgs(trailingOnly = FALSE)
    file_arg <- grep("^--file=", args, value = TRUE)
    if (length(file_arg) > 0L) {
        return(normalizePath(dirname(sub("^--file=", "", file_arg[1]))))
    }
    # sourced interactively: use the calling frame's filename
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
