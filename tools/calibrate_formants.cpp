/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version. See <http://www.gnu.org/licenses/>.                                                                  *
 *                                                                                                                     *
 * Created: 03/07/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Headless calibration / evaluation harness for the Phase-1 intrinsic formant selector                       *
 * (speech::select_analysis_intrinsic). It runs the REAL scoring code path (speech::score_candidate_intrinsic) used    *
 * by formant queries against a set of tokens with hand-measured "gold" formants (e.g. Hillenbrand et al. 1995) and    *
 * reports token-level error, so we can (a) see whether Phase 1 beats Weenink's W and the fixed-parameter baseline,    *
 * (b) ablate each penalty term, and (c) tune the weights.                                                             *
 *                                                                                                                     *
 * Performance: for each token we sample the LPC tracks ONCE per <ceiling, order> candidate, cache each candidate's    *
 * scaled term breakdown (CandidateScore) and its midpoint formants, then compute the fixed midpoint formants for the  *
 * Weenink and fixed-baseline references. After this one-time build, evaluation / ablation / tuning are pure           *
 * arithmetic over the cache (re-weight + argmin), so --tune is fast. This mirrors the Phase-2 candidate-cache design.  *
 *                                                                                                                     *
 * Manifest (one token per non-comment line, TAB- or whitespace-separated; build for H95 with build_manifest.py):      *
 *     wav_path   t_start   t_end   goldF1   goldF2   goldF3   vowel   class                                           *
 * t_start/t_end are the vowel boundaries in seconds; gold formants in Hz (0 = missing); class in {man,woman,boy,girl} *
 * (picks a per-class ceiling range and nominal formant spacing). Formants are measured at the vowel midpoint.         *
 *                                                                                                                     *
 * Build (headless; PHON_ENGINE_DIR points at the scripting-engine repository, default ../engine):                     *
 *   cmake -B build-cal -DWITH_GUI=OFF -DWITH_APPLICATION=ON -DWITH_WHISPER=OFF -DPHON_BUILD_DOCS=OFF \                 *
 *         -DBUILD_CALIBRATION=ON -DCMAKE_BUILD_TYPE=Release .                                                          *
 *   cmake --build build-cal --target calibrate_formants                                                               *
 * Run:                                                                                                                *
 *   ./calibrate_formants manifest.tsv                 # evaluate + ablate + baselines with default weights            *
 *   ./calibrate_formants manifest.tsv --tune          # + coordinate-descent tuning of the lambdas                    *
 *   ./calibrate_formants manifest.tsv --sample 300    # cap tokens (stratified by vowel) for a quick pass             *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <algorithm>
#include <array>
#include <set>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <phon/runtime.hpp>
#include <phon/application/project.hpp>
#include <phon/application/sound.hpp>
#include <phon/analysis/formant_selection.hpp>
#include <phon/analysis/speech_utils.hpp>
#include <phon/analysis/weenink.hpp>

using namespace phonometrica;

