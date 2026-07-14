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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <algorithm>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <phon/gui/record_sound_dialog.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

// Standard rates we offer when the device's reported list is restrictive or empty.
static const std::vector<unsigned int> kStandardRates = {
	16000, 22050, 32000, 44100, 48000, 88200, 96000
};

RecordSoundDialog::RecordSoundDialog(QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Record sound"));
	setMinimumWidth(520);

	auto *layout = new QVBoxLayout(this);

	// --- Capture parameters -------------------------------------------------
	auto *params_group = new QGroupBox(tr("Capture"), this);
	auto *params_form  = new QFormLayout(params_group);

	m_device_combo  = new QComboBox(params_group);
	m_rate_combo    = new QComboBox(params_group);
	m_channel_combo = new QComboBox(params_group);

	params_form->addRow(tr("Input device:"), m_device_combo);
	params_form->addRow(tr("Sample rate:"),  m_rate_combo);
	params_form->addRow(tr("Channels:"),     m_channel_combo);

	layout->addWidget(params_group);

	// --- Output -------------------------------------------------------------
	auto *output_group = new QGroupBox(tr("Output"), this);
	auto *output_form  = new QFormLayout(output_group);

	m_format_combo = new QComboBox(output_group);
	output_form->addRow(tr("Format:"), m_format_combo);

	auto *path_row = new QHBoxLayout;
	m_path_edit = new QLineEdit(output_group);
	m_browse_btn = new QPushButton(tr("Browse..."), output_group);
	path_row->addWidget(m_path_edit, 1);
	path_row->addWidget(m_browse_btn, 0);
	output_form->addRow(tr("File:"), path_row);

	m_add_to_project = new QCheckBox(tr("Add to project after recording"), output_group);
	m_add_to_project->setChecked(true);
	output_form->addRow(QString(), m_add_to_project);

	layout->addWidget(output_group);

	// --- Live status --------------------------------------------------------
	auto *status_group = new QGroupBox(tr("Status"), this);
	auto *status_layout = new QVBoxLayout(status_group);

	m_elapsed_label = new QLabel(tr("00:00:00"), status_group);
	auto elapsed_font = m_elapsed_label->font();
	elapsed_font.setPointSize(elapsed_font.pointSize() + 4);
	m_elapsed_label->setFont(elapsed_font);
	m_elapsed_label->setAlignment(Qt::AlignCenter);

	m_peak_bar = new QProgressBar(status_group);
	m_peak_bar->setRange(0, 1000);
	m_peak_bar->setValue(0);
	m_peak_bar->setTextVisible(false);
	m_peak_bar->setFixedHeight(12);

	m_status_label = new QLabel(tr("Ready."), status_group);
	m_status_label->setAlignment(Qt::AlignCenter);

	status_layout->addWidget(m_elapsed_label);
	status_layout->addWidget(m_peak_bar);
	status_layout->addWidget(m_status_label);

	layout->addWidget(status_group);

	// --- Buttons ------------------------------------------------------------
	auto *btn_row = new QHBoxLayout;
	m_record_btn = new QPushButton(tr("Record"), this);
	m_close_btn  = new QPushButton(tr("Close"),  this);
	m_record_btn->setDefault(true);
	btn_row->addStretch(1);
	btn_row->addWidget(m_record_btn);
	btn_row->addWidget(m_close_btn);
	layout->addLayout(btn_row);

	// Wire signals.
	connect(m_device_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &RecordSoundDialog::onDeviceChanged);
	connect(m_format_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &RecordSoundDialog::onFormatChanged);
	connect(m_browse_btn,   &QPushButton::clicked, this, &RecordSoundDialog::onBrowse);
	connect(m_record_btn,   &QPushButton::clicked, this, &RecordSoundDialog::onRecordToggled);
	connect(m_close_btn,    &QPushButton::clicked, this, &QDialog::reject);

	// Tick: 30 fps is plenty for a duration label and a falling-back VU bar.
	m_tick = new QTimer(this);
	m_tick->setInterval(33);
	connect(m_tick, &QTimer::timeout, this, &RecordSoundDialog::onTick);

	// Populate UI.
	populateDevices();
	populateFormats();
	// onDeviceChanged is wired to currentIndexChanged but the initial population
	// fires it as a side effect. We still call it explicitly to populate the
	// rate/channel combos when the device list is non-empty; if empty, we just
	// fall through and the populate functions will guard against it.
	onDeviceChanged();

	// Suggest a default filename. The format combo's current value drives the
	// extension; do this after populateFormats() so the combo has a selection.
	if (m_path_edit->text().isEmpty()) {
		m_path_edit->setText(defaultFileName());
	}

	updateUiForState();
}

