# =====================================================================
# gen_reference_binomial.R — produces reference_binomial.json
# =====================================================================
# Run:    Rscript gen_reference_binomial.R
#
# Synthetic schwa-realisation dataset (binomial_schwa.csv): 5400 obs,
# 20 speakers, 15 words, response `realized` ∈ {0, 1}.
#
# Models:
#   M1 — realized ~ position * style                    (interaction)
#   M2 — realized ~ position + style + (1|speaker)
#   M3 — realized ~ position + style + (1+style|speaker)
#   M4 — realized ~ position + style + (1|speaker) + (1|word)
#   M5 — realized ~ position + style + (1+style|speaker) + (1|word)
#
# Slope variable: style is 2-level and varies in 20/20 speakers, so
# (1 + style | speaker) gives a clean 2x2 covariance — preferred over
# the 3-level position alternative.
# =====================================================================

source("_helpers.R")

d <- read.delim(resolve_data_path("binomial_schwa.csv"))
d$position <- factor(d$position, levels = c("final", "initial", "medial"))
d$style    <- factor(d$style,    levels = c("casual", "formal"))

models <- list()

m1 <- glmmTMB(realized ~ position * style, data = d, family = binomial())
models$M1 <- build_model_entry(
    m1, "realized ~ position * style", "glmmTMB", "binomial"
)

m2 <- glmmTMB(realized ~ position + style + (1 | speaker),
              data = d, family = binomial())
models$M2 <- build_model_entry(
    m2, "realized ~ position + style + (1|speaker)", "glmmTMB", "binomial"
)

m3 <- glmmTMB(realized ~ position + style + (1 + style | speaker),
              data = d, family = binomial())
models$M3 <- build_model_entry(
    m3, "realized ~ position + style + (1+style|speaker)", "glmmTMB", "binomial"
)

m4 <- glmmTMB(realized ~ position + style + (1 | speaker) + (1 | word),
              data = d, family = binomial())
models$M4 <- build_model_entry(
    m4, "realized ~ position + style + (1|speaker) + (1|word)", "glmmTMB", "binomial"
)

m5 <- glmmTMB(realized ~ position + style + (1 + style | speaker) + (1 | word),
              data = d, family = binomial())
models$M5 <- build_model_entry(
    m5, "realized ~ position + style + (1+style|speaker) + (1|word)", "glmmTMB", "binomial"
)

write_reference(
    "binomial", models, "binomial_schwa.csv",
    resolve_output_path("reference_binomial.json")
)