namespace {

struct Token {
	std::string wav;
	double t1 = 0, t2 = 0;
	double gold[3] = {0, 0, 0};
	std::string vowel, cls, speaker;
};

// Per-speaker-class analysis settings. Ceiling ranges follow Fast Track / Weenink practice; nominal formant spacing
// (~ c / 2L) scales with vocal-tract length so the density prior expects the right pole count. Revisit after numbers.
struct ClassCfg { double c_lo, c_hi, spacing; };

static ClassCfg class_cfg(const std::string &cls)
{
	if (cls == "man")   return {4000, 6000, 1000};
	if (cls == "woman") return {4500, 6500, 1150};
	return {5000, 7000, 1250}; // boys / girls: shorter tracts
}

constexpr int    NFORMANT  = 3;
constexpr double WIN_SIZE  = 0.025;
double g_freq_step = 250.0;   // ceiling step (Hz); override with --freq-step
int    g_order_lo  = 8;       // LPC order sweep low;  override with --order-lo
int    g_order_hi  = 14;      // LPC order sweep high; override with --order-hi (set == lo to fix the count)
constexpr int    CHANNEL   = 1;
#define MIN_POINTS 8
#define MAX_SELECT_POINTS 16
int g_select_points = MAX_SELECT_POINTS; // selection sampling cap; override with --select-points
int g_measure_pts   = 1;                 // frames averaged for the measurement; override with --measure-pts

// One cached candidate: its scaled term breakdown (independent of the lambdas) and its midpoint formants.
struct CachedCand {
	speech::CandidateScore sc;
	double f[3] = {std::nan(""), std::nan(""), std::nan("")};
	double local[3] = {std::nan(""), std::nan(""), std::nan("")}; // per-formant local badness (roughness + bandwidth)
	double heur = 0.0; // Fast Track-style coarse plausibility-heuristic violations (count)
	std::vector<double> poles; // ALL pole frequencies at the measurement point (for the persistence probe)
};

// Everything precomputed for one token, so re-scoring under new weights needs no LPC work.
struct CachedToken {
	const Token *tk = nullptr;
	std::vector<CachedCand> cands;   // intrinsic candidates over the <ceiling, order> grid
	double weenink_f[3] = {std::nan(""), std::nan(""), std::nan("")};
	double base_f[3]    = {std::nan(""), std::nan(""), std::nan("")};
};

struct ErrAcc {
	double sum_abs[3] = {0, 0, 0};
	long   n[3] = {0, 0, 0}, within5[3] = {0, 0, 0};
	void add(int k, double pred, double gold) {
		if (gold <= 0 || std::isnan(pred)) return;
		double e = std::abs(pred - gold);
		sum_abs[k] += e; n[k] += 1;
		if (e <= 0.05 * gold) within5[k] += 1;
	}
	double mae(int k)  const { return n[k] ? sum_abs[k] / n[k] : 0.0; }
	double pct5(int k) const { return n[k] ? 100.0 * within5[k] / n[k] : 0.0; }
	double mae_all()   const { double s=0; long m=0; for (int k=0;k<3;++k){s+=sum_abs[k];m+=n[k];} return m?s/m:0.0; }
	double pct5_all()  const { long h=0,m=0; for (int k=0;k<3;++k){h+=within5[k];m+=n[k];} return m?100.0*h/m:0.0; }
};

// Intrinsic (single-token) badness of one candidate under the given weights.
static inline double intrinsic_badness(const speech::CandidateScore &s, const speech::IntrinsicWeights &w)
{
	return w.lambda_R * s.R + w.lambda_U * s.U + w.lambda_Cov * s.Cov
	     + w.lambda_D * s.D + w.lambda_Bd * s.Bd + w.lambda_Cln * s.Cln + s.O;
}

// Pick the best cached intrinsic candidate under the given weights and return its midpoint formants.
static const double *
select_cached(const CachedToken &ct, const speech::IntrinsicWeights &w)
{
	const CachedCand *best = nullptr;
	double best_b = (std::numeric_limits<double>::max)();
	for (auto &c : ct.cands) {
		double b = intrinsic_badness(c.sc, w);
		if (b < best_b) { best_b = b; best = &c; }
	}
	return best ? best->f : nullptr;
}

// Build the cache for one token: sample tracks once per candidate, score, and record midpoint formants; plus the
// Weenink and fixed-baseline midpoint formants.
// Measure F1..F3 for a candidate: a single midpoint frame (g_measure_pts==1) or the mean over g_measure_pts frames
// spanning the central 40-60% of [t1,t2] (the steady plateau). Averaging reduces measurement variance under noise,
// mirroring Fast Track's aggregation over its 40-50% bin. NaN frames are skipped per formant.
static bool measure_formants(Sound &snd, double t1, double t2, double nyq, int order, double out[3])
{
	for (int k=0;k<3;++k) out[k] = std::nan("");
	int m = g_measure_pts < 1 ? 1 : g_measure_pts;
	double a = t1 + 0.40*(t2-t1), b = t1 + 0.60*(t2-t1);
	std::vector<double> sum(3,0.0); std::vector<int> cnt(3,0);
	for (int i=0;i<m;++i) {
		double t = (m==1) ? 0.5*(t1+t2) : a + (b-a)*i/(m-1);
		Array<double> fm;
		try { fm = snd.get_formants(CHANNEL, t, NFORMANT, nyq, WIN_SIZE, order); }
		catch (...) { continue; }
		for (int k=0;k<3;++k) { double v = fm(k,0); if(!std::isnan(v)){ sum[k]+=v; cnt[k]++; } }
	}
	bool any=false;
	for (int k=0;k<3;++k) if (cnt[k]>0){ out[k]=sum[k]/cnt[k]; any=true; }
	return any;
}

static double finite_median(std::vector<double> &v); // defined below
static std::string cell_key(const Token &t);          // defined below

static bool build_token_cache(CachedToken &ct)
{
	const Token &tk = *ct.tk;
	auto cfg = class_cfg(tk.cls);
	double t_mid = 0.5 * (tk.t1 + tk.t2);

	Sound snd(nullptr, String(tk.wav.c_str()));

	int npoint = int(1000 * (tk.t2 - tk.t1) / 5);
	if (npoint < MIN_POINTS) npoint = MIN_POINTS;
	if (npoint > g_select_points) npoint = g_select_points; // selection sampling cap (configurable)
	auto times = speech::linspace(tk.t1, tk.t2, npoint, false);

	speech::IntrinsicWeights w;             // weights irrelevant to the cached breakdown, but spacing feeds D
	w.nominal_spacing = cfg.spacing;

	Matrix<double> F(npoint, NFORMANT), B(npoint, NFORMANT);

	for (double nyq = cfg.c_lo; nyq <= cfg.c_hi; nyq += g_freq_step) {
		for (int order = g_order_lo; order <= g_order_hi; ++order) {
			F.setZero(npoint, NFORMANT); B.setZero(npoint, NFORMANT);
			intptr_t i = 0;
			bool edge = false;
			for (double t : times) {
				Array<double> ff;
				try { ff = snd.get_formants(CHANNEL, t, NFORMANT, nyq, WIN_SIZE, order); }
				catch (...) { edge = true; break; } // window ran off the file edge
				for (intptr_t j = 0; j < NFORMANT; ++j) { F(i, j) = ff(j, 0); B(i, j) = ff(j, 1); }
				++i;
			}
			if (edge) continue;

			auto sc = speech::score_candidate_intrinsic(F, B, nyq, order, w);
			if (!sc.valid) continue;

			CachedCand cc; cc.sc = sc;
			// Fast Track coarse plausibility heuristics (ruling-out constraints, robust to speaker size). Each violation
			// adds to a penalty combined with the ladder badness at selection time.
			{
				auto colmed = [&](int k){ std::vector<double> v; for (int r=0;r<npoint;++r) if(!std::isnan(F(r,k))) v.push_back(F(r,k)); return finite_median(v); };
				auto colmedB = [&](int k){ std::vector<double> v; for (int r=0;r<npoint;++r) if(!std::isnan(B(r,k))) v.push_back(std::abs(B(r,k))); return finite_median(v); };
				double mF1=colmed(0), mF2=colmed(1), mF3=colmed(2);
				double mB1=colmedB(0), mB2=colmedB(1), mB3=colmedB(2);
				double h=0;
				if (!std::isnan(mF1) && mF1 > 1200) h += 1;
				if (!std::isnan(mB1) && mB1 > 500) h += 1;
				if (!std::isnan(mB2) && mB2 > 600) h += 1;
				if (!std::isnan(mB3) && mB3 > 900) h += 1;
				if (!std::isnan(mF3) && !std::isnan(mF2) && !std::isnan(mF1) && mF3 < 2000 && (mF2 - mF1) <= 500) h += 1; // rhotic
				cc.heur = h;
			}
			// Per-formant local badness: score EACH formant on its own roughness + bandwidth, so a later per-formant
			// selector can pick F3 from the analysis where F3 looks best, independent of F1/F2.
			{
				auto model = speech::model_segment(F, B, 4);
				if (model.success) {
					for (int k = 0; k < 3; ++k) {
						double num = 0; int den = 0;
						for (int r = 0; r < npoint; ++r) {
							if (std::isnan(F(r, k))) continue;
							double t = (npoint > 1) ? (-1.0 + 2.0 * r / (npoint - 1)) : 0.0;
							double fhat = model.predict(t, (unsigned) k);
							double denom = std::max(fhat, 200.0);
							double rel = (F(r, k) - fhat) / denom;
							num += rel * rel; ++den;
						}
						double rough = den > 0 ? std::sqrt(num / den) : 1.0;
						std::vector<double> bw; double fsum = 0; int fn = 0;
						for (int r = 0; r < npoint; ++r) { bw.push_back(B(r, k)); if (!std::isnan(F(r, k))) { fsum += F(r, k); ++fn; } }
						double medbw = finite_median(bw);
						double bd = 0;
						if (!std::isnan(medbw) && fn > 0 && medbw > 0) {
							double bhat = std::max(30.0, 0.05 * (fsum / fn) + 40.0);
							double l = std::log(medbw / bhat); bd = l * l;
						}
						cc.local[k] = rough / 0.05 + bd;
					}
				}
			}
			if (!measure_formants(snd, tk.t1, tk.t2, nyq, order, cc.f)) continue;
			// full pole set at the midpoint (request many formants; keep the ones the model resolves below the ceiling)
			try {
				double tm = 0.5 * (tk.t1 + tk.t2);
				auto pp = snd.get_formants(CHANNEL, tm, 8, nyq, WIN_SIZE, order);
				for (intptr_t j = 0; j < 8; ++j) { double f = pp(j, 0); if (!std::isnan(f) && f > 0) cc.poles.push_back(f); }
			} catch (...) {}
			ct.cands.push_back(cc);
		}
	}
	if (ct.cands.empty()) return false;

	// Weenink reference (its own smoothness-only selection), measured at the midpoint.
	try {
		auto p = speech::find_lpc_parameters(&snd, CHANNEL, NFORMANT, WIN_SIZE, tk.t1, tk.t2,
		                                     cfg.c_lo, cfg.c_hi, g_freq_step, g_order_lo, g_order_hi);
		if (p.first > 0) {
			auto fm = snd.get_formants(CHANNEL, t_mid, NFORMANT, p.first, WIN_SIZE, (int) p.second);
			for (int k = 0; k < 3; ++k) ct.weenink_f[k] = fm(k, 0);
		}
	} catch (...) {}

	// Fixed baseline (5000 men / 5500 else, order 10).
	try {
		double c = (tk.cls == "man") ? 5000.0 : 5500.0;
		auto fm = snd.get_formants(CHANNEL, t_mid, NFORMANT, c, WIN_SIZE, 10);
		for (int k = 0; k < 3; ++k) ct.base_f[k] = fm(k, 0);
	} catch (...) {}

	return true;
}

enum class Ref { Intrinsic, Weenink, Baseline };

static void
evaluate(const std::vector<CachedToken> &cache, Ref ref, const speech::IntrinsicWeights &w,
         ErrAcc &overall, std::map<std::string, ErrAcc> *by_vowel, std::map<std::string, ErrAcc> *by_class)
{
	for (auto &ct : cache) {
		const double *f = nullptr;
		if      (ref == Ref::Intrinsic) f = select_cached(ct, w);
		else if (ref == Ref::Weenink)   f = ct.weenink_f;
		else                            f = ct.base_f;
		if (!f) continue;
		for (int k = 0; k < 3; ++k) {
			overall.add(k, f[k], ct.tk->gold[k]);
			if (by_vowel) (*by_vowel)[ct.tk->vowel].add(k, f[k], ct.tk->gold[k]);
			if (by_class) (*by_class)[ct.tk->cls].add(k, f[k], ct.tk->gold[k]);
		}
	}
}

static double score_intrinsic(const std::vector<CachedToken> &cache, const speech::IntrinsicWeights &w)
{
	ErrAcc o; evaluate(cache, Ref::Intrinsic, w, o, nullptr, nullptr); return o.mae_all();
}

// Oracle diagnostic: best MAE achievable over the candidate grid, to separate selection from generation failures.
//   actual = what the intrinsic selector picks.
//   whole  = the single candidate per token minimizing total error (all formants from one analysis) -> ceiling for
//            any whole-analysis rule. Near actual => grid-limited (richer grid / Module V); far below => a better
//            selection term could help.
//   perfmt = per formant, the best candidate for that formant (formants combined across analyses) -> ceiling for
//            per-formant selection (Fast Track's cross-analysis trick). Far below whole => per-formant selection wins.
static void run_oracle(const std::vector<CachedToken> &cache, const speech::IntrinsicWeights &w)
{
	ErrAcc actual, whole, perf;
	std::map<std::string, ErrAcc> av, wv, pv;

	for (auto &ct : cache) {
		const double *gold = ct.tk->gold;
		const double *fa = select_cached(ct, w);

		const CachedCand *bw = nullptr;
		double bwe = (std::numeric_limits<double>::max)();
		for (auto &c : ct.cands) {
			double e = 0; int nn = 0;
			for (int k = 0; k < 3; ++k)
				if (gold[k] > 0 && !std::isnan(c.f[k])) { e += std::abs(c.f[k] - gold[k]); ++nn; }
			if (nn > 0) { e /= nn; if (e < bwe) { bwe = e; bw = &c; } }
		}
		double pf[3] = { std::nan(""), std::nan(""), std::nan("") };
		for (int k = 0; k < 3; ++k) {
			double be = (std::numeric_limits<double>::max)();
			for (auto &c : ct.cands)
				if (gold[k] > 0 && !std::isnan(c.f[k])) { double e = std::abs(c.f[k] - gold[k]); if (e < be) { be = e; pf[k] = c.f[k]; } }
		}

		for (int k = 0; k < 3; ++k) {
			if (fa) { actual.add(k, fa[k], gold[k]); av[ct.tk->vowel].add(k, fa[k], gold[k]); }
			if (bw) { whole.add(k, bw->f[k], gold[k]); wv[ct.tk->vowel].add(k, bw->f[k], gold[k]); }
			perf.add(k, pf[k], gold[k]); pv[ct.tk->vowel].add(k, pf[k], gold[k]);
		}
	}

	std::printf("\n===== Oracle (best achievable over the candidate grid) =====\n");
	std::printf("%-6s %20s %20s %20s\n", "", "actual", "whole-orc", "perfmt-orc");
	std::printf("%-6s %20.1f %20.1f %20.1f\n", "ALL", actual.mae_all(), whole.mae_all(), perf.mae_all());
	std::printf("  by vowel  (overall MAE, F3 MAE in parens):\n");
	for (auto &kv : av) {
		const std::string &v = kv.first;
		std::printf("    %-4s  %8.1f (%6.1f) %8.1f (%6.1f) %8.1f (%6.1f)\n",
		            v.c_str(), av[v].mae_all(), av[v].mae(2), wv[v].mae_all(), wv[v].mae(2), pv[v].mae_all(), pv[v].mae(2));
	}
}

// Per-formant selection: instead of one candidate per token, choose each formant's analysis separately, using
// TRUTH-FREE criteria (unlike the oracle). Two rules:
//   local    : for formant k, pick the candidate whose k-th formant looks best on its own (roughness + bandwidth).
//   local+S  : EM; pick argmin(local[k] + lambda * ERB-distance from f[k] to the (speaker x vowel) cell centre for k).
// Reports overall + per-vowel (F3 in parens) so we can see how much of the whole->perfmt oracle gap is recovered.
// Constrained per-formant F3 re-selection. Keep F1/F2 from the whole-analysis winner (they are already competitive),
// and re-pick ONLY F3 from candidates whose F3 sits above the chosen F2 (ordering constraint). Holding F1/F2 fixed and
// enforcing F3 > F2 is the constraint our earlier unconstrained per-formant attempts lacked -- it is what makes
// per-formant selection stable (this is essentially Fast Track's per-formant winner logic, surgically applied to F3).
static void run_f3select(const std::vector<CachedToken> &cache, const speech::IntrinsicWeights &w)
{
	const int n = (int) cache.size();
	struct Base { double f1 = std::nan(""), f2 = std::nan(""), f3 = std::nan(""); };
	std::vector<Base> base(n);
	for (int i = 0; i < n; ++i) {
		const double *f = select_cached(cache[i], w);
		if (f) { base[i].f1 = f[0]; base[i].f2 = f[1]; base[i].f3 = f[2]; }
	}

	auto report = [&](const char *title, const std::vector<double> &f3) {
		ErrAcc o; std::map<std::string, ErrAcc> vw;
		for (int i = 0; i < n; ++i) {
			o.add(0, base[i].f1, cache[i].tk->gold[0]);
			o.add(1, base[i].f2, cache[i].tk->gold[1]);
			o.add(2, f3[i],       cache[i].tk->gold[2]);
			vw[cache[i].tk->vowel].add(2, f3[i], cache[i].tk->gold[2]);
		}
		std::printf("\n===== %s =====\n", title);
		std::printf("Overall  MAE=%6.1f Hz  within5%%=%5.1f%%   (F1 %5.1f  F2 %5.1f  F3 %5.1f)\n",
		            o.mae_all(), o.pct5_all(), o.mae(0), o.mae(1), o.mae(2));
		for (auto &kv : vw)
			std::printf("    %-4s  F3 MAE=%7.1f\n", kv.first.c_str(), kv.second.mae(2));
	};

	// (0) baseline: F3 from the whole-analysis winner (what we ship now)
	{ std::vector<double> f3(n); for (int i=0;i<n;++i) f3[i]=base[i].f3; report("F3 = whole-analysis winner (current)", f3); }

	// (1) roughness: among candidates with F3 > selected F2, the smoothest F3 track
	{
		std::vector<double> f3(n);
		for (int i=0;i<n;++i) {
			double best=(std::numeric_limits<double>::max)(), bf=base[i].f3;
			for (auto &c : cache[i].cands)
				if (!std::isnan(c.f[2]) && !std::isnan(base[i].f2) && c.f[2] > base[i].f2 && !std::isnan(c.local[2]) && c.local[2] < best)
					{ best=c.local[2]; bf=c.f[2]; }
			f3[i]=bf;
		}
		report("F3 re-select: min F3 roughness (F3 > F2)", f3);
	}

	// (2) consensus (EM): among candidates with F3 > F2, the F3 closest to the (speaker x vowel) cell F3 centre
	// (3) roughness + consensus: local[2] + lambda * ERB distance to the cell F3 centre
	for (int mode = 0; mode < 2; ++mode) {
		std::map<std::string, std::vector<int>> cells;
		for (int i=0;i<n;++i) cells[cell_key(*cache[i].tk)].push_back(i);
		std::vector<double> f3(n); for (int i=0;i<n;++i) f3[i]=base[i].f3;
		const double lambda = 1.0;
		for (int it=0; it<3; ++it) {
			std::map<std::string,double> mu;
			for (auto &kv : cells) { std::vector<double> v; for (int i:kv.second) v.push_back(f3[i]); mu[kv.first]=finite_median(v); }
			for (int i=0;i<n;++i) {
				double m = mu[cell_key(*cache[i].tk)];
				double best=(std::numeric_limits<double>::max)(), bf=f3[i];
				for (auto &c : cache[i].cands) {
					if (std::isnan(c.f[2]) || std::isnan(base[i].f2) || c.f[2] <= base[i].f2) continue;
					double sc = 0;
					if (mode == 1 && !std::isnan(c.local[2])) sc += c.local[2];
					if (!std::isnan(m)) sc += lambda * std::abs(speech::hertz_to_erb(c.f[2]) - speech::hertz_to_erb(m));
					if (sc < best) { best=sc; bf=c.f[2]; }
				}
				f3[i]=bf;
			}
		}
		report(mode==0 ? "F3 re-select: consensus only (F3 > F2, EM)" : "F3 re-select: roughness + consensus (F3 > F2, EM)", f3);
	}

	// (4) PRIOR-ORACLE: anchor F3 to the TRUE (gold) per-(speaker x vowel) F3 mean -- an external prior that cannot
	// lock in like consensus. If this closes the gap to Fast Track, the diagnosis holds: the F3 error is assignment,
	// resolvable only with a formant-position prior. If it does NOT, no prior-based method (Fast Track's included)
	// could help on this grid. The +roughness tiebreak keeps it from grabbing a noisy pole that merely lands near mu.
	for (int mode = 0; mode < 2; ++mode) {
		std::map<std::string, std::pair<double,int>> acc;
		for (int i=0;i<n;++i) { auto &a = acc[cell_key(*cache[i].tk)]; a.first += cache[i].tk->gold[2]; a.second++; }
		std::map<std::string,double> mu;
		for (auto &kv : acc) mu[kv.first] = kv.second.second ? kv.second.first / kv.second.second : std::nan("");
		std::vector<double> f3(n);
		for (int i=0;i<n;++i) {
			double m = mu[cell_key(*cache[i].tk)];
			double best=(std::numeric_limits<double>::max)(), bf=base[i].f3;
			for (auto &c : cache[i].cands) {
				if (std::isnan(c.f[2]) || std::isnan(base[i].f2) || c.f[2] <= base[i].f2) continue;
				double sc = 0;
				if (mode == 1 && !std::isnan(c.local[2])) sc += 0.3 * c.local[2];
				if (!std::isnan(m)) sc += std::abs(speech::hertz_to_erb(c.f[2]) - speech::hertz_to_erb(m));
				if (sc < best) { best=sc; bf=c.f[2]; }
			}
			f3[i]=bf;
		}
		report(mode==0 ? "F3 re-select: toward TRUE cell mean (prior ORACLE)" : "F3 re-select: TRUE cell mean + roughness (prior ORACLE)", f3);
	}

	// (5) REALISTIC fixed prior: H95 population F3 mean per (class x vowel) -- no per-token/per-speaker truth. Tests
	// whether a rough population-level prior recovers the assignment gain (it only has to pick the right POLE; the
	// value is then the pole's own measured frequency).
	// (class, vowel, F3) triples; the cell key is joined at build time, exactly like cell_key/prior_of. Do NOT
	// inline the separator into one literal: "man\x1fei" parses \x1fe as a single (out-of-range) hex escape and
	// swallows the 'e', so every vowel starting with a hex digit (ei/eh/ae/ah/aw/er) would silently miss.
	struct H95Row { const char *cls, *vowel; double f3; };
	static const H95Row H95_ROWS[] = {
	  {"man","iy",2994}, {"man","ih",2641}, {"man","ei",2727}, {"man","eh",2602}, {"man","ae",2566}, {"man","ah",2523}, {"man","aw",2509}, {"man","oa",2479}, {"man","oo",2438}, {"man","uw",2359}, {"man","uh",2549}, {"man","er",1704},
	  {"woman","iy",3368}, {"woman","ih",3020}, {"woman","ei",3043}, {"woman","eh",2956}, {"woman","ae",2903}, {"woman","ah",2840}, {"woman","aw",2836}, {"woman","oa",2853}, {"woman","oo",2822}, {"woman","uw",2747}, {"woman","uh",2913}, {"woman","er",1933},
	  {"boy","iy",3577}, {"boy","ih",3304}, {"boy","ei",3280}, {"boy","eh",3221}, {"boy","ae",3110}, {"boy","ah",2865}, {"boy","aw",2868}, {"boy","oa",2943}, {"boy","oo",2999}, {"boy","uw",2938}, {"boy","uh",3039}, {"boy","er",2065},
	  {"girl","iy",3797}, {"girl","ih",3477}, {"girl","ei",3369}, {"girl","eh",3374}, {"girl","ae",3254}, {"girl","ah",3022}, {"girl","aw",3056}, {"girl","oa",3121}, {"girl","oo",3162}, {"girl","uw",3047}, {"girl","uh",3211}, {"girl","er",2222},
	};
	static const std::map<std::string,double> H95_F3 = []{
		std::map<std::string,double> m;
		for (auto &r : H95_ROWS) m[std::string(r.cls) + "\x1f" + r.vowel] = r.f3;
		return m;
	}();
	auto prior_of = [&](const Token &t) -> double {
		auto it = H95_F3.find(t.cls + "\x1f" + t.vowel);
		return it == H95_F3.end() ? std::nan("") : it->second;
	};
	{
		std::vector<double> f3(n);
		for (int i=0;i<n;++i) {
			double m = prior_of(*cache[i].tk);
			double best=(std::numeric_limits<double>::max)(), bf=base[i].f3;
			for (auto &c : cache[i].cands) {
				if (std::isnan(c.f[2]) || std::isnan(base[i].f2) || c.f[2] <= base[i].f2) continue;
				double sc = 0.3 * (std::isnan(c.local[2]) ? 0.0 : c.local[2]);
				if (!std::isnan(m)) sc += std::abs(speech::hertz_to_erb(c.f[2]) - speech::hertz_to_erb(m));
				if (sc < best) { best=sc; bf=c.f[2]; }
			}
			f3[i]=bf;
		}
		report("F3 re-select: toward H95 population prior (REALISTIC, no truth)", f3);
	}

	// (6) PRODUCTION mechanism: prior-anchored consensus. The cell centre is a partial pool of the population prior and
	// the speaker's own consensus median -> the prior breaks the systematic lock-in; consensus refines per speaker.
	{
		std::map<std::string, std::vector<int>> cells;
		for (int i=0;i<n;++i) cells[cell_key(*cache[i].tk)].push_back(i);
		std::vector<double> f3(n); for (int i=0;i<n;++i) f3[i]=base[i].f3;
		const double kappa = 3.0; // strength of the prior anchor (in pseudo-tokens)
		for (int it=0; it<3; ++it) {
			std::map<std::string,double> centre;
			for (auto &kv : cells) {
				std::vector<double> v; for (int i:kv.second) v.push_back(f3[i]);
				double med = finite_median(v);
				double pr = prior_of(*cache[kv.second[0]].tk);
				double nn = (double) v.size();
				centre[kv.first] = std::isnan(pr) ? med : (nn*med + kappa*pr) / (nn + kappa);
			}
			for (int i=0;i<n;++i) {
				double m = centre[cell_key(*cache[i].tk)];
				double best=(std::numeric_limits<double>::max)(), bf=f3[i];
				for (auto &c : cache[i].cands) {
					if (std::isnan(c.f[2]) || std::isnan(base[i].f2) || c.f[2] <= base[i].f2) continue;
					double sc = 0.3 * (std::isnan(c.local[2]) ? 0.0 : c.local[2]);
					if (!std::isnan(m)) sc += std::abs(speech::hertz_to_erb(c.f[2]) - speech::hertz_to_erb(m));
					if (sc < best) { best=sc; bf=c.f[2]; }
				}
				f3[i]=bf;
			}
		}
		report("F3 re-select: prior-anchored consensus (PRODUCTION, no truth)", f3);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Persistence probe (tests PEPT's central bet). At the measurement point, pool EVERY pole from EVERY grid cell,
// cluster them in ERB, and score each cluster's persistence = fraction of grid cells that place a pole there. A true
// resonance should recur across order/ceiling (high persistence); a fitting artifact should flicker (low). The
// decisive question: on the vowels where our F3 is systematically wrong, is the TRUE F3 a high-persistence cluster
// while the impostor we pick is not?  And does restricting to persistent clusters still contain the right F3?
// ---------------------------------------------------------------------------------------------------------------------
static void run_persist(const std::vector<CachedToken> &cache, const speech::IntrinsicWeights &w)
{
	struct Cluster { double centre; double persistence; };
	auto erb = [](double hz){ return speech::hertz_to_erb(hz); };
	const double TOL = 1.0;      // cluster tolerance in ERB-rate units
	const double PMIN = 0.5;     // "persistent" threshold for the restricted oracle

	// accumulators, per vowel
	struct Acc { double trueP=0, ourP=0, trueDist=0; ErrAcc perf, persOracle; long n=0; };
	std::map<std::string, Acc> byv;

	for (auto &ct : cache) {
		int ncand = (int) ct.cands.size();
		if (ncand == 0) continue;
		// pool poles: (freq, candidate index)
		std::vector<std::pair<double,int>> pv;
		for (int ci = 0; ci < ncand; ++ci) for (double f : ct.cands[ci].poles) pv.push_back({f, ci});
		if (pv.empty()) continue;
		std::sort(pv.begin(), pv.end());
		// greedy ERB clustering
		std::vector<Cluster> cl;
		std::vector<double> curf; std::set<int> curc; double anchor = pv[0].first;
		auto flush = [&](){ if (curf.empty()) return; std::sort(curf.begin(), curf.end());
			Cluster c; c.centre = curf[curf.size()/2]; c.persistence = (double) curc.size() / ncand; cl.push_back(c); curf.clear(); curc.clear(); };
		for (auto &pr : pv) {
			if (!curf.empty() && erb(pr.first) - erb(anchor) > TOL) { flush(); }
			if (curf.empty()) anchor = pr.first;
			curf.push_back(pr.first); curc.insert(pr.second);
		}
		flush();
		if (cl.empty()) continue;

		double goldF3 = ct.tk->gold[2];
		const double *fsel = select_cached(ct, w);
		double ourF3 = fsel ? fsel[2] : std::nan("");
		double selF2 = fsel ? fsel[1] : std::nan("");

		// cluster nearest true F3, and nearest our (selected) F3
		const Cluster *nearTrue=nullptr, *nearOur=nullptr; double dT=1e18, dO=1e18;
		for (auto &c : cl) {
			double a = std::abs(c.centre - goldF3); if (a < dT) { dT=a; nearTrue=&c; }
			if (!std::isnan(ourF3)) { double b = std::abs(c.centre - ourF3); if (b < dO) { dO=b; nearOur=&c; } }
		}
		// per-formant oracle (nearest cluster to true F3, any persistence) and persistence-restricted oracle
		double bestAll=1e18, bestPers=1e18;
		for (auto &c : cl) {
			if (!std::isnan(selF2) && c.centre <= selF2) continue; // ordering: F3 above F2
			double a = std::abs(c.centre - goldF3);
			if (a < bestAll) bestAll = a;
			if (c.persistence >= PMIN && a < bestPers) bestPers = a;
		}

		Acc &A = byv[ct.tk->vowel];
		if (nearTrue) { A.trueP += nearTrue->persistence; A.trueDist += dT; }
		if (nearOur)  A.ourP  += nearOur->persistence;
		if (bestAll < 1e17)  A.perf.add(2, goldF3 + (bestAll), goldF3);        // encode |err| as MAE contribution
		if (bestPers < 1e17) A.persOracle.add(2, goldF3 + (bestPers), goldF3);
		A.n++;
	}

	std::printf("\n===== Persistence probe (pooled poles across the grid, at the midpoint) =====\n");
	std::printf("%-5s %8s %8s %10s %12s %12s\n", "vowel", "trueP", "ourP", "trueDist", "oracleF3", "persOracleF3");
	std::printf("  (trueP/ourP = persistence of the cluster nearest the true / our-selected F3;\n");
	std::printf("   trueDist = Hz from nearest cluster to true F3; oracleF3 = best cluster above F2;\n");
	std::printf("   persOracleF3 = best cluster above F2 with persistence >= %.2f)\n", PMIN);
	for (auto &kv : byv) {
		Acc &A = kv.second; if (A.n==0) continue;
		std::printf("  %-5s %8.2f %8.2f %10.1f %12.1f %12.1f\n", kv.first.c_str(),
		            A.trueP/A.n, A.ourP/A.n, A.trueDist/A.n, A.perf.mae(2), A.persOracle.mae(2));
	}
}

static void run_heur(const std::vector<CachedToken> &cache, const speech::IntrinsicWeights &w)
{
	// Fast Track-style whole-analysis selection: argmin(intrinsic badness + lambda_H * heuristic violations).
	std::printf("\n===== Baseline: intrinsic ladder (no heuristics) =====\n");
	{ ErrAcc o; for (auto &ct: cache){ const double* f=select_cached(ct,w); if(f) for(int k=0;k<3;++k) o.add(k,f[k],ct.tk->gold[k]); }
	  std::printf("Overall  MAE=%6.1f Hz   F3=%6.1f\n", o.mae_all(), o.mae(2)); }

	for (double lh : {0.5, 1.0, 2.0, 4.0}) {
		ErrAcc o; std::map<std::string, ErrAcc> vw;
		for (auto &ct : cache) {
			const CachedCand *best=nullptr; double bb=(std::numeric_limits<double>::max)();
			for (auto &c : ct.cands) { double b = intrinsic_badness(c.sc, w) + lh * c.heur; if (b<bb){bb=b; best=&c;} }
			if (!best) continue;
			for (int k=0;k<3;++k){ o.add(k,best->f[k],ct.tk->gold[k]); vw[ct.tk->vowel].add(2,best->f[k],ct.tk->gold[k]); }
		}
		std::printf("\n===== Intrinsic + FastTrack heuristics (lambda_H=%.1f) =====\n", lh);
		std::printf("Overall  MAE=%6.1f Hz  within5%%=%5.1f%%   (F1 %5.1f  F2 %5.1f  F3 %5.1f)\n",
		            o.mae_all(), o.pct5_all(), o.mae(0), o.mae(1), o.mae(2));
		for (auto &kv : vw) std::printf("    %-4s  F3 MAE=%7.1f\n", kv.first.c_str(), kv.second.mae(2));
	}
}

static void run_performant(const std::vector<CachedToken> &cache)
{
	const int n = (int) cache.size();

	auto eval_report = [&](const char *title, const std::vector<std::array<double,3>> &cur) {
		ErrAcc o; std::map<std::string, ErrAcc> vw;
		for (int i = 0; i < n; ++i)
			for (int k = 0; k < 3; ++k) { o.add(k, cur[i][k], cache[i].tk->gold[k]); vw[cache[i].tk->vowel].add(k, cur[i][k], cache[i].tk->gold[k]); }
		std::printf("\n===== %s =====\n", title);
		std::printf("Overall     MAE=%6.1f Hz   within5%%=%5.1f%%   (F1 %5.1f  F2 %5.1f  F3 %5.1f)\n",
		            o.mae_all(), o.pct5_all(), o.mae(0), o.mae(1), o.mae(2));
		for (auto &kv : vw)
			std::printf("    %-4s  MAE=%7.1f   F1=%5.1f F2=%5.1f F3=%5.1f\n",
			            kv.first.c_str(), kv.second.mae_all(), kv.second.mae(0), kv.second.mae(1), kv.second.mae(2));
	};

	// ---- rule 1: local-only ----
	std::vector<std::array<double,3>> loc(n);
	for (int i = 0; i < n; ++i)
		for (int k = 0; k < 3; ++k) {
			double best = (std::numeric_limits<double>::max)(); double bf = std::nan("");
			for (auto &c : cache[i].cands)
				if (!std::isnan(c.f[k]) && !std::isnan(c.local[k]) && c.local[k] < best) { best = c.local[k]; bf = c.f[k]; }
			loc[i][k] = bf;
		}
	eval_report("Per-formant local-only", loc);

	// ---- rule 2: local + consensus (EM) ----
	std::map<std::string, std::vector<int>> cells;
	for (int i = 0; i < n; ++i) cells[cell_key(*cache[i].tk)].push_back(i);
	std::vector<std::array<double,3>> cur = loc; // init from local-only
	const double lambda = 1.0;
	for (int it = 0; it < 3; ++it) {
		std::map<std::string, std::array<double,3>> mu;
		for (auto &kv : cells) {
			std::array<double,3> m;
			for (int k = 0; k < 3; ++k) { std::vector<double> v; for (int i : kv.second) v.push_back(cur[i][k]); m[k] = finite_median(v); }
			mu[kv.first] = m;
		}
		for (int i = 0; i < n; ++i) {
			const auto &m = mu[cell_key(*cache[i].tk)];
			for (int k = 0; k < 3; ++k) {
				double best = (std::numeric_limits<double>::max)(); double bf = cur[i][k];
				for (auto &c : cache[i].cands) {
					if (std::isnan(c.f[k]) || std::isnan(c.local[k])) continue;
					double b = c.local[k];
					if (!std::isnan(m[k])) b += lambda * std::abs(speech::hertz_to_erb(c.f[k]) - speech::hertz_to_erb(m[k]));
					if (b < best) { best = b; bf = c.f[k]; }
				}
				cur[i][k] = bf;
			}
		}
	}
	eval_report("Per-formant local + consensus (EM, lambda=1)", cur);
}

// ---------------------------------------------------------------------------------------------------------------------
// Phase 2b prototype: consensus / shrinkage via EM.
//   Cells are (speaker x vowel). Pass 0 = intrinsic selection. Then iterate: estimate a robust cell centre (per-formant
//   median of current selections) and re-select each token, adding an ERB-distance pull toward its cell centre
//   (term S). This is meant to rescue tokens whose formants blew up under noise by pulling them back toward the tight
//   cluster formed by the same speaker's other repetitions of that vowel. Truth is visible here, so we can measure
//   directly whether consensus helps (and check it doesn't lock in a wrong centre) without needing the Cln antidote.
//   NOTE: plain cell median (no partial pooling) is fine for the synthetic set's 10 reps/cell; sparse real cells will
//   need pooling toward speaker/vowel pools (that is the port-to-real-data step, not this prototype).
// ---------------------------------------------------------------------------------------------------------------------
// Median of the finite (non-NaN) values; NaN if none.
static double finite_median(std::vector<double> &v)
{
	v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return std::isnan(x); }), v.end());
	if (v.empty()) return std::nan("");
	auto mid = v.begin() + v.size() / 2;
	std::nth_element(v.begin(), mid, v.end());
	double m = *mid;
	if (v.size() % 2 == 0) { double lo = *std::max_element(v.begin(), mid); m = 0.5 * (lo + m); }
	return m;
}

static std::string cell_key(const Token &t)
{
	return t.speaker + "\x1f" + t.vowel; // unit-separator join; if speaker is empty this degrades to a per-vowel cell
}

static void
run_consensus(const std::vector<CachedToken> &cache, const speech::IntrinsicWeights &w, double lambda_S, int em_iters,
              ErrAcc &overall, std::map<std::string, ErrAcc> *by_vowel, std::map<std::string, ErrAcc> *by_class)
{
	const int n = (int) cache.size();
	std::map<std::string, std::vector<int>> cells;
	for (int i = 0; i < n; ++i) cells[cell_key(*cache[i].tk)].push_back(i);

	// current per-token estimate = midpoint formants of the currently selected candidate; init from intrinsic-only.
	std::vector<std::array<double, 3>> cur(n);
	for (int i = 0; i < n; ++i) {
		const double *f = select_cached(cache[i], w);
		for (int k = 0; k < 3; ++k) cur[i][k] = f ? f[k] : std::nan("");
	}

	for (int it = 0; it < em_iters; ++it) {
		// M-step: robust cell centres (per-formant median of current selections).
		std::map<std::string, std::array<double, 3>> mu;
		for (auto &kv : cells) {
			std::array<double, 3> m;
			for (int k = 0; k < 3; ++k) {
				std::vector<double> v; v.reserve(kv.second.size());
				for (int i : kv.second) v.push_back(cur[i][k]);
				m[k] = finite_median(v);
			}
			mu[kv.first] = m;
		}
		// E-step: re-select each token = argmin(intrinsic badness + lambda_S * ERB distance to cell centre).
		for (int i = 0; i < n; ++i) {
			const auto &m = mu[cell_key(*cache[i].tk)];
			const double *best = nullptr;
			double best_b = (std::numeric_limits<double>::max)();
			for (auto &c : cache[i].cands) {
				double b = intrinsic_badness(c.sc, w);
				for (int k = 0; k < 3; ++k)
					if (!std::isnan(c.f[k]) && !std::isnan(m[k]))
						b += lambda_S * std::abs(speech::hertz_to_erb(c.f[k]) - speech::hertz_to_erb(m[k]));
				if (b < best_b) { best_b = b; best = c.f; }
			}
			for (int k = 0; k < 3; ++k) cur[i][k] = best ? best[k] : std::nan("");
		}
	}

	for (int i = 0; i < n; ++i)
		for (int k = 0; k < 3; ++k) {
			overall.add(k, cur[i][k], cache[i].tk->gold[k]);
			if (by_vowel) (*by_vowel)[cache[i].tk->vowel].add(k, cur[i][k], cache[i].tk->gold[k]);
			if (by_class) (*by_class)[cache[i].tk->cls].add(k, cur[i][k], cache[i].tk->gold[k]);
		}
}

static void print_report(const char *title, ErrAcc &o,
                         std::map<std::string, ErrAcc> &vw, std::map<std::string, ErrAcc> &cl)
{
	std::printf("\n===== %s =====\n", title);
	std::printf("Overall     MAE=%6.1f Hz   within5%%=%5.1f%%   "
	            "(F1 %5.1f/%4.1f  F2 %5.1f/%4.1f  F3 %5.1f/%4.1f)\n",
	            o.mae_all(), o.pct5_all(), o.mae(0), o.pct5(0), o.mae(1), o.pct5(1), o.mae(2), o.pct5(2));
	std::printf("  by class:\n");
	for (auto &p : cl) std::printf("    %-6s  MAE=%6.1f  within5%%=%5.1f%%\n", p.first.c_str(), p.second.mae_all(), p.second.pct5_all());
	std::printf("  by vowel:\n");
	for (auto &p : vw)
		std::printf("    %-4s   MAE=%6.1f  within5%%=%5.1f%%   F1=%5.1f F2=%5.1f F3=%5.1f\n",
		            p.first.c_str(), p.second.mae_all(), p.second.pct5_all(),
		            p.second.mae(0), p.second.mae(1), p.second.mae(2));
}

static void tune(const std::vector<CachedToken> &cache, speech::IntrinsicWeights &w)
{
	double *lam[6] = { &w.lambda_R, &w.lambda_U, &w.lambda_Cov, &w.lambda_D, &w.lambda_Bd, &w.lambda_Cln };
	const char *nm[6] = { "R", "U", "Cov", "D", "Bd", "Cln" };
	const double factors[] = { 0.5, 0.7, 1.0, 1.4, 2.0 };
	if (w.lambda_Cln <= 0.0) w.lambda_Cln = 1.0; // seed so multiplicative descent can explore it
	double best = score_intrinsic(cache, w);
	std::printf("\n[tune] start MAE=%.2f\n", best);
	for (int pass = 0; pass < 4; ++pass) {
		bool improved = false;
		for (int i = 0; i < 6; ++i) {
			double base = *lam[i], best_v = base;
			for (double f : factors) {
				*lam[i] = base * f;
				double s = score_intrinsic(cache, w);
				if (s < best - 1e-6) { best = s; best_v = *lam[i]; improved = true; }
			}
			*lam[i] = best_v;
			std::printf("[tune] pass %d  lambda_%-3s = %.3f   MAE=%.2f\n", pass + 1, nm[i], *lam[i], best);
		}
		if (!improved) break;
	}
	std::printf("\n[tune] final weights:  R=%.3f U=%.3f Cov=%.3f D=%.3f Bd=%.3f Cln=%.3f\n",
	            w.lambda_R, w.lambda_U, w.lambda_Cov, w.lambda_D, w.lambda_Bd, w.lambda_Cln);
}

// Basename of a path without directory or extension (for matching external measurements to manifest tokens).
static std::string base_stem(const std::string &path)
{
	size_t slash = path.find_last_of("/\\");
	std::string b = (slash == std::string::npos) ? path : path.substr(slash + 1);
	size_t dot = b.find_last_of('.');
	if (dot != std::string::npos) b = b.substr(0, dot);
	return b;
}

// Evaluate an external method's per-file formants against the loaded token gold, matched by filename stem. The external
// file has one row per token: <filename> <F1> <F2> <F3> (whitespace/tab/comma separated; a header line is skipped;
// filename may be a full path or bare name). Formants must be measured at the same point as the gold (vowel midpoint).
static void run_external(const std::vector<Token> &tokens, const std::string &path, const char *label)
{
	std::map<std::string, std::array<double,3>> ext;
	std::ifstream in(path);
	if (!in) { std::fprintf(stderr, "cannot open external file: %s\n", path.c_str()); return; }
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		for (char &c : line) if (c == ',' || c == '\t') c = ' ';
		std::istringstream ss(line);
		std::string name; double f1, f2, f3;
		if (!(ss >> name >> f1 >> f2 >> f3)) continue;   // skips a header row automatically
		ext[base_stem(name)] = { f1, f2, f3 };
	}