// --------------------------------------------------------------------------
// Population.
// --------------------------------------------------------------------------

void RecordSoundDialog::populateDevices()
{
	m_devices = SoundRecorder::enumerate_input_devices();
	m_device_combo->clear();

	if (m_devices.empty()) {
		m_device_combo->addItem(tr("(no input device available)"));
		m_device_combo->setEnabled(false);
		m_record_btn->setEnabled(false);
		return;
	}

	int default_idx = 0;
	for (size_t i = 0; i < m_devices.size(); i++) {
		const auto &d = m_devices[i];
		QString label = QString::fromStdString(d.name);
		if (d.is_default) {
			label += tr(" (default)");
			default_idx = static_cast<int>(i);
		}
		m_device_combo->addItem(label);
	}
	m_device_combo->setCurrentIndex(default_idx);
}

void RecordSoundDialog::populateSampleRates()
{
	m_rate_combo->clear();
	if (m_devices.empty()) return;

	const auto &dev = m_devices[std::max(0, m_device_combo->currentIndex())];

	// Intersect device-supported and our standard list. If the device returns
	// no list, fall back to the standard list (some backends don't enumerate
	// rates and accept anything reasonable).
	std::vector<unsigned int> rates;
	if (dev.sample_rates.empty()) {
		rates = kStandardRates;
	}
	else {
		for (auto r : kStandardRates) {
			if (std::find(dev.sample_rates.begin(), dev.sample_rates.end(), r) != dev.sample_rates.end()) {
				rates.push_back(r);
			}
		}
		if (rates.empty()) rates = dev.sample_rates;
	}

	int preferred_idx = 0;
	int default_rate  = 0;
	try { default_rate = Settings::get_int("recording", "default_sample_rate"); }
	catch (...) { default_rate = 44100; }
	const unsigned int target =
		dev.preferred_sample_rate ? dev.preferred_sample_rate :
		                            static_cast<unsigned int>(default_rate);

	for (size_t i = 0; i < rates.size(); i++) {
		m_rate_combo->addItem(QString::number(rates[i]) + tr(" Hz"),
		                      QVariant::fromValue<unsigned int>(rates[i]));
		if (rates[i] == target) preferred_idx = static_cast<int>(i);
	}
	m_rate_combo->setCurrentIndex(preferred_idx);
}

void RecordSoundDialog::populateChannels()
{
	m_channel_combo->clear();
	if (m_devices.empty()) return;

	const auto &dev = m_devices[std::max(0, m_device_combo->currentIndex())];
	const unsigned int max_ch = std::min<unsigned int>(dev.max_input_channels, 2);

	if (max_ch >= 1) m_channel_combo->addItem(tr("Mono"),   QVariant::fromValue<unsigned int>(1));
	if (max_ch >= 2) m_channel_combo->addItem(tr("Stereo"), QVariant::fromValue<unsigned int>(2));
	m_channel_combo->setCurrentIndex(0); // mono by default — matches typical research use
}

