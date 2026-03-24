/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
