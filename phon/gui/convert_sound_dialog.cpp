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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cassert>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QStandardItemModel>
#include <phon/gui/convert_sound_dialog.hpp>
#include <phon/gui/file_dialog.hpp>
#include <phon/application/annotation_ops.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

namespace {

// Map a Format combo entry to a filter string for QFileDialog. Kept here
// so the Browse handler and the suggested-path logic stay in sync.
QString filter_for(Sound::Format fmt)
{
	switch (fmt) {
		case Sound::Format::WAV:  return QObject::tr("WAV (*.wav)");
		case Sound::Format::AIFF: return QObject::tr("AIFF (*.aiff *.aif)");
		case Sound::Format::FLAC: return QObject::tr("FLAC (*.flac)");
		case Sound::Format::OGG:  return QObject::tr("Ogg Vorbis (*.ogg)");
#ifdef SF_FORMAT_MPEG
		case Sound::Format::MP3:  return QObject::tr("MP3 (*.mp3)");
#endif
	}
	return QObject::tr("Sound files (*)");
}

// Whether the runtime libsndfile actually supports writing the given format
// in this build. Used to grey out unavailable choices in the format combo.
bool runtime_supports(Sound::Format fmt)
{
	try {
		(void) Sound::parse_format(Sound::extension_for(fmt).right(
			Sound::extension_for(fmt).size() - 1));
		return true;
	}
	catch (...) {
		return false;
	}
}

} // anonymous namespace


ConvertSoundDialog::ConvertSoundDialog(QWidget *parent, Sound *sound) :
	QDialog(parent),
	m_sound(sound),
	m_source_rate(sound ? sound->sample_rate() : 0)
{
	assert(m_sound != nullptr);
	assert(m_sound->has_path());

	setWindowTitle(tr("Convert sound"));
	setMinimumWidth(540);

	auto *layout = new QVBoxLayout(this);

	// --- Output file -----------------------------------------------------
	auto *file_group = new QGroupBox(tr("Output file"), this);
	auto *file_form  = new QFormLayout(file_group);

	auto *path_row = new QHBoxLayout;
	m_path_edit    = new QLineEdit(file_group);
	m_browse_button = new QPushButton(tr("Browse..."), file_group);
	path_row->addWidget(m_path_edit);
	path_row->addWidget(m_browse_button);
	file_form->addRow(tr("Path:"), path_row);
	connect(m_browse_button, &QPushButton::clicked, this, &ConvertSoundDialog::onBrowse);
	connect(m_path_edit, &QLineEdit::textChanged, this, [this](const QString&){ refreshOkEnabled(); });

	m_format_combo = new QComboBox(file_group);
	// Always offer all the common formats; grey out the ones the runtime
	// libsndfile doesn't actually support so the user can see what's
	// possible and what isn't.
	struct FmtEntry { Sound::Format fmt; const char *label; };
	const FmtEntry entries[] = {
		{ Sound::Format::WAV,  "WAV"        },
		{ Sound::Format::AIFF, "AIFF"       },
		{ Sound::Format::FLAC, "FLAC"       },
		{ Sound::Format::OGG,  "Ogg Vorbis" },
#ifdef SF_FORMAT_MPEG
		{ Sound::Format::MP3,  "MP3"        },
#endif
	};

	int initial_idx = 0;
	int first_enabled = -1;
	for (auto &e : entries)
	{
		bool ok = runtime_supports(e.fmt);
		QString label = tr(e.label);
		if (!ok) {
			label += tr(" (not available in this build)");
		}
		m_format_combo->addItem(label, int(e.fmt));
		auto *model = qobject_cast<QStandardItemModel *>(m_format_combo->model());
		if (model) {
			auto *item = model->item(m_format_combo->count() - 1);
			if (item) {
				item->setFlags(ok ? (item->flags() |  Qt::ItemIsEnabled)
				                  : (item->flags() & ~Qt::ItemIsEnabled));
			}
		}
		if (ok && first_enabled < 0) {
			first_enabled = m_format_combo->count() - 1;
		}
	}
	if (first_enabled >= 0) initial_idx = first_enabled;
	m_format_combo->setCurrentIndex(initial_idx);
	file_form->addRow(tr("Format:"), m_format_combo);
	connect(m_format_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &ConvertSoundDialog::onFormatChanged);

	layout->addWidget(file_group);

	// --- Sample rate -----------------------------------------------------
	auto *rate_group = new QGroupBox(tr("Sample rate"), this);
	auto *rate_inner = new QVBoxLayout(rate_group);

	m_keep_rate = new QCheckBox(
		tr("Keep original sample rate (%1 Hz)").arg(m_source_rate), rate_group);
	m_keep_rate->setChecked(true);
	rate_inner->addWidget(m_keep_rate);

	auto *rate_row  = new QHBoxLayout;
	auto *rate_lbl  = new QLabel(tr("Target rate:"), rate_group);
	m_rate_spin = new QSpinBox(rate_group);
	m_rate_spin->setRange(1000, 384000);
	m_rate_spin->setSingleStep(1000);
	m_rate_spin->setSuffix(tr(" Hz"));
	m_rate_spin->setValue(m_source_rate > 0 ? m_source_rate : 44100);
	m_rate_spin->setEnabled(false);
	rate_row->addWidget(rate_lbl);
	rate_row->addWidget(m_rate_spin);
	rate_row->addStretch();
	rate_inner->addLayout(rate_row);

	connect(m_keep_rate, &QCheckBox::toggled, this, &ConvertSoundDialog::onKeepRateToggled);

	layout->addWidget(rate_group);

	// --- Buttons ---------------------------------------------------------
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_ok_button = buttons->button(QDialogButtonBox::Ok);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	// Initial sync.
	rebuildSuggestedPath();
	refreshOkEnabled();
}

