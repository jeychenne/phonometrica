# Frequentist statistics validation

Validates Phonometrica's frequentist regression engine against four
R reference engines: `lm` (Gaussian fixed-only, closed-form OLS),
`glmmTMB` (the default for everything mixed and for non-Gaussian
fixed-effects), `lme4::glmer.nb` (the NB GLMM reference for the Owls
test specifically — see *Engine choice and dual references* below),
and `MASS::glm.nb` (NB fixed-effects on Owls).

## Layout

```
frequentist/
├── README.md
├── _helpers.R                       ← shared R helpers (jsonlite, glmmTMB)
├── gen_reference_<family>.R         ← per-family reference generators
├── gen_reference_negbin_owls.R      ← Owls sidecar (lme4 + MASS)
├── gen_reference_all.R              ← runs the synthetic-data generators
├── reference_<family>.json          ← generated artifacts (committed)
├── reference_negbin_owls.json       ← Owls sidecar reference (committed)
├── test_<family>.phon               ← per-family validation tests
├── test_negbin_owls.phon            ← Owls sidecar test (manual run)
└── run_all.phon                     ← orchestrator (synthetic suite)
```

## Naming convention

R names categorical coefficients as `paste0(var, level)` and the
intercept as `(Intercept)`. Phonometrica uses brackets and no parens:

| R                      | Phonometrica            |
|------------------------|-------------------------|
| `(Intercept)`          | `Intercept`             |
| `voweli`               | `vowel[i]`              |
| `voweli:genderM`       | `vowel[i]:gender[M]`    |
| `sd((Intercept)\|s)`   | `sd(Intercept\|s)`      |
| `sd(voweli\|s)`        | `sd(vowel[i]\|s)`       |
| `sd(residual)`         | `sd(residual)` (same)   |

`_helpers.R` translates names from R's convention to Phonometrica's
before serialisation (`translate_fixef_name`, `build_cat_levels`).
The JSON references therefore contain Phonometrica-format names
directly, which is also what `summarize(model)` prints in the GUI,
and the phon-side harness looks up by name without knowing R's
quirks.

If you regenerate references with an `_helpers.R` that doesn't do
the translation (anything pre-May 2026), the phon-side lookups will
all miss and you'll see `name not found` failures for every fixed
effect — re-running `Rscript gen_reference_all.R` with the current
helpers fixes it.

## Workflow

### Data and reference are a unit — always regenerate together

Each `gen_reference_<family>.R` script that produces synthetic data
**also fits and writes the reference JSON in the same `Rscript`
invocation**. The two outputs are paired: the JSON's coefficients,
log-likelihoods, and SDs are valid only against the specific draw
of the dataset that the same script wrote to CSV. Modifying the CSV
after the fact (or replacing it from a different run) silently
desynchronizes the pair — and the failures it produces look
*exactly* like an engine bias, because every coefficient drifts in
correlated ways across all five models in the family. The first
debugging session against a desynchronized pair produces a very
plausible "engine has a 3–12% bias on θ that scales with model
complexity" diagnosis that's entirely an artifact of comparing fits
on different data.

The rule, then: never edit a `<family>.csv` file directly, and
never check in a CSV without re-running its `gen_reference_<family>.R`
in the same step. The Owls dataset is the only exception (it's
fixed empirical data, not generated) and its sidecar script reads
the CSV instead of writing it.

### 1. Generate the references (R)

Once, after a fresh clone or after R / `glmmTMB` / `lme4` /
`_helpers.R` updates:

```
Rscript gen_reference_all.R
```

This runs every synthetic-data generator and writes a matched
`<family>.csv` + `reference_<family>.json` pair for each. It does
**not** include `gen_reference_negbin_owls.R` — see *Engine choice
and dual references* below. To regenerate the Owls reference
specifically:

```
Rscript gen_reference_negbin_owls.R
```

Or any single-family generator when only one family changed:

```
Rscript gen_reference_gaussian.R
Rscript gen_reference_beta.R
```

The slow ones are `gen_reference_beta.R` (large dataset, ~7000 obs,
multiple random-slope models) and `gen_reference_real_schwa.R`
(~1200-level word random intercept on real data). On a modern
machine the full set takes ~1–2 minutes.

### 2. Run the phon tests

```
phonometrica run_all.phon
```

