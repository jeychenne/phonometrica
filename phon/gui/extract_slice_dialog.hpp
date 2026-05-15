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
 * Purpose: Dialog for the "Extract slice..." action. Takes either an annotation, a sound, or both (when the           *
 * annotation has a bound sound), and produces sliced output(s). When both are extracted in a single operation, the    *
 * new annotation is automatically bound to the new sound.                                                             *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_EXTRACT_SLICE_DIALOG_HPP
#define PHONOMETRICA_EXTRACT_SLICE_DIALOG_HPP

#include <QDialog>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QRadioButton>
#include <QGroupBox>
#include <phon/application/annotation.hpp>
#include <phon/application/sound.hpp>

namespace phonometrica {

class ExtractSliceDialog final : public QDialog
{
	Q_OBJECT

public:

	enum class Mode { AnnotationOnly, SoundOnly, Both };

	// Pass non-null for the side(s) being sliced. At least one must be non-null.
	// When both are passed, the dialog exposes a mode picker.
	ExtractSliceDialog(QWidget *parent, Annotation *annot, Sound *sound);

	Mode mode() const;

	double startTime() const;
	double endTime() const;
	bool   clipPartial() const;

	// Output paths (each empty unless mode includes that side).
	String annotationOutputPath() const;
	Annotation::Type annotationOutputFormat() const;

	String soundOutputPath() const;
	Sound::Format soundOutputFormat() const;

private slots:

	void onModeChanged();
	void onBrowseAnnotation();
	void onBrowseSound();
	void onAnnotationFormatChanged(int index);
	void onSoundFormatChanged(int index);

private:

	void refreshOkEnabled();
	void refreshSectionVisibility();
	void rebuildSuggestedPaths();

	Annotation *m_annot;       // may be null (sound-only invocation)
	Sound      *m_sound;       // may be null (annotation-only invocation)
	double      m_duration;    // upper bound for the time spinners

	// Mode picker (only shown when both annot and sound were passed in).
	QGroupBox    *m_mode_group;
	QRadioButton *m_mode_both;
	QRadioButton *m_mode_annot_only;
	QRadioButton *m_mode_sound_only;

	// Time range.
	QDoubleSpinBox *m_start_spin;
	QDoubleSpinBox *m_end_spin;

	// Annotation output section.
	QGroupBox *m_annot_group;
	QCheckBox *m_clip_check;
	QLineEdit *m_annot_path_edit;
	QPushButton *m_annot_browse;
	QComboBox *m_annot_format_combo;

	// Sound output section.
	QGroupBox *m_sound_group;
	QLineEdit *m_sound_path_edit;
	QPushButton *m_sound_browse;
	QComboBox *m_sound_format_combo;

	QPushButton *m_ok_button;
};

} // namespace phonometrica

#endif // PHONOMETRICA_EXTRACT_SLICE_DIALOG_HPP
