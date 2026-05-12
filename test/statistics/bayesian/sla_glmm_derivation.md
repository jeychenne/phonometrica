# Joint-covariance SLA correction for GLMMs

## Setup

For a GLMM with canonical link, write γ = (β; u) and W = (X | Z), so the linear
predictor is η_i = W_i^T γ.  At a fixed grid point θ, the joint posterior of γ
given y, θ has log-density

  ℓ_γ(γ) = Σ_i log f(y_i | g⁻¹(η_i)) + log π_β(β) − ½ u^T D⁻¹ u + const

The Gaussian priors contribute zero to all third derivatives, so the joint
third-derivative tensor at γ̂ is purely from the likelihood:

  T_{abc}  =  ∂³ ℓ_γ / ∂γ_a ∂γ_b ∂γ_c |_{γ̂}
           =  Σ_i W_{ia} W_{ib} W_{ic} · ℓ'''(η̂_i)

The joint Hessian and its inverse have block structure:

  H_γ = [ A   B  ]      Σ_γ = H_γ⁻¹ = [ Σ_ββ   Σ_βu ]
        [ B'  M  ]                    [ Σ_uβ   Σ_uu ]

where  A = X' W̃ X + Λ_β,  B = X' W̃ Z,  M = Z' W̃ Z + D⁻¹, and
W̃ = diag(−ℓ''(η̂_i)) is the PIRLS working-weight matrix.


## The marginal-mean correction (Tierney-Kadane / RMC §3.2.2)

For any coordinate γ_j of the joint Gaussian-approximate posterior, the leading
correction to the marginal posterior mean is

  δ_j  =  ½ Σ_{a,b,c} T_{abc} · Σ_{γ,ja} · Σ_{γ,bc}

This follows from expanding ℓ_γ to third order around γ̂ and computing
E[γ_j − γ̂_j] against the Gaussian envelope.  Wick's theorem gives three
identical pairings (by symmetry of T), absorbing the 1/6 into 1/2.

Substituting T:

  δ_j  =  ½ Σ_i ℓ'''(η̂_i) · (Σ_γ W_i)_j · (W_i^T Σ_γ W_i)


## Reducing to a closed form using only Σ_ββ and M⁻¹

We care only about j in the β-block.  Use the block-inverse identities

  Σ_βu = −Σ_ββ B M⁻¹
  Σ_uu =  M⁻¹ + M⁻¹ B' Σ_ββ B M⁻¹

(Both can be verified directly by H_γ Σ_γ = I.  All three identities are
checked numerically in `sla_toy_verify.py`.)

**The j-th row of Σ_γ W_i for j ∈ β.**  Define

  x̃_i  =  x_i − B M⁻¹ z_i        (length p)

Then

  (Σ_γ W_i)_j  =  (Σ_ββ x_i + Σ_βu z_i)_j
              =  (Σ_ββ x_i − Σ_ββ B M⁻¹ z_i)_j
              =  (Σ_ββ x̃_i)_j

**The scalar W_i^T Σ_γ W_i.**  Let v_i = B M⁻¹ z_i (so x̃_i = x_i − v_i):

  W_i^T Σ_γ W_i  =  x_i' Σ_ββ x_i + 2 x_i' Σ_βu z_i + z_i' Σ_uu z_i
                =  x_i' Σ_ββ x_i − 2 x_i' Σ_ββ v_i + z_i' M⁻¹ z_i + v_i' Σ_ββ v_i
                =  (x_i − v_i)' Σ_ββ (x_i − v_i) + z_i' M⁻¹ z_i
                =  x̃_i' Σ_ββ x̃_i + a_i

with a_i = z_i' M⁻¹ z_i.

## The boxed formula

