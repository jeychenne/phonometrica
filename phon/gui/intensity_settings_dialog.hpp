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
 * Purpose: Non-modal, Praat-style dialog to edit intensity track settings (minimum/maximum dB, time step). The dialog *
 *          stays above its parent window (Qt::Tool) so the user can tweak parameters, click Apply, see the effect,    *
 *          and iterate. See formant_settings_dialog.hpp for the design precedent.                                     *
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

	// Overridden so Escape and the title-bar close button trigger the
	// snapshot-revert logic (they default to calling reject()).
	void reject() override;

signals:

	// Emitted after a successful Apply (or a revert-on-Cancel that actually
	// changed anything). Owners connect this to their view-refresh slot.
	void settingsApplied();

private slots:

	void onOk();
	void onApply();
	void onCancel();
	void onResetToDefaults();

private:

	void displayCurrentValues();
	void displayDefaultValues();
	bool validateAndCommit();
	void snapshotSettings();
	bool restoreSnapshot();

	QLineEdit *m_min_edit;
	QLineEdit *m_max_edit;
	QLineEdit *m_step_edit;

	QPushButton *m_ok_btn;
	QPushButton *m_apply_btn;

	struct {
		int    min_db;
		int    max_db;
		double time_step;   // seconds
		bool   applied_any;
	} m_snapshot;
};

} // namespace phonometrica

#endif // PHONOMETRICA_INTENSITY_SETTINGS_DIALOG_HPP
