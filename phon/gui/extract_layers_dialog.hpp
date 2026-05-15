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
 * Purpose: Dialog for the "Extract layers..." annotation action. Lets the user pick which layers of an annotation to  *
 * extract into a new annotation, choose the output path, and (optionally) override the on-disk format.                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_EXTRACT_LAYERS_DIALOG_HPP
#define PHONOMETRICA_EXTRACT_LAYERS_DIALOG_HPP

#include <vector>
#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <phon/application/annotation.hpp>

namespace phonometrica {

class ExtractLayersDialog final : public QDialog
{
	Q_OBJECT

public:

	// `source` must already be loaded (open()d) by the caller so that its
	// layer list is available.
	ExtractLayersDialog(QWidget *parent, Annotation &source);

	// 1-based layer indices selected by the user, in display order. Empty if
	// nothing is selected.
	std::vector<intptr_t> selectedLayers() const;

	// Absolute output path chosen by the user. Empty if no path was given.
	String outputPath() const;

	// Annotation::Type::Native or ::TextGrid; never Undefined (the dialog
	// resolves Undefined to the source's format at construction time).
	Annotation::Type outputFormat() const;

private slots:

	void onBrowse();
	void onFormatChanged(int index);
	void onSelectionChanged();

private:

	void refreshOkEnabled();
	void rebuildSuggestedPath();

	Annotation &m_source;
	String m_source_dir;
	String m_source_stem;

	QListWidget *m_layers_list;
	QLineEdit   *m_path_edit;
	QPushButton *m_browse_button;
	QComboBox   *m_format_combo;
	QPushButton *m_ok_button;
};

} // namespace phonometrica

#endif // PHONOMETRICA_EXTRACT_LAYERS_DIALOG_HPP