`run_all.phon` accumulates pass/fail counters via the shared
`assert.phon` module and asserts a clean run at the end (non-zero
exit code on any failure, suitable for CI). It does **not** include
`test_negbin_owls.phon`; that's a manual test for the documented
hard-fit case.

Or per family — same surgical iteration:

```
phonometrica test_gaussian.phon
phonometrica test_negbin_owls.phon       # sidecar
```

## Models

Each family/dataset is exercised with five models in the same shape:

| Tag | Structure                                   | What it tests                                                  |
|-----|---------------------------------------------|----------------------------------------------------------------|
| M1  | fixed only, with interaction (`*`)          | Closed-form / IRLS for the simplest case                       |
| M2  | + (1\|speaker)                              | Single random intercept                                        |
| M3  | + (1+slope_var\|speaker)                    | Single cluster, random slope (covariance estimation)           |
| M4  | + (1\|speaker) + (1\|word)                  | Crossed random intercepts                                      |
| M5  | + (1+slope_var\|speaker) + (1\|word)        | Crossed RE + slope (full menagerie)                            |

The slope variable per family is the within-cluster covariate that
varies cleanly. Datasets and structure as currently committed:

| Family             | Dataset                  | M1 fixed structure          | Slope variable | Cluster  |
|--------------------|--------------------------|-----------------------------|----------------|----------|
| Gaussian           | `inst_eval.csv`          | `studage * service`         | `service`      | `s`, `d` |
| Binomial           | `binomial_schwa.csv`     | `position * style`          | `style`        | speaker, word |
| Poisson            | `poisson_disfluency.csv` | `task * age`                | `task`         | speaker, word |
| Negative binomial  | `negbin_counts.csv`      | `condition * context`       | `condition`    | subject, item |
| Negative binomial *(sidecar)* | `owls.csv`    | `food * sex`                | `food`         | nest    |
| Beta               | `beta_accuracy.csv`      | `difficulty * domain`       | `difficulty`   | subject, item |
| Real binomial      | `schwa_eychenne2019.csv` | `dialect * gender + task`   | `task`         | subject, word |
| Student-t          | (disabled)               | —                           | —              | —       |

Student-t is intentionally excluded from the v1.0 frequentist suite.
The glmmTMB references for that family aren't trustworthy (most
fits report `converged=false` or null logLik). The right reference
for Student-t is brms HMC, which lives in the Bayesian suite. See
the banner in `test_student.phon` for full rationale.

## Engine choice and dual references

For most families a single reference engine suffices: well-behaved
data lets `glmmTMB`, `lme4`, and Phonometrica all converge to the
same point, so the choice is arbitrary and we use `glmmTMB` for
consistency. On harder fits — flat likelihoods, large slope
variances relative to intercept variance, weakly identified
dispersion parameters — different algorithm families settle at
slightly different stationary points of the same Laplace marginal
NLL, separated by ~0.05–0.20 in log-likelihood. Both engines are
correct implementations of slightly different algorithms; neither
is "the truth."

Phonometrica's NB GLMM uses PIRLS with profile-θ Newton in spirit —
Phase 1 profiles β via PIRLS at each θ, Phase 2 refines (β, θ)
jointly with û profiled out. This sits in the same algorithm family
as `lme4::glmer.nb`, not `glmmTMB` (which does joint TMB
optimisation with AD-exact gradients). On hard datasets Phon
therefore agrees with `lme4` and disagrees with `glmmTMB` by 1–5%
on individual coefficients.

The Owls test (`test_negbin_owls.phon`) exercises this regime
deliberately. Owls (Roulin & Bersier 2007) has a food-treatment
slope SD ≈ 1.06, almost seven times the intercept SD ≈ 0.16 — a
genuinely hard fit where `lme4::glmer.nb` itself emits
`max|grad|` convergence warnings on every mixed model (M2 ≈ 0.009,
M3 ≈ 0.031, M4 ≈ 0.009, M5 ≈ 0.032). We use `lme4::glmer.nb`
(plus `MASS::glm.nb` for M1, since `lme4` doesn't fit fixed-only
NB) as the reference, for an apples-to-apples comparison of two
PIRLS-based engines. With this reference, Phon matches lme4 to ~5
decimal places on coefficients across M2/M4/M5; M3 throws a
documented L-BFGS non-finite-state on cold-start û=0 with strong
slope variance and is flagged as `converged=false` in the JSON
specifically so the harness downgrades the throw to a NOTE (the
right behaviour for a known limitation, not a regression).

