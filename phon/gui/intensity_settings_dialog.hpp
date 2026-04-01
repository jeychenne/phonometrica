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
