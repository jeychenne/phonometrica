"""
Toy verification for the joint-covariance SLA formula for GLMMs.

Validates three claims:
  1. The proposed closed-form δ matches the brute-force six-pattern tensor sum.
  2. In the q=0 limit (no random effects), the closed form reduces to Phase 1.
  3. The wrong-Phase-2 formula (β-block only) diverges from the truth on GLMMs,
     and the magnitude/sign of the gap explains the observed overshoots.

Setup: binomial GLMM with logit link, n=6, p=2, q=3 (random intercept by subject).

We do NOT iterate PIRLS — we pick (β̂, û) and θ by hand and verify the SLA
correction at that point.  The verification is about the formula at a given mode,
not the convergence of the full fit.
"""

import numpy as np
from itertools import product

np.set_printoptions(precision=6, suppress=True, linewidth=120)


# ============================================================================
# 1. Setup
# ============================================================================

# Design: 6 obs, 2 fixed effects (Intercept + binary x), 3 subjects.
X = np.array([
    [1, 0],
    [1, 0],
    [1, 1],
    [1, 1],
    [1, 0],
    [1, 1],
], dtype=float)

# Subject indicator: rows = obs, cols = subjects.
Z = np.array([
    [1, 0, 0],
    [1, 0, 0],
    [0, 1, 0],
    [0, 1, 0],
    [0, 0, 1],
    [0, 0, 1],
], dtype=float)

y = np.array([0, 1, 1, 1, 0, 1], dtype=float)

n, p = X.shape
_, q = Z.shape
print(f"Setup:  n={n}, p={p}, q={q}")
print()

# Augmented design.
W_aug = np.hstack([X, Z])         # n × (p+q)
assert W_aug.shape == (n, p + q)

# Priors: β ~ N(0, σ_β² I) with σ_β² = 100  ⇒  Λ_β = 0.01 I_p
# Random intercepts u ~ N(0, σ_u² I) with σ_u² = 1.0  ⇒  D⁻¹ = I_q
sigma2_beta = 100.0
sigma2_u = 1.0
Lambda_beta = (1.0 / sigma2_beta) * np.eye(p)
D_inv = (1.0 / sigma2_u) * np.eye(q)

# Pick a (β̂, û) by hand.  Off-the-MAP is fine; SLA is a formula at a point.
beta_hat = np.array([0.2, 0.5])
u_hat    = np.array([-0.5, 0.3, 0.0])
gamma_hat = np.concatenate([beta_hat, u_hat])    # length p+q

print("β̂ =", beta_hat)
print("û =", u_hat)
print()


# ============================================================================
# 2. Likelihood quantities at (β̂, û)
# ============================================================================

eta_hat = X @ beta_hat + Z @ u_hat       # length n
mu_hat  = 1.0 / (1.0 + np.exp(-eta_hat)) # logit inverse
print("η̂ =", eta_hat)
print("μ̂ =", mu_hat)

# Binomial logit canonical-link derivatives w.r.t. η:
#   ℓ'(η)   =  y − μ
#   ℓ''(η)  = −μ(1−μ)
#   ℓ'''(η) = −μ(1−μ)(1−2μ)
# Working weights W̃_ii = −ℓ''(η̂_i) = μ̂(1−μ̂).
W_tilde = mu_hat * (1.0 - mu_hat)
ell3 = -mu_hat * (1.0 - mu_hat) * (1.0 - 2.0 * mu_hat)
print("W̃ (diag) =", W_tilde)
print("ℓ'''(η̂)  =", ell3)
print()


# ============================================================================
# 3. Joint Henderson Hessian H_γ and its inverse Σ_γ
# ============================================================================

# H_γ = W^T diag(W̃) W + diag(Λ_β, D⁻¹)
#     = [ A   B   ]   where  A = X' W̃ X + Λ_β
#       [ B'  M   ]         B = X' W̃ Z
#                            M = Z' W̃ Z + D⁻¹
W_tilde_diag = np.diag(W_tilde)
A = X.T @ W_tilde_diag @ X + Lambda_beta
B = X.T @ W_tilde_diag @ Z
M = Z.T @ W_tilde_diag @ Z + D_inv

H_full = np.block([[A, B], [B.T, M]])

# Direct inverse for ground truth.
Sigma_gamma = np.linalg.inv(H_full)
Sigma_bb = Sigma_gamma[:p, :p]
Sigma_bu = Sigma_gamma[:p, p:]
Sigma_uu = Sigma_gamma[p:, p:]

# Check block-inverse identities (these are the formulas we'll use later).
M_inv = np.linalg.inv(M)
Sigma_bb_schur = np.linalg.inv(A - B @ M_inv @ B.T)
Sigma_bu_check = -Sigma_bb_schur @ B @ M_inv
Sigma_uu_check = M_inv + M_inv @ B.T @ Sigma_bb_schur @ B @ M_inv

