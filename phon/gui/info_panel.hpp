/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 25/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Information panel displayed as a right dock widget. Shows contextual metadata about the file(s) currently  *
 *          selected in the file manager: properties, description, type-specific details, and editing controls.        *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_INFO_PANEL_HPP
#define PHONOMETRICA_INFO_PANEL_HPP

#include <QWidget>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <phon/application/vfs.hpp>

namespace phonometrica {

class Project;

class InfoPanel : public QWidget
{
	Q_OBJECT

public:

	explicit InfoPanel(Project *project, QWidget *parent = nullptr);

public slots:

	// Called when the file manager selection changes.
	// elem may be nullptr (nothing selected), a Document, or a Directory.
	void onSelectionChanged(Element *elem);

private slots:

	void onAddProperty();
	void onRemoveProperty();
	void onTypeChanged(int index);
	void onCategoryChanged();
	void onValidateProperty();
	void onClearPropertyEditor();
	void onSaveDescription();
	void onBindSound();
	void onPropertySelected();
	void onImportMetadata();
	void onExportMetadata();

private:

	void setupUi();
	QWidget *createEmptyPage();
	QWidget *createSingleFilePage();

	void showEmptyPage();
	void showSingleFilePage(Document *doc);

	void refreshProperties();
	void updateCategoryCombo();
	void updateValueCombo();
	void enablePropertyEditing(bool enabled);

	// Helpers for the dynamic info area.
	void clearInfoArea();
	void addHeading(const QString &text);
	void addValue(const QString &text, const QString &tooltip = QString());

	Project *m_project;

	QStackedWidget *m_stack = nullptr;

	// Page indices.
	int m_empty_page_index = -1;
	int m_single_page_index = -1;

	// ── Single file page widgets ─────────────────

	// Dynamic info area: cleared and rebuilt for each file.
	QVBoxLayout *m_info_layout = nullptr;

	QLabel *m_sound_label = nullptr;
	QPushButton *m_bind_button = nullptr;

	QTableWidget *m_prop_table = nullptr;
	QPushButton *m_add_prop_btn = nullptr;
	QPushButton *m_remove_prop_btn = nullptr;

	// Property editor.
	QComboBox *m_type_combo = nullptr;
	QComboBox *m_category_combo = nullptr;
	QComboBox *m_value_combo = nullptr;
	QPushButton *m_validate_btn = nullptr;
	QPushButton *m_clear_btn = nullptr;

	QTextEdit *m_description_edit = nullptr;
	QPushButton *m_save_desc_btn = nullptr;

	QPushButton *m_import_btn = nullptr;
	QPushButton *m_export_btn = nullptr;

	// Currently displayed document (nullptr if none or multiple).
	Document *m_current_doc = nullptr;

	// True if the user clicked "Add property" and hasn't validated yet.
	bool m_has_unsaved_property = false;
};

} // namespace phonometrica

#endif // PHONOMETRICA_INFO_PANEL_HPP
