# =============================================================================
# REML reference values for Phonometrica's Gaussian LMM REML validation.
#
# Generates lme4::lmer(..., REML = TRUE) fits for two test cases, plus matching
# REML=FALSE (ML) fits for cross-checking the engine's ML path is unchanged:
#
#   1. Simple random intercept: f1 ~ gender + (1 | speaker)
#      Uses test/data/gaussian_f1.csv (already in the repo).
#
#   2. Random intercept + slope:  Reaction ~ Days + (1 + Days | Subject)
#      Uses lme4::sleepstudy (built-in to lme4).
#
# Outputs a tab-separated reference file: test/data/reml_reference.tsv
#
# Acceptance criterion for Phonometrica: every β, σ, σ_u, logLik, AIC, BIC
# in column "lme4_reml" must match Phonometrica's REML fit to within 1e-4
# (relative or absolute, whichever is larger). The "lme4_ml" column is a
# sanity check that the ML path is unchanged by the REML work.
# =============================================================================

suppressPackageStartupMessages({
  library(lme4)
})

# Reproducibility (lme4 is deterministic, but cite the version for clarity).
cat(sprintf("lme4 version: %s\n", packageVersion("lme4")))
cat(sprintf("R   version: %s\n\n", R.version.string))

# ── Helper: extract scalars from a lmer fit into a flat named vector. ──
extract_fit <- function(m) {
  fe   <- fixef(m)
  vc   <- as.data.frame(VarCorr(m))
  sig  <- sigma(m)
  ll   <- as.numeric(logLik(m))
  aic  <- AIC(m)
  bic  <- BIC(m)
  out  <- c(fe,
            sigma = sig,
            logLik = ll,
            AIC = aic,
            BIC = bic)

  # Variance components: name as "var_<grp>_<term1>[:<term2>]" or "sd_<grp>_<term>".
  for (i in seq_len(nrow(vc))) {
    g  <- vc$grp[i]
    v1 <- vc$var1[i]
    v2 <- vc$var2[i]
    sd <- vc$sdcor[i]
    var <- vc$vcov[i]
    if (is.na(v2)) {
      out[paste0("sd_",  g, "_", v1)] <- sd
      out[paste0("var_", g, "_", v1)] <- var
    } else {
      out[paste0("cor_", g, "_", v1, "_", v2)] <- sd  # vc$sdcor is the correlation here
    }
  }
  out
}

# ── Case 1: simple random intercept on gaussian_f1.csv ───────────────────────
cat("=== Case 1: f1 ~ gender + (1 | speaker) ===\n")
data_path <- "test/data/gaussian_f1.csv"
if (!file.exists(data_path)) {
  stop(sprintf("Missing %s. Run from the repository root.", data_path))
}
d1 <- read.csv(data_path, sep="\t")
m1_reml <- lmer(f1 ~ gender + (1 | speaker), data = d1, REML = TRUE)
m1_ml   <- lmer(f1 ~ gender + (1 | speaker), data = d1, REML = FALSE)
cat("\n--- REML ---\n")
print(summary(m1_reml), correlation = FALSE)
cat("\n--- ML ---\n")
print(summary(m1_ml), correlation = FALSE)

# ── Case 2: random intercept + slope on sleepstudy ───────────────────────────
cat("\n\n=== Case 2: Reaction ~ Days + (1 + Days | Subject) on sleepstudy ===\n")
d2 <- sleepstudy
m2_reml <- lmer(Reaction ~ Days + (1 + Days | Subject), data = d2, REML = TRUE)
m2_ml   <- lmer(Reaction ~ Days + (1 + Days | Subject), data = d2, REML = FALSE)
cat("\n--- REML ---\n")
print(summary(m2_reml), correlation = FALSE)
cat("\n--- ML ---\n")
print(summary(m2_ml), correlation = FALSE)

# Also write sleepstudy out so Phonometrica can load it.
write.table(d2, file = "test/data/sleepstudy.tsv", sep = "\t",
            row.names = FALSE, quote = FALSE)
cat(sprintf("\nWrote test/data/sleepstudy.tsv (%d rows)\n", nrow(d2)))

# ── Flat reference file ──────────────────────────────────────────────────────
to_tsv <- function(case, method, vec) {
  data.frame(case = case, method = method,
             quantity = names(vec), value = as.numeric(vec))
}
ref <- rbind(
  to_tsv("f1_intercept_only", "lme4_reml", extract_fit(m1_reml)),
  to_tsv("f1_intercept_only", "lme4_ml",   extract_fit(m1_ml)),
  to_tsv("sleepstudy_random_slope", "lme4_reml", extract_fit(m2_reml)),
  to_tsv("sleepstudy_random_slope", "lme4_ml",   extract_fit(m2_ml))
)
write.table(ref, file = "test/data/reml_reference.tsv", sep = "\t",
            row.names = FALSE, quote = FALSE)
cat(sprintf("Wrote test/data/reml_reference.tsv (%d rows)\n", nrow(ref)))
