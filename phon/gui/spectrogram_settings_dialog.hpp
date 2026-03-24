/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 23/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog to edit spectrogram settings (window size, frequency range, window type, dynamic range,             *
 *          pre-emphasis threshold).                                                                                    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SPECTROGRAM_SETTINGS_DIALOG_HPP
#define PHONOMETRICA_SPECTROGRAM_SETTINGS_DIALOG_HPP

#include <QDialog>
#include <QRadioButton>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>

namespace phonometrica {

class SpectrogramSettingsDialog : public QDialog
{
	Q_OBJECT

public:

	explicit SpectrogramSettingsDialog(QWidget *parent = nullptr);

private slots:

	void onOk();
	void onReset();
	void onDynamicRangeChanged(int value);
	void onBandTypeChanged();

private:

	void displayValues();
	void enableCustomBand(bool value);
	void setDynamicRangeLabel(int value);
	void resetAndDisplay();

	QRadioButton *m_wide_btn;
	QRadioButton *m_narrow_btn;
	QRadioButton *m_custom_btn;
	QLineEdit *m_winlen_edit;
	QLineEdit *m_bandwidth_edit;
	QLineEdit *m_preemph_edit;
	QComboBox *m_window_combo;
	QSlider *m_dyn_range_slider;
	QLabel *m_dyn_range_label;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SPECTROGRAM_SETTINGS_DIALOG_HPP
