# ============================================================
# R validation suite for Phonometrica regression families
# Requires: lme4, glmmTMB
#
# Each family is tested with four model structures:
#   M1: fixed effects only
#   M2: one random intercept (speaker)
#   M3: two crossed random intercepts (speaker + word)
#   M4: random slope (slope_var | speaker)
#
# Run from the directory containing the data/ folder.
# ============================================================

library(lme4)
library(glmmTMB)

options(width = 120)

datadir <- "data"

print_header <- function(family, model) {
    cat("\n")
    cat(strrep("=", 70), "\n")
    cat(sprintf(" %s — %s\n", family, model))
    cat(strrep("=", 70), "\n\n")
}

print_summary <- function(m) {
    s <- summary(m)
    print(s)
    cat(sprintf("\nlogLik = %.4f\n", as.numeric(logLik(m))))
    cat(sprintf("AIC    = %.1f\n", AIC(m)))
    cat(sprintf("BIC    = %.1f\n", BIC(m)))
    cat("\n")
}

# =====================================================================
# 1. GAUSSIAN — F1 values
# =====================================================================

d <- read.delim(file.path(datadir, "gaussian_f1.csv"))
d$vowel  <- factor(d$vowel,  levels = c("a", "i", "u"))
d$gender <- factor(d$gender, levels = c("F", "M"))

print_header("GAUSSIAN", "M1: f1 ~ vowel + gender")
m <- lm(f1 ~ vowel + gender, data = d)
print(summary(m))
cat(sprintf("logLik = %.4f\n", as.numeric(logLik(m))))
cat(sprintf("AIC = %.1f  BIC = %.1f\n\n", AIC(m), BIC(m)))

print_header("GAUSSIAN", "M2: f1 ~ vowel + gender + (1|speaker)")
m <- lmer(f1 ~ vowel + gender + (1|speaker), data = d, REML = FALSE)
print_summary(m)

print_header("GAUSSIAN", "M3: f1 ~ vowel + gender + (1|speaker) + (1|word)")
m <- lmer(f1 ~ vowel + gender + (1|speaker) + (1|word), data = d, REML = FALSE)
print_summary(m)

print_header("GAUSSIAN", "M4: f1 ~ vowel + (1 + gender|speaker)")
m <- lmer(f1 ~ vowel + (1 + gender|speaker), data = d, REML = FALSE)
print_summary(m)

# =====================================================================
# 2. BINOMIAL — Schwa realization
# =====================================================================

d <- read.delim(file.path(datadir, "binomial_schwa.csv"))
d$position <- factor(d$position, levels = c("final", "initial", "medial"))
d$style    <- factor(d$style,    levels = c("casual", "formal"))

print_header("BINOMIAL", "M1: realized ~ position + style")
m <- glmmTMB(realized ~ position + style, family = binomial(), data = d)
print_summary(m)

print_header("BINOMIAL", "M2: realized ~ position + style + (1|speaker)")
m <- glmmTMB(realized ~ position + style + (1|speaker), family = binomial(), data = d)
print_summary(m)

print_header("BINOMIAL", "M3: realized ~ position + style + (1|speaker) + (1|word)")
m <- glmmTMB(realized ~ position + style + (1|speaker) + (1|word), family = binomial(), data = d)
print_summary(m)

print_header("BINOMIAL", "M4: realized ~ position + (1 + style|speaker)")
m <- glmmTMB(realized ~ position + (1 + style|speaker), family = binomial(), data = d)
print_summary(m)

# =====================================================================
# 3. POISSON — Disfluency counts
# =====================================================================

d <- read.delim(file.path(datadir, "poisson_disfluency.csv"))
d$task <- factor(d$task, levels = c("conversation", "reading"))
d$age  <- factor(d$age,  levels = c("old", "young"))

print_header("POISSON", "M1: count ~ task + age")
m <- glmmTMB(count ~ task + age, family = poisson(), data = d)
print_summary(m)

print_header("POISSON", "M2: count ~ task + age + (1|speaker)")
m <- glmmTMB(count ~ task + age + (1|speaker), family = poisson(), data = d)
print_summary(m)

print_header("POISSON", "M3: count ~ task + age + (1|speaker) + (1|word)")
m <- glmmTMB(count ~ task + age + (1|speaker) + (1|word), family = poisson(), data = d)
print_summary(m)

print_header("POISSON", "M4: count ~ task + (1 + age|speaker)")
m <- glmmTMB(count ~ task + (1 + age|speaker), family = poisson(), data = d)
print_summary(m)

# =====================================================================
# 4. NEGATIVE BINOMIAL — Hesitation counts
# =====================================================================

d <- read.delim(file.path(datadir, "negbin_hesitation.csv"))
d$complexity <- factor(d$complexity, levels = c("complex", "simple"))
d$stress     <- factor(d$stress,     levels = c("stressed", "unstressed"))

print_header("NEGBIN", "M1: hesitations ~ complexity + stress")
m <- glmmTMB(hesitations ~ complexity + stress, family = nbinom2(), data = d)
print_summary(m)

print_header("NEGBIN", "M2: hesitations ~ complexity + stress + (1|speaker)")
m <- glmmTMB(hesitations ~ complexity + stress + (1|speaker), family = nbinom2(), data = d)
print_summary(m)

print_header("NEGBIN", "M3: hesitations ~ complexity + stress + (1|speaker) + (1|word)")
m <- glmmTMB(hesitations ~ complexity + stress + (1|speaker) + (1|word), family = nbinom2(), data = d)
print_summary(m)

print_header("NEGBIN", "M4: hesitations ~ complexity + (1 + stress|speaker)")
m <- glmmTMB(hesitations ~ complexity + (1 + stress|speaker), family = nbinom2(), data = d)
print_summary(m)

# =====================================================================
# 5. BETA — Voicing proportion
# =====================================================================

d <- read.delim(file.path(datadir, "beta_voicing.csv"))
d$consonant <- factor(d$consonant, levels = c("b", "d", "p", "t"))
d$position  <- factor(d$position,  levels = c("final", "initial", "medial"))

print_header("BETA", "M1: voicing ~ consonant + position")
m <- glmmTMB(voicing ~ consonant + position, family = beta_family(), data = d)
print_summary(m)

print_header("BETA", "M2: voicing ~ consonant + position + (1|speaker)")
m <- glmmTMB(voicing ~ consonant + position + (1|speaker), family = beta_family(), data = d)
print_summary(m)

print_header("BETA", "M3: voicing ~ consonant + position + (1|speaker) + (1|word)")
m <- glmmTMB(voicing ~ consonant + position + (1|speaker) + (1|word), family = beta_family(), data = d)
print_summary(m)

print_header("BETA", "M4: voicing ~ consonant + (1 + position|speaker)")
m <- glmmTMB(voicing ~ consonant + (1 + position|speaker), family = beta_family(), data = d)
print_summary(m)
