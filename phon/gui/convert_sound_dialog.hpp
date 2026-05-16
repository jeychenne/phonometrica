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
 * Created: 16/05/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for the "Convert..." action on sound files. Lets the user pick an output format, output path, and  *
 * an optional target sample rate (default: keep the source's sample rate). Channel count is preserved.                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CONVERT_SOUND_DIALOG_HPP
#define PHONOMETRICA_CONVERT_SOUND_DIALOG_HPP

#include <QDialog>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <phon/application/sound.hpp>

namespace phonometrica {

class ConvertSoundDialog final : public QDialog
{
	Q_OBJECT

public:

	// `sound` must be non-null and have a path on disk.
	ConvertSoundDialog(QWidget *parent, Sound *sound);

	// Chosen output path (UTF-8 String, ready for libsndfile).
	String outputPath() const;

	// Chosen output format.
	Sound::Format outputFormat() const;

	// Target sample rate, or 0 when the user chose "keep original".
	int targetSampleRate() const;

private slots:

	void onBrowse();
	void onFormatChanged(int index);
	void onKeepRateToggled(bool keep);

private:

	void refreshOkEnabled();
	void rebuildSuggestedPath();

	Sound *m_sound;          // source sound; never null
	int    m_source_rate;    // cached for the "Keep original" label

	QLineEdit   *m_path_edit;
	QPushButton *m_browse_button;
	QComboBox   *m_format_combo;

	QCheckBox *m_keep_rate;
	QSpinBox  *m_rate_spin;

	QPushButton *m_ok_button;
};

} // namespace phonometrica

#endif // PHONOMETRICA_CONVERT_SOUND_DIALOG_HPP
