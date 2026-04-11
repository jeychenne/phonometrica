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
 * Created: 11/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Vowel normalization methods (Lobanov, Nearey 1 & 2, Watt & Fabricius). All functions are stateless and     *
 *          Qt-free so they can be called from both the GUI and the scripting engine.                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_VOWEL_NORMALIZER_HPP
#define PHONOMETRICA_VOWEL_NORMALIZER_HPP

#include <span>
#include <string>
#include <vector>

namespace phonometrica {

enum class VowelNormMethod
{
	Lobanov,       // z-score per formant per speaker
	Nearey1,       // per-formant log-mean subtraction per speaker
	Nearey2,       // grand log-mean subtraction per speaker (uniform)
	WattFabricius  // S-centroid transform per speaker
};

class VowelNormalizer
{
public:

	/// Lobanov (1971): z_ij = (F_ij - mean_j) / sd_j, computed per speaker.
	/// @param formants  One span per formant column (e.g. F1, F2, F3). All must have the same length.
	/// @param speakers  Speaker label for each row (same length as formant spans).
	/// @return One vector per formant column, in the same order.
	static std::vector<std::vector<double>> lobanov(
		const std::vector<std::span<const double>> &formants,
		const std::vector<std::string> &speakers);

	/// Nearey 1 (1978) — per-formant extrinsic: F*_ij = log(F_ij) - mean_j(log(F)), per speaker.
	static std::vector<std::vector<double>> nearey1(
		const std::vector<std::span<const double>> &formants,
		const std::vector<std::string> &speakers);

	/// Nearey 2 (1978) — uniform: F*_ij = log(F_ij) - grand_mean(log(F)), per speaker.
	static std::vector<std::vector<double>> nearey2(
		const std::vector<std::span<const double>> &formants,
		const std::vector<std::string> &speakers);

	/// Watt & Fabricius (2002): S-centroid transform using point vowels /i a u/.
	/// @param vowels  Vowel label for each row (same length as formant spans).
	/// @param label_i Label that identifies /i/ tokens.
	/// @param label_a Label that identifies /a/ tokens.
	/// @param label_u Label that identifies /u/ tokens.
	/// @note  Requires exactly 2 formant columns (F1, F2). F1_u is estimated as per Watt & Fabricius (2002).
	static std::vector<std::vector<double>> watt_fabricius(
		const std::vector<std::span<const double>> &formants,
		const std::vector<std::string> &speakers,
		const std::vector<std::string> &vowels,
		const std::string &label_i,
		const std::string &label_a,
		const std::string &label_u);

	/// Return a short suffix string for the given method (e.g. "lob", "nr1").
	static const char *method_suffix(VowelNormMethod method);

	/// Return a display name for the given method.
	static const char *method_name(VowelNormMethod method);
};

} // namespace phonometrica

#endif // PHONOMETRICA_VOWEL_NORMALIZER_HPP
