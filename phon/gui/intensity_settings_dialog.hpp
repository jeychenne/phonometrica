/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 23/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog to edit intensity track settings (minimum/maximum dB, time step).                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_INTENSITY_SETTINGS_DIALOG_HPP
#define PHONOMETRICA_INTENSITY_SETTINGS_DIALOG_HPP

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>

namespace phonometrica {

class IntensitySettingsDialog : public QDialog
{
	Q_OBJECT

public:

	explicit IntensitySettingsDialog(QWidget *parent = nullptr);

private slots:

	void onOk();
	void onReset();

private:

	void displayValues();

	QLineEdit *m_min_edit;
	QLineEdit *m_max_edit;
	QLineEdit *m_step_edit;
};

} // namespace phonometrica

#endif // PHONOMETRICA_INTENSITY_SETTINGS_DIALOG_HPP
