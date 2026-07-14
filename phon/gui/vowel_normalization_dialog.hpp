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
 * Purpose: Dialog for vowel normalization. The user selects a method, formant columns, a speaker column, and          *
 *          (for Watt & Fabricius) a vowel column with point-vowel label mappings.                                      *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_VOWEL_NORMALIZATION_DIALOG_HPP
#define PHONOMETRICA_VOWEL_NORMALIZATION_DIALOG_HPP

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <phon/application/vowel_normalizer.hpp>
#include <phon/gui/checkable_combo_box.hpp>

class QWidget;
class QGroupBox;

namespace phonometrica {

class VowelNormalizationDialog final : public QDialog
{
	Q_OBJECT

public:

	/// @param numericColumns   Display names of numeric columns.
	/// @param numericIndices   Corresponding 0-based column indices.
	/// @param textColumns      Display names of text/factor columns.
	/// @param textIndices      Corresponding 0-based column indices.
	/// @param vowelLevels      For each text column index in textIndices, the unique levels (for W&F point-vowel mapping).
	VowelNormalizationDialog(const QStringList &numericColumns, const QVector<int> &numericIndices,
	                         const QStringList &textColumns, const QVector<int> &textIndices,
	                         const QVector<QStringList> &vowelLevels,
	                         QWidget *parent = nullptr);

	// Results (valid after accept).
	VowelNormMethod selectedMethod() const;
	QVector<int> selectedFormantColumns() const;   // 0-based dataset column indices
	int speakerColumn() const;                     // 0-based dataset column index
	int vowelColumn() const;                       // 0-based dataset column index (W&F only)
	QString labelI() const;
	QString labelA() const;
	QString labelU() const;
	QStringList outputColumnNames() const;

private slots:

	void onMethodChanged(int index);
	void onVowelColumnChanged(int index);

private:

	void setupUi(const QStringList &numericColumns, const QStringList &textColumns);
	void updateOutputNames();

	QComboBox *m_method_combo = nullptr;
	CheckableComboBox *m_formant_combo = nullptr;
	QComboBox *m_speaker_combo = nullptr;

	// Watt & Fabricius sub-panel.
	QGroupBox *m_wf_group = nullptr;
	QComboBox *m_vowel_combo = nullptr;
	QComboBox *m_label_i_combo = nullptr;
	QComboBox *m_label_a_combo = nullptr;
	QComboBox *m_label_u_combo = nullptr;

	QLineEdit *m_suffix_edit = nullptr;
	QLineEdit *m_preview_edit = nullptr;

	QStringList m_numeric_names;
	QVector<int> m_numeric_indices;
	QStringList m_text_names;
	QVector<int> m_text_indices;
	QVector<QStringList> m_vowel_levels;
};

} // namespace phonometrica

#endif // PHONOMETRICA_VOWEL_NORMALIZATION_DIALOG_HPP
