# =====================================================================
# gen_reference_binomial.R — Binomial Bayesian reference (brms HMC).
#
# Mirrors frequentist/gen_reference_binomial.R: same dataset
# (test/data/binomial_schwa.csv, 5400 obs synthetic schwa-realisation
# data), same five model formulas. Bernoulli (0/1) likelihood;
# brms uses bernoulli() rather than binomial() for 0/1 outcomes.
#
# Five models:
#   M1 — realized ~ position * style                    (interaction, no RE)
#   M2 — realized ~ position + style + (1|speaker)
#   M3 — realized ~ position + style + (1+style|speaker)
#   M4 — realized ~ position + style + (1|speaker) + (1|word)
#   M5 — realized ~ position + style + (1+style|speaker) + (1|word)
#
# Runtime caveat:
#   M3 / M5 (random slope on speaker, 20 levels × 2 in the slope)
#   are the slow ones — typically ~10 min each at default sampler
#   settings, longer if adapt_delta needs raising. Set
#   PHON_BAYES_ADAPT_DELTA=0.99 if divergent transitions appear.
# =====================================================================

source("_helpers.R")

data_path <- resolve_data_path("binomial_schwa.csv")
if (!file.exists(data_path)) {
    stop(sprintf("%s not found.", data_path))
}

d <- read.delim(data_path)
# Match the factor levels used by the frequentist generator so the
# coefficient names line up across suites.
d$position <- factor(d$position, levels = c("final", "initial", "medial"))
d$style    <- factor(d$style,    levels = c("casual", "formal"))
d$speaker  <- factor(d$speaker)
d$word     <- factor(d$word)

# ── Fit and serialise ─────────────────────────────────────────────────
cat("Fitting M1 (interaction, no RE)...\n")
m1 <- fit_brms("realized ~ position * style", d, "binomial", "binom_M1")

cat("Fitting M2 (1|speaker)...\n")
m2 <- fit_brms("realized ~ position + style + (1|speaker)",
               d, "binomial", "binom_M2")

cat("Fitting M3 (1+style|speaker)...\n")
m3 <- fit_brms("realized ~ position + style + (1+style|speaker)",
               d, "binomial", "binom_M3")

cat("Fitting M4 (1|speaker) + (1|word)...\n")
m4 <- fit_brms("realized ~ position + style + (1|speaker) + (1|word)",
               d, "binomial", "binom_M4")

cat("Fitting M5 (1+style|speaker) + (1|word)...\n")
m5 <- fit_brms("realized ~ position + style + (1+style|speaker) + (1|word)",
               d, "binomial", "binom_M5")

models <- list(
    M1 = build_model_entry_brms(m1, "realized ~ position * style",                                   "binomial"),
    M2 = build_model_entry_brms(m2, "realized ~ position + style + (1|speaker)",                     "binomial"),
    M3 = build_model_entry_brms(m3, "realized ~ position + style + (1+style|speaker)",               "binomial"),
    M4 = build_model_entry_brms(m4, "realized ~ position + style + (1|speaker) + (1|word)",          "binomial"),
    M5 = build_model_entry_brms(m5, "realized ~ position + style + (1+style|speaker) + (1|word)",    "binomial")
)

write_reference_brms(
    "binomial", models, "binomial_schwa.csv",
    resolve_output_path("reference_binomial.json")
)
cat("Done.\n")
