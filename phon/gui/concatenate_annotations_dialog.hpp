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
 * Purpose: Dialog for the "Concatenate annotations..." action. Lets the user reorder the selected annotations and    *
 * supply explicit durations for any unbound sources. Shape compatibility (same layer count and matching kinds) is     *
 * checked at OK time by the underlying annotation_ops call.                                                           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CONCATENATE_ANNOTATIONS_DIALOG_HPP
#define PHONOMETRICA_CONCATENATE_ANNOTATIONS_DIALOG_HPP

#include <QDialog>
#include <QList>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHash>
#include <vector>
#include <phon/application/annotation.hpp>

namespace phonometrica {

class ConcatenateAnnotationsDialog final : public QDialog
{
	Q_OBJECT

public:

	// `candidates` are the annotations selected in the file manager (2 or more).
	// They must already be loaded.
	ConcatenateAnnotationsDialog(QWidget *parent, const QList<Annotation *> &candidates);

	// User-ordered list of annotations to concatenate.
	QList<Annotation *> orderedSources() const;

	// One duration per source, in the same order as orderedSources(). Sources
	// bound to a sound report that sound's duration; unbound sources report
	// the value the user typed in the corresponding spinner.
	std::vector<double> orderedDurations() const;

	// Output path and format.
	String outputPath() const;
	Annotation::Type outputFormat() const;

private slots:

	void onBrowse();
	void onFormatChanged(int index);

private:

	void refreshOkEnabled();
	void rebuildSuggestedPath();

	QList<Annotation *> m_candidates;
	QListWidget *m_order_list;

	// One spinner per unbound annotation. Empty when every source is bound.
	QHash<Annotation *, QDoubleSpinBox *> m_duration_spins;
	QGroupBox *m_duration_group;

	QLineEdit   *m_path_edit;
	QPushButton *m_browse_button;
	QComboBox   *m_format_combo;
	QPushButton *m_ok_button;
};

} // namespace phonometrica

#endif // PHONOMETRICA_CONCATENATE_ANNOTATIONS_DIALOG_HPP
