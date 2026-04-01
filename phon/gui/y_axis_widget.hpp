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