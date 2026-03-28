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
 *          Supports both single-file and multi-file selection for batch property editing.                             *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_INFO_PANEL_HPP
#define PHONOMETRICA_INFO_PANEL_HPP

#include <QWidget>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QComboBox>
#include <QCheckBox>
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
	void onSelectionChanged(QList<Document*> docs);

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
	void onOverwriteDescriptionToggled(bool checked);

private:

	void setupUi();
	QWidget *createEmptyPage();
	QWidget *createSingleFilePage();
	QWidget *createMultiFilePage();

	void showEmptyPage();
	void showSingleFilePage(Document *doc);
	void showMultiFilePage();

	void refreshProperties();
	void refreshMultiProperties();
	void updateCategoryCombo();
	void updateValueCombo();
	void enablePropertyEditing(bool enabled);

	// Helpers for the dynamic info area (single-file page).
	void clearInfoArea();
	void addHeading(const QString &text);
	void addValue(const QString &text, const QString &tooltip = QString());

	Project *m_project;

	QStackedWidget *m_stack = nullptr;

	// Page indices.
	int m_empty_page_index = -1;
	int m_single_page_index = -1;
	int m_multi_page_index = -1;

	// Currently selected documents.
	QList<Document*> m_selected_docs;

	// ── Single file page widgets ─────────────────

	// Dynamic info area: cleared and rebuilt for each file.
	QVBoxLayout *m_info_layout = nullptr;

	QLabel *m_sound_label = nullptr;
	QPushButton *m_bind_button = nullptr;

	QTableWidget *m_prop_table = nullptr;
	QPushButton *m_add_prop_btn = nullptr;
	QPushButton *m_remove_prop_btn = nullptr;

	// Property editor (shared between single and multi pages via reparenting — see setupUi).
	QComboBox *m_type_combo = nullptr;
	QComboBox *m_category_combo = nullptr;
	QComboBox *m_value_combo = nullptr;
	QPushButton *m_validate_btn = nullptr;
	QPushButton *m_clear_btn = nullptr;

	QTextEdit *m_description_edit = nullptr;
	QPushButton *m_save_desc_btn = nullptr;

	QPushButton *m_import_btn = nullptr;
	QPushButton *m_export_btn = nullptr;

	// ── Multi-file page widgets ──────────────────

	QLabel *m_multi_header = nullptr;

	QTableWidget *m_multi_prop_table = nullptr;
	QPushButton *m_multi_add_prop_btn = nullptr;
	QPushButton *m_multi_remove_prop_btn = nullptr;

	// Property editor for multi-file page.
	QComboBox *m_multi_type_combo = nullptr;
	QComboBox *m_multi_category_combo = nullptr;
	QComboBox *m_multi_value_combo = nullptr;
	QPushButton *m_multi_validate_btn = nullptr;
	QPushButton *m_multi_clear_btn = nullptr;

	QCheckBox *m_overwrite_desc_check = nullptr;
	QTextEdit *m_multi_description_edit = nullptr;
	QPushButton *m_multi_save_desc_btn = nullptr;

	QPushButton *m_multi_import_btn = nullptr;
	QPushButton *m_multi_export_btn = nullptr;

	// True if the user clicked "Add property" and hasn't validated yet.
	bool m_has_unsaved_property = false;
};

} // namespace phonometrica

#endif // PHONOMETRICA_INFO_PANEL_HPP