	ErrAcc o; std::map<std::string, ErrAcc> vw, cl;
	long matched = 0, missing = 0;
	for (auto &t : tokens) {
		auto it = ext.find(base_stem(t.wav));
		if (it == ext.end()) { ++missing; continue; }
		++matched;
		for (int k = 0; k < 3; ++k) {
			o.add(k, it->second[k], t.gold[k]);
			vw[t.vowel].add(k, it->second[k], t.gold[k]);
			cl[t.cls].add(k, it->second[k], t.gold[k]);
		}
	}
	std::printf("\n===== External: %s  (matched %ld / %ld tokens) =====\n", label, matched, matched + missing);
	print_report(label, o, vw, cl);
}

static std::vector<Token> load_manifest(const std::string &path)
{
	std::vector<Token> v;
	std::ifstream in(path);
	if (!in) { std::fprintf(stderr, "cannot open manifest: %s\n", path.c_str()); return v; }
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		std::istringstream ss(line);
		Token t;
		if (!(ss >> t.wav >> t.t1 >> t.t2 >> t.gold[0] >> t.gold[1] >> t.gold[2] >> t.vowel >> t.cls)) continue;
		ss >> t.speaker; // optional 9th field; empty if absent (e.g. H95). Needed for consensus cells.
		v.push_back(std::move(t));
	}
	return v;
}

