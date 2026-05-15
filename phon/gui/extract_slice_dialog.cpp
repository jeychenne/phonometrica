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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cassert>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <phon/gui/extract_slice_dialog.hpp>
#include <phon/gui/file_dialog.hpp>
#include <phon/application/annotation_ops.hpp>
#include <phon/application/constants.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

ExtractSliceDialog::ExtractSliceDialog(QWidget *parent, Annotation *annot, Sound *sound) :
	QDialog(parent),
	m_annot(annot),
	m_sound(sound)
{
	assert(m_annot != nullptr || m_sound != nullptr);

	setWindowTitle(tr("Extract slice"));
	setMinimumWidth(560);

	// Determine the upper bound for time inputs. If a sound is involved (either
	// the source itself, or the annotation's bound sound), use its duration.
	// Otherwise fall back to the annotation's effective duration.
	if (m_sound) {
		m_duration = m_sound->duration();
	}
	else if (m_annot) {
		m_duration = effective_duration(*m_annot);
	}
	else {
		m_duration = 0.0;
	}

	// If the user picked an annotation that's bound to a sound, surface the
	// "both" option even though they only passed the annotation in.
	Sound *bound_sound = (m_annot && m_annot->has_sound()) ? m_annot->sound().get() : nullptr;
	if (!m_sound && bound_sound) {
		m_sound = bound_sound;
		// Re-evaluate the time bound now that we have a sound.
		m_duration = m_sound->duration();
	}

	auto *layout = new QVBoxLayout(this);

	// --- Mode picker (only when both sides are available) ----------------
	m_mode_group = new QGroupBox(tr("Extract"), this);
	auto *mode_inner = new QVBoxLayout(m_mode_group);
	m_mode_both        = new QRadioButton(tr("Annotation and sound (binds new annotation to new sound)"), m_mode_group);
	m_mode_annot_only  = new QRadioButton(tr("Annotation only"), m_mode_group);
	m_mode_sound_only  = new QRadioButton(tr("Sound only"), m_mode_group);
	mode_inner->addWidget(m_mode_both);
	mode_inner->addWidget(m_mode_annot_only);
	mode_inner->addWidget(m_mode_sound_only);
	layout->addWidget(m_mode_group);

	// Default mode depends on what's available.
	if (m_annot && m_sound) {
		m_mode_both->setChecked(true);
	}
	else if (m_annot) {
		m_mode_annot_only->setChecked(true);
	}
	else {
		m_mode_sound_only->setChecked(true);
	}

	// Hide options that aren't applicable.
	m_mode_both->setEnabled(m_annot != nullptr && m_sound != nullptr);
	m_mode_annot_only->setEnabled(m_annot != nullptr);
	m_mode_sound_only->setEnabled(m_sound != nullptr);

	// Hide the whole mode group when only one side is available; the disabled
	// radio buttons would only confuse the user.
	if (!m_annot || !m_sound) {
		m_mode_group->setVisible(false);
	}

	connect(m_mode_both,       &QRadioButton::toggled, this, [this](bool on){ if (on) onModeChanged(); });
	connect(m_mode_annot_only, &QRadioButton::toggled, this, [this](bool on){ if (on) onModeChanged(); });
	connect(m_mode_sound_only, &QRadioButton::toggled, this, [this](bool on){ if (on) onModeChanged(); });

	// --- Time range ------------------------------------------------------
	auto *time_group = new QGroupBox(tr("Time range"), this);
	auto *time_form  = new QFormLayout(time_group);

	m_start_spin = new QDoubleSpinBox(time_group);
	m_start_spin->setDecimals(4);
	m_start_spin->setSingleStep(0.1);
	m_start_spin->setSuffix(tr(" s"));
	m_start_spin->setMinimum(0.0);
	m_start_spin->setMaximum(std::max(0.0, m_duration));
	m_start_spin->setValue(0.0);
	time_form->addRow(tr("Start:"), m_start_spin);

	m_end_spin = new QDoubleSpinBox(time_group);
	m_end_spin->setDecimals(4);
	m_end_spin->setSingleStep(0.1);
	m_end_spin->setSuffix(tr(" s"));
	m_end_spin->setMinimum(0.0);
	m_end_spin->setMaximum(std::max(0.0, m_duration));
	m_end_spin->setValue(m_duration);
	time_form->addRow(tr("End:"), m_end_spin);

	if (m_duration > 0) {
		auto *hint = new QLabel(
			tr("Source duration: %1 s").arg(m_duration, 0, 'f', 3), time_group);
		hint->setStyleSheet("QLabel { color: #555; }");
		time_form->addRow(hint);
	}
	else {
		auto *hint = new QLabel(
			tr("Source duration: 0 — nothing to extract. Cancel and add events "
			   "(or bind a sound) before retrying."), time_group);
		hint->setStyleSheet("QLabel { color: #b00; }");
		hint->setWordWrap(true);
		time_form->addRow(hint);
	}

	layout->addWidget(time_group);

	connect(m_start_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
	        this, [this](double){ refreshOkEnabled(); });
	connect(m_end_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
	        this, [this](double){ refreshOkEnabled(); });

	// --- Annotation output -----------------------------------------------
	m_annot_group = new QGroupBox(tr("Annotation output"), this);
	auto *annot_form = new QFormLayout(m_annot_group);

	m_clip_check = new QCheckBox(
		tr("Clip events that straddle the boundary "
		   "(otherwise such events are dropped)"), m_annot_group);
	m_clip_check->setChecked(true);
	annot_form->addRow(m_clip_check);

	auto *a_path_row = new QHBoxLayout;
	m_annot_path_edit = new QLineEdit(m_annot_group);
	m_annot_browse    = new QPushButton(tr("Browse..."), m_annot_group);
	a_path_row->addWidget(m_annot_path_edit);
	a_path_row->addWidget(m_annot_browse);
	annot_form->addRow(tr("File:"), a_path_row);
	connect(m_annot_browse, &QPushButton::clicked, this, &ExtractSliceDialog::onBrowseAnnotation);
	connect(m_annot_path_edit, &QLineEdit::textChanged, this, [this](const QString&){ refreshOkEnabled(); });

	m_annot_format_combo = new QComboBox(m_annot_group);
	m_annot_format_combo->addItem(tr("Phonometrica annotation"), int(Annotation::Type::Native));
	m_annot_format_combo->addItem(tr("Praat TextGrid"),          int(Annotation::Type::TextGrid));
	if (m_annot) {
		m_annot_format_combo->setCurrentIndex(m_annot->is_textgrid() ? 1 : 0);
	}
	annot_form->addRow(tr("Format:"), m_annot_format_combo);
	connect(m_annot_format_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &ExtractSliceDialog::onAnnotationFormatChanged);

	layout->addWidget(m_annot_group);

	// --- Sound output ----------------------------------------------------
	m_sound_group = new QGroupBox(tr("Sound output"), this);
	auto *sound_form = new QFormLayout(m_sound_group);

	auto *s_path_row = new QHBoxLayout;
	m_sound_path_edit = new QLineEdit(m_sound_group);
	m_sound_browse    = new QPushButton(tr("Browse..."), m_sound_group);
	s_path_row->addWidget(m_sound_path_edit);
	s_path_row->addWidget(m_sound_browse);
	sound_form->addRow(tr("File:"), s_path_row);
	connect(m_sound_browse, &QPushButton::clicked, this, &ExtractSliceDialog::onBrowseSound);
	connect(m_sound_path_edit, &QLineEdit::textChanged, this, [this](const QString&){ refreshOkEnabled(); });

	m_sound_format_combo = new QComboBox(m_sound_group);
	m_sound_format_combo->addItem(tr("WAV"),  int(Sound::Format::WAV));
	m_sound_format_combo->addItem(tr("AIFF"), int(Sound::Format::AIFF));
	m_sound_format_combo->addItem(tr("FLAC"), int(Sound::Format::FLAC));
	m_sound_format_combo->addItem(tr("OGG"),  int(Sound::Format::OGG));
	// Default to WAV — almost always the right choice for sub-clips.
	m_sound_format_combo->setCurrentIndex(0);
	sound_form->addRow(tr("Format:"), m_sound_format_combo);
	connect(m_sound_format_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &ExtractSliceDialog::onSoundFormatChanged);

	layout->addWidget(m_sound_group);

	// --- Buttons ---------------------------------------------------------
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_ok_button = buttons->button(QDialogButtonBox::Ok);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	// Initial sync — everything is now constructed.
	rebuildSuggestedPaths();
	refreshSectionVisibility();
	refreshOkEnabled();
}

