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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QHeaderView>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QFileInfo>
#include <QColor>
#include <QPoint>
#include <set>
#include <phon/gui/conc/protocol_builder_dialog.hpp>
#include <phon/gui/main_window.hpp>
#include <phon/gui/file_dialog.hpp>
#include <phon/application/project.hpp>
#include <phon/application/plugin.hpp>
#include <phon/application/protocol.hpp>
#include <phon/application/protocol_apply.hpp>
#include <phon/runtime/runtime.hpp>
#include <phon/runtime/file.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

// ── Utility: JSON string escaping ─────────────────────────────────────────
// Produces a valid JSON string literal from an arbitrary UTF-8 String. We escape the mandatory
// JSON metacharacters plus ASCII control bytes; everything else (including multi-byte UTF-8
// sequences) passes through unchanged because JSON allows UTF-8 in string literals.
static String escape_json_string(const String &s)
{
	String out;
	out.append('"');
	for (auto it = s.begin(); it != s.end(); ++it)
	{
		char c = *it;
		switch (c)
		{
			case '"':  out.append("\\\""); break;
			case '\\': out.append("\\\\"); break;
			case '\b': out.append("\\b");  break;
			case '\f': out.append("\\f");  break;
			case '\n': out.append("\\n");  break;
			case '\r': out.append("\\r");  break;
			case '\t': out.append("\\t");  break;
			default:
				if ((unsigned char) c < 0x20) {
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char) c);
					out.append(buf);
				}
				else {
					out.push_back(c);
				}
				break;
		}
	}
	out.append('"');
	return out;
}

// ── Constructors ──────────────────────────────────────────────────────────

ProtocolBuilderDialog::ProtocolBuilderDialog(Runtime &rt, QWidget *parent) :
	QDialog(parent), m_runtime(rt)
{
	setWindowTitle(tr("Build coding protocol"));
	setMinimumSize(900, 600);
	setupUi();

	// Debounce timer for live preview: single-shot; any edit restarts it via QTimer::start().
	m_preview_timer = new QTimer(this);
	m_preview_timer->setSingleShot(true);
	m_preview_timer->setInterval(200);
	connect(m_preview_timer, &QTimer::timeout, this, &ProtocolBuilderDialog::runPreview);

	// Standalone mode: Apply is not meaningful until the user saves and chooses a column to
	// apply to via the column context menu. Keep it visible but disabled for discoverability.
	m_apply_btn->setEnabled(false);
	m_apply_btn->setToolTip(tr("Apply is enabled when the dialog is launched from a column"));
}

ProtocolBuilderDialog::ProtocolBuilderDialog(Runtime &rt, Handle<Concordance> conc, intptr_t source_col,
                                             QWidget *parent) :
	ProtocolBuilderDialog(rt, parent)
{
	m_target_conc = std::move(conc);
	m_target_col = source_col;
	m_apply_btn->setEnabled(true);
	m_apply_btn->setToolTip(QString());
}

ProtocolBuilderDialog::~ProtocolBuilderDialog() = default;

// ── Layout ────────────────────────────────────────────────────────────────