// Keep at most `cap` tokens per vowel (stratified subsample), for a quick pass.
static void subsample(std::vector<Token> &v, int cap)
{
	std::map<std::string, int> seen;
	std::vector<Token> out;
	for (auto &t : v) if (seen[t.vowel]++ < cap) out.push_back(t);
	v.swap(out);
}

} // namespace

// =====================================================================================================================
// DIPHTHONG (multi-point) evaluation. Reads a 16-column manifest with 3-point gold (F1-F3 at 25/50/75% of the nucleus),
// reuses the intrinsic ladder for selection (it already scores the glide trajectory), and measures the selected
// analysis at 25/50/75%. Reports MAE per point, per formant, and per diphthong for baseline / Weenink / ours / external.
// =====================================================================================================================
namespace {

struct DiphToken { std::string wav, diph, cls, speaker, snr; double t1=0, t2=0; double gold[3][3]; };
struct DiphCand  { speech::CandidateScore sc; double f[3][3]; };
struct DiphCached { const DiphToken *tk=nullptr; std::vector<DiphCand> cands; double ween[3][3]; double base[3][3]; };

struct DiphErr {
	double sum[3][3] = {{0}}; long n[3][3] = {{0}};
	void add(int p, int k, double pred, double g) { if (!std::isnan(pred) && !std::isnan(g)) { sum[p][k] += std::abs(pred - g); n[p][k]++; } }
	double mae_pt(int p)  const { double s=0; long m=0; for (int k=0;k<3;++k){s+=sum[p][k];m+=n[p][k];} return m?s/m:0; }
	double mae_fmt(int k) const { double s=0; long m=0; for (int p=0;p<3;++p){s+=sum[p][k];m+=n[p][k];} return m?s/m:0; }
	double mae_all()      const { double s=0; long m=0; for (int p=0;p<3;++p) for (int k=0;k<3;++k){s+=sum[p][k];m+=n[p][k];} return m?s/m:0; }
};

static std::vector<DiphToken> load_diph_manifest(const std::string &path)
{
	std::vector<DiphToken> out; std::ifstream in(path); std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		std::istringstream ss(line); DiphToken t; double g[9];
		if (!(ss >> t.wav >> t.t1 >> t.t2 >> g[0]>>g[1]>>g[2]>>g[3]>>g[4]>>g[5]>>g[6]>>g[7]>>g[8]
		         >> t.diph >> t.cls >> t.speaker >> t.snr)) continue;
		for (int pt=0; pt<3; ++pt) for (int k=0;k<3;++k) t.gold[pt][k] = g[pt*3+k];
		out.push_back(t);
	}
	return out;
}

