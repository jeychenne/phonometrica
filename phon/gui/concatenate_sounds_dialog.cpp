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

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <phon/gui/concatenate_sounds_dialog.hpp>
#include <phon/gui/file_dialog.hpp>
#include <phon/application/annotation_ops.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

ConcatenateSoundsDialog::ConcatenateSoundsDialog(QWidget *parent,
                                                 const QList<Sound *> &candidates) :
	QDialog(parent),
	m_candidates(candidates)
{
	setWindowTitle(tr("Concatenate sounds"));
	setMinimumWidth(620);

	auto *layout = new QVBoxLayout(this);

	layout->addWidget(new QLabel(
		tr("Drag rows to set the concatenation order. All sources must share "
		   "the same sample rate and channel count."),
		this));

	// --- Compatibility warning -------------------------------------------
	m_warning_label = new QLabel(this);
	m_warning_label->setStyleSheet("QLabel { color: #b00; }");
	m_warning_label->setWordWrap(true);
	m_warning_label->hide();
	layout->addWidget(m_warning_label);

	// Compute compatibility once upfront. The properties are immutable for the
	// lifetime of the dialog (sources can't be edited from here) so we do not
	// re-check on every reorder.
	{
		int rate = m_candidates.first()->sample_rate();
		int channels = m_candidates.first()->nchannel();
		QStringList offenders;
		for (int i = 1; i < m_candidates.size(); ++i)
		{
			auto *s = m_candidates[i];
			if (s->sample_rate() != rate || s->nchannel() != channels)
			{
				auto lbl = s->browser_label();
				QString name = QString::fromUtf8(lbl.data(), (int) lbl.size());
				offenders.append(tr("%1 (%2 Hz, %3 ch)")
				                 .arg(name).arg(s->sample_rate()).arg(s->nchannel()));
			}
		}
		if (offenders.isEmpty()) {
			m_compatible = true;
		}
		else {
			m_warning_label->setText(
				tr("Sample-rate or channel-count mismatch — these sources are incompatible "
				   "with the first (%1 Hz, %2 ch): %3.")
				   .arg(rate).arg(channels).arg(offenders.join(QStringLiteral(", "))));
			m_warning_label->show();
		}
	}

	// --- Ordered list -----------------------------------------------------
	m_order_list = new QListWidget(this);
	m_order_list->setDragDropMode(QAbstractItemView::InternalMove);
	m_order_list->setSelectionMode(QAbstractItemView::SingleSelection);
	m_order_list->setDefaultDropAction(Qt::MoveAction);

	for (auto *s : m_candidates)
	{
		auto lbl = s->browser_label();
		QString name = QString::fromUtf8(lbl.data(), (int) lbl.size());
		QString text = tr("%1   —   %2 Hz, %3 ch, %4 s")
		               .arg(name)
		               .arg(s->sample_rate())
		               .arg(s->nchannel())
		               .arg(s->duration(), 0, 'f', 3);

		auto *item = new QListWidgetItem(text, m_order_list);
		item->setData(Qt::UserRole, qulonglong(reinterpret_cast<quintptr>(s)));
	}
	layout->addWidget(m_order_list, /*stretch=*/1);

	// --- Output -----------------------------------------------------------
	auto *out_group = new QGroupBox(tr("Output"), this);
	auto *out_form  = new QFormLayout(out_group);

	auto *path_row = new QHBoxLayout;
	m_path_edit     = new QLineEdit(out_group);
	m_browse_button = new QPushButton(tr("Browse..."), out_group);
	path_row->addWidget(m_path_edit);
	path_row->addWidget(m_browse_button);
	connect(m_browse_button, &QPushButton::clicked, this, &ConcatenateSoundsDialog::onBrowse);
	connect(m_path_edit, &QLineEdit::textChanged, this, [this](const QString&){ refreshOkEnabled(); });
	out_form->addRow(tr("File:"), path_row);

	m_format_combo = new QComboBox(out_group);
	m_format_combo->addItem(tr("WAV"),  int(Sound::Format::WAV));
	m_format_combo->addItem(tr("AIFF"), int(Sound::Format::AIFF));
	m_format_combo->addItem(tr("FLAC"), int(Sound::Format::FLAC));
	m_format_combo->addItem(tr("OGG"),  int(Sound::Format::OGG));
	m_format_combo->setCurrentIndex(0);  // default WAV
	connect(m_format_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &ConcatenateSoundsDialog::onFormatChanged);
	out_form->addRow(tr("Format:"), m_format_combo);

	layout->addWidget(out_group);

	// --- Buttons ----------------------------------------------------------
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_ok_button = buttons->button(QDialogButtonBox::Ok);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	rebuildSuggestedPath();
	refreshOkEnabled();
}

