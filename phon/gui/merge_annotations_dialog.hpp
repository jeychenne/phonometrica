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
 * Purpose: Dialog for the "Merge annotations..." action. Triggered by multi-selecting 2+ annotations in the file      *
 * manager. The user picks one annotation as the base (its sound binding, description and format are inherited) and   *
 * the others are appended. All inputs must share the same duration; the dialog reports any mismatch as a warning at  *
 * the top of the window and blocks OK until the user resolves it.                                                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_MERGE_ANNOTATIONS_DIALOG_HPP
#define PHONOMETRICA_MERGE_ANNOTATIONS_DIALOG_HPP

#include <QDialog>
#include <QList>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QRadioButton>
#include <QButtonGroup>
#include <phon/application/annotation.hpp>

namespace phonometrica {

class MergeAnnotationsDialog final : public QDialog
{
	Q_OBJECT

public:

	// `candidates` is the list of annotations selected in the file manager
	// (2 or more). All must already be loaded.
	MergeAnnotationsDialog(QWidget *parent, const QList<Annotation *> &candidates);

	// The annotation chosen as base. Valid after exec() returns Accepted.
	Annotation *baseAnnotation() const;

	// All non-base candidates, in their original order.
	QList<Annotation *> otherAnnotations() const;

	// Absolute output path.
	String outputPath() const;

	// Output format (never Undefined; defaults to base's format).
	Annotation::Type outputFormat() const;

private slots:

	void onBrowse();
	void onBaseChanged();
	void onFormatChanged(int index);

private:

	void refreshDurationWarning();
	void refreshOkEnabled();
	void rebuildSuggestedPath();

	QList<Annotation *> m_candidates;
	QButtonGroup *m_base_group;
	QList<QRadioButton *> m_base_radios;
	QLabel *m_duration_label;
	QLabel *m_warning_label;

	QLineEdit *m_path_edit;
	QPushButton *m_browse_button;
	QComboBox *m_format_combo;
	QPushButton *m_ok_button;

	bool m_durations_compatible = true;
};

} // namespace phonometrica

#endif // PHONOMETRICA_MERGE_ANNOTATIONS_DIALOG_HPP