The synthetic NB test (`test_negbin.phon`) keeps `glmmTMB` as the
reference because moderate slope variance keeps every engine on the
same interior optimum and there's no disagreement to navigate.
With the May 2026 data + reference regeneration, Phon and glmmTMB
agree on logLik to 6 decimal places on M1 and within ordinary
tolerances on M2–M5.

## Tolerances

Tolerances are tuned per-family in each `test_<family>.phon`. The
core `check_fit` helper takes three:

- `rtol_fit` — applied to scalar fit summaries (loglik, AIC, BIC,
  RSE, R², theta, phi, sigma, nu).
- `rtol_coef` — applied to fixed-effect estimates and SEs.
- `rtol_ranef` — applied to random-effect SDs (with a 1.0 absolute
  floor so near-zero SDs don't fail spuriously).

Each tolerance has a small absolute floor in `check_fit` itself
(typically 1e-3 to 1e-2 depending on the quantity).

Notable per-family deviations from the default tight tolerances:

- **Gaussian M1** uses near-machine-precision tolerances (1e-6 on
  fit, 1e-5 on coef) since both engines compute closed-form OLS.
- **Negative binomial** uses `rtol_fit = 0.02` as a conservative
  margin. With the current synthetic dataset and a co-generated
  reference, the actual logLik agreement is ~6 decimals — the
  wider tolerance is defensive against future drift if the data
  generator is re-seeded or the engine evolves. The test banners
  still mention a "~1.3-unit additive offset" for historical
  reasons; that diagnosis came from a desynchronized data/reference
  pair (see *Data and reference are a unit*) and does not reflect
  current engine behaviour.
- **Negative binomial Owls** uses `rtol_coef = 0.01–0.02` because
  the engine choice is matched (lme4 ↔ Phon, both PIRLS); the
  actual coefficient agreement is on the order of 1e-5.

When choosing tolerances for a new test, the practical rule is:
start at the engine-disagreement floor for the family (tight on
clean data; up to 5% on flat-likelihood cases like Owls), and only
widen further if a specific dataset has weakly identified
parameters. See the comment block at the top of each
`test_<family>.phon` for the rationale behind specific values.

## Adding a new family or model

1. Write `gen_reference_<family>.R` next to its peers; source
   `_helpers.R` and call `build_model_entry()` for each model. If
   the family generates synthetic data, `set.seed(...)` and write
   the CSV and JSON in the same script run — never separate steps.
2. Add a `Rscript gen_reference_<family>.R` line (or a new
   `source(...)` entry) to `gen_reference_all.R`.
3. Write `test_<family>.phon`. The pattern is fixed: import
   `../lib/assert`, load the JSON, fit each model, call
   `A.fit_and_check("M<n>", formula, d, family, refs["M<n>"], rtol_fit, rtol_coef, rtol_ranef)`.
4. Add the family to `run_all.phon`'s `test_files` list.
5. Commit the regenerated `<family>.csv` + `reference_<family>.json`
   alongside the code.

If your reference needs a non-standard fit summary field (e.g. a
custom dispersion parameter), extend `check_fit` in
`../lib/assert.phon` — the existing scaffolding (loglik, AIC, BIC,
rse, r2, adj_r2, theta, phi, sigma, nu) covers every family in this
suite.

### Adding a sidecar (alternative-engine) test

The Owls test is the template when a family's Phonometrica
implementation is in a different algorithm family from the default
reference engine, and the engine disagreement is large enough on
specific datasets that comparing against the wrong engine produces
spurious failures.

1. Write `gen_reference_<family>_<dataset>.R` reading the existing
   data file (no `write.csv` step — sidecars are read-only on data).
   Use the alternative reference engine in its top-level fit calls.
2. Define a `boundary_models <- c(...)` vector listing any models
   where Phonometrica is documented to throw or fail to converge,
   so the JSON marks them `converged=false` and the harness
   downgrades the failure to a NOTE.
3. Write `test_<family>_<dataset>.phon` mirroring the standard
   pattern, with tolerances appropriate to the engine choice.
4. Do **not** add the sidecar to `gen_reference_all.R` or
   `run_all.phon` — sidecars are intentional manual runs that
   document known difficult cases without polluting the default
   suite. Reference them in this README so future contributors know
   they exist.