void ProtocolBuilderDialog::setupUi()
{
	auto *outer = new QVBoxLayout(this);

	// ── Top strip: protocol-level fields ────────────────────────────────
	auto *top_row = new QHBoxLayout;
	top_row->addWidget(new QLabel(tr("Name:")));
	m_name_edit = new QLineEdit;
	top_row->addWidget(m_name_edit, 2);

	top_row->addSpacing(12);
	top_row->addWidget(new QLabel(tr("Separator:")));
	m_separator_edit = new QLineEdit;
	m_separator_edit->setMaximumWidth(80);
	m_separator_edit->setToolTip(tr("Character(s) separating fields in a coded value. "
	                                 "Leave empty if fields are concatenated directly."));
	top_row->addWidget(m_separator_edit);

	top_row->addSpacing(12);
	m_case_sensitive = new QCheckBox(tr("Case-sensitive"));
	top_row->addWidget(m_case_sensitive);
	top_row->addStretch(1);
	outer->addLayout(top_row);

	// ── Main three-pane splitter ────────────────────────────────────────
	auto *splitter = new QSplitter(Qt::Horizontal);

	// Left: field list + buttons
	auto *left = new QWidget;
	auto *left_layout = new QVBoxLayout(left);
	left_layout->setContentsMargins(0, 0, 0, 0);
	left_layout->addWidget(new QLabel(tr("Fields:")));
	m_field_list = new QListWidget;
	m_field_list->setSelectionMode(QAbstractItemView::SingleSelection);
	left_layout->addWidget(m_field_list, 1);

	auto *field_buttons = new QHBoxLayout;
	m_field_add_btn = new QPushButton(tr("+"));
	m_field_add_btn->setToolTip(tr("Add a new field"));
	m_field_add_btn->setMaximumWidth(32);
	m_field_remove_btn = new QPushButton(tr("−"));
	m_field_remove_btn->setToolTip(tr("Remove the selected field"));
	m_field_remove_btn->setMaximumWidth(32);
	m_field_up_btn = new QPushButton(tr("↑"));
	m_field_up_btn->setToolTip(tr("Move the selected field up"));
	m_field_up_btn->setMaximumWidth(32);
	m_field_down_btn = new QPushButton(tr("↓"));
	m_field_down_btn->setToolTip(tr("Move the selected field down"));
	m_field_down_btn->setMaximumWidth(32);
	field_buttons->addWidget(m_field_add_btn);
	field_buttons->addWidget(m_field_remove_btn);
	field_buttons->addWidget(m_field_up_btn);
	field_buttons->addWidget(m_field_down_btn);
	field_buttons->addStretch(1);
	left_layout->addLayout(field_buttons);
	splitter->addWidget(left);

	// Middle: selected-field editor
	auto *middle = new QWidget;
	auto *middle_layout = new QVBoxLayout(middle);
	middle_layout->setContentsMargins(0, 0, 0, 0);

	auto *field_form = new QFormLayout;
	m_field_name_edit = new QLineEdit;
	field_form->addRow(tr("Field name:"), m_field_name_edit);
	m_field_match_all_edit = new QLineEdit;
	m_field_match_all_edit->setToolTip(tr("Regular expression that matches every possible value of this field "
	                                       "(e.g. '[0-2]' for a digit in 0..2). Do not use capturing groups: "
	                                       "write '(?:a|b)' rather than '(a|b)'."));
	field_form->addRow(tr("Matches all:"), m_field_match_all_edit);
	middle_layout->addLayout(field_form);

	middle_layout->addWidget(new QLabel(tr("Values:")));
	m_value_table = new QTableWidget(0, 2);
	m_value_table->setHorizontalHeaderLabels({tr("Match"), tr("Label")});
	m_value_table->horizontalHeader()->setStretchLastSection(true);
	m_value_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_value_table->verticalHeader()->hide();
	m_value_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	middle_layout->addWidget(m_value_table, 1);

	auto *value_buttons = new QHBoxLayout;
	m_value_add_btn = new QPushButton(tr("Add value"));
	m_value_remove_btn = new QPushButton(tr("Remove value"));
	value_buttons->addWidget(m_value_add_btn);
	value_buttons->addWidget(m_value_remove_btn);
	value_buttons->addStretch(1);
	middle_layout->addLayout(value_buttons);
	splitter->addWidget(middle);

	// Right: sample + preview
	auto *right = new QWidget;
	auto *right_layout = new QVBoxLayout(right);
	right_layout->setContentsMargins(0, 0, 0, 0);
	right_layout->addWidget(new QLabel(tr("Sample input (one value per line):")));
	m_sample_edit = new QPlainTextEdit;
	m_sample_edit->setPlaceholderText(tr("Type or paste sample codings here, one per line."));
	right_layout->addWidget(m_sample_edit, 1);

	right_layout->addWidget(new QLabel(tr("Preview:")));
	m_preview_table = new QTableWidget(0, 0);
	m_preview_table->horizontalHeader()->setStretchLastSection(true);
	m_preview_table->verticalHeader()->hide();
	m_preview_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	right_layout->addWidget(m_preview_table, 2);

	m_preview_status = new QLabel;
	m_preview_status->setWordWrap(true);
	m_preview_status->setStyleSheet(QStringLiteral("color: #aa0000;"));
	right_layout->addWidget(m_preview_status);
	splitter->addWidget(right);

	splitter->setStretchFactor(0, 1);
	splitter->setStretchFactor(1, 2);
	splitter->setStretchFactor(2, 2);
	outer->addWidget(splitter, 1);

	// ── Bottom action bar ────────────────────────────────────────────────
	auto *action_row = new QHBoxLayout;
	m_load_btn = new QPushButton(tr("Load..."));
	m_save_btn = new QPushButton(tr("Save..."));
	m_apply_btn = new QPushButton(tr("Apply..."));
	m_close_btn = new QPushButton(tr("Close"));
	action_row->addWidget(m_load_btn);
	action_row->addWidget(m_save_btn);
	action_row->addStretch(1);
	action_row->addWidget(m_apply_btn);
	action_row->addWidget(m_close_btn);
	outer->addLayout(action_row);

	// ── Wiring ───────────────────────────────────────────────────────────
	connect(m_field_add_btn,    &QPushButton::clicked, this, &ProtocolBuilderDialog::onAddField);
	connect(m_field_remove_btn, &QPushButton::clicked, this, &ProtocolBuilderDialog::onRemoveField);
	connect(m_field_up_btn,     &QPushButton::clicked, this, &ProtocolBuilderDialog::onMoveFieldUp);
	connect(m_field_down_btn,   &QPushButton::clicked, this, &ProtocolBuilderDialog::onMoveFieldDown);
	connect(m_field_list, &QListWidget::currentRowChanged, this,
	        [this](int) { onFieldSelectionChanged(); });

	connect(m_value_add_btn,    &QPushButton::clicked, this, &ProtocolBuilderDialog::onAddValue);
	connect(m_value_remove_btn, &QPushButton::clicked, this, &ProtocolBuilderDialog::onRemoveValue);

	// Any edit to protocol-level, field-level, value, or sample widgets retriggers the preview.
	connect(m_name_edit,             &QLineEdit::textChanged,    this, &ProtocolBuilderDialog::onAnyEditChanged);
	connect(m_separator_edit,        &QLineEdit::textChanged,    this, &ProtocolBuilderDialog::onAnyEditChanged);
	connect(m_case_sensitive,        &QCheckBox::toggled,        this, &ProtocolBuilderDialog::onAnyEditChanged);
	connect(m_field_name_edit,       &QLineEdit::textChanged,    this, &ProtocolBuilderDialog::onAnyEditChanged);
	connect(m_field_match_all_edit,  &QLineEdit::textChanged,    this, &ProtocolBuilderDialog::onAnyEditChanged);
	connect(m_value_table,           &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
		onAnyEditChanged();
	});
	connect(m_sample_edit, &QPlainTextEdit::textChanged, this, &ProtocolBuilderDialog::onAnyEditChanged);

	connect(m_load_btn,  &QPushButton::clicked, this, &ProtocolBuilderDialog::onLoad);
	connect(m_save_btn,  &QPushButton::clicked, this, &ProtocolBuilderDialog::onSave);
	connect(m_apply_btn, &QPushButton::clicked, this, &ProtocolBuilderDialog::onApply);
	connect(m_close_btn, &QPushButton::clicked, this, &QDialog::reject);

	// Editor starts with no field selected, so disable the editor widgets until one exists.
	loadFieldIntoEditor(FieldData{});
	m_field_name_edit->setEnabled(false);
	m_field_match_all_edit->setEnabled(false);
	m_value_table->setEnabled(false);
	m_value_add_btn->setEnabled(false);
	m_value_remove_btn->setEnabled(false);
}

