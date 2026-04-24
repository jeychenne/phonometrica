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
 * Purpose: Non-modal, Praat-style dialog to edit spectrogram settings (window size, frequency range, window type,     *
 *          dynamic range, pre-emphasis threshold). The dialog stays above its parent window (Qt::Tool) so the user    *
 *          can tweak parameters, click Apply, see the effect, and iterate. See formant_settings_dialog.hpp for the    *
 *          design precedent.                                                                                          *
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
#include <QString>

namespace phonometrica {

class SpectrogramSettingsDialog : public QDialog
{
	Q_OBJECT

public:

	explicit SpectrogramSettingsDialog(QWidget *parent = nullptr);

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
	void onDynamicRangeChanged(int value);
	void onBandTypeChanged();

private:

	void displayCurrentValues();
	void displayDefaultValues();
	bool validateAndCommit();
	void snapshotSettings();
	bool restoreSnapshot();
	void enableCustomBand(bool value);
	void setDynamicRangeLabel(int value);

	// Populates all widgets from an in-memory value tuple. Used by both
	// displayCurrentValues() (Settings-sourced) and displayDefaultValues()
	// (hard-coded defaults).
	void applyValuesToWidgets(double window_size, int freq_range,
	                          const QString &window_type, int dynamic_range,
	                          int preemph_threshold);

	QRadioButton *m_wide_btn;
	QRadioButton *m_narrow_btn;
	QRadioButton *m_custom_btn;
	QLineEdit *m_winlen_edit;
	QLineEdit *m_bandwidth_edit;
	QLineEdit *m_preemph_edit;
	QComboBox *m_window_combo;
	QSlider *m_dyn_range_slider;
	QLabel *m_dyn_range_label;

	QPushButton *m_ok_btn;
	QPushButton *m_apply_btn;

	struct {
		double  window_size;       // seconds
		int     freq_range;        // Hz
		QString window_type;
		int     dynamic_range;     // dB
		int     preemph_threshold; // Hz
		bool    applied_any;
	} m_snapshot;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SPECTROGRAM_SETTINGS_DIALOG_HPP
