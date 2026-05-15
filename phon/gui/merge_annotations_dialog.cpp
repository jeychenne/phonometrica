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

#include <cmath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <phon/gui/merge_annotations_dialog.hpp>
#include <phon/gui/file_dialog.hpp>
#include <phon/application/annotation_ops.hpp>
#include <phon/application/constants.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

MergeAnnotationsDialog::MergeAnnotationsDialog(QWidget *parent,
                                               const QList<Annotation *> &candidates) :
	QDialog(parent),
	m_candidates(candidates)
{
	setWindowTitle(tr("Merge annotations"));
	setMinimumWidth(560);

	auto *layout = new QVBoxLayout(this);

	layout->addWidget(new QLabel(
		tr("Pick the base annotation. Its sound binding, description and format "
		   "are inherited. Layers from the other annotations are appended."), this));

	// Base picker. One radio button per candidate, in a scroll area in case
	// the user multi-selected many files.
	auto *base_group = new QGroupBox(tr("Base annotation"), this);
	auto *base_inner = new QVBoxLayout(base_group);

	m_base_group = new QButtonGroup(this);
	m_base_group->setExclusive(true);

	for (int i = 0; i < m_candidates.size(); ++i)
	{
		auto *a = m_candidates[i];
		auto lbl = a->browser_label();
		QString text = QString::fromUtf8(lbl.data(), (int) lbl.size());
		double d = effective_duration(*a);
		text += tr("   (%1 layers, %2 s)").arg(a->layer_count()).arg(d, 0, 'f', 3);

		auto *rb = new QRadioButton(text, base_group);
		base_inner->addWidget(rb);
		m_base_radios.append(rb);
		m_base_group->addButton(rb, i);
		connect(rb, &QRadioButton::toggled, this, [this](bool on) {
			if (on) onBaseChanged();
		});
	}
	// Don't call setChecked() yet — the toggled signal would fire onBaseChanged()
	// before m_warning_label / m_path_edit / m_format_combo exist. Defer to the
	// bottom of the constructor where everything is ready.

	auto *scroll = new QScrollArea(this);
	scroll->setWidget(base_group);
	scroll->setWidgetResizable(true);
	scroll->setMaximumHeight(200);
	layout->addWidget(scroll);

	// Duration warning (hidden when compatible).
	m_warning_label = new QLabel(this);
	m_warning_label->setStyleSheet("QLabel { color: #b00; }");
	m_warning_label->setWordWrap(true);
	m_warning_label->hide();
	layout->addWidget(m_warning_label);

	// Informational label showing currently chosen base's duration (helps
	// the user understand which value others must match).
	m_duration_label = new QLabel(this);
	m_duration_label->setStyleSheet("QLabel { color: #555; }");
	layout->addWidget(m_duration_label);

	// Output configuration.
	auto *out_group = new QGroupBox(tr("Output"), this);
	auto *out_form  = new QFormLayout(out_group);

	auto *path_row = new QHBoxLayout;
	m_path_edit    = new QLineEdit(out_group);
	m_browse_button = new QPushButton(tr("Browse..."), out_group);
	path_row->addWidget(m_path_edit);
	path_row->addWidget(m_browse_button);
	connect(m_browse_button, &QPushButton::clicked, this, &MergeAnnotationsDialog::onBrowse);
	connect(m_path_edit, &QLineEdit::textChanged, this, [this](const QString&){ refreshOkEnabled(); });
	out_form->addRow(tr("File:"), path_row);

	m_format_combo = new QComboBox(out_group);
	m_format_combo->addItem(tr("Phonometrica annotation"), int(Annotation::Type::Native));
	m_format_combo->addItem(tr("Praat TextGrid"),          int(Annotation::Type::TextGrid));
	connect(m_format_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &MergeAnnotationsDialog::onFormatChanged);
	out_form->addRow(tr("Format:"), m_format_combo);

	layout->addWidget(out_group);

	// Buttons.
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_ok_button = buttons->button(QDialogButtonBox::Ok);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	// Now that every widget exists, check the first radio (its toggled signal
	// will fire onBaseChanged(), which expects all widgets to be ready).
	m_base_radios.first()->setChecked(true);
}

Annotation *MergeAnnotationsDialog::baseAnnotation() const
{
	int id = m_base_group->checkedId();
	if (id < 0 || id >= m_candidates.size()) return nullptr;
	return m_candidates[id];
}

QList<Annotation *> MergeAnnotationsDialog::otherAnnotations() const
{
	int base_id = m_base_group->checkedId();
	QList<Annotation *> others;
	for (int i = 0; i < m_candidates.size(); ++i) {
		if (i != base_id) others.append(m_candidates[i]);
	}
	return others;
}

String MergeAnnotationsDialog::outputPath() const
{
	auto bytes = m_path_edit->text().toUtf8();
	return String(bytes.constData(), bytes.size());
}