assert np.allclose(Sigma_bb, Sigma_bb_schur), "Σ_ββ Schur identity failed"
assert np.allclose(Sigma_bu, Sigma_bu_check), "Σ_βu identity failed"
assert np.allclose(Sigma_uu, Sigma_uu_check), "Σ_uu identity failed"
print("Block-inverse identities verified.")
print()


# ============================================================================
# 4. THREE versions of δ for j in the β-block
# ============================================================================

# -------- (A) Wrong-Phase-2: uses only Σ_ββ and x_i  --------------------
#
#   δ_A_j = ½ Σ_i ℓ'''(η̂_i) (Σ_ββ x_i)_j (x_i' Σ_ββ x_i)
#
# This is what we shipped in the reverted Phase 2.  Equivalent to Phase 1's
# formula with Σ_ββ replacing Σ_post — but it's the WRONG correction in the
# presence of random effects, because it ignores u-block contributions.
def delta_wrong_phase2():
    out = np.zeros(p)
    for i in range(n):
        xi = X[i]
        Sx = Sigma_bb @ xi              # (Σ_ββ x_i), length p
        var_eta = xi @ Sx               # x_i' Σ_ββ x_i, scalar
        out += ell3[i] * Sx * var_eta
    return 0.5 * out


# -------- (B) Proposed closed form with x̃ and a  -----------------------
#
#   x̃_i = x_i − B M⁻¹ z_i
#   a_i = z_i' M⁻¹ z_i
#   δ_B_j = ½ Σ_i ℓ'''(η̂_i) (Σ_ββ x̃_i)_j (x̃_i' Σ_ββ x̃_i + a_i)
def delta_proposed():
    BM_inv = B @ M_inv                  # p × q
    out = np.zeros(p)
    for i in range(n):
        xi = X[i]
        zi = Z[i]
        x_tilde = xi - BM_inv @ zi      # length p
        a_i = zi @ M_inv @ zi           # scalar
        S_xt = Sigma_bb @ x_tilde       # length p
        var_eta_full = x_tilde @ S_xt + a_i
        out += ell3[i] * S_xt * var_eta_full
    return 0.5 * out


# -------- (C) Ground truth: full joint six-pattern tensor sum  ----------
#
#   δ_C_j = ½ Σ_{a,b,c=1..p+q} T_{abc} Σ_γ_{ja} Σ_γ_{bc}
#
# where T_{abc} = Σ_i W_{ia} W_{ib} W_{ic} ℓ'''(η̂_i).  This is the Wick-expanded
# leading SLA correction.  Implementation: build the full tensor explicitly
# and do the three nested sums.  O((p+q)³) — fine for tiny test cases.
def delta_truth():
    s = p + q
    # T_{abc} = Σ_i W_{ia} W_{ib} W_{ic} ℓ'''_i
    # Express via outer products: T = Σ_i ell3[i] · W_i ⊗ W_i ⊗ W_i
    T = np.einsum("i,ia,ib,ic->abc", ell3, W_aug, W_aug, W_aug)

    out = np.zeros(p)
    for j in range(p):           # only β-block
        for aa in range(s):
            for bb in range(s):
                for cc in range(s):
                    out[j] += T[aa, bb, cc] * Sigma_gamma[j, aa] * Sigma_gamma[bb, cc]
    return 0.5 * out


# -------- (D) Ground truth via einsum (equivalent to C, sanity check) ----
def delta_truth_einsum():
    T = np.einsum("i,ia,ib,ic->abc", ell3, W_aug, W_aug, W_aug)
    # δ_j = ½ Σ_{abc} T[a,b,c] · Σ_γ[j,a] · Σ_γ[b,c]
    # The j index is restricted to [0..p).
    delta_full = 0.5 * np.einsum("abc,ja,bc->j", T, Sigma_gamma, Sigma_gamma)
    return delta_full[:p]


# ============================================================================
# 5. Run and compare
# ============================================================================

dA = delta_wrong_phase2()
dB = delta_proposed()
dC = delta_truth()
dD = delta_truth_einsum()

print("=" * 72)
print("δ for the β-block, three formulas (n=6, p=2, q=3, binomial logit)")
print("=" * 72)
print(f"  (A) wrong Phase 2 (β-block only)        : {dA}")
print(f"  (B) proposed closed form (x̃, a)         : {dB}")
print(f"  (C) ground truth (full T contraction)   : {dC}")
print(f"  (D) ground truth (einsum, sanity)       : {dD}")
print()
print(f"  |B − C| (proposed vs truth)              : {np.linalg.norm(dB - dC):.3e}")
print(f"  |C − D| (loop vs einsum)                 : {np.linalg.norm(dC - dD):.3e}")
print(f"  |A − C| (wrong vs truth, the regression) : {np.linalg.norm(dA - dC):.3e}")
print()
print(f"  componentwise A − C: {dA - dC}")
print(f"  componentwise B − C: {dB - dC}")
print()