QList<Sound *> ConcatenateSoundsDialog::orderedSources() const
{
	QList<Sound *> result;
	for (int i = 0; i < m_order_list->count(); ++i)
	{
		auto *item = m_order_list->item(i);
		auto raw = item->data(Qt::UserRole).toULongLong();
		auto *s = reinterpret_cast<Sound *>(quintptr(raw));
		if (s) result.append(s);
	}
	return result;
}

String ConcatenateSoundsDialog::outputPath() const
{
	auto bytes = m_path_edit->text().toUtf8();
	return String(bytes.constData(), bytes.size());
}

Sound::Format ConcatenateSoundsDialog::outputFormat() const
{
	return Sound::Format(m_format_combo->currentData().toInt());
}

void ConcatenateSoundsDialog::onFormatChanged(int /*index*/)
{
	rebuildSuggestedPath();
}

void ConcatenateSoundsDialog::onBrowse()
{
	QString filter, ext;
	switch (outputFormat()) {
		case Sound::Format::WAV:  filter = tr("WAV (*.wav)");   ext = QStringLiteral(".wav");  break;
		case Sound::Format::AIFF: filter = tr("AIFF (*.aiff)"); ext = QStringLiteral(".aiff"); break;
		case Sound::Format::FLAC: filter = tr("FLAC (*.flac)"); ext = QStringLiteral(".flac"); break;
		case Sound::Format::OGG:  filter = tr("OGG (*.ogg)");   ext = QStringLiteral(".ogg");  break;
	}

	QString defaultName = m_path_edit->text();
	if (defaultName.isEmpty()) {
		defaultName = QStringLiteral("concatenated") + ext;
	}
	auto chosen = getSaveFileName(this, tr("Save concatenated sound"), filter, defaultName);
	if (!chosen.isEmpty()) {
		m_path_edit->setText(chosen);
	}
}

void ConcatenateSoundsDialog::rebuildSuggestedPath()
{
	auto *first = m_candidates.first();
	if (!first->has_path()) return;

	String ext;
	switch (outputFormat()) {
		case Sound::Format::WAV:  ext = String(".wav");  break;
		case Sound::Format::AIFF: ext = String(".aiff"); break;
		case Sound::Format::FLAC: ext = String(".flac"); break;
		case Sound::Format::OGG:  ext = String(".ogg");  break;
	}

	auto dir = filesystem::directory_name(first->path());

	String desired;
	desired.append("concatenated");
	desired.append(ext);
	String suggestion = unique_path(filesystem::join(dir, desired));

	auto cur = m_path_edit->text();
	if (cur.isEmpty()) {
		m_path_edit->setText(QString::fromUtf8(suggestion.data(), (int) suggestion.size()));
		return;
	}

	auto cur_bytes = cur.toUtf8();
	String cur_s(cur_bytes.constData(), cur_bytes.size());
	auto cur_base = filesystem::strip_ext(filesystem::base_name(cur_s));
	if (cur_base == String("concatenated"))
	{
		auto cur_stem_path = filesystem::strip_ext(cur_s);
		String swapped;
		swapped.append(cur_stem_path);
		swapped.append(ext);
		m_path_edit->setText(QString::fromUtf8(swapped.data(), (int) swapped.size()));
	}
}

void ConcatenateSoundsDialog::refreshOkEnabled()
{
	bool path_ok = !m_path_edit->text().trimmed().isEmpty();
	m_ok_button->setEnabled(path_ok && m_compatible);
}

} // namespace phonometrica
