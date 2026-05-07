# =====================================================================
# gen_reference_poisson.R — produces reference_poisson.json
# =====================================================================
# Run:    Rscript gen_reference_poisson.R
#
# Synthetic disfluency-count dataset (poisson_disfluency.csv): 1800
# obs, 20 speakers, 15 words, response `count` ∈ N≥0.
#
# Models:
#   M1 — count ~ task * age                        (interaction)
#   M2 — count ~ task + age + (1|speaker)
#   M3 — count ~ task + age + (1+task|speaker)
#   M4 — count ~ task + age + (1|speaker) + (1|word)
#   M5 — count ~ task + age + (1+task|speaker) + (1|word)
#
# Slope variable: age is constant within speaker (it's a person
# attribute), so the slope on speaker is on `task`, the only covariate
# that varies within speaker.
# =====================================================================

source("_helpers.R")

d <- read.delim(resolve_data_path("poisson_disfluency.csv"))
d$task <- factor(d$task, levels = c("conversation", "reading"))
d$age  <- factor(d$age,  levels = c("old", "young"))

models <- list()

m1 <- glmmTMB(count ~ task * age, data = d, family = poisson())
models$M1 <- build_model_entry(
    m1, "count ~ task * age", "glmmTMB", "poisson"
)

m2 <- glmmTMB(count ~ task + age + (1 | speaker),
              data = d, family = poisson())
models$M2 <- build_model_entry(
    m2, "count ~ task + age + (1|speaker)", "glmmTMB", "poisson"
)

m3 <- glmmTMB(count ~ task + age + (1 + task | speaker),
              data = d, family = poisson())
models$M3 <- build_model_entry(
    m3, "count ~ task + age + (1+task|speaker)", "glmmTMB", "poisson"
)

m4 <- glmmTMB(count ~ task + age + (1 | speaker) + (1 | word),
              data = d, family = poisson())
models$M4 <- build_model_entry(
    m4, "count ~ task + age + (1|speaker) + (1|word)", "glmmTMB", "poisson"
)

m5 <- glmmTMB(count ~ task + age + (1 + task | speaker) + (1 | word),
              data = d, family = poisson())
models$M5 <- build_model_entry(
    m5, "count ~ task + age + (1+task|speaker) + (1|word)", "glmmTMB", "poisson"
)

write_reference(
    "poisson", models, "poisson_disfluency.csv",
    resolve_output_path("reference_poisson.json")
)
