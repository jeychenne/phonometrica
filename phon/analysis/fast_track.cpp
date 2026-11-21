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
 * Created: 21/11/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <algorithm>
#include <phon/third_party/Eigen/Dense>
#include <phon/analysis/fast_track.hpp>
#include <phon/application/sound.hpp>

namespace phonometrica::speech {

namespace {

// Fast Track rounds errors to 0.1 Hz before comparing them, which decides near ties: keep that behaviour.
double round1(double x)
{
	return std::round(x * 10) / 10;
}

double median(std::vector<double> v)
{
	if (v.empty()) return std::nan("");
	std::sort(v.begin(), v.end());
	size_t n = v.size();
	return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

} // namespace

std::vector<double> FastTrackSettings::ceilings() const
{
	std::vector<double> result;
	if (nsteps <= 1)
	{
		result.push_back(std::round(lowest_ceiling));
		return result;
	}
	double step = (highest_ceiling - lowest_ceiling) / double(nsteps - 1);
	for (int i = 0; i < nsteps; i++) {
		result.push_back(std::round(lowest_ceiling + step * i));
	}
	return result;
}

std::vector<double> fast_track_frame_times(double t1, double t2, double time_step)
{
	std::vector<double> times;
	if (!(t2 >= t1) || time_step <= 0) return times;
	// Praat: numberOfFrames = floor((duration - windowDuration) / timeStep) + 1, centred. In a sound extracted with a
	// buffer of half the physical window on each side, duration - windowDuration is the duration of the interval.
	auto n = intptr_t(std::floor((t2 - t1) / time_step + 1e-9)) + 1;
	double first = 0.5 * (t1 + t2) - 0.5 * double(n - 1) * time_step;
	times.reserve(n);
	for (intptr_t i = 0; i < n; i++) {
		times.push_back(first + double(i) * time_step);
	}
	return times;
}

double FastTrackCandidate::predict(int k, double t) const
{
	if (!valid || k < 0 || k >= int(coefficients.size()) || frame_times.empty()) return std::nan("");

	// Rank (1-based) of time t among the kept frames, interpolated between frames.
	const auto &ft = frame_times;
	auto n = double(ft.size());
	double rank;
	if (t <= ft.front()) {
		rank = 1;
	}
	else if (t >= ft.back()) {
		rank = n;
	}
	else
	{
		auto it = std::upper_bound(ft.begin(), ft.end(), t);
		auto hi = size_t(it - ft.begin());
		auto lo = hi - 1;
		double frac = (t - ft[lo]) / (ft[hi] - ft[lo]);
		rank = double(lo + 1) + frac;
	}

	double x = rank / n;
	const auto &c = coefficients[k];
	double value = c[0];
	for (size_t j = 1; j < c.size(); j++) {
		value += c[j] * std::cos(M_PI * double(j) * x);
	}
	return value;
}

FastTrackCandidate fast_track_score(const std::vector<double> &times, const FormantFrames &frames, double ceiling,
                                    const FastTrackSettings &settings)
{
	FastTrackCandidate cand;
	cand.ceiling = ceiling;
	const int nf = std::clamp(settings.nformant, 3, 4);
	const int ncoeff = std::max(settings.ncoeff, 0);

	// Keep the frames with more than 3 formants (findError.praat: "nformants greater than 3"). In these frames, F1-F4
	// are all defined.
	std::vector<size_t> rows;
	for (size_t i = 0; i < frames.size() && i < times.size(); i++) {
		if (frames[i].size() > 3) rows.push_back(i);
	}
	const auto N = Eigen::Index(rows.size());
	const Eigen::Index P = ncoeff + 1;
	if (N <= P) return cand; // not enough frames to fit the model

	// Design matrix: intercept and cos(pi * c * i / N), i = 1..N being the frame's rank among the kept frames.
	Eigen::MatrixXd X(N, P);
	for (Eigen::Index r = 0; r < N; r++)
	{
		X(r, 0) = 1.0;
		double x = double(r + 1) / double(N);
		for (Eigen::Index c = 1; c < P; c++) {
			X(r, c) = std::cos(M_PI * double(c) * x);
		}
	}
	Eigen::ColPivHouseholderQR<Eigen::MatrixXd> qr(X);

	cand.coefficients.resize(nf);
	cand.frame_times.reserve(rows.size());
	for (auto i : rows) cand.frame_times.push_back(times[i]);

	std::array<std::vector<double>, 4> freq, bw;
	for (int k = 0; k < 4; k++)
	{
		for (auto i : rows)
		{
			freq[k].push_back(frames[i][k].first);
			bw[k].push_back(frames[i][k].second);
		}
	}

	for (int k = 0; k < nf; k++)
	{
		Eigen::VectorXd y = Eigen::Map<const Eigen::VectorXd>(freq[k].data(), N);
		Eigen::VectorXd beta = qr.solve(y);
		Eigen::VectorXd residual = y - X * beta;
		cand.formant_error[k] = round1(residual.cwiseAbs().mean());
		cand.coefficients[k].assign(beta.data(), beta.data() + P);
	}

	// Heuristics, on the medians of the tracks.
	double mf1 = median(freq[0]), mf2 = median(freq[1]), mf3 = median(freq[2]), mf4 = median(freq[3]);
	const double pen = settings.penalty;
	if (settings.f1_frequency && mf1 > settings.max_f1) cand.formant_error[0] += pen;
	if (settings.b1_bandwidth && median(bw[0]) > settings.max_b1) cand.formant_error[0] += pen;
	if (settings.b2_bandwidth && median(bw[1]) > settings.max_b2) cand.formant_error[1] += pen;
	if (settings.b3_bandwidth && median(bw[2]) > settings.max_b3) cand.formant_error[2] += pen;
	if (settings.rhotic && (mf2 - mf1) < 500 && mf3 < 2000) cand.formant_error[2] += pen;
	if (nf == 4)
	{
		if (settings.f4_frequency && mf4 < settings.min_f4) cand.formant_error[3] += pen;
		if (settings.f3f4_proximity && (mf2 - mf1) < 1500 && (mf4 - mf3) < 500) cand.formant_error[3] += pen;
	}

	double total = 0;
	for (int k = 0; k < nf; k++) total += cand.formant_error[k];
	cand.error = round1(total);
	cand.valid = true;

	return cand;
}

FastTrackResult fast_track(Sound &sound, int channel, double t1, double t2, const FastTrackSettings &settings)
{
	FastTrackResult result;
	auto times = fast_track_frame_times(t1, t2, settings.time_step);
	if (times.empty()) return result;

	double min_error = std::numeric_limits<double>::infinity();
	for (double ceiling : settings.ceilings())
	{
		auto frames = sound.get_formant_frames(channel, times, ceiling, settings.window_size, settings.lpc_order);
		auto cand = fast_track_score(times, frames, ceiling, settings);
		// Strictly smaller: the first of several equal errors wins, as in the plugin.
		if (cand.valid && cand.error < min_error)
		{
			min_error = cand.error;
			result.winner = int(result.candidates.size());
		}
		result.candidates.push_back(std::move(cand));
	}
	result.ok = (result.winner >= 0);

	return result;
}

} // namespace phonometrica::speech
