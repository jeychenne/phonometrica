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
#include <phon/gui/concatenate_annotations_dialog.hpp>
#include <phon/gui/file_dialog.hpp>
#include <phon/application/annotation_ops.hpp>
#include <phon/application/constants.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

ConcatenateAnnotationsDialog::ConcatenateAnnotationsDialog(QWidget *parent,
                                                           const QList<Annotation *> &candidates) :
	QDialog(parent),
	m_candidates(candidates)
{
	setWindowTitle(tr("Concatenate annotations"));
	setMinimumWidth(620);

	auto *layout = new QVBoxLayout(this);

	layout->addWidget(new QLabel(
		tr("Drag rows to set the concatenation order. The first annotation's "
		   "layer labels, format, properties, and description are inherited."),
		this));

	// --- Ordered list -----------------------------------------------------
	m_order_list = new QListWidget(this);
	m_order_list->setDragDropMode(QAbstractItemView::InternalMove);
	m_order_list->setSelectionMode(QAbstractItemView::SingleSelection);
	m_order_list->setDefaultDropAction(Qt::MoveAction);

	for (auto *a : m_candidates)
	{
		auto lbl = a->browser_label();
		QString name = QString::fromUtf8(lbl.data(), (int) lbl.size());

		QString text;
		if (a->has_sound()) {
			text = tr("%1   —   %2 layers, %3 s (bound)")
			       .arg(name)
			       .arg(a->layer_count())
			       .arg(a->sound()->duration(), 0, 'f', 3);
		}
		else {
			text = tr("%1   —   %2 layers, unbound (set duration below)")
			       .arg(name)
			       .arg(a->layer_count());
		}

		auto *item = new QListWidgetItem(text, m_order_list);
		// Store the annotation pointer directly. We cast through a 64-bit
		// integer because QVariant doesn't accept arbitrary void*.
		item->setData(Qt::UserRole, qulonglong(reinterpret_cast<quintptr>(a)));
	}
	layout->addWidget(m_order_list, /*stretch=*/1);

	// --- Per-source duration input for unbound annotations ----------------
	m_duration_group = new QGroupBox(tr("Durations for unbound annotations"), this);
	auto *dur_form = new QFormLayout(m_duration_group);

	for (auto *a : m_candidates)
	{
		if (a->has_sound()) continue;  // bound sources don't need a manual duration

		double default_dur = effective_duration(*a);
		auto lbl = a->browser_label();
		QString name = QString::fromUtf8(lbl.data(), (int) lbl.size());

		auto *spin = new QDoubleSpinBox(m_duration_group);
		spin->setDecimals(4);
		spin->setSingleStep(0.1);
		spin->setSuffix(tr(" s"));
		spin->setMinimum(1e-4);
		spin->setMaximum(1e9);  // arbitrary high ceiling — durations are unbounded
		spin->setValue(default_dur > 0 ? default_dur : 1.0);

		dur_form->addRow(name + QStringLiteral(":"), spin);
		m_duration_spins.insert(a, spin);

		connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
		        this, [this](double){ refreshOkEnabled(); });
	}

	if (m_duration_spins.isEmpty()) {
		m_duration_group->setVisible(false);
	}
	layout->addWidget(m_duration_group);

	// --- Output -----------------------------------------------------------
	auto *out_group = new QGroupBox(tr("Output"), this);
	auto *out_form  = new QFormLayout(out_group);

	auto *path_row = new QHBoxLayout;
	m_path_edit     = new QLineEdit(out_group);
	m_browse_button = new QPushButton(tr("Browse..."), out_group);
	path_row->addWidget(m_path_edit);
	path_row->addWidget(m_browse_button);
	connect(m_browse_button, &QPushButton::clicked, this, &ConcatenateAnnotationsDialog::onBrowse);
	connect(m_path_edit, &QLineEdit::textChanged, this, [this](const QString&){ refreshOkEnabled(); });
	out_form->addRow(tr("File:"), path_row);

	m_format_combo = new QComboBox(out_group);
	m_format_combo->addItem(tr("Phonometrica annotation"), int(Annotation::Type::Native));
	m_format_combo->addItem(tr("Praat TextGrid"),          int(Annotation::Type::TextGrid));
	// Default to the first source's format.
	auto first_fmt = m_candidates.first()->is_textgrid()
	                 ? Annotation::Type::TextGrid : Annotation::Type::Native;
	m_format_combo->setCurrentIndex(first_fmt == Annotation::Type::TextGrid ? 1 : 0);
	connect(m_format_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &ConcatenateAnnotationsDialog::onFormatChanged);
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

QList<Annotation *> ConcatenateAnnotationsDialog::orderedSources() const
{
	QList<Annotation *> result;
	for (int i = 0; i < m_order_list->count(); ++i)
	{
		auto *item = m_order_list->item(i);
		auto raw = item->data(Qt::UserRole).toULongLong();
		auto *a = reinterpret_cast<Annotation *>(quintptr(raw));
		if (a) result.append(a);
	}
	return result;
}

std::vector<double> ConcatenateAnnotationsDialog::orderedDurations() const
{
	std::vector<double> result;
	auto order = orderedSources();
	result.reserve(order.size());
	for (auto *a : order)
	{
		if (a->has_sound()) {
			result.push_back(a->sound()->duration());
		}
		else {
			auto it = m_duration_spins.find(a);
			result.push_back(it != m_duration_spins.end() ? it.value()->value() : 0.0);
		}
	}
	return result;
}

String ConcatenateAnnotationsDialog::outputPath() const
{
	auto bytes = m_path_edit->text().toUtf8();
	return String(bytes.constData(), bytes.size());
}

Annotation::Type ConcatenateAnnotationsDialog::outputFormat() const
{
	return Annotation::Type(m_format_combo->currentData().toInt());
}

void ConcatenateAnnotationsDialog::onFormatChanged(int /*index*/)
{
	rebuildSuggestedPath();
}

void ConcatenateAnnotationsDialog::onBrowse()
{
	QString filter, ext;
	if (outputFormat() == Annotation::Type::TextGrid) {
		filter = tr("Praat TextGrid (*.TextGrid)");
		ext    = QStringLiteral(".TextGrid");
	}
	else {
		filter = tr("Phonometrica annotation (*.phon-annot)");
		ext    = QStringLiteral(PHON_EXT_ANNOTATION);
	}
	QString defaultName = m_path_edit->text();
	if (defaultName.isEmpty()) {
		defaultName = QStringLiteral("concatenated") + ext;
	}
	auto chosen = getSaveFileName(this, tr("Save concatenated annotation"), filter, defaultName);
	if (!chosen.isEmpty()) {
		m_path_edit->setText(chosen);
	}
}

void ConcatenateAnnotationsDialog::rebuildSuggestedPath()
{
	auto *first = m_candidates.first();
	if (!first->has_path()) return;

	String ext = (outputFormat() == Annotation::Type::TextGrid)
	             ? String(".TextGrid") : String(PHON_EXT_ANNOTATION);

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

void ConcatenateAnnotationsDialog::refreshOkEnabled()
{
	bool path_ok = !m_path_edit->text().trimmed().isEmpty();

	// Every unbound source needs a positive duration.
	bool durations_ok = true;
	for (auto it = m_duration_spins.constBegin(); it != m_duration_spins.constEnd(); ++it)
	{
		if (it.value()->value() <= 0.0) {
			durations_ok = false;
			break;
		}
	}

	m_ok_button->setEnabled(path_ok && durations_ok);
}

} // namespace phonometrica