void RecordSoundDialog::populateFormats()
{
	m_format_combo->clear();

	// Only offer formats that libsndfile has actually compiled in. WAV and
	// AIFF are always present; FLAC and OGG depend on the build.
	const auto names = Sound::supported_sound_format_names();

	auto has_keyword = [&](const char *needle) {
		for (intptr_t i = 0; i < names.size(); i++) {
			if (names[i].contains(needle)) return true;
		}
		return false;
	};

	// Always-available pair first.
	m_format_combo->addItem(tr("WAV (PCM 16-bit)"),  QVariant::fromValue(int(Sound::Format::WAV)));
	m_format_combo->addItem(tr("AIFF (PCM 16-bit)"), QVariant::fromValue(int(Sound::Format::AIFF)));
	if (has_keyword("FLAC")) {
		m_format_combo->addItem(tr("FLAC (PCM 16-bit)"), QVariant::fromValue(int(Sound::Format::FLAC)));
	}
	if (has_keyword("Vorbis") || has_keyword("Ogg")) {
		m_format_combo->addItem(tr("Ogg Vorbis"),         QVariant::fromValue(int(Sound::Format::OGG)));
	}

	// Default to WAV unless the user previously chose otherwise.
	String saved;
	try { saved = Settings::get_string("recording", "default_format"); }
	catch (...) { saved = String("WAV"); }

	int idx = 0;
	for (int i = 0; i < m_format_combo->count(); i++) {
		auto fmt = static_cast<Sound::Format>(m_format_combo->itemData(i).toInt());
		if ((saved == "WAV"  && fmt == Sound::Format::WAV)  ||
		    (saved == "AIFF" && fmt == Sound::Format::AIFF) ||
		    (saved == "FLAC" && fmt == Sound::Format::FLAC) ||
		    (saved == "OGG"  && fmt == Sound::Format::OGG))
		{
			idx = i;
			break;
		}
	}
	m_format_combo->setCurrentIndex(idx);
}

// --------------------------------------------------------------------------
// Slots.
// --------------------------------------------------------------------------

void RecordSoundDialog::onDeviceChanged()
{
	populateSampleRates();
	populateChannels();
}

void RecordSoundDialog::onFormatChanged()
{
	// Update the file extension on the suggested path. Preserve a user-typed
	// directory and stem, only swap the extension. If the field is empty (the
	// initial state before the constructor has set a default), do nothing.
	QString cur = m_path_edit->text();
	if (cur.isEmpty()) return;

	QFileInfo fi(cur);
	const QString new_ext = suggestedExtension();
	const QString new_path = fi.absolutePath().isEmpty()
		? fi.completeBaseName() + "." + new_ext
		: fi.absolutePath() + "/" + fi.completeBaseName() + "." + new_ext;
	m_path_edit->setText(QDir::toNativeSeparators(new_path));
}

void RecordSoundDialog::onBrowse()
{
	const QString filter = QString("%1 (*.%2)").arg(
		m_format_combo->currentText(), suggestedExtension());

	QString start = m_path_edit->text();
	if (start.isEmpty()) start = defaultFileName();

	QString chosen = QFileDialog::getSaveFileName(
		this, tr("Save recording as..."), start, filter);
	if (chosen.isEmpty()) return;

	// If the user didn't type an extension, append the format's default.
	QFileInfo fi(chosen);
	if (fi.suffix().isEmpty()) {
		chosen += "." + suggestedExtension();
	}
	m_path_edit->setText(QDir::toNativeSeparators(chosen));
}

void RecordSoundDialog::onRecordToggled()
{
	if (!m_recording)
	{
		// --- Start recording ---
		if (m_devices.empty()) return;

		QString path = m_path_edit->text().trimmed();
		if (path.isEmpty()) {
			QMessageBox::warning(this, tr("Record sound"),
				tr("Please choose an output file before recording."));
			return;
		}

		const auto &dev = m_devices[std::max(0, m_device_combo->currentIndex())];
		const unsigned int rate = m_rate_combo->currentData().toUInt();
		const unsigned int ch   = m_channel_combo->currentData().toUInt();
		const auto fmt = selectedFormat();

		try {
			m_recorder = std::make_unique<SoundRecorder>(
				String(path.toUtf8().constData()), fmt, dev.id, rate, ch);
			m_recorder->start();
		}
		catch (std::exception &e) {
			QMessageBox::warning(this, tr("Record sound"),
				tr("Could not start recording:\n%1").arg(e.what()));
			m_recorder.reset();
			return;
		}

		// Persist UX defaults so the next invocation remembers the choice.
		String fmt_name;
		switch (fmt) {
			case Sound::Format::WAV:  fmt_name = "WAV";  break;
			case Sound::Format::AIFF: fmt_name = "AIFF"; break;
			case Sound::Format::FLAC: fmt_name = "FLAC"; break;
			case Sound::Format::OGG:  fmt_name = "OGG";  break;
		}
		Settings::set_value("recording", "default_format",      fmt_name);
		Settings::set_value("recording", "default_sample_rate", intptr_t(rate));

		m_recording  = true;
		m_saved_path = String(path.toUtf8().constData());
		m_status_label->setText(tr("Recording..."));
		m_tick->start();
	}
	else
	{
		// --- Stop recording ---
		try {
			m_recorder->stop();
		}
		catch (std::exception &e) {
			QMessageBox::warning(this, tr("Record sound"),
				tr("Error while stopping the recording:\n%1").arg(e.what()));
		}
		m_tick->stop();
		const std::size_t dropped = m_recorder ? m_recorder->dropped_frames() : 0;
		m_recording = false;

		if (dropped > 0) {
			QMessageBox::warning(this, tr("Record sound"),
				tr("The recording is saved, but %1 audio frame(s) were dropped because the writer "
				   "thread could not keep up. Consider increasing recording.pool_blocks in your "
				   "settings or recording to a faster disk.").arg(qlonglong(dropped)));
		}

		m_status_label->setText(tr("Saved to %1").arg(
			QString::fromUtf8(m_saved_path.data(), int(m_saved_path.size()))));
		// Accept on a successful stop so the caller learns the path.
		accept();
		return;
	}

	updateUiForState();
}