# Assert the proposed form matches truth to machine precision.
assert np.allclose(dB, dC, atol=1e-12), "Proposed closed form failed to match truth"
assert np.allclose(dC, dD, atol=1e-12), "Internal sanity (loop vs einsum) failed"


# ============================================================================
# 6. q = 0 limit: should reduce to Phase 1 exactly (no random effects)
# ============================================================================

print("=" * 72)
print("q = 0 limit (no random effects): should reduce to Phase 1")
print("=" * 72)

# Recompute everything with Z removed.
Z0 = np.zeros((n, 0))
W0 = X  # augmented = X
A0 = X.T @ W_tilde_diag @ X + Lambda_beta
H0 = A0
Sigma_bb_0 = np.linalg.inv(H0)

# Phase 1 formula (= what bayesian_adjust does).
def delta_phase1():
    out = np.zeros(p)
    for i in range(n):
        xi = X[i]
        Sx = Sigma_bb_0 @ xi
        var_eta = xi @ Sx
        out += ell3[i] * Sx * var_eta
    return 0.5 * out

# Proposed closed form, but with q=0 (B, M empty).
def delta_proposed_q0():
    # B is p × 0 (empty), M is 0×0 (empty).  x̃_i = x_i, a_i = 0.
    out = np.zeros(p)
    for i in range(n):
        xi = X[i]
        Sx = Sigma_bb_0 @ xi
        var_eta = xi @ Sx + 0.0   # a_i = 0
        out += ell3[i] * Sx * var_eta
    return 0.5 * out

# Ground truth at q=0.
def delta_truth_q0():
    T = np.einsum("i,ia,ib,ic->abc", ell3, X, X, X)
    return 0.5 * np.einsum("abc,ja,bc->j", T, Sigma_bb_0, Sigma_bb_0)[:p]

d_p1 = delta_phase1()
d_pq0 = delta_proposed_q0()
d_tq0 = delta_truth_q0()

print(f"  Phase 1 formula           : {d_p1}")
print(f"  Proposed closed form (q=0): {d_pq0}")
print(f"  Ground truth (q=0)        : {d_tq0}")
print()
print(f"  All three identical? {np.allclose(d_p1, d_pq0) and np.allclose(d_p1, d_tq0)}")
assert np.allclose(d_p1, d_pq0, atol=1e-12)
assert np.allclose(d_p1, d_tq0, atol=1e-12)


# ============================================================================
# 7. Strong-RE limit: σ_u² → ∞, formula should still match truth
# ============================================================================

print()
print("=" * 72)
print("Strong-RE limit (σ_u² = 100): proposed form must still match truth")
print("=" * 72)

D_inv_large = (1.0 / 100.0) * np.eye(q)   # very weak prior on u  ⇒  u nearly free
M_large = Z.T @ W_tilde_diag @ Z + D_inv_large
H_large = np.block([[A, B], [B.T, M_large]])
Sigma_g_large = np.linalg.inv(H_large)
Sigma_bb_large = Sigma_g_large[:p, :p]
M_inv_large = np.linalg.inv(M_large)

def delta_proposed_large():
    BM_inv = B @ M_inv_large
    out = np.zeros(p)
    for i in range(n):
        xi = X[i]
        zi = Z[i]
        x_tilde = xi - BM_inv @ zi
        a_i = zi @ M_inv_large @ zi
        S_xt = Sigma_bb_large @ x_tilde
        var_eta_full = x_tilde @ S_xt + a_i
        out += ell3[i] * S_xt * var_eta_full
    return 0.5 * out

def delta_truth_large():
    T = np.einsum("i,ia,ib,ic->abc", ell3, W_aug, W_aug, W_aug)
    return 0.5 * np.einsum("abc,ja,bc->j", T, Sigma_g_large, Sigma_g_large)[:p]

dBL = delta_proposed_large()
dCL = delta_truth_large()
print(f"  Proposed (strong RE) : {dBL}")
print(f"  Truth (strong RE)    : {dCL}")
print(f"  |B − C|              : {np.linalg.norm(dBL - dCL):.3e}")
assert np.allclose(dBL, dCL, atol=1e-12)


# ============================================================================
# 8. Random-slope case: z_i has multiple nonzero entries
# ============================================================================

print()
print("=" * 72)
print("Random-slope case (subject 1 has random slope on x as well)")
print("=" * 72)