String ConvertSoundDialog::outputPath() const
{
	auto bytes = m_path_edit->text().toUtf8();
	return String(bytes.constData(), bytes.size());
}

Sound::Format ConvertSoundDialog::outputFormat() const
{
	return Sound::Format(m_format_combo->currentData().toInt());
}

int ConvertSoundDialog::targetSampleRate() const
{
	return m_keep_rate->isChecked() ? 0 : m_rate_spin->value();
}

void ConvertSoundDialog::onFormatChanged(int /*index*/)
{
	rebuildSuggestedPath();
	refreshOkEnabled();
}

void ConvertSoundDialog::onKeepRateToggled(bool keep)
{
	m_rate_spin->setEnabled(!keep);
}

void ConvertSoundDialog::onBrowse()
{
	auto fmt = outputFormat();
	QString filter = filter_for(fmt);
	auto ext_s = Sound::extension_for(fmt);
	QString ext = QString::fromUtf8(ext_s.data(), (int) ext_s.size());

	QString defaultName = m_path_edit->text();
	if (defaultName.isEmpty() && m_sound->has_path()) {
		auto base = filesystem::strip_ext(filesystem::base_name(m_sound->path()));
		defaultName = QString::fromUtf8(base.data(), (int) base.size())
		            + QStringLiteral("_converted") + ext;
	}
	auto chosen = getSaveFileName(this, tr("Save converted sound"), filter, defaultName);
	if (!chosen.isEmpty()) {
		m_path_edit->setText(chosen);
	}
}

void ConvertSoundDialog::rebuildSuggestedPath()
{
	// Build a suggested path next to the source, with the chosen extension.
	if (!m_sound->has_path())
		return;

	auto fmt = outputFormat();
	auto ext_s = Sound::extension_for(fmt);

	auto dir  = filesystem::directory_name(m_sound->path());
	auto stem = filesystem::strip_ext(filesystem::base_name(m_sound->path()));

	String desired;
	desired.append(stem);
	desired.append("_converted");
	desired.append(ext_s);
	String suggestion = unique_path(filesystem::join(dir, desired));

	auto cur = m_path_edit->text();
	if (cur.isEmpty()) {
		m_path_edit->setText(QString::fromUtf8(suggestion.data(), (int) suggestion.size()));
		return;
	}

	// If the path still looks like our auto-suggested stem, just swap the
	// extension to match the new format. This matches the behaviour of
	// ExtractSliceDialog::rebuildSuggestedPaths.
	auto cur_bytes = cur.toUtf8();
	String cur_s(cur_bytes.constData(), cur_bytes.size());
	auto cur_base = filesystem::strip_ext(filesystem::base_name(cur_s));
	String expected_stem;
	expected_stem.append(stem);
	expected_stem.append("_converted");
	if (cur_base == expected_stem) {
		auto cur_stem_path = filesystem::strip_ext(cur_s);
		String swapped;
		swapped.append(cur_stem_path);
		swapped.append(ext_s);
		m_path_edit->setText(QString::fromUtf8(swapped.data(), (int) swapped.size()));
	}
}

void ConvertSoundDialog::refreshOkEnabled()
{
	bool path_ok = !m_path_edit->text().trimmed().isEmpty();

	// Also gate on the chosen format being runtime-available (the user could
	// have picked a disabled item with the keyboard).
	bool fmt_ok = runtime_supports(outputFormat());

	m_ok_button->setEnabled(path_ok && fmt_ok);
}

} // namespace phonometrica
