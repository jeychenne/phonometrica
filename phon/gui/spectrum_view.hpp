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
 * Created: 28/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog that displays a power spectrum (spectral slice) as a frequency-vs-power line plot. Similar to       *
 *          Praat's "View spectral slice". The user can hover over the plot to read frequency and power values.         *
 *                                                                                                                     *
 *          Supports three display modes:                                                                              *
 *            - FFT only:      traditional power spectrum (blue curve).                                                *
 *            - LPC only:      smooth spectral envelope from LPC analysis (red curve).                                 *
 *            - FFT + LPC:     FFT spectrum with LPC envelope superimposed.                                            *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SPECTRUM_VIEW_HPP
#define PHONOMETRICA_SPECTRUM_VIEW_HPP

#include <QDialog>
#include <QLabel>
#include <QPixmap>
#include <QToolBar>
#include <phon/application/spectrum.hpp>

namespace phonometrica {

/// Display mode for the spectral slice dialog.
enum class SpectrumDisplayMode
{
	FFT,    ///< Show only the FFT power spectrum.
	LPC,    ///< Show only the LPC spectral envelope.
	Both    ///< Show both FFT and LPC superimposed.
};


class SpectrumView : public QDialog
{
	Q_OBJECT

public:

	/// Construct a spectrum view dialog.
	///
	/// @param spectrum   The spectrum document to display.
	/// @param mode       Which curve(s) to draw (FFT, LPC, or both).
	/// @param parent     Parent widget.
	explicit SpectrumView(const Handle<Spectrum> &spectrum,
	                      SpectrumDisplayMode mode = SpectrumDisplayMode::FFT,
	                      QWidget *parent = nullptr);

	QSize sizeHint() const override { return {700, 400}; }

protected:

	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void leaveEvent(QEvent *event) override;

private slots:

	void onSavePNG();
	void onSavePDF();
	void onSaveSVG();

private:

	void rebuildCache();

	/// Render the full plot (background, axes, curve(s) — no crosshair) into
	/// the given painter at the specified logical dimensions.
	void renderPlot(QPainter &p, int w, int h);

	// ── Coordinate mapping ───────────────────────
	double frequencyToX(double hz) const;
	double xToFrequency(double x) const;
	double dBToY(double dB) const;
	double yToDB(double y) const;

	// Margins around the plot area (in logical pixels).
	int marginLeft() const;
	int marginRight() const;
	int marginTop() const;
	int marginBottom() const;

	// Plot area dimensions.
	int plotWidth() const;
	int plotHeight() const;

	Handle<Spectrum> m_spectrum;

	// Which curve(s) to display.
	SpectrumDisplayMode m_mode = SpectrumDisplayMode::FFT;

	// Toolbar with export actions.
	QToolBar *m_toolbar = nullptr;

	// Cached rendering of the spectrum curve.
	QPixmap m_cache;
	bool m_cache_valid = false;

	// Mouse position for readout (in widget coordinates); negative if outside.
	double m_mouse_x = -1;
	double m_mouse_y = -1;

	// Status bar label at the bottom of the dialog.
	QLabel *m_status = nullptr;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SPECTRUM_VIEW_HPP