# Add a 4th column to Z: random slope on x for subject 1.
Z_rs = np.hstack([Z, np.array([[0, 0, 1, 1, 0, 0]]).T * np.array([[0, 0, 1, 1, 0, 0]]).T])
# Hmm that's not right. Let me just add the column manually:
Z_rs = np.array([
    [1, 0, 0, 0],
    [1, 0, 0, 0],
    [0, 1, 0, 1],   # subj 2, x=1: random-slope col gets 1
    [0, 1, 0, 1],
    [0, 0, 1, 0],
    [0, 0, 1, 0],
], dtype=float)

q_rs = Z_rs.shape[1]
D_inv_rs = np.eye(q_rs)
M_rs = Z_rs.T @ W_tilde_diag @ Z_rs + D_inv_rs
B_rs = X.T @ W_tilde_diag @ Z_rs
H_rs = np.block([[A, B_rs], [B_rs.T, M_rs]])
Sigma_g_rs = np.linalg.inv(H_rs)
Sigma_bb_rs = Sigma_g_rs[:p, :p]
M_inv_rs = np.linalg.inv(M_rs)
W_aug_rs = np.hstack([X, Z_rs])

def delta_proposed_rs():
    BM_inv = B_rs @ M_inv_rs
    out = np.zeros(p)
    for i in range(n):
        xi = X[i]
        zi = Z_rs[i]
        x_tilde = xi - BM_inv @ zi
        a_i = zi @ M_inv_rs @ zi
        S_xt = Sigma_bb_rs @ x_tilde
        var_eta_full = x_tilde @ S_xt + a_i
        out += ell3[i] * S_xt * var_eta_full
    return 0.5 * out

def delta_truth_rs():
    T = np.einsum("i,ia,ib,ic->abc", ell3, W_aug_rs, W_aug_rs, W_aug_rs)
    return 0.5 * np.einsum("abc,ja,bc->j", T, Sigma_g_rs, Sigma_g_rs)[:p]

dB_rs = delta_proposed_rs()
dC_rs = delta_truth_rs()
print(f"  Proposed (random slope): {dB_rs}")
print(f"  Truth (random slope)   : {dC_rs}")
print(f"  |B − C|                : {np.linalg.norm(dB_rs - dC_rs):.3e}")
assert np.allclose(dB_rs, dC_rs, atol=1e-12)


# ============================================================================
# 9. Diagnostic: WHY does the wrong formula overshoot the Intercept?
# ============================================================================

print()
print("=" * 72)
print("Diagnostic: per-observation breakdown for the Intercept (column 0)")
print("=" * 72)
print()
print(f"  {'i':>3} {'ℓ_i_3':>10} {'(Σ_ββ x)_0':>12} {'x_i_Sxx':>10}  "
      f"{'(Σ_ββ x̃)_0':>12} {'x̃_iSx̃x̃+a':>12}  "
      f"{'wrong term':>12} {'true term':>12} {'gap':>10}")
BM_inv = B @ M_inv
for i in range(n):
    xi = X[i]
    zi = Z[i]
    x_tilde = xi - BM_inv @ zi
    a_i = zi @ M_inv @ zi

    Sx = Sigma_bb @ xi
    var_x = xi @ Sx
    wrong_term_i = 0.5 * ell3[i] * Sx[0] * var_x

    S_xt = Sigma_bb @ x_tilde
    var_xt = x_tilde @ S_xt + a_i
    true_term_i = 0.5 * ell3[i] * S_xt[0] * var_xt

    print(f"  {i:>3} {ell3[i]:>10.5f} {Sx[0]:>12.5f} {var_x:>10.5f}  "
          f"{S_xt[0]:>12.5f} {var_xt:>12.5f}  "
          f"{wrong_term_i:>12.5f} {true_term_i:>12.5f} "
          f"{wrong_term_i - true_term_i:>10.5f}")
print()
print(f"  Σ wrong = {dA[0]:.5f}    Σ true = {dC[0]:.5f}    gap = {dA[0]-dC[0]:.5f}")
print()
print("The Intercept column has x_i ≡ 1, so x_tilde_i,0 = 1 − (B M⁻¹ z_i)_0.")
print("The bracketed term (B M⁻¹ z_i)_0 represents the 'load on the Intercept")
print("that random intercepts absorb at this observation', which is sizable.")
print("Ignoring it (the wrong-Phase-2 formula) keeps the multiplier near 1,")
print("inflating δ_Intercept.  The correct formula uses the shrunk x̃, which")
print("partially cancels and gives a much smaller correction.")
print()
print("ALL ASSERTS PASSED.  Proposed closed form matches the brute-force")
print("six-pattern truth in every case checked, and reduces to Phase 1 at q=0.")
