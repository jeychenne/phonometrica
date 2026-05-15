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
#include <QLabel>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <phon/gui/extract_layers_dialog.hpp>
#include <phon/gui/file_dialog.hpp>
#include <phon/application/annotation_ops.hpp>
#include <phon/application/constants.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

ExtractLayersDialog::ExtractLayersDialog(QWidget *parent, Annotation &source) :
	QDialog(parent),
	m_source(source)
{
	setWindowTitle(tr("Extract layers"));
	setMinimumWidth(480);

	// Cache source path components for use when the user changes format.
	m_source_dir  = filesystem::directory_name(source.path());
	m_source_stem = filesystem::strip_ext(filesystem::base_name(source.path()));

	auto *layout = new QVBoxLayout(this);

	layout->addWidget(new QLabel(tr("Select the layers to extract:"), this));

	// Layer multi-select.
	m_layers_list = new QListWidget(this);
	m_layers_list->setSelectionMode(QAbstractItemView::NoSelection);
	for (intptr_t i = 1; i <= source.layer_count(); ++i)
	{
		auto label = source.get_layer_label(i);
		auto kind  = source.layer_has_instants(i) ? tr("instants") : tr("intervals");
		QString lbl_qs = QString::fromUtf8(label.data(), (int) label.size());
		if (lbl_qs.isEmpty()) lbl_qs = tr("(unnamed)");

		QString text = tr("%1. %2  —  %3").arg(i).arg(lbl_qs).arg(kind);
		auto *item = new QListWidgetItem(text, m_layers_list);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Checked);  // default: all on
		item->setData(Qt::UserRole, qint64(i));
	}
	connect(m_layers_list, &QListWidget::itemChanged,
	        this, [this](QListWidgetItem*){ onSelectionChanged(); });
	layout->addWidget(m_layers_list);

	// Output configuration.
	auto *out_group = new QGroupBox(tr("Output"), this);
	auto *out_form  = new QFormLayout(out_group);

	auto *path_row = new QHBoxLayout;
	m_path_edit    = new QLineEdit(out_group);
	m_browse_button = new QPushButton(tr("Browse..."), out_group);
	path_row->addWidget(m_path_edit);
	path_row->addWidget(m_browse_button);
	connect(m_browse_button, &QPushButton::clicked, this, &ExtractLayersDialog::onBrowse);
	connect(m_path_edit, &QLineEdit::textChanged, this, [this](const QString&){ refreshOkEnabled(); });
	out_form->addRow(tr("File:"), path_row);

	m_format_combo = new QComboBox(out_group);
	m_format_combo->addItem(tr("Phonometrica annotation"), int(Annotation::Type::Native));
	m_format_combo->addItem(tr("Praat TextGrid"),          int(Annotation::Type::TextGrid));
	// Default to the source's format.
	auto src_fmt = source.is_textgrid() ? Annotation::Type::TextGrid : Annotation::Type::Native;
	int src_idx = (src_fmt == Annotation::Type::TextGrid) ? 1 : 0;
	m_format_combo->setCurrentIndex(src_idx);
	connect(m_format_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &ExtractLayersDialog::onFormatChanged);
	out_form->addRow(tr("Format:"), m_format_combo);

	layout->addWidget(out_group);

	// Buttons.
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_ok_button = buttons->button(QDialogButtonBox::Ok);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	// Initial path suggestion (sibling of source).
	rebuildSuggestedPath();
	refreshOkEnabled();
}

std::vector<intptr_t> ExtractLayersDialog::selectedLayers() const
{
	std::vector<intptr_t> result;
	for (int i = 0; i < m_layers_list->count(); ++i)
	{
		auto *item = m_layers_list->item(i);
		if (item->checkState() == Qt::Checked) {
			result.push_back(intptr_t(item->data(Qt::UserRole).toLongLong()));
		}
	}
	return result;
}

String ExtractLayersDialog::outputPath() const
{
	auto bytes = m_path_edit->text().toUtf8();
	return String(bytes.constData(), bytes.size());
}

Annotation::Type ExtractLayersDialog::outputFormat() const
{
	return Annotation::Type(m_format_combo->currentData().toInt());
}

void ExtractLayersDialog::onBrowse()
{
	QString filter;
	QString ext_pattern;
	if (outputFormat() == Annotation::Type::TextGrid) {
		filter = tr("Praat TextGrid (*.TextGrid)");
		ext_pattern = QStringLiteral(".TextGrid");
	}
	else {
		filter = tr("Phonometrica annotation (*.phon-annot)");
		ext_pattern = QStringLiteral(PHON_EXT_ANNOTATION);
	}

	QString defaultName = m_path_edit->text();
	if (defaultName.isEmpty()) {
		defaultName = QString::fromUtf8(m_source_stem.data(), (int) m_source_stem.size())
		            + QStringLiteral("_layers") + ext_pattern;
	}
	auto chosen = getSaveFileName(this, tr("Save extracted annotation"), filter, defaultName);
	if (!chosen.isEmpty()) {
		m_path_edit->setText(chosen);
	}
}

void ExtractLayersDialog::onFormatChanged(int /*index*/)
{
	// Keep the suggested path in sync with the chosen format. Don't overwrite
	// a path the user already edited to something other than our suggestion.
	rebuildSuggestedPath();
}

void ExtractLayersDialog::onSelectionChanged()
{
	refreshOkEnabled();
}

void ExtractLayersDialog::rebuildSuggestedPath()
{
	String ext = (outputFormat() == Annotation::Type::TextGrid)
	             ? String(".TextGrid")
	             : String(PHON_EXT_ANNOTATION);

	String desired;
	desired.append(m_source_stem);
	desired.append("_layers");
	desired.append(ext);
	String suggestion = unique_path(filesystem::join(m_source_dir, desired));

	// Only update if the field is empty or holds our previous suggestion's stem.
	if (m_path_edit->text().isEmpty())
	{
		m_path_edit->setText(QString::fromUtf8(suggestion.data(), (int) suggestion.size()));
	}
	else
	{
		// If only the extension changed, swap it.
		auto cur_bytes = m_path_edit->text().toUtf8();
		String cur(cur_bytes.constData(), cur_bytes.size());
		auto cur_stem = filesystem::strip_ext(cur);
		String swapped;
		swapped.append(cur_stem);
		swapped.append(ext);
		// Only auto-swap when the current path is in the source's directory and
		// matches the "<stem>_layers" pattern we generated. Otherwise leave the
		// user's path alone.
		auto cur_base = filesystem::strip_ext(filesystem::base_name(cur));
		String expected_stem;
		expected_stem.append(m_source_stem);
		expected_stem.append("_layers");
		if (cur_base == expected_stem) {
			m_path_edit->setText(QString::fromUtf8(swapped.data(), (int) swapped.size()));
		}
	}
}

void ExtractLayersDialog::refreshOkEnabled()
{
	bool any_checked = false;
	for (int i = 0; i < m_layers_list->count(); ++i) {
		if (m_layers_list->item(i)->checkState() == Qt::Checked) {
			any_checked = true;
			break;
		}
	}
	bool path_ok = !m_path_edit->text().trimmed().isEmpty();
	m_ok_button->setEnabled(any_checked && path_ok);
}

} // namespace phonometrica
