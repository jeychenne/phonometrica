/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 24/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog to edit pitch tracking settings (method, min/max pitch, time step, voicing threshold).              *
 *          The voicing threshold has both a text entry and a slider whose range depends on the selected method.        *
 *          Praat-specific parameters (octave jump cost, voicing cost) are shown only when Praat is selected.           *
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
	QLabel *m_octave_label;
	QLineEdit *m_octave_edit;
	QLabel *m_voicing_cost_label;
	QLineEdit *m_voicing_cost_edit;

	int m_slider_min = 0;
	int m_slider_max = 100;

	bool m_updating = false;
};

} // namespace phonometrica

#endif // PHONOMETRICA_PITCH_SETTINGS_DIALOG_HPP