// ── Public setters ────────────────────────────────────────────────────────

void ProtocolBuilderDialog::setSampleText(const Array<String> &samples)
{
	QStringList lines;
	lines.reserve((int) samples.size());
	for (intptr_t i = 1; i <= samples.size(); i++) {
		lines << QString::fromUtf8(samples[i].data(), (int) samples[i].size());
	}
	m_sample_edit->setPlainText(lines.join(QStringLiteral("\n")));
}

// ── FieldData accessors ───────────────────────────────────────────────────

FieldData *ProtocolBuilderDialog::fieldDataAt(int row)
{
	if (row < 0 || row >= (int) m_field_data.size()) return nullptr;
	return &m_field_data[(size_t) row];
}

const FieldData *ProtocolBuilderDialog::fieldDataAt(int row) const
{
	if (row < 0 || row >= (int) m_field_data.size()) return nullptr;
	return &m_field_data[(size_t) row];
}

// ── Commit / load selected-field widget state ─────────────────────────────

void ProtocolBuilderDialog::commitSelectedField()
{
	if (m_loaded_row < 0) return;
	auto *fd = fieldDataAt(m_loaded_row);
	if (!fd) return;

	fd->name = String(m_field_name_edit->text().toUtf8().constData());
	fd->match_all = String(m_field_match_all_edit->text().toUtf8().constData());

	fd->values = Array<std::pair<String, String>>();
	const int n = m_value_table->rowCount();
	for (int i = 0; i < n; i++)
	{
		auto *match_item = m_value_table->item(i, 0);
		auto *label_item = m_value_table->item(i, 1);
		String m = match_item ? String(match_item->text().toUtf8().constData()) : String();
		String l = label_item ? String(label_item->text().toUtf8().constData()) : String();
		fd->values.append({m, l});
	}

	// Reflect the (possibly-edited) name back into the list widget row label.
	auto *item = m_field_list->item(m_loaded_row);
	if (item) {
		auto qname = QString::fromUtf8(fd->name.data(), (int) fd->name.size());
		if (qname.isEmpty()) qname = tr("(unnamed field)");
		item->setText(qname);
	}
}

