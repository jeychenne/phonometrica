/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 23/09/2026                                                                                                 *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <algorithm>
#include <complex>
#include <limits>
#include <cstring>
#include <phon/analysis/formant_spectral.hpp>
#include <phon/third_party/pocketfft-cpp/pocketfft_hdronly.h>

namespace phonometrica::speech {

static double erb(double f)
{
	return 21.4 * std::log10(0.00437 * (std::max)(f, 0.0) + 1.0);
}

static long next_pow2(long n)
{
	long p = 1;
	while (p < n) p <<= 1;
	return p;
}

SpectralEnvelope
compute_spectral_envelope(const double *samples, long n, double sample_rate, double max_freq, const SpectralWeights &w)
{
	SpectralEnvelope env;
	if (!samples || n < 32 || sample_rate <= 0.0) return env;

	// Zero-pad generously: the envelope is read at peak resolution, and interpolating a coarse grid would quantise
	// peak frequencies into the same bin for neighbouring candidates.
	const long nfft = next_pow2(n * 4);
	const long nbin = nfft / 2 + 1;

	// Pre-emphasis, then a Hann taper. The pre-emphasis flattens the source tilt so a peak of the observed spectrum
	// coincides with a resonance of the vocal tract rather than sitting below it; the taper keeps rectangular-window
	// sidelobes from manufacturing peaks of their own.
	std::vector<double> pre((size_t) n);
	std::memcpy(pre.data(), samples, (size_t) n * sizeof(double));
	if (w.pre_emphasis_hz > 0.0) {
		const double alpha = std::exp(-2.0 * M_PI * w.pre_emphasis_hz / sample_rate);
		for (long i = n - 1; i >= 1; --i) pre[(size_t) i] -= alpha * pre[(size_t) (i - 1)];
	}

	std::vector<double> buf((size_t) nfft, 0.0);
	for (long i = 0; i < n; ++i) {
		double win = 0.5 * (1.0 - std::cos(2.0 * M_PI * double(i) / double(n - 1)));
		buf[(size_t) i] = pre[(size_t) i] * win;
	}

	pocketfft::shape_t shape{ (size_t) nfft };
	pocketfft::stride_t stride_r{ (ptrdiff_t) sizeof(double) };
	pocketfft::stride_t stride_c{ (ptrdiff_t) sizeof(std::complex<double>) };

	std::vector<std::complex<double>> spec((size_t) nbin);
	pocketfft::r2c(shape, stride_r, stride_c, { 0 }, true, buf.data(), spec.data(), 1.0);

	// Log-magnitude spectrum, floored well below anything audible so that silent bins cannot dominate the cepstrum.
	std::vector<double> logmag((size_t) nbin);
	double peak_mag = 0.0;
	for (long k = 0; k < nbin; ++k) peak_mag = (std::max)(peak_mag, std::abs(spec[(size_t) k]));
	if (peak_mag <= 0.0) return env;
	const double floor_mag = peak_mag * 1e-6; // -120 dB relative
	for (long k = 0; k < nbin; ++k)
		logmag[(size_t) k] = 20.0 * std::log10((std::max)(std::abs(spec[(size_t) k]), floor_mag));

	// ── Cepstral smoothing ──────────────────────────────────────────────────────────────────────────────────────
	// The real cepstrum is the inverse transform of the log-magnitude spectrum. Build the full even-symmetric
	// log-magnitude sequence; because it is real and even its transform is real and even, so a forward r2c gives the
	// cepstrum directly (up to the 1/nfft scaling, which cancels on the way back).
	std::vector<double> sym((size_t) nfft);
	for (long k = 0; k < nbin; ++k) sym[(size_t) k] = logmag[(size_t) k];
	for (long k = nbin; k < nfft; ++k) sym[(size_t) k] = logmag[(size_t) (nfft - k)];

	std::vector<std::complex<double>> ceps((size_t) nbin);
	pocketfft::r2c(shape, stride_r, stride_c, { 0 }, true, sym.data(), ceps.data(), 1.0);

	// Lifter: keep quefrencies up to the one whose period equals `lifter_hz` of spectral detail. Quefrency index q
	// corresponds to a ripple of period sample_rate/q Hz, so the cutoff is q_max = sample_rate / lifter_hz.
	const long q_max = (long) std::lround(sample_rate / (std::max)(w.lifter_hz, 1.0));
	std::vector<double> cut((size_t) nfft, 0.0);
	for (long q = 0; q < nbin; ++q) {
		if (q > q_max) break;
		// Taper the last few quefrencies rather than truncating hard: a rectangular lifter rings in the frequency
		// domain and the ringing looks exactly like low-prominence peaks.
		double t = 1.0;
		const long taper = (std::max)(q_max / 4, 1L);
		if (q > q_max - taper) t = 0.5 * (1.0 + std::cos(M_PI * double(q - (q_max - taper)) / double(taper)));
		double v = ceps[(size_t) q].real() * t;
		cut[(size_t) q] = v;
		if (q > 0 && q < nfft - q) cut[(size_t) (nfft - q)] = v; // keep it even
	}

	std::vector<std::complex<double>> smooth_c((size_t) nbin);
	pocketfft::r2c(shape, stride_r, stride_c, { 0 }, true, cut.data(), smooth_c.data(), 1.0);

	std::vector<double> smooth((size_t) nbin);
	for (long k = 0; k < nbin; ++k) smooth[(size_t) k] = smooth_c[(size_t) k].real() / double(nfft);

	// ── Peak picking with prominence ────────────────────────────────────────────────────────────────────────────
	const double df = sample_rate / double(nfft);
	const long k_lo = (std::max)(1L, (long) std::lround(w.min_freq / df));
	const long k_hi = (std::min)(nbin - 2, (long) std::lround((std::min)(max_freq, sample_rate * 0.5) / df));
	if (k_hi <= k_lo) return env;

	for (long k = k_lo; k <= k_hi; ++k) {
		if (!(smooth[(size_t) k] > smooth[(size_t) (k - 1)] && smooth[(size_t) k] >= smooth[(size_t) (k + 1)]))
			continue;

		// Walk out to the flanking valleys; prominence is the rise above the *higher* of them, which is what keeps a
		// shoulder on the skirt of a strong formant from counting as a resonance of its own.
		double left_min = smooth[(size_t) k];
		for (long j = k - 1; j >= k_lo - 1 && j >= 1; --j) {
			if (smooth[(size_t) j] > smooth[(size_t) (j + 1)] && smooth[(size_t) j] > smooth[(size_t) k]) break;
			left_min = (std::min)(left_min, smooth[(size_t) j]);
		}
		double right_min = smooth[(size_t) k];
		for (long j = k + 1; j <= k_hi + 1 && j < nbin; ++j) {
			if (smooth[(size_t) j] > smooth[(size_t) (j - 1)] && smooth[(size_t) j] > smooth[(size_t) k]) break;
			right_min = (std::min)(right_min, smooth[(size_t) j]);
		}
		double prom = smooth[(size_t) k] - (std::max)(left_min, right_min);
		if (prom < w.peak_min_prominence) continue;

		// Parabolic interpolation on the log-magnitude gives sub-bin peak frequency.
		double y0 = smooth[(size_t) (k - 1)], y1 = smooth[(size_t) k], y2 = smooth[(size_t) (k + 1)];
		double denom = y0 - 2.0 * y1 + y2;
		double delta = (denom != 0.0) ? 0.5 * (y0 - y2) / denom : 0.0;
		if (!(delta > -1.0 && delta < 1.0)) delta = 0.0;

		SpectralPeak p;
		p.freq = (double(k) + delta) * df;
		p.prominence = prom;
		env.peaks.push_back(p);
	}

	env.valid = !env.peaks.empty();
	return env;
}

SpectralScore
score_spectral(const SpectralEnvelope &env, const double *formants, int nformant, double ceiling,
               const SpectralWeights &w)
{
	SpectralScore s;
	if (!env.valid || !formants || nformant < 1) return s;

	// ── Pk : peak support ───────────────────────────────────────────────────────────────────────────────────────
	// Mean ERB distance from each claimed formant to the nearest envelope peak. A correct analysis puts every formant
	// on a resonance; a mis-assigned one parks a formant in a valley, or doubles two formants onto one peak.
	{
		double sum = 0.0;
		int n = 0;
		for (int k = 0; k < nformant; ++k) {
			double f = formants[k];
			if (std::isnan(f) || f <= 0.0) continue;
			double best = (std::numeric_limits<double>::max)();
			for (auto &p : env.peaks) best = (std::min)(best, std::abs(erb(f) - erb(p.freq)));
			if (best < (std::numeric_limits<double>::max)()) { sum += best; ++n; }
		}
		if (n == 0) return s;
		s.Pk = (sum / n) / w.bad_Pk;
	}

	// ── Ux : unexplained peak ───────────────────────────────────────────────────────────────────────────────────
	// A prominent peak lying *below* the highest claimed formant that no formant sits on is a formant the analysis
	// skipped. Peaks above the highest claimed formant are F4 and beyond and are legitimately unclaimed, so the search
	// stops there -- this is what makes the term a skipped-formant detector rather than a pole counter.
	{
		double f_top = 0.0;
		for (int k = 0; k < nformant; ++k) {
			double f = formants[k];
			if (!std::isnan(f) && f > f_top) f_top = f;
		}
		if (f_top <= 0.0) return s;
		const double limit = (std::min)(f_top, ceiling > 0.0 ? ceiling : f_top);

		double unexplained = 0.0;
		for (auto &p : env.peaks) {
			if (p.freq >= limit || p.prominence < w.min_prominence) continue;
			double best = (std::numeric_limits<double>::max)();
			for (int k = 0; k < nformant; ++k) {
				double f = formants[k];
				if (std::isnan(f) || f <= 0.0) continue;
				best = (std::min)(best, std::abs(erb(f) - erb(p.freq)));
			}
			if (best > w.claim_tol_erb) unexplained += p.prominence;
		}
		s.Ux = unexplained / w.bad_Ux;
	}

	// ── Dm : domination ─────────────────────────────────────────────────────────────────────────────────────────
	// For each claimed formant, find the peak it actually sits on, then ask whether a stronger peak stands nearby
	// that no formant claims. Claiming the weaker of two neighbouring resonances is the signature of an analysis
	// that has taken a minor pole for a formant, and unlike Ux this is direction-free: the stronger peak may lie
	// above the claim (the /oa/, /uw/ case) or below it.
	{
		double deficit = 0.0;
		for (int k = 0; k < nformant; ++k) {
			double f = formants[k];
			if (std::isnan(f) || f <= 0.0) continue;

			// The peak this formant claims, if any.
			double own_prom = 0.0, own_d = (std::numeric_limits<double>::max)();
			for (auto &p : env.peaks) {
				double d = std::abs(erb(f) - erb(p.freq));
				if (d < own_d) { own_d = d; own_prom = p.prominence; }
			}
			if (own_d > w.claim_tol_erb) continue; // not on a peak at all: that is Pk's business, not Dm's

			// The strongest peak in the neighbourhood that no formant claims.
			double best_rival = 0.0;
			for (auto &p : env.peaks) {
				if (std::abs(erb(f) - erb(p.freq)) > w.dom_window_erb) continue;
				bool claimed = false;
				for (int j = 0; j < nformant; ++j) {
					double g = formants[j];
					if (std::isnan(g) || g <= 0.0) continue;
					if (std::abs(erb(g) - erb(p.freq)) <= w.claim_tol_erb) { claimed = true; break; }
				}
				if (!claimed && p.prominence > best_rival) best_rival = p.prominence;
			}
			double d = best_rival - own_prom - w.dom_margin_db;
			if (d > 0.0) deficit += d;
		}
		s.Dm = deficit / w.bad_Dm;
	}

	s.valid = true;
	return s;
}

} // namespace phonometrica::speech
