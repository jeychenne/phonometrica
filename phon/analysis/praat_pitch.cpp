/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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

std::vector<double> get_pitch_praat(const Array<double> &idat, double sample_rate, double min_pitch, double max_pitch, double time_step, double voicing_threshold, double octave_jump_cost = 0.35, double voicing_cost = 0.45)
{
    std::span<const double> input{idat.data(), size_t(idat.size())};
    const int win_len = static_cast<int>(std::floor(3.0 * sample_rate / min_pitch));
    const int step_samples = static_cast<int>(time_step * sample_rate);
    
    if (input.size() < (size_t)win_len) return {};

    // 1. Pre-calculate Windowing
    Eigen::VectorXd window(win_len);
    double mid = (win_len - 1) / 2.0;
    for (int n = 0; n < win_len; ++n) {
        double edge = (n - mid) / (win_len / 2.0);
        window[n] = std::exp(-12.0 * edge * edge); 
    }
    Eigen::VectorXd r_window = calculate_ac(window);

    // 2. Candidate Extraction
    std::vector<std::vector<PitchCandidate>> frame_candidates;
    for (size_t start = 0; start + win_len <= input.size(); start += step_samples) {
        Eigen::Map<const Eigen::VectorXd> frame_raw(&input[start], win_len);
        Eigen::VectorXd frame = (frame_raw.array() - frame_raw.mean()) * window.array();
        Eigen::VectorXd r_frame = calculate_ac(frame);

        std::vector<PitchCandidate> candidates;
        // Always add an unvoiced candidate (frequency 0)
        candidates.push_back({0.0, voicing_threshold});

        int min_lag_idx = static_cast<int>(std::ceil(sample_rate / max_pitch));
        int max_lag_idx = static_cast<int>(std::floor(sample_rate / min_pitch));

        for (int lag = min_lag_idx; lag <= max_lag_idx && lag < win_len; ++lag) {
            // Check for local peak in normalized AC
            double v_prev = r_frame[lag-1] / r_window[lag-1];
            double v_curr = r_frame[lag] / r_window[lag];
            double v_next = r_frame[lag+1] / r_window[lag+1];

            if (v_curr > v_prev && v_curr > v_next) {
                // Parabolic interpolation for peak
                double shift = 0.5 * (v_prev - v_next) / (v_prev - 2.0 * v_curr + v_next);
                candidates.push_back({ sample_rate / (lag + shift), v_curr });
            }
        }
        frame_candidates.push_back(candidates);
    }

    // 3. Viterbi Path Finding
    size_t num_frames = frame_candidates.size();
    std::vector<std::vector<double>> d(num_frames); // min costs
    std::vector<std::vector<int>> phi(num_frames);   // backpointers

    // Initialization
    d[0].resize(frame_candidates[0].size(), 0.0);
    for (size_t i = 0; i < frame_candidates[0].size(); ++i) {
        d[0][i] = 1.0 - frame_candidates[0][i].strength; 
    }

    // Recursion
    for (size_t t = 1; t < num_frames; ++t) {
        d[t].resize(frame_candidates[t].size(), std::numeric_limits<double>::max());
        phi[t].resize(frame_candidates[t].size(), 0);

        for (size_t j = 0; j < frame_candidates[t].size(); ++j) {
            double f2 = frame_candidates[t][j].frequency;
            
            for (size_t i = 0; i < frame_candidates[t-1].size(); ++i) {
                double f1 = frame_candidates[t-1][i].frequency;
                double transition_cost = 0.0;

                if (f1 == 0 && f2 == 0) transition_cost = 0;
                else if (f1 == 0 || f2 == 0) transition_cost = voicing_cost;
                else transition_cost = octave_jump_cost * std::abs(std::log2(f1 / f2));

                double current_cost = d[t-1][i] + transition_cost + (1.0 - frame_candidates[t][j].strength);
                
                if (current_cost < d[t][j]) {
                    d[t][j] = current_cost;
                    phi[t][j] = static_cast<int>(i);
                }
            }
        }
    }

    // Backtracking
    std::vector<double> pitch_track(num_frames);
    int best_last_idx = static_cast<int>(std::min_element(d.back().begin(), d.back().end()) - d.back().begin());
    
    for (int t = static_cast<int>(num_frames) - 1; t >= 0; --t) {
        pitch_track[t] = frame_candidates[t][best_last_idx].frequency;
        best_last_idx = phi[t][best_last_idx];
    }

    return pitch_track;
}

}} // namespace phonometrica::speech