Annotation::Type MergeAnnotationsDialog::outputFormat() const
{
	return Annotation::Type(m_format_combo->currentData().toInt());
}

void MergeAnnotationsDialog::onBaseChanged()
{
	auto *base = baseAnnotation();
	if (!base) return;

	// Default the format combo to the base's format on every base change.
	auto fmt = base->is_textgrid() ? Annotation::Type::TextGrid : Annotation::Type::Native;
	int idx = (fmt == Annotation::Type::TextGrid) ? 1 : 0;
	bool blocked = m_format_combo->blockSignals(true);
	m_format_combo->setCurrentIndex(idx);
	m_format_combo->blockSignals(blocked);

	rebuildSuggestedPath();
	refreshDurationWarning();
	refreshOkEnabled();
}

void MergeAnnotationsDialog::onFormatChanged(int /*index*/)
{
	rebuildSuggestedPath();
}

void MergeAnnotationsDialog::onBrowse()
{
	QString filter;
	QString ext;
	if (outputFormat() == Annotation::Type::TextGrid) {
		filter = tr("Praat TextGrid (*.TextGrid)");
		ext = QStringLiteral(".TextGrid");
	}
	else {
		filter = tr("Phonometrica annotation (*.phon-annot)");
		ext = QStringLiteral(PHON_EXT_ANNOTATION);
	}

	QString defaultName = m_path_edit->text();
	if (defaultName.isEmpty()) {
		defaultName = QStringLiteral("merged") + ext;
	}
	auto chosen = getSaveFileName(this, tr("Save merged annotation"), filter, defaultName);
	if (!chosen.isEmpty()) {
		m_path_edit->setText(chosen);
	}
}

void MergeAnnotationsDialog::refreshDurationWarning()
{
	auto *base = baseAnnotation();
	if (!base) {
		m_duration_label->clear();
		m_warning_label->hide();
		m_durations_compatible = false;
		return;
	}

	double base_dur = effective_duration(*base);
	m_duration_label->setText(tr("Base duration: %1 s — all others must match within %2 s.")
		.arg(base_dur, 0, 'f', 3)
		.arg(DEFAULT_DURATION_TOLERANCE, 0, 'f', 3));

	QStringList offenders;
	for (int i = 0; i < m_candidates.size(); ++i)
	{
		if (m_candidates[i] == base) continue;
		double d = effective_duration(*m_candidates[i]);
		if (std::abs(d - base_dur) > DEFAULT_DURATION_TOLERANCE)
		{
			auto lbl = m_candidates[i]->browser_label();
			QString name = QString::fromUtf8(lbl.data(), (int) lbl.size());
			offenders.append(tr("%1 (%2 s)").arg(name).arg(d, 0, 'f', 3));
		}
	}

	if (offenders.isEmpty()) {
		m_warning_label->hide();
		m_durations_compatible = true;
	}
	else {
		m_warning_label->setText(
			tr("Duration mismatch — these annotations cannot be merged with the chosen base: %1")
				.arg(offenders.join(QStringLiteral(", "))));
		m_warning_label->show();
		m_durations_compatible = false;
	}
}

void MergeAnnotationsDialog::rebuildSuggestedPath()
{
	auto *base = baseAnnotation();
	if (!base || !base->has_path()) return;

	String ext = (outputFormat() == Annotation::Type::TextGrid)
	             ? String(".TextGrid")
	             : String(PHON_EXT_ANNOTATION);

	auto dir  = filesystem::directory_name(base->path());
	auto stem = filesystem::strip_ext(filesystem::base_name(base->path()));

	String desired;
	desired.append(stem);
	desired.append("_merged");
	desired.append(ext);
	String suggestion = unique_path(filesystem::join(dir, desired));

	// Replace path only if the field is empty or holds our suggestion's stem.
	auto cur_text = m_path_edit->text();
	if (cur_text.isEmpty()) {
		m_path_edit->setText(QString::fromUtf8(suggestion.data(), (int) suggestion.size()));
		return;
	}

	auto cur_bytes = cur_text.toUtf8();
	String cur(cur_bytes.constData(), cur_bytes.size());
	auto cur_base = filesystem::strip_ext(filesystem::base_name(cur));
	String expected_stem;
	expected_stem.append(stem);
	expected_stem.append("_merged");
	if (cur_base == expected_stem)
	{
		// Same auto-named stem: just swap the extension to match the new format.
		auto cur_stem_path = filesystem::strip_ext(cur);
		String swapped;
		swapped.append(cur_stem_path);
		swapped.append(ext);
		m_path_edit->setText(QString::fromUtf8(swapped.data(), (int) swapped.size()));
	}
}

void MergeAnnotationsDialog::refreshOkEnabled()
{
	bool path_ok = !m_path_edit->text().trimmed().isEmpty();
	m_ok_button->setEnabled(path_ok && m_durations_compatible && baseAnnotation() != nullptr);
}

} // namespace phonometrica
