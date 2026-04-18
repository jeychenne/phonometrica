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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QSettings>
#include <QMessageBox>
#include <QFileInfo>
#include <phon/gui/transcribe_dialog.hpp>
#include <phon/application/project.hpp>

namespace phonometrica {

// Curated subset of the languages whisper supports well. "auto" lets whisper detect.
struct LangEntry { const char *code; const char *label; };
static const LangEntry LANGUAGES[] = {
	{"auto", "Auto-detect"},
	{"en",   "English"},
	{"fr",   "French"},
	{"de",   "German"},
	{"es",   "Spanish"},
	{"it",   "Italian"},
	{"pt",   "Portuguese"},
	{"nl",   "Dutch"},
	{"ru",   "Russian"},
	{"pl",   "Polish"},
	{"tr",   "Turkish"},
	{"ar",   "Arabic"},
	{"ja",   "Japanese"},
	{"zh",   "Chinese"},
	{"ko",   "Korean"},
	{"hi",   "Hindi"},
	{"sv",   "Swedish"},
	{"da",   "Danish"},
	{"no",   "Norwegian"},
	{"fi",   "Finnish"},
	{"cs",   "Czech"},
	{"el",   "Greek"},
	{"he",   "Hebrew"},
	{"uk",   "Ukrainian"},
	{"vi",   "Vietnamese"},
	{"ca",   "Catalan"},
	{"ro",   "Romanian"},
};


TranscribeDialog::TranscribeDialog(QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Transcribe audio"));
	setMinimumWidth(500);

	auto *layout = new QVBoxLayout(this);
	auto *form = new QFormLayout;

	// --- Sound selection ---
	m_sound_combo = new QComboBox(this);
	m_sounds = Project::get()->get_sounds();
	for (intptr_t i = 1; i <= m_sounds.size(); i++)
	{
		auto lbl = m_sounds[i]->browser_label();
		m_sound_combo->addItem(QString::fromUtf8(lbl.data(), (int) lbl.size()));
	}
	form->addRow(tr("Sound file:"), m_sound_combo);

	// --- Model file ---
	auto *model_row = new QHBoxLayout;
	m_model_edit = new QLineEdit(this);
	m_model_edit->setPlaceholderText(tr("Path to ggml whisper model (.bin)"));
	m_browse_button = new QPushButton(tr("Browse..."), this);
	model_row->addWidget(m_model_edit);
	model_row->addWidget(m_browse_button);
	form->addRow(tr("Model:"), model_row);
	connect(m_browse_button, &QPushButton::clicked, this, &TranscribeDialog::onBrowseModel);

	// --- Language ---
	m_language_combo = new QComboBox(this);
	for (const auto &entry : LANGUAGES)
		m_language_combo->addItem(QString::fromUtf8(entry.label), QString::fromUtf8(entry.code));
	form->addRow(tr("Language:"), m_language_combo);

	// --- Translate ---
	m_translate_check = new QCheckBox(tr("Translate to English"), this);
	form->addRow(QString(), m_translate_check);

	// --- Layer label ---
	m_layer_edit = new QLineEdit(this);
	m_layer_edit->setText(tr("transcription"));
	form->addRow(tr("Layer name:"), m_layer_edit);

	layout->addLayout(form);

	// --- Info line ---
	auto *info = new QLabel(tr("The selected audio will be transcribed to an interval layer in a new annotation.\n"
	                           "Transcription runs locally; no data is sent over the network."), this);
	info->setWordWrap(true);
	info->setStyleSheet("color: gray;");
	layout->addWidget(info);

	// --- Buttons ---
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, [this, buttons]() {
		// Validate before accepting.
		if (m_sounds.empty())
		{
			QMessageBox::warning(this, tr("Transcribe"),
				tr("There are no sound files in the project."));
			return;
		}
		if (m_model_edit->text().trimmed().isEmpty())
		{
			QMessageBox::warning(this, tr("Transcribe"),
				tr("Please select a whisper model file."));
			return;
		}
		if (!QFileInfo::exists(m_model_edit->text()))
		{
			QMessageBox::warning(this, tr("Transcribe"),
				tr("The selected model file does not exist:\n%1").arg(m_model_edit->text()));
			return;
		}
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	// Restore previous choices from QSettings.
	QSettings settings;
	auto saved_model = settings.value("transcribe/model_path").toString();
	if (!saved_model.isEmpty())
		m_model_edit->setText(saved_model);

	auto saved_lang = settings.value("transcribe/language", "auto").toString();
	int idx = m_language_combo->findData(saved_lang);
	m_language_combo->setCurrentIndex(idx >= 0 ? idx : 0);

	m_translate_check->setChecked(settings.value("transcribe/translate", false).toBool());
}

void TranscribeDialog::onBrowseModel()
{
	auto current = m_model_edit->text();
	auto start_dir = current.isEmpty() ? QDir::homePath() : QFileInfo(current).absolutePath();

	auto path = QFileDialog::getOpenFileName(this, tr("Select whisper model"),
	                                         start_dir, tr("Whisper model (*.bin);;All files (*)"));
	if (!path.isEmpty())
		m_model_edit->setText(path);
}

Handle<Sound> TranscribeDialog::sound() const
{
	int idx = m_sound_combo->currentIndex();
	if (idx < 0 || m_sounds.empty())
		return Handle<Sound>();
	return m_sounds[idx + 1]; // 1-based Array
}

Transcriber::Options TranscribeDialog::options() const
{
	Transcriber::Options opts;

	auto model_path = m_model_edit->text();
	opts.model_path = String(model_path.toUtf8().constData());

	auto lang = m_language_combo->currentData().toString();
	opts.language = String(lang.toUtf8().constData());

	opts.translate = m_translate_check->isChecked();

	auto label = m_layer_edit->text().trimmed();
	if (label.isEmpty()) label = QStringLiteral("transcription");
	opts.layer_label = String(label.toUtf8().constData());

	// Persist the choices for next time.
	QSettings settings;
	settings.setValue("transcribe/model_path", model_path);
	settings.setValue("transcribe/language",   lang);
	settings.setValue("transcribe/translate",  opts.translate);

	return opts;
}

} // namespace phonometrica