static void measure_at(Sound &snd, double t, double nyq, int order, double out[3])
{
	for (int k=0;k<3;++k) out[k] = std::nan("");
	try { auto fm = snd.get_formants(CHANNEL, t, NFORMANT, nyq, WIN_SIZE, order);
	      for (int k=0;k<3;++k) out[k] = fm(k,0); } catch (...) {}
}

static bool build_diph_cache(DiphCached &dc)
{
	const DiphToken &tk = *dc.tk;
	auto cfg = class_cfg(tk.cls);
	double tp[3] = { tk.t1 + 0.25*(tk.t2-tk.t1), tk.t1 + 0.50*(tk.t2-tk.t1), tk.t1 + 0.75*(tk.t2-tk.t1) };
	Sound snd(nullptr, String(tk.wav.c_str()));

	int npoint = int(1000 * (tk.t2 - tk.t1) / 5);
	if (npoint < MIN_POINTS) npoint = MIN_POINTS;
	if (npoint > g_select_points) npoint = g_select_points;
	auto times = speech::linspace(tk.t1, tk.t2, npoint, false);
	speech::IntrinsicWeights w; w.nominal_spacing = cfg.spacing;
	Matrix<double> F(npoint, NFORMANT), B(npoint, NFORMANT);

	for (double nyq = cfg.c_lo; nyq <= cfg.c_hi; nyq += g_freq_step) {
		for (int order = g_order_lo; order <= g_order_hi; ++order) {
			F.setZero(npoint, NFORMANT); B.setZero(npoint, NFORMANT);
			intptr_t i = 0; bool edge = false;
			for (double t : times) {
				Array<double> ff;
				try { ff = snd.get_formants(CHANNEL, t, NFORMANT, nyq, WIN_SIZE, order); }
				catch (...) { edge = true; break; }
				for (intptr_t j = 0; j < NFORMANT; ++j) { F(i,j)=ff(j,0); B(i,j)=ff(j,1); }
				++i;
			}
			if (edge) continue;
			auto sc = speech::score_candidate_intrinsic(F, B, nyq, order, w);
			if (!sc.valid) continue;
			DiphCand cc; cc.sc = sc;
			for (int pt=0; pt<3; ++pt) { double m[3]; measure_at(snd, tp[pt], nyq, order, m); for (int k=0;k<3;++k) cc.f[pt][k]=m[k]; }
			dc.cands.push_back(cc);
		}
	}
	if (dc.cands.empty()) return false;

	for (int pt=0;pt<3;++pt) for (int k=0;k<3;++k) { dc.ween[pt][k]=std::nan(""); dc.base[pt][k]=std::nan(""); }
	try {
		auto p = speech::find_lpc_parameters(&snd, CHANNEL, NFORMANT, WIN_SIZE, tk.t1, tk.t2, cfg.c_lo, cfg.c_hi, g_freq_step, g_order_lo, g_order_hi);
		if (p.first > 0) for (int pt=0;pt<3;++pt) { double m[3]; measure_at(snd, tp[pt], p.first, (int)p.second, m); for (int k=0;k<3;++k) dc.ween[pt][k]=m[k]; }
	} catch (...) {}
	try {
		double c = (tk.cls == "man") ? 5000.0 : 5500.0;
		for (int pt=0;pt<3;++pt) { double m[3]; measure_at(snd, tp[pt], c, 10, m); for (int k=0;k<3;++k) dc.base[pt][k]=m[k]; }
	} catch (...) {}
	return true;
}

