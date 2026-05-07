# =====================================================================
# gen_reference_real_schwa.R — produces reference_real_schwa.json
# =====================================================================
# Run:    Rscript gen_reference_real_schwa.R
#
# Real binomial dataset (schwa_eychenne2019.csv): 7787 obs, 45
# subjects, 1211 words. Response: schwa ∈ {present, absent}, recoded
# to 0/1 with present = 1.
#
# Speaker-level fixed effects (constant within subject):
#   dialect ∈ {Languedocian, Basque, Provençal}
#   gender  ∈ {female, male}
#
# Within-subject covariates (vary in 45/45 subjects):
#   task    ∈ {text, formal, informal}     — well-balanced, good slope candidate
#   left    ∈ {vowel, consonant, sc}       — sc is sparse (57/7787), risky slope
#   right   ∈ {consonant, IP edge}         — well-balanced 2-level
#
# Models. Same five-shape spec as the synthetic families:
#   M1 — schwa01 ~ dialect * gender + task               (interaction)
#   M2 — schwa01 ~ dialect + gender + task + (1|subject)
#   M3 — schwa01 ~ dialect + gender + task + (1+task|subject)
#   M4 — schwa01 ~ dialect + gender + task + (1|subject) + (1|word)
#   M5 — schwa01 ~ dialect + gender + task + (1+task|subject) + (1|word)
#
# Heads-up. M5 fits a 1211-level word random intercept on top of a
# 3-dimensional task slope on subject. This will be the slowest fit
# in the suite — typically ~30s on a modern machine. If you only
# changed something local (e.g. a fixed-effect parameterisation),
# you can comment out M5 and re-run to keep iteration fast.
# =====================================================================

source("_helpers.R")

d <- read.csv(resolve_data_path("schwa_eychenne2019.csv"),
              fileEncoding = "UTF-8", stringsAsFactors = FALSE)

# Recode response to 0/1 with present = 1 (the expected positive class).
d$schwa01 <- as.integer(d$schwa == "present")

# Set explicit factor levels — reference levels need to match what
# Phonometrica picks (alphabetical by default in both engines).
d$dialect <- factor(d$dialect, levels = c("Basque", "Languedocian", "Provençal"))
d$gender  <- factor(d$gender,  levels = c("female", "male"))
d$task    <- factor(d$task,    levels = c("formal", "informal", "text"))

models <- list()

m1 <- glmmTMB(schwa01 ~ dialect * gender + task,
              data = d, family = binomial())
models$M1 <- build_model_entry(
    m1, "schwa01 ~ dialect * gender + task", "glmmTMB", "binomial"
)

m2 <- glmmTMB(schwa01 ~ dialect + gender + task + (1 | subject),
              data = d, family = binomial())
models$M2 <- build_model_entry(
    m2, "schwa01 ~ dialect + gender + task + (1|subject)", "glmmTMB", "binomial"
)

m3 <- glmmTMB(schwa01 ~ dialect + gender + task + (1 + task | subject),
              data = d, family = binomial())
models$M3 <- build_model_entry(
    m3, "schwa01 ~ dialect + gender + task + (1+task|subject)", "glmmTMB", "binomial"
)

m4 <- glmmTMB(schwa01 ~ dialect + gender + task + (1 | subject) + (1 | word),
              data = d, family = binomial())
models$M4 <- build_model_entry(
    m4, "schwa01 ~ dialect + gender + task + (1|subject) + (1|word)", "glmmTMB", "binomial"
)

m5 <- glmmTMB(schwa01 ~ dialect + gender + task + (1 + task | subject) + (1 | word),
              data = d, family = binomial())
models$M5 <- build_model_entry(
    m5, "schwa01 ~ dialect + gender + task + (1+task|subject) + (1|word)", "glmmTMB", "binomial"
)

write_reference(
    "binomial", models, "schwa_eychenne2019.csv",
    resolve_output_path("reference_real_schwa.json")
)
