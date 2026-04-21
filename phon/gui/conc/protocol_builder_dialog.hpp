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
 * Purpose: Interactive dialog for authoring a coding protocol. The dialog is driven directly by its widgets (no       *
 *          intermediate data structure): each edit retriggers a debounced live preview that serializes the current    *
 *          widget state to JSON, parses it through Protocol(rt, json, FromString), and applies it to a user-provided  *
 *          sample. Launchable standalone (Plugins menu) or from a concordance column (pre-fills sample, enables       *
 *          apply-to-this-column).                                                                                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PROTOCOL_BUILDER_DIALOG_HPP
#define PHONOMETRICA_PROTOCOL_BUILDER_DIALOG_HPP

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <vector>
#include <phon/array.hpp>
#include <phon/string.hpp>
#include <phon/application/conc/concordance.hpp>

namespace phonometrica {

class Runtime;
class Plugin;

// Per-field widget state, kept in sync with the currently-selected row of m_field_list.
// When the user switches rows, the editor widgets are saved back into the outgoing row's
// FieldData (via its QListWidgetItem data slot) and reloaded from the incoming row's.
struct FieldData
{
	String name;
	String match_all;
	// Parallel arrays: values[i].first is the match regex, values[i].second is the human label.
	Array<std::pair<String, String>> values;
};

class ProtocolBuilderDialog : public QDialog
{
	Q_OBJECT

public:

	// Standalone constructor: the Apply button is hidden (save then use the column context menu
	// to apply). Sample textbox is empty.
	ProtocolBuilderDialog(Runtime &rt, QWidget *parent = nullptr);

	// Column-launched constructor: sample textbox is pre-filled with column values; Apply button
	// is enabled and defaults to applying to that column on the given concordance.
	ProtocolBuilderDialog(Runtime &rt, Handle<Concordance> conc, intptr_t source_col,
	                      QWidget *parent = nullptr);

	~ProtocolBuilderDialog() override;

	// Pre-fill the sample textbox with lines of text. Each entry becomes one line.
	void setSampleText(const Array<String> &samples);

private slots:

	// Field list management.
	void onAddField();
	void onRemoveField();
	void onMoveFieldUp();
	void onMoveFieldDown();
	void onFieldSelectionChanged();

	// Value table management (for the currently-selected field).
	void onAddValue();
	void onRemoveValue();

	// Protocol-level or field-level edits that should retrigger the preview.
	void onAnyEditChanged();

	// Debounced preview runner.
	void runPreview();

	// Menu and file actions.
	void onLoad();
	void onSave();
	void onApply();

private:

	void setupUi();

	// Build the "Load" popup menu: installed protocols (grouped by plugin) + Browse... entry.
	// Invoked as a popup from the Load button.
	void showLoadMenu();

	// Load a protocol file into the dialog (replaces current widget state).
	void loadFromPath(const String &path);

	// Serialize current widget state to JSON. Returns an empty string if the protocol is
	// structurally incomplete (no fields, etc.); the preview treats that as "nothing to do".
	String buildJson() const;

	// Flush the currently-selected field's editor widgets back into its FieldData payload.
	// Called before switching rows or before any operation that reads field data.
	void commitSelectedField();

	// Load a FieldData payload into the editor widgets.
	void loadFieldIntoEditor(const FieldData &field);

	// Access the FieldData payload for a given row (nullptr if row is out of range).
	FieldData *fieldDataAt(int row);
	const FieldData *fieldDataAt(int row) const;

	// Enumerate all protocols installed under plugins. Returns (plugin label, protocol path)
	// pairs so the Load menu can group them.
	Array<std::pair<String, String>> enumeratePluginProtocols() const;

	// Run the preview pipeline end-to-end: buildJson -> Protocol(FromString) -> apply_protocol
	// on the sample lines -> populate the preview table. Errors populate the status label.
	void doPreview();

	Runtime &m_runtime;

	// Launch context: when non-null, Apply is enabled and targets this concordance/column pair.
	Handle<Concordance> m_target_conc;
	intptr_t m_target_col = 0;

	// ── Widgets ──────────────────────────────────────────────────────────

	// Protocol-level widgets (top / bottom strip).
	QLineEdit *m_name_edit = nullptr;
	QLineEdit *m_separator_edit = nullptr;
	QCheckBox *m_case_sensitive = nullptr;

	// Left pane: field list + +/-/up/down buttons. Each item's data slot holds a FieldData.
	QListWidget *m_field_list = nullptr;
	QPushButton *m_field_add_btn = nullptr;
	QPushButton *m_field_remove_btn = nullptr;
	QPushButton *m_field_up_btn = nullptr;
	QPushButton *m_field_down_btn = nullptr;

	// Parallel storage: m_field_data[row] is the data for m_field_list row `row`. Kept in sync
	// with the list widget by all add/remove/move operations. Avoids Q_DECLARE_METATYPE.
	std::vector<FieldData> m_field_data;

	// Middle pane: selected-field editor. Enabled iff exactly one field is selected.
	QLineEdit *m_field_name_edit = nullptr;
	QLineEdit *m_field_match_all_edit = nullptr;
	QTableWidget *m_value_table = nullptr;
	QPushButton *m_value_add_btn = nullptr;
	QPushButton *m_value_remove_btn = nullptr;

	// Right pane: sample + live preview.
	QPlainTextEdit *m_sample_edit = nullptr;
	QTableWidget *m_preview_table = nullptr;
	QLabel *m_preview_status = nullptr;   // shows parse errors / match failure counts

	// Bottom action bar.
	QPushButton *m_load_btn = nullptr;
	QPushButton *m_save_btn = nullptr;
	QPushButton *m_apply_btn = nullptr;
	QPushButton *m_close_btn = nullptr;

	// Debounce timer for live preview: any edit restarts the timer; preview fires when it
	// expires (200 ms after the last keystroke).
	QTimer *m_preview_timer = nullptr;

	// Tracks the row currently loaded into the editor widgets. When the user switches rows we
	// need to save the outgoing row's state back before the new row overwrites the widgets.
	int m_loaded_row = -1;

	// Guard flag: when true, edit signals do not trigger the preview timer. Set while
	// programmatically loading a field into the editor so we don't fire a spurious preview.
	bool m_suppress_edits = false;
};

} // namespace phonometrica

#endif // PHONOMETRICA_PROTOCOL_BUILDER_DIALOG_HPP
