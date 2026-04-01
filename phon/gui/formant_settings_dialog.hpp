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
 * Purpose: Dialog to edit formant tracking settings (number of formants, max frequency, window length, LPC order,     *
 *          time step).                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FORMANT_SETTINGS_DIALOG_HPP
#define PHONOMETRICA_FORMANT_SETTINGS_DIALOG_HPP

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>

namespace phonometrica {

class FormantSettingsDialog : public QDialog
{
	Q_OBJECT

public:

	explicit FormantSettingsDialog(QWidget *parent = nullptr);

private slots:

	void onOk();
	void onReset();

private:

	void displayValues();

	QLineEdit *m_nformant_edit;
	QLineEdit *m_max_freq_edit;
	QLineEdit *m_window_edit;
	QLineEdit *m_lpc_edit;
	QLineEdit *m_step_edit;
};

} // namespace phonometrica

#endif // PHONOMETRICA_FORMANT_SETTINGS_DIALOG_HPP
