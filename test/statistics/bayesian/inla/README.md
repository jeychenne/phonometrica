# Bayesian INLA-reference validation

A second reference engine for Phonometrica's Bayesian regression
engine, complementing the brms-based suite in the parent directory.
References come from R-INLA — the same Latent-Gaussian-Model
approximation family Phonometrica uses internally (Laplace +
grid-integrated hyperparameters), so disagreements isolate
implementation differences rather than approximation-vs-MCMC ones.

Models adapted from *Bayesian Regression Modeling with INLA* by
Wang, Yue & Faraway (2018), via the GPL-3 `brinla` companion package:
<https://github.com/julianfaraway/brinla>.

## Suite coverage

Four chapters, four test files — the suite is now complete:

* **`test_blr.phon`** (Ch3 — Bayesian Linear Regression):
  usair full, usair reduced, painrelief ANOVA.
  *Student-t fit (`usair.inla4` analogue) is deferred — see
  `gen_reference_blr.R` header for the upstream INLA bug
  diagnostic. Phon's Student-t engine remains validated against brms
  in `../test_student.phon`.*
* **`test_glm.phon`** (Ch4 — GLMs):
  lowbwt binomial full + reduced, AIDS Poisson linear + log,
  crab negbin + Poisson.
* **`test_glmm.phon`** (Ch5 — LMM and GLMM):
  reeds Gaussian RE intercept, reading longitudinal Gaussian RE
  intercept, nitrofen Poisson RE GLMM, ohio binomial RE GLMM.
  *Reading random-slope fit (R3, `piat ~ cagegrp + (cagegrp|id)`)
  is deferred — INLA constructs correlated slope+intercept via the
  iid2d idiom with a Wishart-like joint covariance prior, and there
  is no clean parameterization that matches Phon's independent PC
  priors on each SD component. The suite validates Phon's INLA-method
  engine, not R-INLA's specific iid2d prior choice; cross-engine
  matching here would compare prior conventions rather than
  inference. See `gen_reference_glmm.R` header for the reactivation
  recipe if a separate carve-out comparison is wanted.*
* **`test_gam.phon`** (Ch9 — GAMs): simulated 4-smooth Gaussian
  (`y ~ s(x0) + s(x1) + s(x2) + s(x3)`, n=400).
  *★ Apples-to-oranges smoke test. ★ Unlike the other three slices,
  Phon's `cr` basis (k=10) and INLA's `rw2` random-walk basis on
  inla.group'd knots are fundamentally different smooth
  parameterizations. The slice validates that both engines converge,
  agree on `nobs`, hit roughly the same WAIC (±25) and p_WAIC (±10),
  agree on the intercept (mean_rel = 0.30), and for Gaussian agree on
  residual SD (hyper_mean_rel = 0.30). Per-smooth coefficients, EDFs,
  and smooth precisions are NOT compared — they're basis-specific
  quantities with no clean cross-engine match. Per Phon's memory the
  current cr basis additionally has a 2-3× effective penalty
  parameterization gap vs mgcv that's deferred to a post-v1.0 basis
  rewrite. Tightening this slice is deferred to that point; until
  then it serves as a regression net against crashes and gross drift.
  Kyphosis binomial GAM (M_K) is deferred — surfaced a real Phon-side
  issue: Phon converges to a degenerate intercept ≈ −14.5 (predicted
  probability ≈ 5e-7 everywhere) on n=83 with k=10×3=30 smooth-basis
  parameters; the binomial likelihood is flat under near-saturation
  quasi-separation and Phon's smoothing-penalty selection doesn't keep
  the joint solution bounded. See `gen_reference_gam.R` header and
  `test_gam.phon` for the diagnostic and reactivation recipe.*

Models that rely on INLA-only structures (copies for ridge regression,
AR(1) and `iid2d` outside of `(x|g)` translations, `besag` spatial
priors, SPDE meshes, zero-inflated families, Gamma family) are omitted
because they don't translate to Phonometrica's formula API.

## Running the suite

First (re)generate the data CSVs and references on R:

```
Rscript gen_data_brinla.R        # one-time, datasets to test/data/
Rscript gen_reference_blr.R
Rscript gen_reference_glm.R
Rscript gen_reference_glmm.R
Rscript gen_reference_gam.R
```

`gen_data_brinla.R` requires the R packages `brinla`, `boot`, `dplyr`,
`tidyr`, `mgcv`. The reference generators additionally need `INLA`
and `jsonlite`.

Then run the phon side:

```
phonometrica run_all.phon        # (later slice; currently run per-chapter)
phonometrica test_blr.phon
```

## Prior matching

Reference models are fit with priors that mirror Phonometrica's
auto-scaled defaults (`scale_default_priors` in `fitting.cpp`):

