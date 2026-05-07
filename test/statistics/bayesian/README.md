# Bayesian statistics validation

**Placeholder.** Will be populated in a separate session, mirroring
the layout of `../frequentist/`:

```
bayesian/
├── README.md
├── _helpers.R                           ← shared brms-side helpers
├── gen_reference_<family>.R             ← per-family generators (HMC)
├── gen_reference_all.R
├── reference_<family>.json              ← committed artifacts
├── test_<family>.phon                   ← phon-side checks
└── run_all.phon
```

## Plan

The phon side will follow the same pattern as frequentist: import
`../lib/assert`, fit with an explicit `Prior()` argument, compare
posterior summaries against brms HMC.

## Reference engine

`brms` 2.20+ with HMC sampling via Stan, matched-prior on every
random-effect SD, residual SD, and fixed-effect coefficient. The
prior dictionary is fixed across families:

| Phonometrica spec                | brms equivalent                                         |
|----------------------------------|---------------------------------------------------------|
| `set_fixed(p, 0, 10)`            | `prior(normal(0, 10), class = b)` + same on `Intercept` |
| `set_variance(p, "pc", 1, 0.05)` | `prior(exponential(2.9957), class = sd)`                |
| `set_residual(p, "pc", 1, 0.05)` | `prior(exponential(2.9957), class = sigma)`             |
| `set_negbin_theta(p, 1, 0.01)`   | `prior(gamma(1, 0.01), class = shape)`                  |
| `set_beta_phi(p, 1, 0.01)`       | `prior(gamma(1, 0.01), class = phi)`                    |

The PC↔Exponential equivalence: PC(σ₀, α) on σ has density
λ exp(−λσ) with λ = −log(α)/σ₀. For PC(1, 0.05): λ ≈ 2.9957. brms
doesn't have a PC prior keyword, but the underlying distribution is
identical.

## Tolerance philosophy (anticipated)

brms HMC with sufficient warmup is the gold standard, but each
posterior summary has an MC standard error. Phonometrica's INLA-style
grid integration plus PC priors should match HMC to within ~1–2 MC
standard errors on every comparable quantity. Tolerances will be
tuned to "qualitative inference doesn't change" rather than "decimal
agreement" — see the project memory entry on the Bayesian validation
philosophy.

## Existing material to fold in

`brms_phonometrica_comparison.R` and `validation_bayesian.md` (both
currently in the repo's `test/` root) document the matched-prior
comparisons run during the May 2026 (τ, ω) refactor. They'll be
restructured into per-family `gen_reference_<family>.R` scripts +
matching `test_<family>.phon` files when this directory is wired up.
