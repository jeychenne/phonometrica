/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2026 Julien Eychenne                                                                                  *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more        *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 09/05/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for the Speech > Record sound... action. Lets the user pick an input device, sample rate, channel  *
 * count, output file and format, then drives a SoundRecorder. The dialog stays modal while recording: capture is in  *
 * a background thread (the recorder spins its own writer); the dialog only polls duration and peak level via QTimer. *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_RECORD_SOUND_DIALOG_HPP
#define PHONOMETRICA_RECORD_SOUND_DIALOG_HPP

#include <memory>
#include <vector>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <phon/string.hpp>
#include <phon/application/sound.hpp>
#include <phon/application/sound_recorder.hpp>

namespace phonometrica {

class RecordSoundDialog final : public QDialog
{
	Q_OBJECT

public:

	explicit RecordSoundDialog(QWidget *parent);

	// Path of the recording on disk (valid after exec() returns Accepted).
	String savedPath() const { return m_saved_path; }

	// True when the user wanted the recording to be added to the project.
	bool addToProject() const;

protected:

	void closeEvent(QCloseEvent *event) override;
	void reject() override;

private slots:

	void onDeviceChanged();
	void onFormatChanged();
	void onBrowse();
	void onRecordToggled();
	void onTick();

private:

	void populateDevices();
	void populateSampleRates();
	void populateChannels();
	void populateFormats();
	void updateUiForState();
	Sound::Format selectedFormat() const;
	QString suggestedExtension() const;
	QString defaultFileName() const;

	// Inputs.
	QComboBox    *m_device_combo;
	QComboBox    *m_rate_combo;
	QComboBox    *m_channel_combo;
	QComboBox    *m_format_combo;
	QLineEdit    *m_path_edit;
	QPushButton  *m_browse_btn;
	QCheckBox    *m_add_to_project;

	// Run controls.
	QPushButton  *m_record_btn;
	QPushButton  *m_close_btn;
	QLabel       *m_elapsed_label;
	QLabel       *m_status_label;
	QProgressBar *m_peak_bar;

	QTimer       *m_tick;

	std::vector<SoundRecorder::InputDevice> m_devices;
	std::unique_ptr<SoundRecorder>          m_recorder;

	// True between start() and stop(); used to drive UI state transitions.
	bool   m_recording = false;
	String m_saved_path;
};

} // namespace phonometrica

#endif // PHONOMETRICA_RECORD_SOUND_DIALOG_HPP