void ProtocolBuilderDialog::loadFieldIntoEditor(const FieldData &field)
{
	// Suppress edit signals while programmatically setting widget content, so we don't fire a
	// spurious preview. The trailing onAnyEditChanged() call retriggers the debounce after all
	// widgets are in their final state.
	m_suppress_edits = true;

	m_field_name_edit->setText(QString::fromUtf8(field.name.data(), (int) field.name.size()));
	m_field_match_all_edit->setText(QString::fromUtf8(field.match_all.data(), (int) field.match_all.size()));

	m_value_table->setRowCount(0);
	for (intptr_t i = 1; i <= field.values.size(); i++)
	{
		const auto &p = field.values[i];
		int r = m_value_table->rowCount();
		m_value_table->insertRow(r);
		m_value_table->setItem(r, 0, new QTableWidgetItem(
			QString::fromUtf8(p.first.data(), (int) p.first.size())));
		m_value_table->setItem(r, 1, new QTableWidgetItem(
			QString::fromUtf8(p.second.data(), (int) p.second.size())));
	}

	m_suppress_edits = false;
}

// ── Field list slots ──────────────────────────────────────────────────────

void ProtocolBuilderDialog::onAddField()
{
	commitSelectedField();

	FieldData fd;
	fd.name = "new field";
	fd.match_all = ".";
	m_field_data.push_back(fd);

	auto *item = new QListWidgetItem(tr("new field"));
	m_field_list->addItem(item);
	m_field_list->setCurrentRow(m_field_list->count() - 1);
	// currentRowChanged fires synchronously and runs onFieldSelectionChanged(), which loads the
	// new row into the editor.
}

void ProtocolBuilderDialog::onRemoveField()
{
	int row = m_field_list->currentRow();
	if (row < 0) return;

	// Drop the in-memory payload first, then the list item. m_loaded_row becomes stale briefly
	// but is reset by the ensuing currentRowChanged signal.
	m_field_data.erase(m_field_data.begin() + row);
	delete m_field_list->takeItem(row);
	m_loaded_row = -1;

	if (m_field_list->count() == 0) {
		loadFieldIntoEditor(FieldData{});
		m_field_name_edit->setEnabled(false);
		m_field_match_all_edit->setEnabled(false);
		m_value_table->setEnabled(false);
		m_value_add_btn->setEnabled(false);
		m_value_remove_btn->setEnabled(false);
	}
	onAnyEditChanged();
}

void ProtocolBuilderDialog::onMoveFieldUp()
{
	int row = m_field_list->currentRow();
	if (row <= 0) return;
	commitSelectedField();

	std::swap(m_field_data[(size_t) row], m_field_data[(size_t) row - 1]);
	auto *item = m_field_list->takeItem(row);
	m_field_list->insertItem(row - 1, item);
	m_field_list->setCurrentRow(row - 1);
}

