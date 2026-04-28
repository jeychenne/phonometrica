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
 * Created: 17/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog to configure a whisper.cpp transcription run: sound file, model file, language, translate flag and  *
 * layer label. Model path and language are persisted via QSettings across sessions.                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_TRANSCRIBE_DIALOG_HPP
#define PHONOMETRICA_TRANSCRIBE_DIALOG_HPP

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <phon/application/sound.hpp>
#include <phon/application/transcriber.hpp>

namespace phonometrica {

class TranscribeDialog final : public QDialog
{
	Q_OBJECT

public:

	explicit TranscribeDialog(QWidget *parent);

	// The sound selected by the user (valid after exec() returns Accepted).
	Handle<Sound> sound() const;

	// The configured transcriber options (valid after exec() returns Accepted).
	Transcriber::Options options() const;

private slots:

	void onBrowseModel();

private:

	// Populated from the current project; index maps into m_sounds.
	QComboBox *m_sound_combo;

	QLineEdit *m_model_edit;
	QPushButton *m_browse_button;
	QComboBox *m_language_combo;
	QLineEdit *m_layer_edit;

	Array<Handle<Sound>> m_sounds;
};

} // namespace phonometrica

#endif // PHONOMETRICA_TRANSCRIBE_DIALOG_HPP
