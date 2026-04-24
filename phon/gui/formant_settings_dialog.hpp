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
 * Created: 24/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Non-modal, Praat-style dialog to edit formant tracking settings. The dialog stays above its parent window  *
 *          (Qt::Tool) so the user can tweak parameters, click Apply, see the effect on the spectrogram, and iterate.  *
 *          Apply validates and commits to Settings, then emits settingsApplied() so the owner can refresh its views.  *
 *          Reset fills the fields with defaults but does NOT commit until Apply is clicked.                           *
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

	// Overridden so Escape and the title-bar close button both trigger the
	// snapshot-revert logic (they default to calling reject()). Without this,
	// closing via those paths would silently drop any Apply-committed changes
	// without reverting — which would contradict the "Cancel means undo"
	// semantics we want.
	void reject() override;

signals:

	// Emitted after Apply has validated the fields and written them to the
	// Settings registry. Owners (e.g. SoundView) connect this to their
	// view-refresh slot.
	void settingsApplied();

private slots:

	void onOk();
	void onApply();
	void onCancel();
	void onResetToDefaults();

private:

	// Reads the current values from Settings and populates the line edits.
	void displayCurrentValues();

	// Populates the line edits with Phonometrica's default values WITHOUT
	// touching the Settings registry. Matches Praat's "Standards" behavior:
	// the user still has to click Apply to commit.
	void displayDefaultValues();

	// Parses and validates all fields. On success, writes to Settings and
	// returns true. On failure, shows an error dialog and returns false.
	bool validateAndCommit();

	// Captures the current Settings values so Cancel can revert any Apply()
	// calls made during this dialog's lifetime.
	void snapshotSettings();

	// Writes the snapshot back to Settings. Returns true iff anything
	// actually changed (i.e. at least one Apply happened), so the caller
	// knows whether to emit settingsApplied.
	bool restoreSnapshot();

	QLineEdit *m_nformant_edit;
	QLineEdit *m_max_freq_edit;
	QLineEdit *m_window_edit;
	QLineEdit *m_lpc_edit;
	QLineEdit *m_step_edit;

	QPushButton *m_ok_btn;
	QPushButton *m_apply_btn;

	// Snapshot of Settings at dialog-open time. If the user clicks Apply
	// (committing new values) and then Cancel, we revert to these. If they
	// click OK or hit Esc without having Applied, the snapshot is discarded.
	struct {
		int    nformant;
		int    max_freq;
		double window_size;   // seconds
		int    lpc_order;
		double time_step;     // seconds
		bool   applied_any;   // set true on every successful validateAndCommit
	} m_snapshot;
};

} // namespace phonometrica

#endif // PHONOMETRICA_FORMANT_SETTINGS_DIALOG_HPP