static const DiphCand *select_diph(const DiphCached &dc, const speech::IntrinsicWeights &w)
{
	const DiphCand *best = nullptr; double bb = (std::numeric_limits<double>::max)();
	for (auto &c : dc.cands) { double b = intrinsic_badness(c.sc, w); if (b < bb) { bb = b; best = &c; } }
	return best;
}

static void run_diph(const std::string &manifest, const std::string &external_file, bool consensus)
{
	auto tokens = load_diph_manifest(manifest);
	std::printf("loaded %zu diphthong tokens\n", tokens.size());
	std::vector<DiphCached> cache;
	for (auto &t : tokens) { DiphCached dc; dc.tk = &t; if (build_diph_cache(dc)) cache.push_back(std::move(dc)); }
	std::printf("cached %zu (%zu failed to analyse)\n", cache.size(), tokens.size() - cache.size());
	speech::IntrinsicWeights w;

	auto report = [&](const char *title, auto getpred) {
		DiphErr o; std::map<std::string, DiphErr> bd; long matched = 0;
		for (auto &dc : cache) {
			double pr[3][3]; if (!getpred(dc, pr)) continue; ++matched;
			for (int p=0;p<3;++p) for (int k=0;k<3;++k) { o.add(p,k,pr[p][k],dc.tk->gold[p][k]); bd[dc.tk->diph].add(p,k,pr[p][k],dc.tk->gold[p][k]); }
		}
		std::printf("\n===== %s (diphthongs; matched %ld/%zu) =====\n", title, matched, cache.size());
		std::printf("Overall MAE=%6.1f Hz   points[25/50/75]= %.1f / %.1f / %.1f   formants[F1/F2/F3]= %.1f / %.1f / %.1f\n",
		            o.mae_all(), o.mae_pt(0), o.mae_pt(1), o.mae_pt(2), o.mae_fmt(0), o.mae_fmt(1), o.mae_fmt(2));
		for (auto &kv : bd) std::printf("    %-4s MAE=%6.1f  (F1 %.1f  F2 %.1f  F3 %.1f)\n",
		                                kv.first.c_str(), kv.second.mae_all(), kv.second.mae_fmt(0), kv.second.mae_fmt(1), kv.second.mae_fmt(2));
	};

	report("Fixed baseline", [](const DiphCached &dc, double pr[3][3]) { for (int p=0;p<3;++p) for (int k=0;k<3;++k) pr[p][k]=dc.base[p][k]; return true; });
	report("Weenink W (smoothness only)", [](const DiphCached &dc, double pr[3][3]) { for (int p=0;p<3;++p) for (int k=0;k<3;++k) pr[p][k]=dc.ween[p][k]; return true; });
	report("Intrinsic ladder (ours)", [&](const DiphCached &dc, double pr[3][3]) { auto *b = select_diph(dc, w); if (!b) return false; for (int p=0;p<3;++p) for (int k=0;k<3;++k) pr[p][k]=b->f[p][k]; return true; });

	// ---- Trajectory consensus (Phase 2b generalized to glides) -----------------------------------------------------
	// Cells are (speaker x diphthong). The cell centre is a 3-point trajectory (median per point x formant, partially
	// pooled toward the diphthong- and speaker-level trajectories). Each token is re-selected by adding a light pull
	// toward its cell trajectory summed over all 3 points (normalized per point so lambda_S is comparable to the
	// single-point vowel consensus). EM, 3 passes.
	if (consensus) {
		int n = (int) cache.size();
		auto ckey = [&](int i){ return cache[i].tk->speaker + "\x1f" + cache[i].tk->diph; };
		std::vector<std::array<std::array<double,3>,3>> init(n);
		for (int i=0;i<n;++i) { auto *b = select_diph(cache[i], w);
			for (int p=0;p<3;++p) for (int k=0;k<3;++k) init[i][p][k] = b ? b->f[p][k] : std::nan(""); }
		const double kappa = 2.0, kappa2 = 2.0;

		for (double lambda_S : {0.5, 1.0}) {
			auto cur = init;
			for (int it=0; it<3; ++it) {
				std::map<std::string, std::array<std::array<std::vector<double>,3>,3>> cellv, diphv, spkv;
				for (int i=0;i<n;++i) {
					std::string ck=ckey(i), dk=cache[i].tk->diph, sk=cache[i].tk->speaker;
					for (int p=0;p<3;++p) for (int k=0;k<3;++k) { double v=cur[i][p][k]; if (std::isnan(v)) continue;
						cellv[ck][p][k].push_back(v); diphv[dk][p][k].push_back(v); spkv[sk][p][k].push_back(v); }
				}
				for (int i=0;i<n;++i) {
					std::string ck=ckey(i), dk=cache[i].tk->diph, sk=cache[i].tk->speaker;
					double m[3][3];
					for (int p=0;p<3;++p) for (int k=0;k<3;++k) {
						double mc=finite_median(cellv[ck][p][k]), md=finite_median(diphv[dk][p][k]), ms=finite_median(spkv[sk][p][k]);
						double nn=(double)cellv[ck][p][k].size(), num=0, den=0;
						if (!std::isnan(mc)) { num+=nn*mc; den+=nn; }
						if (!std::isnan(md)) { num+=kappa*md; den+=kappa; }
						if (!std::isnan(ms)) { num+=kappa2*ms; den+=kappa2; }
						m[p][k] = den>0 ? num/den : std::nan("");
					}
					const DiphCand *best=nullptr; double bb=(std::numeric_limits<double>::max)();
					for (auto &c : cache[i].cands) {
						double b = intrinsic_badness(c.sc, w), pull=0;
						for (int p=0;p<3;++p) for (int k=0;k<3;++k) { double f=c.f[p][k];
							if (!std::isnan(f) && !std::isnan(m[p][k])) pull += std::abs(speech::hertz_to_erb(f) - speech::hertz_to_erb(m[p][k])); }
						b += (lambda_S/3.0) * pull;
						if (b < bb) { bb=b; best=&c; }
					}
					if (best) for (int p=0;p<3;++p) for (int k=0;k<3;++k) cur[i][p][k]=best->f[p][k];
				}
			}
			DiphErr o; std::map<std::string, DiphErr> bd;
			for (int i=0;i<n;++i) for (int p=0;p<3;++p) for (int k=0;k<3;++k) {
				o.add(p,k,cur[i][p][k],cache[i].tk->gold[p][k]); bd[cache[i].tk->diph].add(p,k,cur[i][p][k],cache[i].tk->gold[p][k]); }
			std::printf("\n===== Intrinsic + trajectory consensus (lambda_S=%.2f) (diphthongs) =====\n", lambda_S);
			std::printf("Overall MAE=%6.1f Hz   points[25/50/75]= %.1f / %.1f / %.1f   formants[F1/F2/F3]= %.1f / %.1f / %.1f\n",
			            o.mae_all(), o.mae_pt(0), o.mae_pt(1), o.mae_pt(2), o.mae_fmt(0), o.mae_fmt(1), o.mae_fmt(2));
			for (auto &kv : bd) std::printf("    %-4s MAE=%6.1f  (F1 %.1f  F2 %.1f  F3 %.1f)\n",
			                                kv.first.c_str(), kv.second.mae_all(), kv.second.mae_fmt(0), kv.second.mae_fmt(1), kv.second.mae_fmt(2));
		}
	}

	if (!external_file.empty()) {
		std::map<std::string, std::array<double,9>> ext;
		std::ifstream in(external_file); std::string line;
		while (std::getline(in, line)) {
			if (line.empty() || line[0]=='#') continue;
			for (char &c : line) if (c==','||c=='\t') c=' ';
			std::istringstream ss(line); std::string name; std::array<double,9> g; bool ok=true;
			if (!(ss >> name)) continue;
			for (int i=0;i<9;++i) if (!(ss >> g[i])) { ok=false; break; }
			if (ok) ext[base_stem(name)] = g;
		}
		report("External: FastTrack", [&](const DiphCached &dc, double pr[3][3]) {
			auto it = ext.find(base_stem(dc.tk->wav)); if (it == ext.end()) return false;
			for (int p=0;p<3;++p) for (int k=0;k<3;++k) pr[p][k] = it->second[p*3+k]; return true; });
	}
}

} // namespace (diphthong)

