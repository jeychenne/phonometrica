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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QSettings>
#include <QMessageBox>
#include <phon/gui/find_silences_dialog.hpp>
#include <phon/application/project.hpp>

namespace phonometrica {

FindSilencesDialog::FindSilencesDialog(QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Find silences"));
	setMinimumWidth(500);

	auto *layout = new QVBoxLayout(this);
	auto *top_form = new QFormLayout;

	// --- Sound selection ---
	m_sound_combo = new QComboBox(this);
	m_sounds = Project::get()->get_sounds();
	for (intptr_t i = 1; i <= m_sounds.size(); i++)
	{
		auto lbl = m_sounds[i]->browser_label();
		m_sound_combo->addItem(QString::fromUtf8(lbl.data(), (int) lbl.size()));
	}
	top_form->addRow(tr("Sound file:"), m_sound_combo);

	// --- Layer name ---
	m_layer_edit = new QLineEdit(this);
	m_layer_edit->setText(tr("silences"));
	top_form->addRow(tr("Layer name:"), m_layer_edit);

	layout->addLayout(top_form);

	// --- Detection parameters ---
	auto *params_group = new QGroupBox(tr("Detection parameters"), this);
	auto *params_form = new QFormLayout(params_group);

	m_silence_threshold_spin = new QDoubleSpinBox(params_group);
	m_silence_threshold_spin->setRange(-60.0, 0.0);
	m_silence_threshold_spin->setDecimals(1);
	m_silence_threshold_spin->setSingleStep(1.0);
	m_silence_threshold_spin->setSuffix(tr(" dB"));
	m_silence_threshold_spin->setValue(-25.0);
	m_silence_threshold_spin->setToolTip(
		tr("Level below the peak short-term power at or below which a frame is considered silent. "
		   "Lower (more negative) values are more lenient — more audio counts as speech."));
	params_form->addRow(tr("Silence threshold:"), m_silence_threshold_spin);

	m_min_silence_spin = new QDoubleSpinBox(params_group);
	m_min_silence_spin->setRange(0.05, 5.0);
	m_min_silence_spin->setDecimals(2);
	m_min_silence_spin->setSingleStep(0.05);
	m_min_silence_spin->setSuffix(tr(" s"));
	m_min_silence_spin->setValue(0.70);
	m_min_silence_spin->setToolTip(
		tr("Shortest silent stretch that splits the audio into separate regions. "
		   "Silences shorter than this are absorbed into surrounding speech — this prevents "
		   "splits on plosive closures and short intra-word gaps."));
	params_form->addRow(tr("Min. silence duration:"), m_min_silence_spin);

	m_min_speech_spin = new QDoubleSpinBox(params_group);
	m_min_speech_spin->setRange(0.05, 2.0);
	m_min_speech_spin->setDecimals(2);
	m_min_speech_spin->setSingleStep(0.05);
	m_min_speech_spin->setSuffix(tr(" s"));
	m_min_speech_spin->setValue(0.10);
	m_min_speech_spin->setToolTip(
		tr("Shortest isolated speech region to keep. Shorter runs are discarded as noise "
		   "(clicks, coughs, taps)."));
	params_form->addRow(tr("Min. speech duration:"), m_min_speech_spin);

	m_padding_spin = new QDoubleSpinBox(params_group);
	m_padding_spin->setRange(0.0, 1.0);
	m_padding_spin->setDecimals(2);
	m_padding_spin->setSingleStep(0.05);
	m_padding_spin->setSuffix(tr(" s"));
	m_padding_spin->setValue(0.10);
	m_padding_spin->setToolTip(
		tr("Extra audio kept on each side of every detected speech region, so that plosive "
		   "bursts and offsets aren't clipped."));
	params_form->addRow(tr("Padding:"), m_padding_spin);

	layout->addWidget(params_group);

	// --- Labels ---
	auto *labels_group = new QGroupBox(tr("Interval labels"), this);
	auto *labels_form = new QFormLayout(labels_group);

	m_silence_label_edit = new QLineEdit(labels_group);
	m_silence_label_edit->setPlaceholderText(tr("(leave empty for no label)"));
	m_silence_label_edit->setToolTip(tr("Text to put in silent intervals."));
	labels_form->addRow(tr("Silence label:"), m_silence_label_edit);

	m_speech_label_edit = new QLineEdit(labels_group);
	m_speech_label_edit->setPlaceholderText(tr("(leave empty for no label)"));
	m_speech_label_edit->setToolTip(tr("Text to put in speech intervals. Leave empty "
	                                   "when you plan to fill in each interval by hand."));
	labels_form->addRow(tr("Speech label:"), m_speech_label_edit);

	layout->addWidget(labels_group);

	// --- Info ---
	auto *info = new QLabel(
		tr("A new annotation will be created with one interval layer covering the whole sound. "
		   "Silent and speech regions alternate, so you can review and adjust boundaries before "
		   "filling in labels or running transcription."),
		this);
	info->setWordWrap(true);
	info->setStyleSheet("color: gray;");
	layout->addWidget(info);

	// --- Buttons ---
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		if (m_sounds.empty())
		{
			QMessageBox::warning(this, tr("Find silences"),
				tr("There are no sound files in the project."));
			return;
		}
		if (m_layer_edit->text().trimmed().isEmpty())
		{
			QMessageBox::warning(this, tr("Find silences"),
				tr("Please provide a layer name."));
			return;
		}
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	// Restore previous choices from QSettings.
	QSettings settings;

	auto saved_layer = settings.value("find_silences/layer_name").toString();
	if (!saved_layer.isEmpty())
		m_layer_edit->setText(saved_layer);

	m_silence_threshold_spin->setValue(settings.value("find_silences/silence_threshold_db", -25.0).toDouble());
	m_min_silence_spin->setValue      (settings.value("find_silences/min_silence_duration",  0.70).toDouble());
	m_min_speech_spin->setValue       (settings.value("find_silences/min_speech_duration",   0.10).toDouble());
	m_padding_spin->setValue          (settings.value("find_silences/speech_padding",        0.10).toDouble());
	m_silence_label_edit->setText     (settings.value("find_silences/silence_label",         QString()).toString());
	m_speech_label_edit->setText      (settings.value("find_silences/speech_label",          QString()).toString());
}

Handle<Sound> FindSilencesDialog::sound() const
{
	int idx = m_sound_combo->currentIndex();
	if (idx < 0 || m_sounds.empty())
		return Handle<Sound>();
	return m_sounds[idx + 1]; // 1-based Array
}

SilenceDetector::Options FindSilencesDialog::options() const
{
	SilenceDetector::Options opts;
	opts.silence_threshold_db = m_silence_threshold_spin->value();
	opts.min_silence_duration = m_min_silence_spin->value();
	opts.min_speech_duration  = m_min_speech_spin->value();
	opts.speech_padding       = m_padding_spin->value();

	// Persist for next time.
	QSettings settings;
	settings.setValue("find_silences/layer_name",            m_layer_edit->text());
	settings.setValue("find_silences/silence_threshold_db",  opts.silence_threshold_db);
	settings.setValue("find_silences/min_silence_duration",  opts.min_silence_duration);
	settings.setValue("find_silences/min_speech_duration",   opts.min_speech_duration);
	settings.setValue("find_silences/speech_padding",        opts.speech_padding);
	settings.setValue("find_silences/silence_label",         m_silence_label_edit->text());
	settings.setValue("find_silences/speech_label",          m_speech_label_edit->text());

	return opts;
}

QString FindSilencesDialog::layerName() const
{
	auto t = m_layer_edit->text().trimmed();
	return t.isEmpty() ? QStringLiteral("silences") : t;
}

QString FindSilencesDialog::silenceLabel() const
{
	return m_silence_label_edit->text();
}

QString FindSilencesDialog::speechLabel() const
{
	return m_speech_label_edit->text();
}

} // namespace phonometrica