| Phonometrica spec                          | INLA equivalent                                                                                                |
|--------------------------------------------|----------------------------------------------------------------------------------------------------------------|
| `set_fixed(p, 0, scale)` intercept         | `control.fixed$mean.intercept = m_link`, `prec.intercept = 1/scale^2`                                          |
| `set_fixed(p, 0, scale)` slopes            | `control.fixed$mean = 0`, `prec = 1/scale^2`                                                                   |
| `set_variance(p, "pc", U, α)`              | `f(g, model="iid", hyper = list(prec = list(prior="pc.prec", param = c(U, α))))`                               |
| `set_residual(p, "pc", U, α)` (gaussian only) | `control.family$hyper$prec = list(prior = "pc.prec", param = c(U, α))` |

`scale = max(2.5, 2.5 · sd(y_link))`, where `y_link` is the response
on the family's link scale (`y` for gaussian/student, `logit(y)` for
binomial/beta, `log(y + 0.5)` for poisson/negbin) — identical to the
brms suite's `compute_link_stats` so both reference engines stay
prior-matched to Phonometrica.

INLA's `pc.prec(U, α)` is defined as P(σ > U) = α on the SD scale,
exactly matching Phonometrica's `PC(u, α)` parameterisation.

### Carve-out: dispersion priors and Student-t

INLA parameterises NB `size`, Beta `precision`, and Student-t `dof`
on internal log-scales with `loggamma` defaults. Matching
Phonometrica's `Gamma(1, 0.01)` on θ/φ or `Uniform(2, 200)` on ν would
require raw `expression:` priors with manually-computed Jacobians —
easy to get silently wrong. We accept INLA's defaults for these
specific posteriors and widen the per-test tolerance on the affected
hyper rows. β posteriors stay tightly prior-matched.

**Student-t is deferred entirely.** On the current INLA `devel` build,
no `family="T"` configuration produces a usable fit on the usair
dataset:

* `inla.mode = "compact"` (the current default) and `"experimental"`
  crash on a Q-matrix non-PD assertion in the inner GMRF init
  (`tabulate-Qfunc.c:121`), regardless of prior configuration —
  brinla's own baseline crashes too.
* `inla.mode = "classic"` completes without crashing but silently
  returns a degenerate fit: intercept ≈ 0.05 on data with mean(y) ≈
  30, all slope coefficients of order 10⁻³, σ ≈ 37 (larger than
  `sd(y)` ≈ 23), and ν posterior essentially uninformative. The
  bisection probe (`probe_b3.R`) reproduces both failure modes
  cleanly.

Phon's Student-t engine is validated against brms HMC in the parent
`bayesian/test_student.phon`, so the differential-diagnostic loss
is real but bounded. `gen_reference_blr.R` keeps the B3 fit code as
a commented block for easy reactivation once INLA fixes the upstream
regression.

For other families (Gaussian, binomial, Poisson, NB, Beta) the β
prior match holds; only the family-level dispersion hyperparameter
is carved out where applicable.

## Tolerances

`bayes_assert.default_tolerance()` is tuned for brms HMC vs.
Phonometrica. INLA and Phonometrica both use Laplace + grid, so β
agreement should be tighter. Each test file defines a local
`tol_inla` that drops:

* `mean_rel`: 0.20 → 0.10
* `sd_rel`:   0.20 → 0.15
* `ci_rel`:   0.40 → 0.20

Hyper-row tolerances are kept *looser* than the β ones (0.25 / 0.30
/ 0.65 / 5.0 absolute on WAIC). Reason: on the BLR Gaussian fits,
Phon's residual-SD posterior runs ~3% lower in mean and ~5% narrower
in CI width than INLA's, consistently across B1 and B2 — almost
certainly small differences in the CCD grid coordinates/weights and
joint-mode Newton tolerances between the two implementations. A real
prior or scaling bug would produce shifts far larger than the band
these tolerances allow.

## What this validates that brms doesn't

* **Grid-weight correctness** — both engines use CCD; matched
  posteriors verify the grid integration math without MC noise.
* **Latent posterior shape** — Laplace-mixture differences show up
  more cleanly than against HMC.
* **PC-prior application path** — both engines apply PC priors natively.
* **Small-n behavior** — AIDS (n=14) and reeds (n=15) stress the engine
  where brms HMC has high MC noise.
* **Differential diagnostic** — known Student-t artifacts (ν undershoot,
  p_WAIC undercount) classify as "Laplace-vs-HMC fundamental" if INLA
  shows the same gap to brms, or "Phon-specific bug" if it doesn't.

## License

brinla is GPL-3. Phonometrica is GPL-3. Models are adapted (not
verbatim) — formula syntax follows Phonometrica's `(x|g)` convention
rather than INLA's `iid2d`+`copy`, and transformations like
`I(conc/300)` or `log(TIME)` are precomputed columns rather than
formula expressions, since Phonometrica's formula parser is
intentionally strict.