For j ∈ β:

  ┌──────────────────────────────────────────────────────────────────────────┐
  │                                                                          │
  │  δ_j  =  ½ Σ_i ℓ'''(η̂_i) · (Σ_ββ x̃_i)_j · (x̃_i' Σ_ββ x̃_i + a_i)         │
  │                                                                          │
  │    x̃_i = x_i − B M⁻¹ z_i,   a_i = z_i' M⁻¹ z_i,                          │
  │    B   = X' W̃ Z,            M    = Z' W̃ Z + D⁻¹                          │
  │                                                                          │
  └──────────────────────────────────────────────────────────────────────────┘


## Limits and sanity checks (all verified numerically)

1. **No random effects (q=0)**.  B, M, Z are empty ⇒ x̃_i = x_i, a_i = 0.
   The formula collapses to Phase 1 exactly.

2. **Vanishing RE (σ_u² → 0)**.  D⁻¹ → ∞ ⇒ M → ∞ ⇒ M⁻¹ → 0.  Then v_i → 0,
   x̃_i → x_i, a_i → 0.  Reduces to Phase 1.  Correct: if REs cannot move,
   they cannot contribute skewness.

3. **Strong RE (σ_u² → ∞)**.  D⁻¹ → 0 ⇒ M → Z'W̃Z.  v_i and a_i pick up
   substantial values; this is where the wrong-Phase-2 formula diverged
   most.  Closed form still matches truth at machine precision.

4. **Random slope**.  z_i has multiple nonzero entries; nothing in the
   derivation assumed indicator-only z_i.  Verified explicitly.


## Why the previous Phase 2 went wrong

Phase 2 used the fixed-effects formula with Σ_ββ in place of Σ_post:

  δ_j (wrong)  =  ½ Σ_i ℓ'''(η̂_i) · (Σ_ββ x_i)_j · (x_i' Σ_ββ x_i)

This is the special case x̃_i = x_i, a_i = 0 — equivalent to assuming v_i = 0,
which happens only when β and u are *independent* in the joint posterior
(Σ_βu = 0).  For a random-intercept model that condition fails badly: the
random intercepts compete with the fixed Intercept for explanatory load, so
Σ_βu has large negative entries on the Intercept row, and v_i = B M⁻¹ z_i
is far from zero.

Numerically (toy example, n=6, p=2, q=3, binomial logit):

  formula                        δ_Intercept    δ_slope
  ─────────────────────────────────────────────────────
  (A) wrong Phase 2             −0.0314        +0.5881
  (B) proposed (x̃, a)            −0.0224        +0.4014
  (C) brute-force truth         −0.0224        +0.4014

  |B − C| = 2.9 × 10⁻¹⁶ (machine precision)
  |A − C| = 0.187       (the regression we saw on real data)


## Implementation surface

All in `mixed_model.cpp`.  The required quantities at each grid point:

  per grid k:
    M_k        = Z' W̃_k Z + D_k⁻¹   (already exists in WAIC scaffolding as M_k)
    B_k        = X' W̃_k Z            (n × q product, easy)
    M_k⁻¹      = via Cholesky (already in u_llt[k])

  per grid k, per observation i:
    x̃_{k,i}    = x_i − B_k M_k⁻¹ z_i
    a_{k,i}    = z_i' M_k⁻¹ z_i

Then the SLA δ_k computation has the same vectorized shape as Phase 1:

    X̃_k = X − Z M_k⁻¹ B_k'                   // n × p
    diag_aux_k = vector { z_i' M_k⁻¹ z_i }   // length n, computed row-wise
    A = X̃_k Σ_ββ,k                           // n × p, row i = (Σ_ββ x̃_i)^T
    var_eta_full_i = X̃_k.row(i).dot(A.row(i)) + diag_aux_k[i]
    δ_k = A' (½ ℓ''' ⊙ var_eta_full)

Cost per grid point: one n × q solve (M⁻¹ B' and M⁻¹ Z' both leverage same
Cholesky), one n × p² matmul, plus n dot products of length p.  All standard.


## Open questions before coding

(None.  The derivation is closed-form, limits check, brute-force truth matches.)