int main(int argc, char **argv)
{
	if (argc < 2) { std::fprintf(stderr, "usage: %s manifest.tsv [--tune] [--consensus] [--oracle] [--performant] [--f3] [--heur] [--persist] [--order-lo N] [--order-hi N] [--freq-step HZ] [--select-points N] [--measure-pts N] [--sample N]\n", argv[0]); return 1; }
	std::string manifest = argv[1];
	bool do_tune = false; int sample = 0; bool do_consensus = false; std::string external_file, external_label = "external"; bool do_oracle = false; bool do_perf = false; bool do_f3 = false; bool do_heur = false; bool do_diph = false; bool do_persist = false;
	for (int i = 2; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--tune")) do_tune = true;
		else if (!std::strcmp(argv[i], "--consensus")) do_consensus = true;
		else if (!std::strcmp(argv[i], "--external") && i + 1 < argc) external_file = argv[++i];
		else if (!std::strcmp(argv[i], "--label") && i + 1 < argc) external_label = argv[++i];
		else if (!std::strcmp(argv[i], "--oracle")) do_oracle = true;
		else if (!std::strcmp(argv[i], "--performant")) do_perf = true;
		else if (!std::strcmp(argv[i], "--f3")) do_f3 = true;
		else if (!std::strcmp(argv[i], "--heur")) do_heur = true;
		else if (!std::strcmp(argv[i], "--diph")) do_diph = true;
		else if (!std::strcmp(argv[i], "--persist")) do_persist = true;
		else if (!std::strcmp(argv[i], "--order-lo") && i + 1 < argc) g_order_lo = std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--order-hi") && i + 1 < argc) g_order_hi = std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--freq-step") && i + 1 < argc) g_freq_step = std::atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--select-points") && i + 1 < argc) g_select_points = std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--measure-pts") && i + 1 < argc) g_measure_pts = std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--sample") && i + 1 < argc) sample = std::atoi(argv[++i]);
	}

	// New engine (roadmap A1): the Runtime is default-constructed; it no longer takes argv[0].
	Runtime rt;

	if (do_diph) { run_diph(manifest, external_file, do_consensus); return 0; }
	Project::preinitialize(rt);
	Sound::set_sound_formats();

	std::printf("[grid] LPC order %d..%d, ceiling step %.0f Hz, %d formants; select-points %d, measure-pts %d\n", g_order_lo, g_order_hi, g_freq_step, NFORMANT, g_select_points, g_measure_pts);
	auto tokens = load_manifest(manifest);
	if (tokens.empty()) { std::fprintf(stderr, "no tokens loaded\n"); return 1; }
	if (sample > 0) subsample(tokens, sample);
	std::printf("loaded %zu tokens from %s\n", tokens.size(), manifest.c_str());

	// One-time cache build (the only LPC-heavy phase).
	std::vector<CachedToken> cache;
	cache.reserve(tokens.size());
	long failed = 0;
	for (auto &t : tokens) {
		CachedToken ct; ct.tk = &t;
		try {
			if (build_token_cache(ct)) cache.push_back(std::move(ct));
			else ++failed;
		} catch (...) { ++failed; }
	}
	std::printf("cached %zu tokens (%ld failed to analyse)\n", cache.size(), failed);

	speech::IntrinsicWeights w; // all lambdas = 1

	{ ErrAcc o; std::map<std::string, ErrAcc> vw, cl; evaluate(cache, Ref::Baseline,  w, o, &vw, &cl); print_report("Fixed baseline (5000 men / 5500 else, order 10)", o, vw, cl); }
	{ ErrAcc o; std::map<std::string, ErrAcc> vw, cl; evaluate(cache, Ref::Weenink,   w, o, &vw, &cl); print_report("Weenink W (smoothness only)", o, vw, cl); }
	{ ErrAcc o; std::map<std::string, ErrAcc> vw, cl; evaluate(cache, Ref::Intrinsic, w, o, &vw, &cl); print_report("Intrinsic ladder (default weights = 1, Cln off)", o, vw, cl); }

	if (!external_file.empty()) run_external(tokens, external_file, external_label.c_str());

	if (do_oracle) run_oracle(cache, w);
	if (do_perf) run_performant(cache);
	if (do_f3) run_f3select(cache, w);
	if (do_heur) run_heur(cache, w);
	if (do_persist) run_persist(cache, w);

	// Phase 2b: consensus / shrinkage. Sweep a few lambda_S so we can see whether pulling toward the speaker x vowel
	// cluster rescues the noise-blown tokens (big win expected at low SNR) without hurting the clean condition.
	if (do_consensus) {
		int has_speaker = 0;
		for (auto &ct : cache) if (!ct.tk->speaker.empty()) { has_speaker = 1; break; }
		if (!has_speaker)
			std::printf("\n[consensus] WARNING: manifest has no speaker column; cells degrade to per-vowel.\n");
		for (double lambda_S : {0.5, 1.0, 2.0}) {
			ErrAcc o; std::map<std::string, ErrAcc> vw, cl;
			run_consensus(cache, w, lambda_S, 3, o, &vw, &cl);
			char title[96];
			std::snprintf(title, sizeof title, "Intrinsic + consensus (S, lambda_S=%.1f, EM=3)", lambda_S);
			print_report(title, o, vw, cl);
		}
	}

	// Phase 2a: does the physical-cleanliness term help the residual merge cases (iy / er)?
	{ speech::IntrinsicWeights wc = w; wc.lambda_Cln = 1.0;
	  ErrAcc o; std::map<std::string, ErrAcc> vw, cl; evaluate(cache, Ref::Intrinsic, wc, o, &vw, &cl);
	  print_report("Intrinsic + Cln (lambda_Cln = 1)", o, vw, cl); }

	// Ablation: zero one lambda at a time (higher MAE => the term was helping).
	std::printf("\n===== Ablation (drop one term) =====\n");
	{
		speech::IntrinsicWeights wa = w; if (wa.lambda_Cln <= 0.0) wa.lambda_Cln = 1.0;
		double *lam[6] = { &wa.lambda_R, &wa.lambda_U, &wa.lambda_Cov, &wa.lambda_D, &wa.lambda_Bd, &wa.lambda_Cln };
		const char *nm[6] = { "R", "U", "Cov", "D", "Bd", "Cln" };
		double full = score_intrinsic(cache, wa);
		std::printf("  full ladder (Cln on)   MAE=%.2f\n", full);
		for (int i = 0; i < 6; ++i) {
			double save = *lam[i]; *lam[i] = 0.0;
			double m = score_intrinsic(cache, wa);
			std::printf("  without %-3s            MAE=%.2f  (%+.2f)\n", nm[i], m, m - full);
			*lam[i] = save;
		}
	}

	if (do_tune) {
		tune(cache, w);
		ErrAcc o; std::map<std::string, ErrAcc> vw, cl;
		evaluate(cache, Ref::Intrinsic, w, o, &vw, &cl);
		print_report("Intrinsic ladder (tuned weights)", o, vw, cl);
	}

	return 0;
}
