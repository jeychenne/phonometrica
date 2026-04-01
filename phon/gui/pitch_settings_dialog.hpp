/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any      *
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
 * Purpose: Dialog to edit pitch tracking settings (method, min/max pitch, time step, voicing threshold).              *
 *          The voicing threshold has both a text entry and a slider whose range depends on the selected method.        *
 *          Praat-specific parameters are shown only when Praat is selected.                                           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PITCH_SETTINGS_DIALOG_HPP
#define PHONOMETRICA_PITCH_SETTINGS_DIALOG_HPP

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>

namespace phonometrica {

class PitchSettingsDialog : public QDialog
{
	Q_OBJECT

public:

	explicit PitchSettingsDialog(QWidget *parent = nullptr);

private slots:

	void onOk();
	void onReset();
	void onMethodChanged(int index);
	void onSliderMoved(int value);
	void onVoicingEdited();

private:

	void displayValues();
	void updateSliderRange(const QString &method);
	void setVoicingDefault(const QString &method);
	void updatePraatFieldsVisibility(const QString &method);

	int thresholdToSlider(double value) const;
	double sliderToThreshold(int value) const;

	QComboBox *m_method_combo;
	QLineEdit *m_min_edit;
	QLineEdit *m_max_edit;
	QLineEdit *m_step_edit;
	QLineEdit *m_voicing_edit;
	QSlider *m_voicing_slider;

	// Praat-specific fields.
	QLabel *m_silence_label;
	QLineEdit *m_silence_edit;
	QLabel *m_octave_cost_label;
	QLineEdit *m_octave_cost_edit;
	QLabel *m_octave_jump_label;
	QLineEdit *m_octave_jump_edit;
	QLabel *m_voicing_cost_label;
	QLineEdit *m_voicing_cost_edit;

	int m_slider_min = 0;
	int m_slider_max = 100;

	bool m_updating = false;
};

} // namespace phonometrica

#endif // PHONOMETRICA_PITCH_SETTINGS_DIALOG_HPP