void ProtocolBuilderDialog::onMoveFieldDown()
{
	int row = m_field_list->currentRow();
	if (row < 0 || row >= m_field_list->count() - 1) return;
	commitSelectedField();

	std::swap(m_field_data[(size_t) row], m_field_data[(size_t) row + 1]);
	auto *item = m_field_list->takeItem(row);
	m_field_list->insertItem(row + 1, item);
	m_field_list->setCurrentRow(row + 1);
}

void ProtocolBuilderDialog::onFieldSelectionChanged()
{
	// Save the outgoing row (if any) before overwriting the editor widgets.
	if (m_loaded_row >= 0 && m_loaded_row < (int) m_field_data.size()) {
		commitSelectedField();
	}

	int row = m_field_list->currentRow();
	if (row < 0) {
		m_loaded_row = -1;
		loadFieldIntoEditor(FieldData{});
		m_field_name_edit->setEnabled(false);
		m_field_match_all_edit->setEnabled(false);
		m_value_table->setEnabled(false);
		m_value_add_btn->setEnabled(false);
		m_value_remove_btn->setEnabled(false);
		return;
	}

	auto *fd = fieldDataAt(row);
	if (!fd) return;

	loadFieldIntoEditor(*fd);
	m_loaded_row = row;
	m_field_name_edit->setEnabled(true);
	m_field_match_all_edit->setEnabled(true);
	m_value_table->setEnabled(true);
	m_value_add_btn->setEnabled(true);
	m_value_remove_btn->setEnabled(true);
}

// ── Value table slots ─────────────────────────────────────────────────────

void ProtocolBuilderDialog::onAddValue()
{
	int r = m_value_table->rowCount();
	m_value_table->insertRow(r);
	m_value_table->setItem(r, 0, new QTableWidgetItem(QString()));
	m_value_table->setItem(r, 1, new QTableWidgetItem(QString()));
	m_value_table->setCurrentCell(r, 0);
	m_value_table->editItem(m_value_table->item(r, 0));
}

void ProtocolBuilderDialog::onRemoveValue()
{
	int r = m_value_table->currentRow();
	if (r < 0) return;
	m_value_table->removeRow(r);
	onAnyEditChanged();
}

// ── Debounced preview trigger ─────────────────────────────────────────────

void ProtocolBuilderDialog::onAnyEditChanged()
{
	if (m_suppress_edits) return;
	m_preview_timer->start();
}

// Placeholder — full implementation lands in the next turn.
void ProtocolBuilderDialog::runPreview()
{
	doPreview();
}

