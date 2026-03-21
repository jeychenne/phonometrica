/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 06/11/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: Implement Weenink's method for automatic formant measurement. See:                                         *
 * Weenink, D. J. M. (2015). "Improved formant frequency measurements of short segments". In Proceedings of the 18th   *
 * International Congress of Phonetic Sciences Glasgow: The University of Glasgow.                                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_WEENINK_HPP
#define PHONOMETRICA_WEENINK_HPP

#include <cassert>
#include <iostream>
#include <phon/third_party/Eigen/Dense>
#include <phon/third_party/Eigen/SVD>
#include <phon/string.hpp>
#include <phon/utils/file_system.hpp>
#include <phon/utils/matrix.hpp>

namespace phonometrica {
class Sound;
}

namespace phonometrica::speech {

class WeeninkModel final
{
public:

	WeeninkModel(unsigned int formants, int p) :
		coeff(p, formants), var(p, formants), observations(formants, 0), chi2(formants), p(p)
	{ }

	// Predict formant value at time t based on a fitted model
	double predict(double t, unsigned int formant) const;

	// Score the fitted model
	double score(double t = 1.2) const;

	unsigned int formant_count() const { return (unsigned int) coeff.cols(); }

	// An MxN matrix: rows represent parameter values (one per Legendre polynomial) and columns represent measured formants.
	Matrix<double> coeff;

	// An MxN matrix where each cell var(i,j) contains the variance of parameter param(i,j).
	Matrix<double> var;

	// Number of observations (excluding undefined values) for each formant.
	std::vector<unsigned int> observations;

	// A vector of goodness-of-fit values (1 per formant)
	Vector<double> chi2;

	// Number of polynomials
	int p;

	// Whether smoothness could be properly estimated.
	bool success = false;
};


/**
 * Evaluate Legendre polynomial of degree n (up to degree 7).
 * @param x value to be evaluated
 * @param n degree of the polynomial
 * @return y value
 */
double legendre(double x, unsigned int n);

/**
 * Model a vocoid segment's formants using Weenink's method. Each formant track is modeled using a linear combination
 * of Legendre polynomials.
 * @param F an NxM matrix with M formants (typically F1, F2, F3) measured at N time points
 * @param B an NxM matrix where each cell (i,j) contains the bandwidth corresponding to formant F(i,j)
 * @param p the number of Legendre polynomial to be included in the model, starting at degree 0
 * @return a model object for the segment
 */
WeeninkModel model_segment(const Matrix<double> &F, const Matrix<double> &B, unsigned int p = 4);

// Find the best <Nyquist frequency, LPC order> pair for a vocoid given a set of parameter to search for.
std::pair<double, double>
find_lpc_parameters(Sound *sound, int channel, int nformant, double win_size, double t1, double t2, double max_freq1, double max_freq2, double step, int lpc_order1, int lpc_order2);


} // namespace phonometrica::speech

#endif // PHONOMETRICA_WEENINK_HPP
