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
 * Created: 24/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: implementation of Praat's "raw autocorrelation" pitch tracking algorithm, described in the following       *
 * paper: Boersma, P. (1993, March). Accurate short-term analysis of the fundamental frequency and the                 *
 * harmonics-to-noise ratio of a sampled sound. In Proceedings of the institute of phonetic sciences                   *
 * (Vol. 17, No. 1193, pp. 97-110).                                                                                    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <vector>
#include <cmath>
#include <algorithm>
#include <Eigen/Dense>
#include <limits>
#include <phon/array.hpp>
#include <phon/analysis/signal_processing.hpp>
#include <phon/third_party/pocketfft-cpp/pocketfft_hdronly.h>


namespace phonometrica {
namespace speech {


struct PitchCandidate {
    double frequency;
    double strength; // Normalized AC value
};

/**
 * Helper to calculate Autocorrelation using PocketFFT
 */
Eigen::VectorXd calculate_ac(const Eigen::VectorXd& data) {
    size_t n = data.size();
    // Padding to at least 2n-1 to avoid cyclic convolution artifacts
    size_t n_fft = 1;
    while (n_fft < (2 * n - 1)) n_fft <<= 1;

    std::vector<double> real_input(n_fft, 0.0);
    std::copy(data.data(), data.data() + n, real_input.begin());

    // 1. Forward FFT (Real to Complex)
    size_t n_complex = (n_fft / 2) + 1;
    std::vector<std::complex<double>> complex_out(n_complex);
    pocketfft::shape_t shape{n_fft};
    pocketfft::stride_t stride_in{sizeof(double)}, stride_out{sizeof(std::complex<double>)};
    pocketfft::r2c(shape, stride_in, stride_out, 0, pocketfft::FORWARD, 
                   real_input.data(), complex_out.data(), 1.0);

    // 2. Power Spectrum (Magnitude Squared)
    for (auto& c : complex_out) {
        c = std::complex<double>(std::norm(c), 0.0);
    }

    // 3. Inverse FFT (Complex to Real)
    std::vector<double> ac_result(n_fft);
    pocketfft::stride_t stride_inv_in{sizeof(std::complex<double>)}, stride_inv_out{sizeof(double)};
    pocketfft::c2r(shape, stride_inv_in, stride_inv_out, 0, pocketfft::BACKWARD, 
                   complex_out.data(), ac_result.data(), 1.0 / n_fft);

    // Return the first n coefficients (lags)
    return Eigen::Map<Eigen::VectorXd>(ac_result.data(), n);
}

std::vector<double> get_pitch_praat(const Array<double> &idat, double sample_rate, double min_pitch, double max_pitch, double time_step, double voicing_threshold, double octave_jump_cost, double voicing_cost, double silence_threshold, double octave_cost)
{
    std::span<const double> input{idat.data(), size_t(idat.size())};
    const int win_len = static_cast<int>(std::floor(3.0 * sample_rate / min_pitch));
    const int step_samples = static_cast<int>(time_step * sample_rate);
    
    if (win_len < 2 || step_samples < 1 || input.size() < (size_t)win_len)
        return {};

    // Compute global maximum absolute amplitude for the silence threshold.
    double global_max = 0.0;
    for (size_t i = 0; i < input.size(); i++) {
        double a = std::abs(input[i]);
        if (a > global_max) global_max = a;
    }
    double silence_level = silence_threshold * global_max;

    // 1. Pre-calculate window autocorrelation
    Eigen::VectorXd window(win_len);
    double mid = (win_len - 1) / 2.0;
    for (int n = 0; n < win_len; ++n) {
        double edge = (n - mid) / (win_len / 2.0);
        window[n] = std::exp(-12.0 * edge * edge); 
    }
    Eigen::VectorXd r_window = calculate_ac(window);

    // 2. Candidate Extraction
    int min_lag = static_cast<int>(std::ceil(sample_rate / max_pitch));
    int max_lag = static_cast<int>(std::floor(sample_rate / min_pitch));
    // Clamp so that lag-1 >= 0 and lag+1 < win_len.
    if (min_lag < 1) min_lag = 1;
    if (max_lag >= win_len - 1) max_lag = win_len - 2;

    std::vector<std::vector<PitchCandidate>> frame_candidates;
    for (size_t start = 0; start + win_len <= input.size(); start += step_samples) {
        Eigen::Map<const Eigen::VectorXd> frame_raw(&input[start], win_len);

        std::vector<PitchCandidate> candidates;
        // Always add an unvoiced candidate (frequency 0).
        candidates.push_back({0.0, voicing_threshold});

        // Silence threshold: if the frame's peak amplitude is below the
        // threshold, treat the frame as silent (only the unvoiced candidate).
        double frame_peak = frame_raw.cwiseAbs().maxCoeff();
        if (frame_peak < silence_level) {
            frame_candidates.push_back(std::move(candidates));
            continue;
        }

        Eigen::VectorXd frame = (frame_raw.array() - frame_raw.mean()) * window.array();
        Eigen::VectorXd r_frame = calculate_ac(frame);

        // Normalization factor: the corrected energy at lag 0.
        // Dividing all r_frame/r_window values by this turns them into
        // autocorrelation coefficients in the range [-1, 1].
        double energy = (std::abs(r_window[0]) > 1e-30) ? r_frame[0] / r_window[0] : 0.0;
        if (energy < 1e-30) {
            // Silent frame — no voiced candidates possible.
            frame_candidates.push_back(std::move(candidates));
            continue;
        }

        for (int lag = min_lag; lag <= max_lag; ++lag) {
            double rw_prev = r_window[lag-1];
            double rw_curr = r_window[lag];
            double rw_next = r_window[lag+1];

            // Guard against division by zero in the window autocorrelation.
            if (std::abs(rw_prev) < 1e-30 || std::abs(rw_curr) < 1e-30 || std::abs(rw_next) < 1e-30)
                continue;

            // Normalized autocorrelation coefficients (in [-1, 1]).
            double v_prev = (r_frame[lag-1] / rw_prev) / energy;
            double v_curr = (r_frame[lag]   / rw_curr) / energy;
            double v_next = (r_frame[lag+1] / rw_next) / energy;

            if (v_curr > v_prev && v_curr > v_next && v_curr > 0) {
                // Parabolic interpolation for sub-sample peak.
                double denom = v_prev - 2.0 * v_curr + v_next;
                double shift = 0.0;
                if (std::abs(denom) > 1e-30)
                    shift = 0.5 * (v_prev - v_next) / denom;

                double interp_lag = lag + shift;
                if (interp_lag > 0)
                    candidates.push_back({ sample_rate / interp_lag, v_curr });
            }
        }
        frame_candidates.push_back(std::move(candidates));
    }

    // 3. Viterbi Path Finding
    size_t num_frames = frame_candidates.size();
    if (num_frames == 0) return {};

    std::vector<std::vector<double>> d(num_frames); // accumulated costs
    std::vector<std::vector<int>> phi(num_frames);   // backpointers

    // Initialization (frame 0): no backpointer needed.
    // The local cost for each candidate includes:
    //   - (1 - strength) as the base observation cost
    //   - octave_cost * log2(F0/min_pitch) for voiced candidates (favors lower F0)
    d[0].resize(frame_candidates[0].size());
    for (size_t i = 0; i < frame_candidates[0].size(); ++i) {
        double f = frame_candidates[0][i].frequency;
        double local_cost = 1.0 - frame_candidates[0][i].strength;
        if (f > 0 && min_pitch > 0)
            local_cost += octave_cost * std::log2(f / min_pitch);
        d[0][i] = local_cost;
    }

    // Recursion (frames 1 .. N-1)
    for (size_t t = 1; t < num_frames; ++t) {
        size_t ncand = frame_candidates[t].size();
        d[t].assign(ncand, std::numeric_limits<double>::max());
        phi[t].assign(ncand, 0);

        for (size_t j = 0; j < ncand; ++j) {
            double f2 = frame_candidates[t][j].frequency;

            // Local cost for candidate j.
            double local_cost = 1.0 - frame_candidates[t][j].strength;
            if (f2 > 0 && min_pitch > 0)
                local_cost += octave_cost * std::log2(f2 / min_pitch);

            for (size_t i = 0; i < frame_candidates[t-1].size(); ++i) {
                double f1 = frame_candidates[t-1][i].frequency;
                double transition_cost = 0.0;

                if (f1 == 0 && f2 == 0)
                    transition_cost = 0;
                else if (f1 == 0 || f2 == 0)
                    transition_cost = voicing_cost;
                else
                    transition_cost = octave_jump_cost * std::abs(std::log2(f1 / f2));

                double cost = d[t-1][i] + transition_cost + local_cost;
                
                if (cost < d[t][j]) {
                    d[t][j] = cost;
                    phi[t][j] = static_cast<int>(i);
                }
            }
        }
    }

    // 4. Backtracking — phi[0] does not exist, so stop at t >= 1.
    std::vector<double> pitch_track(num_frames);
    int best_idx = static_cast<int>(
        std::min_element(d.back().begin(), d.back().end()) - d.back().begin());
    
    for (int t = static_cast<int>(num_frames) - 1; t >= 1; --t) {
        pitch_track[t] = frame_candidates[t][best_idx].frequency;
        best_idx = phi[t][best_idx];
    }
    pitch_track[0] = frame_candidates[0][best_idx].frequency;

    return pitch_track;
}

}} // namespace phonometrica::speech