void ProtocolBuilderDialog::doPreview()
{
	// Flush outstanding editor-widget state into m_field_data before reading it. buildJson()
	// is a pure reader; all committing happens here and in onSave().
	commitSelectedField();

	m_preview_status->clear();
	m_preview_status->setStyleSheet(QString());
	m_preview_table->setRowCount(0);
	m_preview_table->setColumnCount(0);

	if (m_field_data.empty()) {
		m_preview_status->setStyleSheet(QStringLiteral("color: #666;"));
		m_preview_status->setText(tr("Add at least one field to see a preview."));
		return;
	}

	// Collect sample lines, skipping blanks.
	auto qlines = m_sample_edit->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
	Array<String> samples;
	for (const auto &line : qlines) {
		if (line.isEmpty()) continue;
		samples.append(String(line.toUtf8().constData()));
	}

	if (samples.empty()) {
		m_preview_status->setStyleSheet(QStringLiteral("color: #666;"));
		m_preview_status->setText(tr("Add sample input to see a preview."));
		return;
	}

	// Serialize the widget state and parse it into a Protocol. A malformed regex in a
	// match_all field propagates out of the Protocol constructor (the regex is compiled lazily
	// inside apply_protocol(), but JSON-level errors surface here).
	String json = buildJson();
	std::shared_ptr<Protocol> protocol;
	try {
		protocol = std::make_shared<Protocol>(m_runtime, json, Protocol::FromString{});
	}
	catch (std::exception &e) {
		m_preview_status->setStyleSheet(QStringLiteral("color: #aa0000;"));
		m_preview_status->setText(tr("Protocol error: %1").arg(QString::fromUtf8(e.what())));
		return;
	}

	// Apply the protocol. Structural errors (e.g. malformed match_all regex compilation)
	// throw; per-row match failures are reported via failed_rows.
	ProtocolApplyResult result;
	try {
		result = apply_protocol(samples, *protocol, /*translate=*/true);
	}
	catch (std::exception &e) {
		m_preview_status->setStyleSheet(QStringLiteral("color: #aa0000;"));
		m_preview_status->setText(tr("Apply error: %1").arg(QString::fromUtf8(e.what())));
		return;
	}

	// Populate the preview table. Columns: Input | field_1 | ... | field_N | Match.
	const int n_fields = (int) result.headers.size();
	const int n_rows = (int) samples.size();
	m_preview_table->setColumnCount(n_fields + 2);

	QStringList headers;
	headers << tr("Input");
	for (intptr_t j = 1; j <= result.headers.size(); j++) {
		const auto &h = result.headers[j];
		headers << QString::fromUtf8(h.data(), (int) h.size());
	}
	headers << tr("Match");
	m_preview_table->setHorizontalHeaderLabels(headers);

	// Build a fast-lookup set of failed 1-based row indices, and a set of untranslated rows.
	std::set<intptr_t> failed_set;
	for (intptr_t i = 1; i <= result.failed_rows.size(); i++) {
		failed_set.insert(result.failed_rows[i]);
	}
	std::set<intptr_t> untranslated_set;
	for (intptr_t i = 1; i <= result.untranslated_rows.size(); i++) {
		untranslated_set.insert(result.untranslated_rows[i]);
	}

	m_preview_table->setRowCount(n_rows);
	for (intptr_t i = 1; i <= samples.size(); i++)
	{
		auto input_qs = QString::fromUtf8(samples[i].data(), (int) samples[i].size());
		m_preview_table->setItem((int)(i - 1), 0, new QTableWidgetItem(input_qs));

		for (intptr_t j = 1; j <= result.headers.size(); j++) {
			const auto &cell = result.columns[j][i];
			auto cell_qs = QString::fromUtf8(cell.data(), (int) cell.size());
			m_preview_table->setItem((int)(i - 1), (int) j, new QTableWidgetItem(cell_qs));
		}

		// Three-state indicator: ✗ for outright regex failure, ⚠ for raw-value fall-through,
		// ✓ for fully translated. Failures dominate fall-throughs (a failed row produces no
		// captures, so it can't also be untranslated).
		const bool failed = failed_set.count(i) > 0;
		const bool untranslated = !failed && (untranslated_set.count(i) > 0);
		QString glyph;
		QColor colour;
		if (failed)             { glyph = QString::fromUtf8("✗"); colour = QColor(0xaa, 0x00, 0x00); }
		else if (untranslated)  { glyph = QString::fromUtf8("⚠"); colour = QColor(0xaa, 0x66, 0x00); }
		else                    { glyph = QString::fromUtf8("✓"); colour = QColor(0x00, 0x80, 0x00); }
		auto *mark = new QTableWidgetItem(glyph);
		mark->setForeground(colour);
		m_preview_table->setItem((int)(i - 1), n_fields + 1, mark);
	}
	m_preview_table->resizeColumnsToContents();

	// Compose status text. Two independent conditions; when both present we show both lines.
	QStringList status_lines;
	if (!result.failed_rows.empty()) {
		status_lines << tr("%1 row(s) did not match the protocol.")
			.arg((qlonglong) result.failed_rows.size());
	}
	if (!result.untranslated_rows.empty()) {
		status_lines << tr("%1 row(s) contain values not defined in the protocol (raw kept).")
			.arg((qlonglong) result.untranslated_rows.size());
	}
	if (!status_lines.isEmpty()) {
		m_preview_status->setStyleSheet(QStringLiteral("color: #aa6600;"));
		m_preview_status->setText(status_lines.join(QStringLiteral("\n")));
	}
}

// ── Load ──────────────────────────────────────────────────────────────────

void ProtocolBuilderDialog::onLoad()
{
	showLoadMenu();
}

Array<std::pair<String, String>> ProtocolBuilderDialog::enumeratePluginProtocols() const
{
	Array<std::pair<String, String>> result;
	auto *mw = MainWindow::instance();
	if (!mw) return result;

	const auto &plugins = mw->plugins();
	for (intptr_t i = 1; i <= plugins.size(); i++)
	{
		auto &plugin = plugins[i];
		auto dir = plugin->get_protocol_directory();
		if (!filesystem::exists(dir)) continue;

		for (auto &name : filesystem::list_directory(dir))
		{
			auto qname = QString::fromUtf8(name.data(), (int) name.size());
			if (!qname.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) continue;
			auto path = filesystem::join(dir, name);
			result.append({plugin->label(), path});
		}
	}
	return result;
}

