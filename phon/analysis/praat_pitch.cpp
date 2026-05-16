/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * Portions of this file are derived from Praat's Sound_to_Pitch.cpp and Pitch.cpp, specifically the autocorrelation   *
 * candidate extraction, the frame-intensity model and the Viterbi path finder.                                        *
 *     Praat: doing phonetics by computer. Copyright (C) 1992-2025 Paul Boersma.                                       *
 *     https://github.com/praat/praat — licensed under the GNU General Public License v3 or later.                     *
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
 * Created: 24/03/2026                                                                                                 *
 * Revised: 23/04/2026 — rewrite to faithfully match Praat's raw autocorrelation pitch tracker.                        *
 *                                                                                                                     *
 * Purpose: implementation of Praat's "raw autocorrelation" pitch tracking algorithm, as described in                  *
 *     Boersma, P. (1993). Accurate short-term analysis of the fundamental frequency and the harmonics-to-noise       *
 *     ratio of a sampled sound. Proceedings of the Institute of Phonetic Sciences, 17, 97–110.                        *
 *                                                                                                                     *
 * Two window types are supported, mirroring Praat:                                                                    *
 *     - Hanning, 3 periods per window (Praat's default, fast mode).                                                   *
 *     - Gaussian, 6 periods per window ("very accurate" mode).                                                        *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>
#include <Eigen/Dense>
#include <phon/array.hpp>
#include <phon/analysis/signal_processing.hpp>
#include <phon/third_party/pocketfft-cpp/pocketfft_hdronly.h>

namespace phonometrica {
namespace speech {

namespace {

struct PitchCandidate {
    double frequency;
    double strength;
};

// Praat's default maximum number of candidates per frame (Sound_to_Pitch.cpp).
constexpr int PRAAT_MAX_N_CANDIDATES = 15;

// -------------------------------------------------------------------------------------------------------------------
// Autocorrelation of a real sequence via FFT -> power spectrum -> IFFT.
// The input is zero-padded to the next power of two >= 2n-1, which suppresses cyclic wrap-around completely.
// Praat uses a smaller FFT and clips the AC at brent_ixmax; the result is mathematically equivalent for our lags.
// -------------------------------------------------------------------------------------------------------------------
Eigen::VectorXd calculate_ac(const Eigen::VectorXd& data)
{
    size_t n = data.size();
    size_t n_fft = 1;
    while (n_fft < (2 * n - 1)) n_fft <<= 1;

    std::vector<double> real_input(n_fft, 0.0);
    std::copy(data.data(), data.data() + n, real_input.begin());

    size_t n_complex = (n_fft / 2) + 1;
    std::vector<std::complex<double>> complex_out(n_complex);
    pocketfft::shape_t shape{n_fft};
    pocketfft::stride_t stride_in{sizeof(double)}, stride_out{sizeof(std::complex<double>)};
    pocketfft::r2c(shape, stride_in, stride_out, 0, pocketfft::FORWARD,
                   real_input.data(), complex_out.data(), 1.0);

    for (auto& c : complex_out) {
        c = std::complex<double>(std::norm(c), 0.0);
    }

    std::vector<double> ac_result(n_fft);
    pocketfft::stride_t stride_inv_in{sizeof(std::complex<double>)}, stride_inv_out{sizeof(double)};
    pocketfft::c2r(shape, stride_inv_in, stride_inv_out, 0, pocketfft::BACKWARD,
                   complex_out.data(), ac_result.data(), 1.0 / n_fft);

    return Eigen::Map<Eigen::VectorXd>(ac_result.data(), n);
}

// -------------------------------------------------------------------------------------------------------------------
// Hanning window matching Praat's formula (Sound_to_Pitch.cpp:414, 1-indexed -> 0-indexed here):
//     window[k] = 0.5 - 0.5 * cos(2π * (k+1) / (N+1))
// Tapers to ~0 at k=0 and k=N-1, peaks at 1 at the centre.
// -------------------------------------------------------------------------------------------------------------------
Eigen::VectorXd make_hanning_window(int N)
{
    Eigen::VectorXd w(N);
    for (int i = 0; i < N; ++i) {
        w[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * (i + 1) / (N + 1));
    }
    return w;
}

// -------------------------------------------------------------------------------------------------------------------
// Gaussian window matching Praat's formula (Sound_to_Pitch.cpp:408-411):
//     imid_1  = (N+1)/2                              (1-indexed centre)
//     window[i_1] = (exp(-48*(i_1-imid_1)^2 / (N+1)^2) - exp(-12)) / (1 - exp(-12))
// In 0-indexed form, centre = (N-1)/2. The edge subtraction + normalisation forces the window to reach exactly zero
// at the edges; omitting either is the bug fixed by this rewrite.
// -------------------------------------------------------------------------------------------------------------------
Eigen::VectorXd make_gaussian_window(int N)
{
    Eigen::VectorXd w(N);
    const double mid = (N - 1) / 2.0;
    const double Np1 = static_cast<double>(N + 1);
    const double edge = std::exp(-12.0);
    const double denom = 1.0 - edge;
    for (int i = 0; i < N; ++i) {
        const double d = i - mid;
        w[i] = (std::exp(-48.0 * d * d / (Np1 * Np1)) - edge) / denom;
    }
    return w;
}

inline bool frequency_is_voiced(double f, double ceiling)
{
    // Praat: Pitch_util_frequencyIsVoiced (Pitch.h:66).
    return f > 0.0 && f < ceiling;
}

} // anonymous namespace

// -------------------------------------------------------------------------------------------------------------------
// Praat raw-autocorrelation pitch tracker.
//
// The algorithm has two phases:
//   1) For each frame, extract voiced candidates as local maxima of the window-corrected autocorrelation
//      r(tau) = ac_frame(tau) * ac_window(0) / (ac_frame(0) * ac_window(tau)), together with the frame intensity
//      localPeak / globalPeak that drives the silence model in the path finder.
//   2) Viterbi path finder across frames, maximising a reward that penalises low frequencies (octave_cost),
//      frequency jumps between voiced frames (octave_jump_cost), and voiced<->unvoiced transitions (voicing_cost).
//      The silence threshold enters here as a bonus on the unvoiced-candidate strength that scales with how quiet
//      the frame is relative to the global peak.
//
// See Praat's Sound_to_Pitch.cpp (Sound_into_PitchFrame) and Pitch.cpp (Pitch_pathFinder) for the reference logic.
// -------------------------------------------------------------------------------------------------------------------
std::vector<double> get_pitch_praat(
        const Array<double> &idat, double sample_rate,
        double min_pitch, double max_pitch, double time_step,
        double voicing_threshold, double octave_jump_cost,
        double voicing_cost, double silence_threshold, double octave_cost,
        bool use_gaussian,
        std::vector<double> *out_strengths)
{
    if (out_strengths) out_strengths->clear();
    if (idat.size() < 2 || sample_rate <= 0.0 || min_pitch <= 0.0 || max_pitch <= min_pitch)
        return {};

    std::span<const double> input{idat.data(), size_t(idat.size())};

    const int step_samples = std::max(1, static_cast<int>(std::round(time_step * sample_rate)));

    // Window: 3 periods of min_pitch for Hanning, 6 for Gaussian (Praat: periodsPerWindow *= 2 for AC_GAUSS).
    const double periods_per_window = use_gaussian ? 6.0 : 3.0;
    int win_len = static_cast<int>(std::floor(periods_per_window * sample_rate / min_pitch));

    // Praat normalises to an even window length: halfN = N/2 - 1; N = halfN * 2.
    int half_len = win_len / 2 - 1;
    if (half_len < 2) return {};
    win_len = half_len * 2;

    if (static_cast<int>(input.size()) < win_len) return {};

    // Lag range. Praat:
    //     minimumLag = max(2, floor(sample_rate / pitchCeiling))
    //     maximumLag = min(floor(nsamp_window / periodsPerWindow) + 2, nsamp_window)
    int min_lag = std::max(2, static_cast<int>(std::floor(sample_rate / max_pitch)));
    int max_lag = std::min(static_cast<int>(std::floor(win_len / periods_per_window)) + 2, win_len);
    if (max_lag > win_len - 2) max_lag = win_len - 2;
    if (max_lag <= min_lag) return {};

    // One period at min_pitch, used to define the central region over which localPeak is measured.
    const int half_period = static_cast<int>(std::floor(sample_rate / min_pitch)) / 2 + 1;
    const int centre_start = std::max(0, half_len - half_period);
    const int centre_end   = std::min(win_len - 1, half_len + half_period);

    // Global peak of the mean-subtracted signal. Praat zeros out the mean channel-by-channel; we have one channel.
    double global_mean = 0.0;
    for (double v : input) global_mean += v;
    global_mean /= static_cast<double>(input.size());
    double global_peak = 0.0;
    for (double v : input) {
        double a = std::abs(v - global_mean);
        if (a > global_peak) global_peak = a;
    }

    // Count frames once so we can return an all-zero track for absolute silence.
    size_t nframes_total = 0;
    for (size_t s = 0; s + static_cast<size_t>(win_len) <= input.size(); s += static_cast<size_t>(step_samples))
        ++nframes_total;
    if (nframes_total == 0) return {};
    if (global_peak == 0.0) {
        if (out_strengths) out_strengths->assign(nframes_total, 0.0);
        return std::vector<double>(nframes_total, 0.0);
    }

    // Build window and normalised window autocorrelation (windowR[0] = 1, windowR[i] = ac_w[i]/ac_w[0]).
    Eigen::VectorXd window = use_gaussian ? make_gaussian_window(win_len)
                                          : make_hanning_window(win_len);
    Eigen::VectorXd raw_wR = calculate_ac(window);
    const double wR0 = raw_wR[0];
    if (wR0 < 1e-30) return {};
    Eigen::VectorXd windowR(win_len);
    windowR[0] = 1.0;
    for (int i = 1; i < win_len; ++i) {
        windowR[i] = raw_wR[i] / wR0;
    }

    // -------- Phase 1: per-frame candidate extraction --------
    std::vector<std::vector<PitchCandidate>> frame_candidates;
    std::vector<double> frame_intensity;
    frame_candidates.reserve(nframes_total);
    frame_intensity.reserve(nframes_total);

    const double ceiling = max_pitch;
    const double peak_threshold = 0.5 * voicing_threshold;          // Praat Sound_to_Pitch.cpp:186
    const int max_n_candidates = std::max<int>(PRAAT_MAX_N_CANDIDATES,
                                               static_cast<int>(std::floor(max_pitch / min_pitch)));

    Eigen::VectorXd frame(win_len);

    for (size_t start = 0; start + static_cast<size_t>(win_len) <= input.size();
         start += static_cast<size_t>(step_samples))
    {
        // Local mean (simpler than Praat's ±1-period mean; adequate for a 3-6-period window).
        double local_mean = 0.0;
        for (int j = 0; j < win_len; ++j) local_mean += input[start + j];
        local_mean /= static_cast<double>(win_len);

        // Windowed frame: (data - localMean) * window.
        for (int j = 0; j < win_len; ++j) {
            frame[j] = (input[start + j] - local_mean) * window[j];
        }

        // localPeak on the windowed frame in the central half-period region, as Praat does.
        double local_peak = 0.0;
        for (int j = centre_start; j <= centre_end; ++j) {
            double a = std::abs(frame[j]);
            if (a > local_peak) local_peak = a;
        }

        // Intensity used by the path finder; Praat clamps localPeak/globalPeak to 1.
        double intensity = (local_peak > global_peak) ? 1.0 : (local_peak / global_peak);
        frame_intensity.push_back(intensity);

        // Every frame carries the unvoiced candidate. Its strength is set in the path finder.
        std::vector<PitchCandidate> cands;
        cands.reserve(8);
        cands.push_back({0.0, 0.0});

        // Absolute-silence shortcut (Praat Sound_to_Pitch.cpp:177).
        if (local_peak == 0.0) {
            frame_candidates.push_back(std::move(cands));
            continue;
        }

        Eigen::VectorXd ac_frame = calculate_ac(frame);
        const double af0 = ac_frame[0];
        if (af0 < 1e-30) {
            frame_candidates.push_back(std::move(cands));
            continue;
        }

        // Normalised autocorrelation coefficient at lag i:
        //     r[i] = ac_frame[i] / (ac_frame[0] * windowR[i])            (Praat: Sound_to_Pitch.cpp:163)
        // Only lags 0 .. max_lag+1 are used (we need i-1 .. i+1 for parabolic interpolation).
        std::vector<double> r(static_cast<size_t>(max_lag) + 2, 0.0);
        r[0] = 1.0;
        for (int i = 1; i <= max_lag + 1; ++i) {
            double wR = windowR[i];
            r[i] = (std::abs(wR) < 1e-30) ? 0.0 : (ac_frame[i] / af0) / wR;
        }

        // Scan for local maxima of r[] in [min_lag, max_lag].
        for (int i = min_lag; i <= max_lag; ++i) {
            if (r[i] <= peak_threshold) continue;
            if (!(r[i] > r[i-1] && r[i] >= r[i+1])) continue;

            // Parabolic interpolation for the sub-sample peak (Praat Sound_to_Pitch.cpp:195-199).
            //   dr  = 0.5 * (r[i+1] - r[i-1])
            //   d2r = (r[i] - r[i-1]) + (r[i] - r[i+1])   > 0 at a maximum
            //   shift = dr / d2r;  peak value = r[i] + 0.5 * dr * shift
            double dr = 0.5 * (r[i+1] - r[i-1]);
            double d2r = (r[i] - r[i-1]) + (r[i] - r[i+1]);
            if (d2r <= 0.0) continue; // numerical guard; at a true max, d2r > 0
            double shift = dr / d2r;
            double interp_lag = static_cast<double>(i) + shift;
            if (interp_lag <= 0.0) continue;

            double freq = sample_rate / interp_lag;
            if (!frequency_is_voiced(freq, ceiling)) continue;

            double strength = r[i] + 0.5 * dr * shift;
            // Praat reflects strengths > 1 around 1 to handle numerical overshoots in the interpolation.
            if (strength > 1.0) strength = 1.0 / strength;

            cands.push_back({freq, strength});
        }

        // Cap the candidate list at max_n_candidates, keeping the unvoiced entry plus the strongest voiced ones.
        if (static_cast<int>(cands.size()) > max_n_candidates) {
            auto voiced_begin = cands.begin() + 1;
            auto keep_end = cands.begin() + max_n_candidates;
            std::nth_element(voiced_begin, keep_end, cands.end(),
                [](const PitchCandidate& a, const PitchCandidate& b) {
                    return a.strength > b.strength;
                });
            cands.resize(static_cast<size_t>(max_n_candidates));
        }

        frame_candidates.push_back(std::move(cands));
    }

    const size_t nframes = frame_candidates.size();
    if (nframes == 0) return {};

    // -------- Phase 2: Viterbi path finder (reward maximisation, matching Praat) --------

    // Praat calibrates transition costs for a 10 ms frame step (Pitch.cpp:543-545).
    const double tsc = (time_step > 0.0) ? (0.01 / time_step) : 1.0;
    const double adj_jump_cost = octave_jump_cost * tsc;
    const double adj_vu_cost   = voicing_cost   * tsc;

    std::vector<std::vector<double>> delta(nframes);
    std::vector<std::vector<int>>    psi(nframes);

    // Local rewards (Praat Pitch.cpp:551-562):
    //     voiceless candidate: delta = voicingThreshold + max(0, 2 - intensity / (silenceThreshold/(1+voicingThreshold)))
    //     voiced candidate:    delta = strength - octaveCost * log2(ceiling / frequency)
    for (size_t t = 0; t < nframes; ++t) {
        const auto& cands = frame_candidates[t];
        delta[t].resize(cands.size());
        psi[t].assign(cands.size(), 0);

        double unvoiced_strength;
        if (silence_threshold <= 0.0) {
            unvoiced_strength = voicing_threshold;
        } else {
            double bonus = 2.0 - frame_intensity[t] / (silence_threshold / (1.0 + voicing_threshold));
            unvoiced_strength = voicing_threshold + std::max(0.0, bonus);
        }

        for (size_t j = 0; j < cands.size(); ++j) {
            double f = cands[j].frequency;
            if (frequency_is_voiced(f, ceiling)) {
                delta[t][j] = cands[j].strength - octave_cost * std::log2(ceiling / f);
            } else {
                delta[t][j] = unvoiced_strength;
            }
        }
    }

    // Forward pass. We read cur_delta[j] (still the local reward at this point) and overwrite it with the best
    // accumulated reward, exactly as Praat's Pitch_pathFinder does.
    for (size_t t = 1; t < nframes; ++t) {
        const auto& prev_cands = frame_candidates[t-1];
        const auto& cur_cands  = frame_candidates[t];
        const auto& prev_delta = delta[t-1];
        auto& cur_delta = delta[t];
        auto& cur_psi = psi[t];

        for (size_t j = 0; j < cur_cands.size(); ++j) {
            const double f2 = cur_cands[j].frequency;
            const bool cur_voiceless = !frequency_is_voiced(f2, ceiling);
            const double local_reward = cur_delta[j];

            double best = -std::numeric_limits<double>::infinity();
            int best_i = 0;

            for (size_t i = 0; i < prev_cands.size(); ++i) {
                const double f1 = prev_cands[i].frequency;
                const bool prev_voiceless = !frequency_is_voiced(f1, ceiling);

                double transition_cost;
                if (cur_voiceless) {
                    transition_cost = prev_voiceless ? 0.0 : adj_vu_cost;
                } else if (prev_voiceless) {
                    transition_cost = adj_vu_cost;
                } else {
                    transition_cost = adj_jump_cost * std::abs(std::log2(f1 / f2));
                }

                double value = prev_delta[i] - transition_cost + local_reward;
                if (value > best) {
                    best = value;
                    best_i = static_cast<int>(i);
                }
            }
            cur_delta[j] = best;
            cur_psi[j] = best_i;
        }
    }

    // Find the end of the best path.
    int place = 0;
    double best_end = delta[nframes-1][0];
    for (size_t j = 1; j < delta[nframes-1].size(); ++j) {
        if (delta[nframes-1][j] > best_end) {
            best_end = delta[nframes-1][j];
            place = static_cast<int>(j);
        }
    }

    // Backtrack.
    std::vector<double> pitch_track(nframes);
    if (out_strengths) out_strengths->assign(nframes, 0.0);
    for (int t = static_cast<int>(nframes) - 1; t >= 0; --t) {
        pitch_track[t] = frame_candidates[t][place].frequency;
        if (out_strengths) (*out_strengths)[t] = frame_candidates[t][place].strength;
        if (t > 0) place = psi[t][place];
    }

    return pitch_track;
}

}} // namespace phonometrica::speech
