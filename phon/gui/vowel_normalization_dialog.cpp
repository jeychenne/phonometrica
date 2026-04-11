/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 11/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <phon/gui/vowel_normalization_dialog.hpp>

namespace phonometrica {

VowelNormalizationDialog::VowelNormalizationDialog(
	const QStringList &numericColumns, const QVector<int> &numericIndices,
	const QStringList &textColumns, const QVector<int> &textIndices,
	const QVector<QStringList> &vowelLevels,
	QWidget *parent) :
	QDialog(parent),
	m_numeric_names(numericColumns),
	m_numeric_indices(numericIndices),
	m_text_names(textColumns),
	m_text_indices(textIndices),
	m_vowel_levels(vowelLevels)
{
	setWindowTitle(tr("Vowel normalization"));
	setMinimumWidth(450);
	setupUi(numericColumns, textColumns);
}

void VowelNormalizationDialog::setupUi(const QStringList &numericColumns,
                                       const QStringList &textColumns)
{
	auto *layout = new QVBoxLayout(this);

	auto *form = new QFormLayout;
	form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

	// Method.
	m_method_combo = new QComboBox;
	m_method_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_method_combo->addItem(tr("Lobanov"),        (int) VowelNormMethod::Lobanov);
	m_method_combo->addItem(tr("Nearey 1 (per-formant)"), (int) VowelNormMethod::Nearey1);
	m_method_combo->addItem(tr("Nearey 2 (uniform)"),     (int) VowelNormMethod::Nearey2);
	m_method_combo->addItem(tr("Watt & Fabricius"),       (int) VowelNormMethod::WattFabricius);
	form->addRow(tr("Method:"), m_method_combo);

	// Formant columns (multi-select).
	m_formant_combo = new CheckableComboBox;
	m_formant_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_formant_combo->setItems(numericColumns);
	form->addRow(tr("Formant column(s):"), m_formant_combo);

	// Speaker column.
	m_speaker_combo = new QComboBox;
	m_speaker_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_speaker_combo->addItems(textColumns);
	form->addRow(tr("Speaker column:"), m_speaker_combo);

	layout->addLayout(form);

	// ── Watt & Fabricius sub-panel ──
	m_wf_group = new QGroupBox(tr("Point vowel mapping"));
	auto *wf_form = new QFormLayout(m_wf_group);
	wf_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

	m_vowel_combo = new QComboBox;
	m_vowel_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_vowel_combo->addItems(textColumns);
	wf_form->addRow(tr("Vowel column:"), m_vowel_combo);

	m_label_i_combo = new QComboBox;
	m_label_i_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	wf_form->addRow(tr("Label for /i/:"), m_label_i_combo);

	m_label_a_combo = new QComboBox;
	m_label_a_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	wf_form->addRow(tr("Label for /a/:"), m_label_a_combo);

	m_label_u_combo = new QComboBox;
	m_label_u_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	wf_form->addRow(tr("Label for /u/:"), m_label_u_combo);

	m_wf_group->setVisible(false);
	layout->addWidget(m_wf_group);

	// ── Output naming ──
	layout->addSpacing(8);
	auto *out_form = new QFormLayout;
	out_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

	m_suffix_edit = new QLineEdit;
	out_form->addRow(tr("Suffix:"), m_suffix_edit);

	m_preview_edit = new QLineEdit;
	m_preview_edit->setReadOnly(true);
	m_preview_edit->setStyleSheet(QStringLiteral("color: gray;"));
	out_form->addRow(tr("Output columns:"), m_preview_edit);

	layout->addLayout(out_form);

	// ── Buttons ──
	layout->addSpacing(8);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		// Validate.
		auto checked = m_formant_combo->checkedItems();
		if (checked.isEmpty()) {
			QMessageBox::information(this, tr("Information"),
				tr("Please select at least one formant column."));
			return;
		}
		auto method = selectedMethod();
		if (method == VowelNormMethod::WattFabricius && checked.size() != 2) {
			QMessageBox::information(this, tr("Information"),
				tr("Watt & Fabricius normalization requires exactly 2 formant columns (F1 and F2)."));
			return;
		}
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	// ── Connections ──
	connect(m_method_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &VowelNormalizationDialog::onMethodChanged);
	connect(m_formant_combo, &CheckableComboBox::checkedItemsChanged,
	        this, [this](const QStringList &) { updateOutputNames(); });
	connect(m_suffix_edit, &QLineEdit::textChanged,
	        this, [this](const QString &) { updateOutputNames(); });
	connect(m_vowel_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &VowelNormalizationDialog::onVowelColumnChanged);

	// Initialize.
	onMethodChanged(0);
	if (!m_vowel_levels.isEmpty()) {
		onVowelColumnChanged(0);
	}
}

void VowelNormalizationDialog::onMethodChanged(int)
{
	auto method = selectedMethod();
	m_wf_group->setVisible(method == VowelNormMethod::WattFabricius);
	m_suffix_edit->setText(QString::fromLatin1(VowelNormalizer::method_suffix(method)));
	updateOutputNames();
}

void VowelNormalizationDialog::onVowelColumnChanged(int index)
{
	if (index < 0 || index >= m_vowel_levels.size()) return;

	auto &levels = m_vowel_levels[index];
	for (auto *combo : { m_label_i_combo, m_label_a_combo, m_label_u_combo }) {
		combo->clear();
		combo->addItems(levels);
	}

	// Try to auto-select sensible defaults from IPA symbols.
	auto trySelect = [&](QComboBox *combo, const QStringList &candidates) {
		for (auto &c : candidates) {
			int idx = levels.indexOf(c, Qt::CaseInsensitive);
			if (idx >= 0) { combo->setCurrentIndex(idx); return; }
		}
	};
	trySelect(m_label_i_combo, { "i", "iː", "i:", "iy", "IY" });
	trySelect(m_label_a_combo, { "a", "aː", "a:", "ɑ", "ɑː", "aa", "AA" });
	trySelect(m_label_u_combo, { "u", "uː", "u:", "uw", "UW" });
}

void VowelNormalizationDialog::updateOutputNames()
{
	auto checked = m_formant_combo->checkedItems();
	auto suffix = m_suffix_edit->text().trimmed();
	QStringList names;
	for (auto &col : checked) {
		names << col + QStringLiteral("_") + suffix;
	}
	m_preview_edit->setText(names.join(QStringLiteral(", ")));
}

VowelNormMethod VowelNormalizationDialog::selectedMethod() const
{
	return (VowelNormMethod) m_method_combo->currentData().toInt();
}

QVector<int> VowelNormalizationDialog::selectedFormantColumns() const
{
	QVector<int> result;
	auto checked = m_formant_combo->checkedItems();
	for (auto &name : checked) {
		int idx = m_numeric_names.indexOf(name);
		if (idx >= 0 && idx < m_numeric_indices.size()) {
			result.append(m_numeric_indices[idx]);
		}
	}
	return result;
}

int VowelNormalizationDialog::speakerColumn() const
{
	int idx = m_speaker_combo->currentIndex();
	if (idx < 0 || idx >= m_text_indices.size()) return 0;
	return m_text_indices[idx];
}

int VowelNormalizationDialog::vowelColumn() const
{
	int idx = m_vowel_combo->currentIndex();
	if (idx < 0 || idx >= m_text_indices.size()) return 0;
	return m_text_indices[idx];
}

QString VowelNormalizationDialog::labelI() const { return m_label_i_combo->currentText(); }
QString VowelNormalizationDialog::labelA() const { return m_label_a_combo->currentText(); }
QString VowelNormalizationDialog::labelU() const { return m_label_u_combo->currentText(); }

QStringList VowelNormalizationDialog::outputColumnNames() const
{
	auto checked = m_formant_combo->checkedItems();
	auto suffix = m_suffix_edit->text().trimmed();
	QStringList names;
	for (auto &col : checked) {
		names << col + QStringLiteral("_") + suffix;
	}
	return names;
}

} // namespace phonometrica