void ProtocolBuilderDialog::showLoadMenu()
{
	QMenu menu(this);
	auto plugins = enumeratePluginProtocols();

	if (!plugins.empty())
	{
		for (intptr_t i = 1; i <= plugins.size(); i++)
		{
			const auto &pair = plugins[i];
			auto qpath = QString::fromUtf8(pair.second.data(), (int) pair.second.size());
			QFileInfo fi(qpath);
			auto qlabel = QString::fromUtf8(pair.first.data(), (int) pair.first.size());
			auto display = tr("%1 — %2").arg(qlabel, fi.baseName());

			auto *action = menu.addAction(display);
			String captured_path = pair.second;  // capture by value into the lambda
			connect(action, &QAction::triggered, this, [this, captured_path]() {
				loadFromPath(captured_path);
			});
		}
		menu.addSeparator();
	}

	auto *browse = menu.addAction(tr("Browse file system..."));
	connect(browse, &QAction::triggered, this, [this]() {
		auto qpath = getOpenFileName(this, tr("Load coding protocol"),
			tr("Coding protocols (*.json);;All files (*)"));
		if (qpath.isEmpty()) return;
		loadFromPath(String(qpath.toUtf8().constData()));
	});

	// Anchor the menu below the Load button.
	menu.exec(m_load_btn->mapToGlobal(QPoint(0, m_load_btn->height())));
}

void ProtocolBuilderDialog::loadFromPath(const String &path)
{
	std::shared_ptr<Protocol> protocol;
	try {
		protocol = std::make_shared<Protocol>(m_runtime, path);
	}
	catch (std::exception &e) {
		QMessageBox::critical(this, tr("Load coding protocol"),
			tr("Could not load protocol:\n%1").arg(QString::fromUtf8(e.what())));
		return;
	}

	// Suppress edit signals during the bulk update so each setText() doesn't spam the preview
	// timer. A single manual doPreview() fires at the end.
	m_suppress_edits = true;

	auto name = protocol->name();
	m_name_edit->setText(QString::fromUtf8(name.data(), (int) name.size()));
	auto sep = protocol->separator();
	m_separator_edit->setText(QString::fromUtf8(sep.data(), (int) sep.size()));
	m_case_sensitive->setChecked(protocol->case_sensitive());

	m_field_list->clear();
	m_field_data.clear();
	m_loaded_row = -1;

	const auto &fields = protocol->fields();
	for (intptr_t i = 1; i <= fields.size(); i++)
	{
		const auto &sf = fields[i];
		FieldData fd;
		fd.name = sf.name;
		fd.match_all = sf.match_all;
		for (intptr_t j = 1; j <= sf.values.size(); j++) {
			const auto &sv = sf.values[j];
			fd.values.append({sv.match, sv.text});
		}
		m_field_data.push_back(std::move(fd));
		auto qname = QString::fromUtf8(sf.name.data(), (int) sf.name.size());
		if (qname.isEmpty()) qname = tr("(unnamed field)");
		m_field_list->addItem(qname);
	}

	m_suppress_edits = false;

	if (m_field_list->count() > 0) {
		m_field_list->setCurrentRow(0);  // triggers onFieldSelectionChanged -> loads editor
	}

	doPreview();
}

// ── Save ──────────────────────────────────────────────────────────────────

void ProtocolBuilderDialog::onSave()
{
	commitSelectedField();

	if (m_field_data.empty()) {
		QMessageBox::warning(this, tr("Save coding protocol"),
			tr("Add at least one field before saving."));
		return;
	}
	if (m_name_edit->text().trimmed().isEmpty()) {
		QMessageBox::warning(this, tr("Save coding protocol"),
			tr("Please give the protocol a name before saving."));
		m_name_edit->setFocus();
		return;
	}

	auto qpath = getSaveFileName(this, tr("Save coding protocol"),
		tr("Coding protocols (*.json)"));
	if (qpath.isEmpty()) return;
	if (!qpath.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
		qpath += QStringLiteral(".json");
	}

	String json = buildJson();
	try {
		File out(String(qpath.toUtf8().constData()), File::Write);
		out.write(json);
	}
	catch (std::exception &e) {
		QMessageBox::critical(this, tr("Save coding protocol"),
			tr("Could not save protocol:\n%1").arg(QString::fromUtf8(e.what())));
	}
}