ExtractSliceDialog::Mode ExtractSliceDialog::mode() const
{
	if (m_mode_both->isChecked())       return Mode::Both;
	if (m_mode_sound_only->isChecked()) return Mode::SoundOnly;
	return Mode::AnnotationOnly;
}

double ExtractSliceDialog::startTime() const { return m_start_spin->value(); }
double ExtractSliceDialog::endTime()   const { return m_end_spin->value();   }
bool   ExtractSliceDialog::clipPartial() const { return m_clip_check->isChecked(); }

String ExtractSliceDialog::annotationOutputPath() const
{
	if (mode() == Mode::SoundOnly) return String();
	auto bytes = m_annot_path_edit->text().toUtf8();
	return String(bytes.constData(), bytes.size());
}

Annotation::Type ExtractSliceDialog::annotationOutputFormat() const
{
	return Annotation::Type(m_annot_format_combo->currentData().toInt());
}

String ExtractSliceDialog::soundOutputPath() const
{
	if (mode() == Mode::AnnotationOnly) return String();
	auto bytes = m_sound_path_edit->text().toUtf8();
	return String(bytes.constData(), bytes.size());
}

Sound::Format ExtractSliceDialog::soundOutputFormat() const
{
	return Sound::Format(m_sound_format_combo->currentData().toInt());
}

