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
 * Created: 19/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for the Speech > Find silences... action. Configures the SilenceDetector (sound, threshold,         *
 * duration limits, padding) and the output annotation layer (layer name, silence label, speech label). All choices   *
 * are persisted via QSettings under the "find_silences/" prefix.                                                      *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FIND_SILENCES_DIALOG_HPP
#define PHONOMETRICA_FIND_SILENCES_DIALOG_HPP

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <phon/application/sound.hpp>
#include <phon/application/silence_detector.hpp>

namespace phonometrica {

class FindSilencesDialog final : public QDialog
{
	Q_OBJECT

public:

	explicit FindSilencesDialog(QWidget *parent);

	// The sound selected by the user (valid after exec() returns Accepted).
	Handle<Sound> sound() const;

	// The configured detector options (valid after exec() returns Accepted).
	SilenceDetector::Options options() const;

	// The name to give the new annotation layer.
	QString layerName() const;

	// The text to put in silence and speech intervals respectively. Either may be
	// empty; empty is the natural default for a word-list fill-in workflow.
	QString silenceLabel() const;
	QString speechLabel() const;

private:

	// Populated from the current project; index maps into m_sounds.
	QComboBox *m_sound_combo;

	QLineEdit      *m_layer_edit;
	QDoubleSpinBox *m_silence_threshold_spin;
	QDoubleSpinBox *m_min_silence_spin;
	QDoubleSpinBox *m_min_speech_spin;
	QDoubleSpinBox *m_padding_spin;
	QLineEdit      *m_silence_label_edit;
	QLineEdit      *m_speech_label_edit;

	Array<Handle<Sound>> m_sounds;
};

} // namespace phonometrica

#endif // PHONOMETRICA_FIND_SILENCES_DIALOG_HPP