// ── Apply ─────────────────────────────────────────────────────────────────

void ProtocolBuilderDialog::onApply()
{
	if (!m_target_conc) return;  // should not happen: Apply is disabled in standalone mode

	commitSelectedField();

	if (m_field_data.empty()) {
		QMessageBox::warning(this, tr("Apply coding protocol"),
			tr("Add at least one field before applying."));
		return;
	}

	// Build the protocol from current widget state (no file written; apply directly).
	String json = buildJson();
	std::shared_ptr<Protocol> protocol;
	try {
		protocol = std::make_shared<Protocol>(m_runtime, json, Protocol::FromString{});
	}
	catch (std::exception &e) {
		QMessageBox::critical(this, tr("Apply coding protocol"),
			tr("Protocol is invalid:\n%1").arg(QString::fromUtf8(e.what())));
		return;
	}

	ProtocolApplyResult result;
	try {
		result = m_target_conc->apply_protocol(m_target_col, *protocol, /*translate=*/true);
	}
	catch (std::exception &e) {
		QMessageBox::critical(this, tr("Apply coding protocol"),
			tr("Could not apply protocol:\n%1").arg(QString::fromUtf8(e.what())));
		return;
	}

	if (!result.failed_rows.empty() || !result.untranslated_rows.empty()) {
		QStringList msgs;
		if (!result.failed_rows.empty()) {
			msgs << tr("%1 row(s) did not match the protocol and were left blank.")
				.arg((qlonglong) result.failed_rows.size());
		}
		if (!result.untranslated_rows.empty()) {
			msgs << tr("%1 row(s) contain values not defined in the protocol; "
			           "raw values were kept.")
				.arg((qlonglong) result.untranslated_rows.size());
		}
		QMessageBox::warning(this, tr("Apply coding protocol"),
			msgs.join(QStringLiteral("\n\n")));
	}

	// The concordance view is refreshed by the caller that owns the view (it closes the
	// dialog and replays its refresh sequence — see concordance_view integration).
	accept();
}

// ── JSON serialization ────────────────────────────────────────────────────

String ProtocolBuilderDialog::buildJson() const
{
	// Snapshot top-level widget state. Note that the currently-selected field's widget state is
	// *not* committed back into m_field_data here — doPreview() does a const_cast commit before
	// calling this, and onSave() does an explicit commit. buildJson() is a pure reader.
	String name(m_name_edit->text().toUtf8().constData());
	String separator(m_separator_edit->text().toUtf8().constData());
	bool case_sensitive = m_case_sensitive->isChecked();

	String out;
	out.append("{\n");
	out.append("  \"type\": \"coding_protocol\",\n");
	out.append("  \"name\": ");
	out.append(escape_json_string(name));
	out.append(",\n");
	out.append("  \"separator\": ");
	out.append(escape_json_string(separator));
	out.append(",\n");
	if (case_sensitive) {
		out.append("  \"case_sensitive\": true,\n");
	}
	out.append("  \"fields\": [\n");

	for (size_t i = 0; i < m_field_data.size(); i++)
	{
		const auto &fd = m_field_data[i];
		out.append("    {\n");
		out.append("      \"name\": ");
		out.append(escape_json_string(fd.name));
		out.append(",\n");
		out.append("      \"match_all\": ");
		out.append(escape_json_string(fd.match_all));
		out.append(",\n");
		out.append("      \"values\": [\n");
		for (intptr_t j = 1; j <= fd.values.size(); j++)
		{
			const auto &p = fd.values[j];
			out.append("        { \"match\": ");
			out.append(escape_json_string(p.first));
			out.append(", \"text\": ");
			out.append(escape_json_string(p.second));
			out.append(" }");
			if (j < fd.values.size()) out.append(",");
			out.append("\n");
		}
		out.append("      ]\n");
		out.append("    }");
		if (i + 1 < m_field_data.size()) out.append(",");
		out.append("\n");
	}
	out.append("  ]\n");
	out.append("}\n");
	return out;
}

} // namespace phonometrica