void ExtractSliceDialog::onModeChanged()
{
	refreshSectionVisibility();
	refreshOkEnabled();
}

void ExtractSliceDialog::onAnnotationFormatChanged(int /*index*/)
{
	rebuildSuggestedPaths();
}

void ExtractSliceDialog::onSoundFormatChanged(int /*index*/)
{
	rebuildSuggestedPaths();
}

void ExtractSliceDialog::onBrowseAnnotation()
{
	QString filter, ext;
	if (annotationOutputFormat() == Annotation::Type::TextGrid) {
		filter = tr("Praat TextGrid (*.TextGrid)");
		ext    = QStringLiteral(".TextGrid");
	}
	else {
		filter = tr("Phonometrica annotation (*.phon-annot)");
		ext    = QStringLiteral(PHON_EXT_ANNOTATION);
	}

	QString defaultName = m_annot_path_edit->text();
	if (defaultName.isEmpty() && m_annot && m_annot->has_path()) {
		auto base = filesystem::strip_ext(filesystem::base_name(m_annot->path()));
		defaultName = QString::fromUtf8(base.data(), (int) base.size())
		            + QStringLiteral("_slice") + ext;
	}
	auto chosen = getSaveFileName(this, tr("Save sliced annotation"), filter, defaultName);
	if (!chosen.isEmpty()) {
		m_annot_path_edit->setText(chosen);
	}
}

void ExtractSliceDialog::onBrowseSound()
{
	QString filter, ext;
	switch (soundOutputFormat()) {
		case Sound::Format::WAV:  filter = tr("WAV (*.wav)");   ext = QStringLiteral(".wav");  break;
		case Sound::Format::AIFF: filter = tr("AIFF (*.aiff)"); ext = QStringLiteral(".aiff"); break;
		case Sound::Format::FLAC: filter = tr("FLAC (*.flac)"); ext = QStringLiteral(".flac"); break;
		case Sound::Format::OGG:  filter = tr("OGG (*.ogg)");   ext = QStringLiteral(".ogg");  break;
	}

	QString defaultName = m_sound_path_edit->text();
	if (defaultName.isEmpty() && m_sound && m_sound->has_path()) {
		auto base = filesystem::strip_ext(filesystem::base_name(m_sound->path()));
		defaultName = QString::fromUtf8(base.data(), (int) base.size())
		            + QStringLiteral("_slice") + ext;
	}
	auto chosen = getSaveFileName(this, tr("Save sliced sound"), filter, defaultName);
	if (!chosen.isEmpty()) {
		m_sound_path_edit->setText(chosen);
	}
}