void RecordSoundDialog::onTick()
{
	if (!m_recorder) return;

	const double secs = m_recorder->current_duration();
	const long long total_ms = static_cast<long long>(secs * 1000.0);
	const int hh = int( total_ms / 3600000);
	const int mm = int((total_ms /   60000) % 60);
	const int ss = int((total_ms /    1000) % 60);
	m_elapsed_label->setText(QString("%1:%2:%3")
		.arg(hh, 2, 10, QChar('0'))
		.arg(mm, 2, 10, QChar('0'))
		.arg(ss, 2, 10, QChar('0')));

	const double peak = m_recorder->peak_level();
	m_peak_bar->setValue(int(peak * 1000.0));
}

// --------------------------------------------------------------------------
// State management.
// --------------------------------------------------------------------------

void RecordSoundDialog::updateUiForState()
{
	const bool busy = m_recording;
	m_device_combo  ->setEnabled(!busy);
	m_rate_combo    ->setEnabled(!busy);
	m_channel_combo ->setEnabled(!busy);
	m_format_combo  ->setEnabled(!busy);
	m_path_edit     ->setEnabled(!busy);
	m_browse_btn    ->setEnabled(!busy);
	m_add_to_project->setEnabled(!busy);
	m_close_btn     ->setEnabled(!busy);
	m_record_btn    ->setText(busy ? tr("Stop") : tr("Record"));
}

bool RecordSoundDialog::addToProject() const
{
	return m_add_to_project->isChecked();
}

Sound::Format RecordSoundDialog::selectedFormat() const
{
	return static_cast<Sound::Format>(m_format_combo->currentData().toInt());
}

QString RecordSoundDialog::suggestedExtension() const
{
	switch (selectedFormat()) {
		case Sound::Format::WAV:  return "wav";
		case Sound::Format::AIFF: return "aiff";
		case Sound::Format::FLAC: return "flac";
		case Sound::Format::OGG:  return "ogg";
	}
	return "wav";
}

QString RecordSoundDialog::defaultFileName() const
{
	const QString stem = QString("recording_%1").arg(
		QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));

	String last = Settings::get_last_directory();
	QString dir;
	if (!last.empty()) {
		QFileInfo fi(QString::fromUtf8(last.data(), int(last.size())));
		dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
	}
	if (dir.isEmpty()) {
		dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	}
	return QDir::toNativeSeparators(dir + "/" + stem + "." + suggestedExtension());
}

// --------------------------------------------------------------------------
// Close / reject behaviour: we must not let the user dismiss the dialog
// while a recording is in progress, otherwise the file would be left
// half-flushed (libsndfile would still close on destruction, but the user
// would have no idea where the file went).
// --------------------------------------------------------------------------

void RecordSoundDialog::closeEvent(QCloseEvent *event)
{
	if (m_recording) {
		QMessageBox::information(this, tr("Record sound"),
			tr("Please press Stop before closing the dialog."));
		event->ignore();
		return;
	}
	QDialog::closeEvent(event);
}

void RecordSoundDialog::reject()
{
	if (m_recording) {
		QMessageBox::information(this, tr("Record sound"),
			tr("Please press Stop before closing the dialog."));
		return;
	}
	QDialog::reject();
}

} // namespace phonometrica
