/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QPainter>
#include <QMouseEvent>
#include <phon/gui/y_axis_widget.hpp>
#include <phon/gui/waveform_widget.hpp>
#include <phon/gui/spectrogram_widget.hpp>
#include <phon/gui/pitch_widget.hpp>
#include <phon/gui/intensity_widget.hpp>
#include <phon/gui/layer_widget.hpp>

namespace phonometrica {

static constexpr int Y_AXIS_WIDTH = 60;
static constexpr int LABEL_PADDING = 2;

YAxisWidget::YAxisWidget(TimeModel *model, QWidget *parent) :
	QWidget(parent), m_model(model)
{
	setFixedWidth(Y_AXIS_WIDTH);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

	connect(m_model, &TimeModel::viewportChanged, this, qOverload<>(&QWidget::update));
}

void YAxisWidget::addWaveform(WaveformWidget *wf)
{
	m_waveforms.push_back(wf);
}

void YAxisWidget::addSpectrogram(SpectrogramWidget *sg)
{
	m_spectrograms.push_back(sg);
}

void YAxisWidget::addIntensity(IntensityWidget *iw)
{
	m_intensities.push_back(iw);
}

void YAxisWidget::addPitch(PitchWidget *pw)
{
	m_pitches.push_back(pw);
}

void YAxisWidget::addLayer(LayerWidget *lw)
{
	m_layers.push_back(lw);
}

void YAxisWidget::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::TextAntialiasing);
	QFontMetrics fm = painter.fontMetrics();
	int w = width();

	for (auto *wf : m_waveforms)
	{
		if (!wf->isVisible())
			continue;

		// Map the waveform's top-left corner into our coordinate system.
		// The Y axis and waveforms are siblings, so we go through global coordinates.
		QPoint top_left = mapFromGlobal(wf->mapToGlobal(QPoint(0, 0)));
		int y_offset = top_left.y();
		int wf_height = wf->height();

		if (wf_height < fm.height() * 3)
			continue; // too small to draw labels

		double mag = wf->magnitude();

		// Top label: +magnitude
		QString top = QString("+%1").arg(mag, 0, 'f', 1);
		int tw = fm.horizontalAdvance(top);
		int x = w - tw - LABEL_PADDING;
		painter.drawText(x, y_offset + fm.ascent(), top);

		// Centre label: 0
		QString centre("0");
		tw = fm.horizontalAdvance(centre);
		x = w - tw - LABEL_PADDING;
		int y_mid = y_offset + (wf_height - fm.height()) / 2;
		painter.drawText(x, y_mid + fm.ascent(), centre);

		// Bottom label: -magnitude
		QString bottom = QString("-%1").arg(mag, 0, 'f', 1);
		tw = fm.horizontalAdvance(bottom);
		x = w - tw - LABEL_PADDING;
		int y_bot = y_offset + wf_height - fm.height();
		painter.drawText(x, y_bot + fm.ascent(), bottom);
	}

	for (auto *sg : m_spectrograms)
	{
		if (!sg->isVisible())
			continue;

		QPoint top_left = mapFromGlobal(sg->mapToGlobal(QPoint(0, 0)));
		int y_offset = top_left.y();
		int sg_height = sg->height();

		if (sg_height < fm.height() * 2)
			continue;

		// Top label: max frequency.
		QString top = QString("%1 Hz").arg(int(sg->maxFrequency()));
		int tw = fm.horizontalAdvance(top);
		int x = w - tw - LABEL_PADDING;
		painter.drawText(x, y_offset + fm.ascent(), top);

		// Bottom label: 0 Hz.
		QString bottom("0 Hz");
		tw = fm.horizontalAdvance(bottom);
		x = w - tw - LABEL_PADDING;
		int y_bot = y_offset + sg_height - fm.height();
		painter.drawText(x, y_bot + fm.ascent(), bottom);
	}

	for (auto *pw : m_pitches)
	{
		if (!pw->isVisible())
			continue;

		QPoint top_left = mapFromGlobal(pw->mapToGlobal(QPoint(0, 0)));
		int y_offset = top_left.y();
		int pw_height = pw->height();

		if (pw_height < fm.height() * 2)
			continue;

		// Top label: max pitch.
		QString top = QString("%1 Hz").arg(int(pw->maxPitch()));
		int tw = fm.horizontalAdvance(top);
		int x = w - tw - LABEL_PADDING;
		painter.drawText(x, y_offset + fm.ascent(), top);

		// Bottom label: min pitch.
		QString bottom = QString("%1 Hz").arg(int(pw->minPitch()));
		tw = fm.horizontalAdvance(bottom);
		x = w - tw - LABEL_PADDING;
		int y_bot = y_offset + pw_height - fm.height();
		painter.drawText(x, y_bot + fm.ascent(), bottom);
	}

	for (auto *iw : m_intensities)
	{
		if (!iw->isVisible())
			continue;

		QPoint top_left = mapFromGlobal(iw->mapToGlobal(QPoint(0, 0)));
		int y_offset = top_left.y();
		int iw_height = iw->height();

		if (iw_height < fm.height() * 2)
			continue;

		// Top label: max intensity.
		QString top = QString("%1 dB").arg(int(iw->maxIntensity()));
		int tw = fm.horizontalAdvance(top);
		int x = w - tw - LABEL_PADDING;
		painter.drawText(x, y_offset + fm.ascent(), top);

		// Bottom label: min intensity.
		QString bottom = QString("%1 dB").arg(int(iw->minIntensity()));
		tw = fm.horizontalAdvance(bottom);
		x = w - tw - LABEL_PADDING;
		int y_bot = y_offset + iw_height - fm.height();
		painter.drawText(x, y_bot + fm.ascent(), bottom);
	}

	for (auto *lw : m_layers)
	{
		if (!lw->isVisible())
			continue;

		QPoint top_left = mapFromGlobal(lw->mapToGlobal(QPoint(0, 0)));
		int y_offset = top_left.y();
		int lw_height = lw->height();

		if (lw_height < fm.height())
			continue;

		// Draw the layer label, centered vertically, right-aligned.
		auto label = QString::number(lw->layerIndex());
		int tw = fm.horizontalAdvance(label);
		int x = w - tw - LABEL_PADDING;
		int y_mid = y_offset + (lw_height - fm.height()) / 2;
		painter.drawText(x, y_mid + fm.ascent(), label);
	}
}

void YAxisWidget::mousePressEvent(QMouseEvent *)
{
	m_model->clearSelection();
}

} // namespace phonometrica
