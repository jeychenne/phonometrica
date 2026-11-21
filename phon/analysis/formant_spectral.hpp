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
 * Purpose: Spectral evidence for formant-analysis selection. Every term in the intrinsic ladder                       *
 * (formant_selection.hpp) is computed from formant *tracks* (roughness, instability, coverage) or from pole           *
 * bookkeeping (density, bandwidth, ordering). None of them ever looks at the signal's own spectrum, so a              *
 * smooth-but-wrong analysis -- the documented F3 failure, where LPC skips the true F3 and reports a higher pole --     *
 * scores just as well as the right one. Smoothness cannot arbitrate formant *assignment*; only the spectrum can.      *
 *                                                                                                                     *
 * This module supplies that missing evidence, independently of LPC. It computes a cepstrally smoothed log-magnitude    *
 * envelope of the token once (harmonics live at high quefrency and are liftered away; the formant envelope lives at    *
 * low quefrency and survives), locates its peaks, and then scores any candidate's formants against that fixed peak     *
 * list. Two penalties:                                                                                                *
 *                                                                                                                     *
 *   Pk  peak support      each claimed formant should sit on a real spectral prominence; a formant floating between    *
 *                         peaks is suspect.                                                                            *
 *   Ux  unexplained peak  a prominent peak *below* the highest claimed formant that no formant accounts for is a       *
 *                         formant the analysis skipped upward (the rhotic-F3 failure).                                 *
 *   Dm  domination        a claimed formant sitting on a weak peak while a stronger unclaimed peak stands right next    *
 *                         to it. This is the dominant failure on the synthetic set, and it is the mirror image of Ux:   *
 *                         measured on /oa/ and /uw/, the selector reports F3 ~500 Hz *below* the true one, having       *
 *                         taken a minor pole between F2 and F3 while the real F3 remains the strongest peak in the      *
 *                         neighbourhood. Ux cannot see it, because the missed peak is above the claim, not below.       *
 *                                                                                                                     *
 * The envelope is computed once per token and reused by every <ceiling, order> candidate, so the cost is one FFT pair  *
 * per token rather than per candidate.                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FORMANT_SPECTRAL_HPP
#define PHONOMETRICA_FORMANT_SPECTRAL_HPP

#include <cmath>
#include <vector>

namespace phonometrica::speech {

// A peak of the smoothed log-magnitude envelope. `prominence` is the dB rise above the higher of the two flanking
// valleys, which is what distinguishes a resonance from a ripple on a slope.
struct SpectralPeak final
{
	double freq       = 0.0; // Hz
	double prominence = 0.0; // dB
};

struct SpectralEnvelope final
{
	std::vector<SpectralPeak> peaks; // ascending by frequency
	bool valid = false;
};

// Tunables for the spectral terms. As in the intrinsic ladder, each term is scaled internally so that a
// "meaningfully bad" value is ~1, hence default weights of 1.
struct SpectralWeights final
{
	double lambda_Pk = 1.0; // peak support
	double lambda_Ux = 1.0; // unexplained peak
	double lambda_Dm = 1.0; // domination

	// Cepstral lifter cutoff, expressed as the finest spectral detail the envelope may keep (Hz). Structure varying
	// faster than this -- i.e. the harmonic comb, whose period is f0 -- is removed. It must stay above the highest f0
	// we expect and below the closest formant spacing we must preserve, and those two bounds nearly meet: 300 Hz
	// flattens the harmonics of a 200 Hz female voice while still resolving an F1/F2 pair 400 Hz apart.
	double lifter_hz = 300.0;

	// Pre-emphasis cutoff (Hz) applied before the transform, 0 to disable. Without it the glottal source's -12 dB/oct
	// tilt drags the low peaks downward -- measured at ~65 Hz of F1 bias on the synthetic set -- because a peak of the
	// *product* of source and filter sits below the resonance of the filter alone.
	double pre_emphasis_hz = 50.0;

	// Peaks below this prominence (dB) are dropped from the envelope entirely. Without pruning, cepstral ripple
	// contributes ~9 peaks per token, so every formant is trivially near "a peak" and the peak-support term carries no
	// information at all. Keeping only real resonances is what makes both spectral terms discriminative.
	double peak_min_prominence = 2.0;

	// A peak is a candidate for the "unexplained" test only above this prominence (dB); below it, envelope wiggle.
	double min_prominence = 3.0;

	// A formant "claims" a peak when it lies within this distance of it, in ERB. ERB rather than Hz so the tolerance
	// tracks auditory resolution instead of over-penalising low F1 (see review_algorithm.md).
	double claim_tol_erb = 0.75;

	// Peaks below this frequency are ignored: the first harmonic / voicing bar is not a formant.
	double min_freq = 150.0;

	// Domination: how far from a claimed formant to look for a stronger unclaimed peak (ERB), and how much more
	// prominent that peak must be before the claim is called into question (dB).
	double dom_window_erb = 2.0;
	double dom_margin_db  = 0.0;

	// Internal bad-level scales (a term equal to its bad level contributes ~1 before weighting).
	double bad_Pk = 1.0;  // 1 ERB of mean distance from claimed formants to the nearest peak
	double bad_Ux = 10.0; // 10 dB of unexplained prominence
	double bad_Dm = 10.0; // 10 dB of domination deficit
};

struct SpectralScore final
{
	double Pk = 0.0;
	double Ux = 0.0;
	double Dm = 0.0;
	bool valid = false;
};

// Cepstrally smoothed envelope of one frame and its peaks, over [0, max_freq].
//
// `samples` is a raw (unwindowed, un-pre-emphasised) frame; this function applies its own pre-emphasis and Hann window.
SpectralEnvelope
compute_spectral_envelope(const double *samples, long n, double sample_rate, double max_freq,
                          const SpectralWeights &w = SpectralWeights());

// Score a candidate's formants against the token's envelope. `formants` holds nformant values in Hz (NaN allowed);
// `ceiling` is the candidate's max-formant frequency, used to bound the unexplained-peak search.
SpectralScore
score_spectral(const SpectralEnvelope &env, const double *formants, int nformant, double ceiling,
               const SpectralWeights &w = SpectralWeights());

} // namespace phonometrica::speech

#endif // PHONOMETRICA_FORMANT_SPECTRAL_HPP
