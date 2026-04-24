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
 * Purpose: Non-modal, Praat-style dialog to edit pitch tracking settings (method, min/max pitch, time step, voicing   *
 *          threshold, and — when Praat is selected — its four additional parameters). The dialog stays above its      *
 *          parent window (Qt::Tool) so the user can tweak parameters, click Apply, see the effect, and iterate.       *
 *          See formant_settings_dialog.hpp for the design precedent.                                                  *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PITCH_SETTINGS_DIALOG_HPP
#define PHONOMETRICA_PITCH_SETTINGS_DIALOG_HPP

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QString>

namespace phonometrica {

class PitchSettingsDialog : public QDialog
{
	Q_OBJECT

public:

	explicit PitchSettingsDialog(QWidget *parent = nullptr);

	// Overridden so Escape and the title-bar close button trigger the
	// snapshot-revert logic (they default to calling reject()).
	void reject() override;

signals:

	void settingsApplied();

private slots:

	void onOk();
	void onApply();
	void onCancel();
	void onResetToDefaults();
	void onMethodChanged(int index);
	void onSliderMoved(int value);
	void onVoicingEdited();

private:

	void displayCurrentValues();
	void displayDefaultValues();
	bool validateAndCommit();
	void snapshotSettings();
	bool restoreSnapshot();

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
	QLabel *m_silence_label;
	QLineEdit *m_silence_edit;
	QLabel *m_octave_cost_label;
	QLineEdit *m_octave_cost_edit;
	QLabel *m_octave_jump_label;
	QLineEdit *m_octave_jump_edit;
	QLabel *m_voicing_cost_label;
	QLineEdit *m_voicing_cost_edit;
	QCheckBox *m_gaussian_check;

	QPushButton *m_ok_btn;
	QPushButton *m_apply_btn;

	int m_slider_min = 0;
	int m_slider_max = 100;

	bool m_updating = false;

	// Snapshot always captures all keys — including the four Praat-specific
	// ones — regardless of which method is currently selected. That way, a
	// user who switches method from reaper→praat, clicks Apply (writing the
	// Praat params), then clicks Cancel gets a full revert to the pre-dialog
	// state including those Praat params.
	struct {
		QString method;
		int     min_pitch;
		int     max_pitch;
		double  time_step;
		double  voicing_threshold;
		double  silence_threshold;
		double  octave_cost;
		double  octave_jump_cost;
		double  voicing_cost;
		bool    use_gaussian;
		bool    applied_any;
	} m_snapshot;
};

} // namespace phonometrica

#endif // PHONOMETRICA_PITCH_SETTINGS_DIALOG_HPP
