/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 28/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog that displays a power spectrum (spectral slice) as a frequency-vs-power line plot. Similar to       *
 *          Praat's "View spectral slice". The user can hover over the plot to read frequency and power values.         *
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

class SpectrumView : public QDialog
{
	Q_OBJECT

public:

	/// Construct a spectrum view dialog.
	///
	/// @param spectrum   The spectrum document to display.
	/// @param parent     Parent widget.
	explicit SpectrumView(const Handle<Spectrum> &spectrum, QWidget *parent = nullptr);

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

	/// Render the full plot (background, axes, curve — no crosshair) into
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