void ExtractSliceDialog::refreshSectionVisibility()
{
	bool show_annot = (mode() != Mode::SoundOnly) && (m_annot != nullptr);
	bool show_sound = (mode() != Mode::AnnotationOnly) && (m_sound != nullptr);
	m_annot_group->setVisible(show_annot);
	m_sound_group->setVisible(show_sound);
}

void ExtractSliceDialog::rebuildSuggestedPaths()
{
	// Annotation path.
	if (m_annot && m_annot->has_path())
	{
		auto dir  = filesystem::directory_name(m_annot->path());
		auto stem = filesystem::strip_ext(filesystem::base_name(m_annot->path()));
		String a_ext = (annotationOutputFormat() == Annotation::Type::TextGrid)
		               ? String(".TextGrid") : String(PHON_EXT_ANNOTATION);

		String desired;
		desired.append(stem);
		desired.append("_slice");
		desired.append(a_ext);
		String suggestion = unique_path(filesystem::join(dir, desired));

		auto cur = m_annot_path_edit->text();
		if (cur.isEmpty()) {
			m_annot_path_edit->setText(QString::fromUtf8(suggestion.data(), (int) suggestion.size()));
		}
		else {
			// Same auto-named stem? Swap extension only.
			auto cur_bytes = cur.toUtf8();
			String cur_s(cur_bytes.constData(), cur_bytes.size());
			auto cur_base = filesystem::strip_ext(filesystem::base_name(cur_s));
			String expected_stem;
			expected_stem.append(stem);
			expected_stem.append("_slice");
			if (cur_base == expected_stem) {
				auto cur_stem_path = filesystem::strip_ext(cur_s);
				String swapped;
				swapped.append(cur_stem_path);
				swapped.append(a_ext);
				m_annot_path_edit->setText(QString::fromUtf8(swapped.data(), (int) swapped.size()));
			}
		}
	}

	// Sound path.
	if (m_sound && m_sound->has_path())
	{
		auto dir  = filesystem::directory_name(m_sound->path());
		auto stem = filesystem::strip_ext(filesystem::base_name(m_sound->path()));
		String s_ext;
		switch (soundOutputFormat()) {
			case Sound::Format::WAV:  s_ext = String(".wav");  break;
			case Sound::Format::AIFF: s_ext = String(".aiff"); break;
			case Sound::Format::FLAC: s_ext = String(".flac"); break;
			case Sound::Format::OGG:  s_ext = String(".ogg");  break;
		}

		String desired;
		desired.append(stem);
		desired.append("_slice");
		desired.append(s_ext);
		String suggestion = unique_path(filesystem::join(dir, desired));

		auto cur = m_sound_path_edit->text();
		if (cur.isEmpty()) {
			m_sound_path_edit->setText(QString::fromUtf8(suggestion.data(), (int) suggestion.size()));
		}
		else {
			auto cur_bytes = cur.toUtf8();
			String cur_s(cur_bytes.constData(), cur_bytes.size());
			auto cur_base = filesystem::strip_ext(filesystem::base_name(cur_s));
			String expected_stem;
			expected_stem.append(stem);
			expected_stem.append("_slice");
			if (cur_base == expected_stem) {
				auto cur_stem_path = filesystem::strip_ext(cur_s);
				String swapped;
				swapped.append(cur_stem_path);
				swapped.append(s_ext);
				m_sound_path_edit->setText(QString::fromUtf8(swapped.data(), (int) swapped.size()));
			}
		}
	}
}

void ExtractSliceDialog::refreshOkEnabled()
{
	bool time_ok = (m_start_spin->value() >= 0.0)
	            && (m_end_spin->value() > m_start_spin->value())
	            && (m_end_spin->value() <= m_duration + 1e-9);

	bool need_annot = (mode() != Mode::SoundOnly);
	bool need_sound = (mode() != Mode::AnnotationOnly);
	bool annot_ok = !need_annot || !m_annot_path_edit->text().trimmed().isEmpty();
	bool sound_ok = !need_sound || !m_sound_path_edit->text().trimmed().isEmpty();

	m_ok_button->setEnabled(time_ok && annot_ok && sound_ok);
}

} // namespace phonometrica
