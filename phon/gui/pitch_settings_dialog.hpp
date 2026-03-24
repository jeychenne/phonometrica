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
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PITCH_SETTINGS_DIALOG_HPP
#define PHONOMETRICA_PITCH_SETTINGS_DIALOG_HPP

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
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

	// Convert between slider integer and floating-point threshold.
	int thresholdToSlider(double value) const;
	double sliderToThreshold(int value) const;

	QComboBox *m_method_combo;
	QLineEdit *m_min_edit;
	QLineEdit *m_max_edit;
	QLineEdit *m_step_edit;
	QLineEdit *m_voicing_edit;
	QSlider *m_voicing_slider;

	// Current slider range (in hundredths).
	int m_slider_min = 0;
	int m_slider_max = 100;

	bool m_updating = false; // guard against signal loops
};

} // namespace phonometrica

#endif // PHONOMETRICA_PITCH_SETTINGS_DIALOG_HPP
