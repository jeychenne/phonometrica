# =====================================================================
# gen_reference_gaussian.R — Gaussian reference using lme4::InstEval.
#
# Why InstEval (and not synthetic data):
#   * Real, well-known dataset (Bates et al. 2015 use it as the
#     canonical crossed-RE example).
#   * Crossed grouping factors: students (s) × lecturers (d). 97% of
#     students see varying values of `service`, so the random slope
#     (1+service|s) has real (non-zero) variance — every model in
#     the suite converges cleanly, no boundary fits.
#   * Two categorical fixed effects with multiple levels for the M1
#     interaction.
#   * Continuous response (rating treated as Gaussian, as is standard).
#
# Subsample: 300 students (~7,000 obs) chosen with set.seed(42) for
# reproducibility. Largest fit (M5) takes ~5s on a modern machine.
# =====================================================================

source("_helpers.R")
suppressPackageStartupMessages({library(lme4); library(glmmTMB)})

# ── Load and subsample ─────────────────────────────────────────────
data(InstEval)
set.seed(42)
sids <- sample(unique(InstEval$s), 300)
d <- InstEval[InstEval$s %in% sids, ]

# Convert categoricals to clearly-textual labels so Phonometrica's
# is_numeric_column auto-detector treats them as factors. Using
# labels like "0"/"1" or "2"/"4"/"6"/"8" would parse as numbers and
# Phonometrica would model them as continuous slopes.
d$studage <- factor(c("2"="A2", "4"="A4", "6"="A6", "8"="A8")[as.character(d$studage)])
d$lectage <- factor(c("1"="L1", "2"="L2", "3"="L3", "4"="L4",
                      "5"="L5", "6"="L6")[as.character(d$lectage)])
d$service <- factor(c("0"="No", "1"="Yes")[as.character(d$service)])
d$y <- as.numeric(d$y)              # rating 1-5 -> Gaussian response

# Reference levels (alphabetical): studage=A2, lectage=L1, service=No.

d <- d[, c("s", "d", "studage", "lectage", "service", "y")]

# Prefix the grouping-factor IDs so they're unambiguously textual
# rather than number-strings that some downstream readers might
# auto-coerce. After this, s = "S2", "S16", ...; d = "D115", "D756", ...
d$s <- factor(paste0("S", as.character(d$s)))
d$d <- factor(paste0("D", as.character(d$d)))

# ── Write the data CSV (consumed by both R and the phon test) ─────
data_path <- file.path("..", "..", "data", "inst_eval.csv")
write.csv(d, data_path, row.names = FALSE)
cat("Wrote", nrow(d), "rows to", data_path, "\n")

# ── Fit and serialise ──────────────────────────────────────────────
m1 <- lm(y ~ studage * service, data = d)
m2 <- glmmTMB(y ~ studage + lectage + service + (1|s),
              data = d, REML = FALSE)
m3 <- glmmTMB(y ~ studage + lectage + service + (1+service|s),
              data = d, REML = FALSE)
m4 <- glmmTMB(y ~ studage + lectage + service + (1|s) + (1|d),
              data = d, REML = FALSE)
m5 <- glmmTMB(y ~ studage + lectage + service + (1+service|s) + (1|d),
              data = d, REML = FALSE)

models <- list(
    M1 = build_model_entry(m1, "y ~ studage * service",
                           "lm",      "gaussian"),
    M2 = build_model_entry(m2, "y ~ studage + lectage + service + (1|s)",
                           "glmmTMB", "gaussian"),
    M3 = build_model_entry(m3, "y ~ studage + lectage + service + (1+service|s)",
                           "glmmTMB", "gaussian"),
    M4 = build_model_entry(m4, "y ~ studage + lectage + service + (1|s) + (1|d)",
                           "glmmTMB", "gaussian"),
    M5 = build_model_entry(m5, "y ~ studage + lectage + service + (1+service|s) + (1|d)",
                           "glmmTMB", "gaussian")
)

write_reference("gaussian", models, "inst_eval.csv", "reference_gaussian.json")
cat("Done.\n")
