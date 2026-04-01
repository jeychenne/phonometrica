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
