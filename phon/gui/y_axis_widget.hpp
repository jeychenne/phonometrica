/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Widget to display labels to the left of waveforms, spectrograms, pitch, intensity, and annotation         *
 *          layers in sound and annotation views.                                                                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_Y_AXIS_WIDGET_HPP
#define PHONOMETRICA_Y_AXIS_WIDGET_HPP

#include <vector>
#include <QWidget>
#include <phon/gui/time_model.hpp>

namespace phonometrica {

class WaveformWidget;
class SpectrogramWidget;
class PitchWidget;
class IntensityWidget;
class LayerWidget;

class YAxisWidget final : public QWidget
{
	Q_OBJECT

public:

	YAxisWidget(TimeModel *model, QWidget *parent = nullptr);

	void addWaveform(WaveformWidget *wf);
	void addSpectrogram(SpectrogramWidget *sg);
	void addPitch(PitchWidget *pw);
	void addIntensity(IntensityWidget *iw);
	void addLayer(LayerWidget *lw);

protected:

	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;

private:

	TimeModel *m_model;
	std::vector<WaveformWidget *> m_waveforms;
	std::vector<SpectrogramWidget *> m_spectrograms;
	std::vector<PitchWidget *> m_pitches;
	std::vector<IntensityWidget *> m_intensities;
	std::vector<LayerWidget *> m_layers;
};

} // namespace phonometrica

#endif // PHONOMETRICA_Y_AXIS_WIDGET_HPP