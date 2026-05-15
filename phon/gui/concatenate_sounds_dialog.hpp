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
 * Created: 12/05/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for the "Concatenate sounds..." action. Lets the user reorder the selected sounds and pick an      *
 * output path/format. Sample-rate and channel-count compatibility is checked upfront: any mismatch is reported in    *
 * red at the top of the dialog and OK is blocked until the user resolves it (by resampling outside Phonometrica).   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CONCATENATE_SOUNDS_DIALOG_HPP
#define PHONOMETRICA_CONCATENATE_SOUNDS_DIALOG_HPP

#include <QDialog>
#include <QList>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QGroupBox>
#include <phon/application/sound.hpp>

namespace phonometrica {

class ConcatenateSoundsDialog final : public QDialog
{
	Q_OBJECT

public:

	// `candidates` are the sounds selected in the file manager (2 or more).
	ConcatenateSoundsDialog(QWidget *parent, const QList<Sound *> &candidates);

	// User-ordered list.
	QList<Sound *> orderedSources() const;

	// Output path and format.
	String outputPath() const;
	Sound::Format outputFormat() const;

private slots:

	void onBrowse();
	void onFormatChanged(int index);

private:

	void refreshOkEnabled();
	void rebuildSuggestedPath();

	QList<Sound *> m_candidates;
	bool m_compatible = false;

	QLabel      *m_warning_label;
	QListWidget *m_order_list;

	QLineEdit   *m_path_edit;
	QPushButton *m_browse_button;
	QComboBox   *m_format_combo;
	QPushButton *m_ok_button;
};

} // namespace phonometrica

#endif // PHONOMETRICA_CONCATENATE_SOUNDS_DIALOG_HPP
