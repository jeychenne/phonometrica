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
 * Created: 17/05/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QGroupBox>
#include <QScrollArea>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QSignalBlocker>
#include <phon/gui/file_dialog.hpp>
#include <QSplitter>
#include <QProgressDialog>
#include <QToolButton>
#include <phon/gui/conc/voice_quality_query_editor.hpp>
#include <phon/gui/help_browser.hpp>
#include <phon/application/project.hpp>
#include <phon/application/property.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

int VoiceQualityQueryEditor::s_query_id = 0;

VoiceQualityQueryEditor::VoiceQualityQueryEditor(QWidget *parent) :
	VoiceQualityQueryEditor(make_handle<VoiceQualityQuery>(nullptr, String()), parent) {}

VoiceQualityQueryEditor::VoiceQualityQueryEditor(Handle<VoiceQualityQuery> query, QWidget *parent) :
	QDialog(parent), m_query(std::move(query))
{
	setWindowTitle(tr("Measure voice quality..."));
	setMinimumSize(900, 650);
	setupUi();
	if (!m_query->empty()) loadQuery();
	if (!m_constraints.empty()) m_constraints[1]->focusSearch();
}

void VoiceQualityQueryEditor::setupUi()
{
	auto *main_layout = new QVBoxLayout(this);
	auto *header = new QHBoxLayout;
	header->addWidget(new QLabel(tr("Query name:")));
	m_name_edit = new QLineEdit;
	m_name_edit->setPlaceholderText(tr("Query %1").arg(++s_query_id));
	header->addWidget(m_name_edit, 1);
	auto *help_btn = new QToolButton;
	help_btn->setIcon(QIcon(":/icons/circle-help.svg"));
	help_btn->setToolTip(tr("Help"));
	help_btn->setAutoRaise(true);
	connect(help_btn, &QToolButton::clicked, this, [this]() {
		HelpBrowser::showPage(QStringLiteral("concordance"), this);
	});
	header->addWidget(help_btn);
	main_layout->addLayout(header);

	auto *splitter = new QSplitter(Qt::Horizontal);
	auto *left_scroll = new QScrollArea;
	left_scroll->setWidgetResizable(true);
	auto *left_widget = new QWidget;
	auto *left_layout = new QVBoxLayout(left_widget);
	left_layout->setContentsMargins(0, 0, 0, 0);
	left_layout->addWidget(createSearchPanel());
	left_layout->addWidget(createVoiceQualitySettingsPanel());
	left_layout->addWidget(createContextPanel());
	left_layout->addStretch();
	left_scroll->setWidget(left_widget);
	splitter->addWidget(left_scroll);

	auto *right_scroll = new QScrollArea;
	right_scroll->setWidgetResizable(true);
	auto *right_widget = new QWidget;
	auto *right_layout = new QVBoxLayout(right_widget);
	right_layout->setContentsMargins(0, 0, 0, 0);
	right_layout->addWidget(createFileSelector());
	right_layout->addWidget(createMetadataPanel());
	right_layout->addStretch();
	right_scroll->setWidget(right_widget);
	splitter->addWidget(right_scroll);

	splitter->setStretchFactor(0, 3);
	splitter->setStretchFactor(1, 2);
	main_layout->addWidget(splitter, 1);
	main_layout->addWidget(createButtonPanel());
}

QWidget *VoiceQualityQueryEditor::createSearchPanel()
{
	auto *group = new QGroupBox(tr("Search constraints"));
	auto *outer = new QVBoxLayout(group);
	m_constraint_layout = new QVBoxLayout;
	m_constraint_layout->setSpacing(2);
	auto *first = new ConstraintWidget(1, group);
	m_constraints.append(first);
	m_constraint_layout->addWidget(first);
	connect(first, &ConstraintWidget::searchRequested, this, &VoiceQualityQueryEditor::onExecute);
	connect(first, &ConstraintWidget::modified, this, [this]() {
		if (m_save_btn) m_save_btn->setEnabled(true);
		if (m_save_as_btn) m_save_as_btn->setEnabled(true);
	});
	outer->addLayout(m_constraint_layout);
	auto *btn_layout = new QHBoxLayout;
	btn_layout->addStretch();
	m_add_btn = new QPushButton(QIcon(":/icons/circle-plus.svg"), QString());
	m_add_btn->setFixedSize(28, 28);
	m_add_btn->setToolTip(tr("Add constraint"));
	m_remove_btn = new QPushButton(QIcon(":/icons/circle-minus.svg"), QString());
	m_remove_btn->setFixedSize(28, 28);
	m_remove_btn->setToolTip(tr("Remove last constraint"));
	m_remove_btn->setEnabled(false);
	btn_layout->addWidget(m_add_btn);
	btn_layout->addWidget(m_remove_btn);
	outer->addLayout(btn_layout);

	// Reference constraint
	auto *ref_layout = new QHBoxLayout;
	ref_layout->addWidget(new QLabel(tr("Reference constraint:")));
	m_ref_constraint = new QSpinBox;
	m_ref_constraint->setRange(1, 1);
	m_ref_constraint->setToolTip(tr("Constraint used for context extraction and acoustic measurements"));
	ref_layout->addWidget(m_ref_constraint);
	ref_layout->addStretch();

	m_duration_check = new QCheckBox(tr("Add target duration(s)"));
	m_duration_check->setToolTip(tr("Add a column with the duration of each target's event"));
	m_duration_s = new QRadioButton(tr("s"));
	m_duration_ms = new QRadioButton(tr("ms"));
	m_duration_s->setChecked(true);
	m_duration_s->setEnabled(false);
	m_duration_ms->setEnabled(false);
	ref_layout->addWidget(m_duration_check);
	ref_layout->addWidget(m_duration_s);
	ref_layout->addWidget(m_duration_ms);
	connect(m_duration_check, &QCheckBox::toggled, this, [this](bool on) {
		m_duration_s->setEnabled(on);
		m_duration_ms->setEnabled(on);
	});

	outer->addLayout(ref_layout);

	connect(m_add_btn, &QPushButton::clicked, this, &VoiceQualityQueryEditor::onAddConstraint);
	connect(m_remove_btn, &QPushButton::clicked, this, &VoiceQualityQueryEditor::onRemoveConstraint);
	return group;
}

QWidget *VoiceQualityQueryEditor::createVoiceQualitySettingsPanel()
{
	auto *group = new QGroupBox(tr("Voice quality"));
	auto *outer = new QVBoxLayout(group);

	// ── F0 range ─────────────────────────────────────────────────────────
	auto *f0_row = new QHBoxLayout;
	f0_row->addWidget(new QLabel(tr("F0 range (Hz):")));
	m_f0_min_edit = new QLineEdit("75");
	m_f0_min_edit->setFixedWidth(60);
	m_f0_min_edit->setToolTip(tr("Minimum F0 in Hz (REAPER periodicity search lower bound)"));
	f0_row->addWidget(m_f0_min_edit);
	f0_row->addWidget(new QLabel(tr("to")));
	m_f0_max_edit = new QLineEdit("600");
	m_f0_max_edit->setFixedWidth(60);
	m_f0_max_edit->setToolTip(tr("Maximum F0 in Hz (REAPER periodicity search upper bound)"));
	f0_row->addWidget(m_f0_max_edit);
	f0_row->addStretch();
	outer->addLayout(f0_row);

	// Disclosure: per-property F0-range override
	outer->addWidget(buildOverrideSection());

	// ── Feature checkbox grid ────────────────────────────────────────────
	auto *grid_label = new QLabel(tr("Measurements:"));
	outer->addWidget(grid_label);

	auto *grid = new QGridLayout;
	grid->setHorizontalSpacing(20);
	grid->setVerticalSpacing(2);

	// Column 0: pulses + F0
	m_num_pulses_check       = new QCheckBox(tr("Number of pulses"));
	m_voicing_check          = new QCheckBox(tr("Voicing (%)"));
	m_voicing_check->setToolTip(tr(
		"Fraction of voiced frames in the pitch contour (= 1 − Praat's "
		"\"fraction of locally unvoiced frames\"). 100 = fully voiced; "
		"0 = entirely voiceless."));
	m_mean_period_check      = new QCheckBox(tr("Mean period (ms)"));
	m_mean_f0_check          = new QCheckBox(tr("Mean F0 (Hz)"));
	// Column 1: jitter
	m_jitter_local_check     = new QCheckBox(tr("Jitter local (%)"));
	m_jitter_local_abs_check = new QCheckBox(tr("Jitter local abs (\xC2\xB5s)"));
	m_jitter_rap_check       = new QCheckBox(tr("RAP (%)"));
	m_jitter_ppq5_check      = new QCheckBox(tr("PPQ5 (%)"));
	m_jitter_ddp_check       = new QCheckBox(tr("DDP (%)"));
	// Column 2: shimmer + HNR
	m_shimmer_local_check    = new QCheckBox(tr("Shimmer local (%)"));
	m_shimmer_local_db_check = new QCheckBox(tr("Shimmer local (dB)"));
	m_shimmer_apq3_check     = new QCheckBox(tr("APQ3 (%)"));
	m_shimmer_apq5_check     = new QCheckBox(tr("APQ5 (%)"));
	m_shimmer_apq11_check    = new QCheckBox(tr("APQ11 (%)"));
	m_hnr_check              = new QCheckBox(tr("HNR (dB)"));

	grid->addWidget(m_num_pulses_check,       0, 0);
	grid->addWidget(m_voicing_check,          1, 0);
	grid->addWidget(m_mean_period_check,      2, 0);
	grid->addWidget(m_mean_f0_check,          3, 0);

	grid->addWidget(m_jitter_local_check,     0, 1);
	grid->addWidget(m_jitter_local_abs_check, 1, 1);
	grid->addWidget(m_jitter_rap_check,       2, 1);
	grid->addWidget(m_jitter_ppq5_check,      3, 1);
	grid->addWidget(m_jitter_ddp_check,       4, 1);

	grid->addWidget(m_shimmer_local_check,    0, 2);
	grid->addWidget(m_shimmer_local_db_check, 1, 2);
	grid->addWidget(m_shimmer_apq3_check,     2, 2);
	grid->addWidget(m_shimmer_apq5_check,     3, 2);
	grid->addWidget(m_shimmer_apq11_check,    4, 2);
	grid->addWidget(m_hnr_check,              5, 2);

	outer->addLayout(grid);

	// Preset buttons
	auto *preset_row = new QHBoxLayout;
	auto *select_all_btn = new QPushButton(tr("Select all"));
	auto *select_default_btn = new QPushButton(tr("Select essentials"));
	select_default_btn->setToolTip(tr("Pulses, voicing, mean F0, jitter local, shimmer local, HNR"));
	preset_row->addStretch();
	preset_row->addWidget(select_default_btn);
	preset_row->addWidget(select_all_btn);
	outer->addLayout(preset_row);

	connect(select_all_btn, &QPushButton::clicked, this, &VoiceQualityQueryEditor::onSelectAll);
	connect(select_default_btn, &QPushButton::clicked, this, &VoiceQualityQueryEditor::onSelectDefault);

	// Initialise to defaults (= essentials)
	onSelectDefault();

	return group;
}

// ── Per-property F0-range override section ────────────────────────────────
// Mirrors the pitch editor's disclosure-triangle pattern. Expanding the triangle
// only toggles panel visibility — the override is active iff there are pending
// entries with at least one non-zero value. The global f0 min/max edits are
// disabled per-field when fully covered.

QWidget *VoiceQualityQueryEditor::buildOverrideSection()
{
	auto *container = new QWidget;
	auto *vbox = new QVBoxLayout(container);
	vbox->setContentsMargins(0, 0, 0, 0);
	vbox->setSpacing(4);

	auto *trow = new QHBoxLayout;
	trow->setContentsMargins(0, 0, 0, 0);
	m_override_triangle = new QToolButton;
	m_override_triangle->setCheckable(true);
	m_override_triangle->setChecked(false);
	m_override_triangle->setArrowType(Qt::RightArrow);
	m_override_triangle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	m_override_triangle->setText(tr("Adjust F0 range by property"));
	m_override_triangle->setAutoRaise(true);
	m_override_triangle->setToolTip(tr(
		"Override the F0 range on a per-file basis using the value of a "
		"text-typed property (e.g. Gender). When the override is active, the "
		"global F0 min/max fields above are disabled per-field once every "
		"known level has an override. Files whose property value isn't listed "
		"here fall back to the global values, and a warning is printed in the "
		"console panel at execution time."));
	trow->addWidget(m_override_triangle);
	trow->addStretch();
	vbox->addLayout(trow);

	m_override_body = new QWidget;
	m_override_body->hide();
	auto *body = new QVBoxLayout(m_override_body);
	body->setContentsMargins(20, 0, 0, 0);
	body->setSpacing(4);

	auto *cat_row = new QHBoxLayout;
	cat_row->addWidget(new QLabel(tr("Property:")));
	m_override_category_combo = new QComboBox;
	m_override_category_combo->setMinimumWidth(180);
	cat_row->addWidget(m_override_category_combo);
	cat_row->addStretch();
	body->addLayout(cat_row);

	m_override_defaults_lbl = new QLabel;
	m_override_defaults_lbl->setStyleSheet("color: gray;");
	body->addWidget(m_override_defaults_lbl);

	m_override_table = new QTableWidget;
	m_override_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_override_table->setSelectionBehavior(QAbstractItemView::SelectItems);
	m_override_table->verticalHeader()->setVisible(false);
	m_override_table->horizontalHeader()->setStretchLastSection(true);
	m_override_table->setMinimumHeight(120);
	m_override_table->setToolTip(tr(
		"Leave a cell blank to inherit the global setting. Levels not listed here "
		"also inherit the global setting; a warning is printed in the console panel "
		"when the query is executed."));
	body->addWidget(m_override_table);

	m_override_status_lbl = new QLabel;
	m_override_status_lbl->setWordWrap(true);
	body->addWidget(m_override_status_lbl);

	// Show/hide per-match parameter columns on the resulting concordance.
	m_show_params_check = new QCheckBox(tr("Show parameter values in concordance"));
	m_show_params_check->setChecked(true);
	m_show_params_check->setToolTip(tr(
		"When checked, the resulting concordance will display the per-match "
		"effective F0 min and F0 max values as extra columns. You can show or "
		"hide these columns later via the concordance's Display menu."));
	body->addWidget(m_show_params_check);

	vbox->addWidget(m_override_body);

	connect(m_override_triangle, &QToolButton::toggled, this, [this](bool on) {
		setOverrideExpanded(on);
	});
	connect(m_override_category_combo, &QComboBox::currentTextChanged, this, [this](const QString &) {
		if (m_override_triangle && m_override_triangle->isChecked()) {
			refreshOverrideTable();
		}
	});
	connect(m_override_table, &QTableWidget::cellChanged, this, [this](int row, int col) {
		if (m_override_table_updating) return;
		syncOverrideCellToCache(row, col);
	});

	return container;
}

void VoiceQualityQueryEditor::setOverrideExpanded(bool expanded)
{
	if (!m_override_triangle || !m_override_body) return;
	m_override_triangle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
	m_override_body->setVisible(expanded);
	if (expanded) {
		refreshOverrideCategoryCombo();
		refreshOverrideTable();
	}
	applyOverrideEnabledState();
}

void VoiceQualityQueryEditor::applyOverrideEnabledState()
{
	int rows_min_covered = 0;
	int rows_max_covered = 0;
	for (auto &entry : m_pending_overrides) {
		if (entry.second.f0_min > 0) ++rows_min_covered;
		if (entry.second.f0_max > 0) ++rows_max_covered;
	}

	bool min_fully_covered = false;
	bool max_fully_covered = false;
	if (m_override_category_combo)
	{
		QString cat_qs = m_override_category_combo->currentText();
		if (!cat_qs.isEmpty()) {
			String category(cat_qs.toUtf8().constData());
			auto values = Property::get_values(category);
			int n = (int) values.size();
			min_fully_covered = (n > 0 && rows_min_covered == n);
			max_fully_covered = (n > 0 && rows_max_covered == n);
		}
	}

	if (m_f0_min_edit) m_f0_min_edit->setEnabled(!min_fully_covered);
	if (m_f0_max_edit) m_f0_max_edit->setEnabled(!max_fully_covered);
}

void VoiceQualityQueryEditor::refreshOverrideCategoryCombo()
{
	if (!m_override_category_combo) return;
	QSignalBlocker blocker(m_override_category_combo);

	QString prev = m_override_category_combo->currentText();
	m_override_category_combo->clear();
	auto text_cats = Property::get_categories_by_type(typeid(String));
	int gender_index = -1;
	int i = 0;
	for (const auto &cat : text_cats) {
		auto qs = QString::fromUtf8(cat.data(), (int) cat.size());
		m_override_category_combo->addItem(qs);
		if (gender_index < 0 && qs.compare(QStringLiteral("Gender"), Qt::CaseInsensitive) == 0) {
			gender_index = i;
		}
		++i;
	}
	int idx = prev.isEmpty() ? -1 : m_override_category_combo->findText(prev, Qt::MatchExactly);
	if (idx >= 0) m_override_category_combo->setCurrentIndex(idx);
	else if (gender_index >= 0) m_override_category_combo->setCurrentIndex(gender_index);
}

void VoiceQualityQueryEditor::refreshOverrideTable()
{
	if (!m_override_table) return;
	m_override_table_updating = true;

	QString hint = tr("Defaults: min %1 Hz, max %2 Hz (leave a cell blank to inherit)")
		.arg(m_f0_min_edit ? m_f0_min_edit->text() : QStringLiteral("?"))
		.arg(m_f0_max_edit ? m_f0_max_edit->text() : QStringLiteral("?"));
	m_override_defaults_lbl->setText(hint);

	m_override_table->setColumnCount(3);
	m_override_table->setHorizontalHeaderLabels(QStringList{
		tr("Value"), tr("F0 min (Hz)"), tr("F0 max (Hz)")
	});

	QString cat_qs = m_override_category_combo ? m_override_category_combo->currentText() : QString();
	if (cat_qs.isEmpty()) {
		m_override_table->setRowCount(0);
		m_override_table_updating = false;
		updateOverrideStatus();
		applyOverrideEnabledState();
		return;
	}

	String category(cat_qs.toUtf8().constData());
	auto values = Property::get_values(category);

	m_override_table->setRowCount((int) values.size());
	auto format_cell = [](double v) -> QString {
		if (v > 0) return QString::number(v, 'f', 1);
		return QString();
	};
	int row = 0;
	for (const auto &v : values)
	{
		auto *value_item = new QTableWidgetItem(QString::fromUtf8(v.data(), (int) v.size()));
		value_item->setFlags(value_item->flags() & ~Qt::ItemIsEditable);
		m_override_table->setItem(row, 0, value_item);

		VoiceQualityQuery::LevelOverride ov;
		auto it = m_pending_overrides.find(v);
		if (it != m_pending_overrides.end()) ov = it->second;

		m_override_table->setItem(row, 1, new QTableWidgetItem(format_cell(ov.f0_min)));
		m_override_table->setItem(row, 2, new QTableWidgetItem(format_cell(ov.f0_max)));
		++row;
	}

	m_override_table->resizeColumnsToContents();
	m_override_table_updating = false;
	updateOverrideStatus();
	applyOverrideEnabledState();
}

void VoiceQualityQueryEditor::syncOverrideCellToCache(int row, int col)
{
	if (row < 0 || col < 0) return;
	auto *value_item = m_override_table->item(row, 0);
	if (!value_item) return;

	String value(value_item->text().toUtf8().constData());
	auto *cell_item = m_override_table->item(row, col);
	if (!cell_item) return;

	QString text = cell_item->text().trimmed();
	bool ok = false;
	double parsed = text.isEmpty() ? 0.0 : text.toDouble(&ok);
	if (!text.isEmpty() && (!ok || parsed <= 0)) {
		m_override_table_updating = true;
		cell_item->setText(QString());
		m_override_table_updating = false;
		parsed = 0.0;
	}

	auto &entry = m_pending_overrides[value];
	if      (col == 1) entry.f0_min = parsed;
	else if (col == 2) entry.f0_max = parsed;

	if (entry.f0_min <= 0 && entry.f0_max <= 0) {
		m_pending_overrides.erase(value);
	}
	updateOverrideStatus();
	applyOverrideEnabledState();
}

void VoiceQualityQueryEditor::updateOverrideStatus()
{
	if (!m_override_status_lbl) return;

	int row_count = m_override_table ? m_override_table->rowCount() : 0;
	if (row_count == 0) {
		m_override_status_lbl->setText(tr(
			"No values available for this property. Add values to the property "
			"via the Properties panel, then return here."));
		m_override_status_lbl->setStyleSheet("color: gray; font-style: italic;");
		return;
	}

	int rows_any = 0;
	int rows_min = 0;
	int rows_max = 0;
	for (int r = 0; r < row_count; r++) {
		auto *vi = m_override_table->item(r, 0);
		if (!vi) continue;
		String value(vi->text().toUtf8().constData());
		auto it = m_pending_overrides.find(value);
		if (it == m_pending_overrides.end()) continue;
		const auto &ov = it->second;
		bool has_min = ov.f0_min > 0;
		bool has_max = ov.f0_max > 0;
		if (has_min || has_max) ++rows_any;
		if (has_min) ++rows_min;
		if (has_max) ++rows_max;
	}

	QString def_text = tr("defaults min %1 Hz, max %2 Hz")
		.arg(m_f0_min_edit ? m_f0_min_edit->text() : QStringLiteral("?"))
		.arg(m_f0_max_edit ? m_f0_max_edit->text() : QStringLiteral("?"));

	if (rows_any == 0) {
		m_override_status_lbl->setText(tr(
			"⚠ No values overridden — all matches will use the %1.").arg(def_text));
		m_override_status_lbl->setStyleSheet("color: rgb(180, 100, 40);");
	}
	else if (rows_min == row_count && rows_max == row_count) {
		m_override_status_lbl->setText(tr(
			"✓ All %n value(s) fully overridden.", nullptr, row_count));
		m_override_status_lbl->setStyleSheet("color: rgb(40, 130, 60);");
	}
	else {
		m_override_status_lbl->setText(tr(
			"Overriding min for %1 of %2 values, max for %3 of %2 values; "
			"uncovered rows will use the %4.")
			.arg(rows_min).arg(row_count).arg(rows_max).arg(def_text));
		m_override_status_lbl->setStyleSheet("color: gray;");
	}
}

QWidget *VoiceQualityQueryEditor::createContextPanel()
{
	auto *group = new QGroupBox(tr("Context"));
	auto *layout = new QHBoxLayout(group);
	m_ctx_none = new QRadioButton(tr("No context"));
	m_ctx_labels = new QRadioButton(tr("Surrounding labels"));
	m_ctx_kwic = new QRadioButton(tr("Number of characters"));
	m_ctx_kwic->setChecked(true);
	m_ctx_length = new QSpinBox;
	m_ctx_length->setRange(1, 1000);
	m_ctx_length->setValue(Settings::get_int("concordance", "context_length"));
	layout->addWidget(m_ctx_none);
	layout->addWidget(m_ctx_labels);
	layout->addWidget(m_ctx_kwic);
	layout->addWidget(m_ctx_length);
	layout->addStretch();
	connect(m_ctx_none, &QRadioButton::toggled, this, [this](bool on) {
		if (on) { m_ctx_length->setEnabled(false); }
	});
	connect(m_ctx_labels, &QRadioButton::toggled, this, [this](bool on) {
		if (on) { m_ctx_length->setEnabled(false); }
	});
	connect(m_ctx_kwic, &QRadioButton::toggled, this, [this](bool on) {
		if (on) { m_ctx_length->setEnabled(true); }
	});
	return group;
}

QWidget *VoiceQualityQueryEditor::createFileSelector()
{
	auto *group = new QGroupBox(tr("Annotations"));
	auto *layout = new QVBoxLayout(group);
	auto *desc_row = new QHBoxLayout;
	desc_row->addWidget(new QLabel(tr("Description")));
	m_desc_op_combo = new QComboBox;
	m_desc_op_combo->addItem(tr("is exactly"));   m_desc_op_combo->addItem(tr("is not"));
	m_desc_op_combo->addItem(tr("contains"));      m_desc_op_combo->addItem(tr("doesn't contain"));
	m_desc_op_combo->addItem(tr("matches"));       m_desc_op_combo->addItem(tr("doesn't match"));
	m_desc_op_combo->setCurrentIndex(2);
	m_desc_op_combo->setFixedWidth(120);
	desc_row->addWidget(m_desc_op_combo);
	m_desc_edit = new QLineEdit;
	m_desc_edit->setPlaceholderText(tr("Filter by description..."));
	desc_row->addWidget(m_desc_edit, 1);
	layout->addLayout(desc_row);
	m_file_list = new QListWidget;
	m_file_list->setMaximumHeight(180);
	m_file_list->setToolTip(tr("Check specific files, or leave all unchecked to search all files"));
	for (auto &annot : Project::get()->get_annotations()) {
		auto qlabel = QString::fromUtf8(annot->label().data(), (int)annot->label().size());
		auto *item = new QListWidgetItem(qlabel);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Unchecked);
		item->setData(Qt::UserRole, QString::fromUtf8(annot->path().data(), (int)annot->path().size()));
		item->setToolTip(item->data(Qt::UserRole).toString());
		m_file_list->addItem(item);
	}
	layout->addWidget(m_file_list);
	return group;
}

QWidget *VoiceQualityQueryEditor::createMetadataPanel()
{
	auto *group = new QGroupBox(tr("File properties"));
	auto categories = Property::get_categories();
	if (categories.empty()) {
		auto *layout = new QVBoxLayout(group);
		auto *label = new QLabel(tr("The current project doesn't have any properties."));
		label->setStyleSheet("color: palette(disabled-text); font-style: italic;");
		layout->addWidget(label);
		return group;
	}
	auto *grid = new QGridLayout(group);
	int col = 0, row = 0;
	for (auto &category : categories) {
		const std::type_info *type;
		if (Property::is_boolean(category)) type = &typeid(bool);
		else if (Property::is_numeric(category)) type = &typeid(double);
		else type = &typeid(String);
		auto *pw = new PropertyWidget(category, *type, group);
		m_properties.append(pw);
		grid->addWidget(pw, row, col);
		connect(pw, &PropertyWidget::modified, this, [this]() {
			if (m_save_btn) m_save_btn->setEnabled(true);
			if (m_save_as_btn) m_save_as_btn->setEnabled(true);
		});
		if (++col >= 4) { col = 0; ++row; }
	}
	return group;
}

QWidget *VoiceQualityQueryEditor::createButtonPanel()
{
	auto *widget = new QWidget;
	auto *layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 4, 0, 0);
	m_save_btn = new QPushButton(tr("Save"));
	m_save_as_btn = new QPushButton(tr("Save as..."));
	auto *cancel_btn = new QPushButton(tr("Cancel"));
	auto *ok_btn = new QPushButton(tr("Search"));
	ok_btn->setDefault(true);
	layout->addWidget(m_save_btn);
	layout->addWidget(m_save_as_btn);
	layout->addStretch();
	layout->addWidget(cancel_btn);
	layout->addWidget(ok_btn);
	connect(ok_btn, &QPushButton::clicked, this, &VoiceQualityQueryEditor::onExecute);
	connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_save_btn, &QPushButton::clicked, this, &VoiceQualityQueryEditor::onSave);
	connect(m_save_as_btn, &QPushButton::clicked, this, &VoiceQualityQueryEditor::onSaveAs);
	return widget;
}

void VoiceQualityQueryEditor::onSelectAll()
{
	m_num_pulses_check->setChecked(true);
	m_voicing_check->setChecked(true);
	m_mean_period_check->setChecked(true);
	m_mean_f0_check->setChecked(true);
	m_jitter_local_check->setChecked(true);
	m_jitter_local_abs_check->setChecked(true);
	m_jitter_rap_check->setChecked(true);
	m_jitter_ppq5_check->setChecked(true);
	m_jitter_ddp_check->setChecked(true);
	m_shimmer_local_check->setChecked(true);
	m_shimmer_local_db_check->setChecked(true);
	m_shimmer_apq3_check->setChecked(true);
	m_shimmer_apq5_check->setChecked(true);
	m_shimmer_apq11_check->setChecked(true);
	m_hnr_check->setChecked(true);
}

void VoiceQualityQueryEditor::onSelectDefault()
{
	// Essentials: pulses, voicing, mean F0, jitter local, shimmer local, HNR.
	m_num_pulses_check->setChecked(true);
	m_voicing_check->setChecked(true);
	m_mean_period_check->setChecked(false);
	m_mean_f0_check->setChecked(true);
	m_jitter_local_check->setChecked(true);
	m_jitter_local_abs_check->setChecked(false);
	m_jitter_rap_check->setChecked(false);
	m_jitter_ppq5_check->setChecked(false);
	m_jitter_ddp_check->setChecked(false);
	m_shimmer_local_check->setChecked(true);
	m_shimmer_local_db_check->setChecked(false);
	m_shimmer_apq3_check->setChecked(false);
	m_shimmer_apq5_check->setChecked(false);
	m_shimmer_apq11_check->setChecked(false);
	m_hnr_check->setChecked(true);
}

void VoiceQualityQueryEditor::parseQuery()
{
	m_query->clear();
	auto name = m_name_edit->text().trimmed();
	if (name.isEmpty()) name = m_name_edit->placeholderText();
	m_query->set_label(String(name.toUtf8().constData()), false);

	if (!m_desc_edit->text().trimmed().isEmpty()) {
		auto op = static_cast<DescMetaConstraint::Operator>(m_desc_op_combo->currentIndex());
		m_query->add_metaconstraint(std::make_shared<DescMetaConstraint>(op, String(m_desc_edit->text().toUtf8().constData())), false);
	}
	for (auto *pw : m_properties) { auto mc = pw->buildMetaConstraint(); if (mc) m_query->add_metaconstraint(std::move(mc), false); }

	Array<Handle<Annotation>> annotations;
	for (int i = 0; i < m_file_list->count(); i++) {
		if (m_file_list->item(i)->checkState() == Qt::Checked) {
			String path(m_file_list->item(i)->data(Qt::UserRole).toString().toUtf8().constData());
			auto annot = recast<Annotation>(Project::get()->get(path));
			if (annot) annotations.append(std::move(annot));
		}
	}
	m_query->set_selection(std::move(annotations));

	m_query->set_reference_constraint(m_ref_constraint->value());

	// Duration
	m_query->set_include_duration(m_duration_check->isChecked());
	m_query->set_duration_in_ms(m_duration_ms->isChecked());

	if (m_ctx_none->isChecked()) { m_query->set_context(Query::Context::None); }
	else if (m_ctx_labels->isChecked()) {
		m_query->set_context(Query::Context::Labels);
	} else if (m_ctx_kwic->isChecked()) {
		m_query->set_context(Query::Context::KWIC);
		m_query->set_context_length(m_ctx_length->value());
		Settings::set_value("concordance", "context_length", intptr_t(m_ctx_length->value()));
	}
	for (auto *cw : m_constraints) m_query->add_constraint(cw->parseConstraint(), false);

	// ── F0 range ────────────────────────────────────────────────────────
	bool ok;
	double f0_min = m_f0_min_edit->text().toDouble(&ok);
	if (!ok || !(f0_min > 0)) throw std::runtime_error("Invalid minimum F0 (must be > 0)");
	double f0_max = m_f0_max_edit->text().toDouble(&ok);
	if (!ok || !(f0_max > f0_min)) throw std::runtime_error("Invalid maximum F0 (must be > minimum F0)");
	m_query->set_f0_min(f0_min);
	m_query->set_f0_max(f0_max);

	// ── Feature selection (at least one must be checked) ───────────────
	m_query->set_output_num_pulses      (m_num_pulses_check->isChecked());
	m_query->set_output_voicing         (m_voicing_check->isChecked());
	m_query->set_output_mean_period     (m_mean_period_check->isChecked());
	m_query->set_output_mean_f0         (m_mean_f0_check->isChecked());
	m_query->set_output_jitter_local    (m_jitter_local_check->isChecked());
	m_query->set_output_jitter_local_abs(m_jitter_local_abs_check->isChecked());
	m_query->set_output_jitter_rap      (m_jitter_rap_check->isChecked());
	m_query->set_output_jitter_ppq5     (m_jitter_ppq5_check->isChecked());
	m_query->set_output_jitter_ddp      (m_jitter_ddp_check->isChecked());
	m_query->set_output_shimmer_local   (m_shimmer_local_check->isChecked());
	m_query->set_output_shimmer_local_db(m_shimmer_local_db_check->isChecked());
	m_query->set_output_shimmer_apq3    (m_shimmer_apq3_check->isChecked());
	m_query->set_output_shimmer_apq5    (m_shimmer_apq5_check->isChecked());
	m_query->set_output_shimmer_apq11   (m_shimmer_apq11_check->isChecked());
	m_query->set_output_hnr             (m_hnr_check->isChecked());

	if (m_query->feature_count() == 0
	    || (!m_num_pulses_check->isChecked()
	        && !m_voicing_check->isChecked()
	        && !m_mean_period_check->isChecked()
	        && !m_mean_f0_check->isChecked()
	        && !m_jitter_local_check->isChecked()
	        && !m_jitter_local_abs_check->isChecked()
	        && !m_jitter_rap_check->isChecked()
	        && !m_jitter_ppq5_check->isChecked()
	        && !m_jitter_ddp_check->isChecked()
	        && !m_shimmer_local_check->isChecked()
	        && !m_shimmer_local_db_check->isChecked()
	        && !m_shimmer_apq3_check->isChecked()
	        && !m_shimmer_apq5_check->isChecked()
	        && !m_shimmer_apq11_check->isChecked()
	        && !m_hnr_check->isChecked()))
	{
		throw std::runtime_error("At least one voice quality measurement must be selected");
	}

	// Per-property F0-range override: active iff at least one pending entry
	// has a non-zero value. The disclosure triangle just controls panel
	// visibility — a collapsed panel with values still applies.
	m_query->clear_override_levels();
	bool has_any_override = false;
	for (auto &entry : m_pending_overrides) {
		const auto &ov = entry.second;
		if (ov.f0_min > 0 || ov.f0_max > 0) {
			has_any_override = true;
			break;
		}
	}
	if (has_any_override && m_override_category_combo)
	{
		String category(m_override_category_combo->currentText().toUtf8().constData());
		m_query->set_override_category(category);
		for (auto &entry : m_pending_overrides) {
			const auto &ov = entry.second;
			if (ov.f0_min > 0 || ov.f0_max > 0) {
				m_query->set_override_level(entry.first, ov);
			}
		}
	}
	else
	{
		m_query->set_override_category(String());
	}
	m_query->set_show_params(m_show_params_check && m_show_params_check->isChecked());
}

bool VoiceQualityQueryEditor::validateQuery()
{
	for (auto *cw : m_constraints) {
		if (cw->parseConstraint().target.empty()) {
			QMessageBox::warning(this, tr("Invalid query"), tr("Cannot run query with an empty search field."));
			return false;
		}
	}
	return true;
}

void VoiceQualityQueryEditor::onExecute()
{
	if (!validateQuery()) return;
	try { parseQuery(); }
	catch (std::exception &e) { QMessageBox::warning(this, tr("Invalid settings"), QString::fromUtf8(e.what())); return; }

	auto *progress = new QProgressDialog(tr("Measuring voice quality..."), tr("Cancel"), 0, 100, this);
	progress->setWindowModality(Qt::WindowModal);
	progress->setMinimumDuration(500);
	auto conn = m_query->query_progress.connect([progress](int current, int total) {
		if (total > 0) { progress->setMaximum(total); progress->setValue(current); }
	});
	connect(progress, &QProgressDialog::canceled, this, [this]() { m_query->request_cancel(); });

	try {
		m_concordance = m_query->execute();
		conn.disconnect(); progress->close();
		if (m_query->modified()) Project::updated();
		accept();
	} catch (std::exception &e) {
		conn.disconnect(); progress->close();
		QMessageBox::critical(this, tr("Query error"), QString::fromUtf8(e.what()));
	}
}

void VoiceQualityQueryEditor::onSave()
{
	if (m_query->path().empty()) { onSaveAs(); return; }
	try { parseQuery(); m_query->save(); }
	catch (std::exception &e) { QMessageBox::warning(this, tr("Save error"), QString::fromUtf8(e.what())); }
}

void VoiceQualityQueryEditor::onSaveAs()
{
	auto name = m_name_edit->text().trimmed();
	if (name.isEmpty()) name = m_name_edit->placeholderText();
	auto suggested = defaultSaveName(String(name.toUtf8().constData()),
	                                 QStringLiteral(".phon-query"));

	auto path = getSaveFileName(this, tr("Save voice quality query..."),
		tr("Phonometrica query (*.phon-query)"), suggested);
	if (path.isEmpty()) return;
	bool is_new = m_query->path().empty();
	try {
		parseQuery();
		m_query->set_path(String(path.toUtf8().constData()), true);
		m_query->save();
		if (is_new) { Project::get()->add_query(m_query); Project::updated(); }
	} catch (std::exception &e) { QMessageBox::warning(this, tr("Save error"), QString::fromUtf8(e.what())); }
}

void VoiceQualityQueryEditor::onAddConstraint()
{
	int idx = (int)m_constraints.size() + 1;
	auto *cw = new ConstraintWidget(idx, this);
	cw->setRelationVisible(true);
	m_constraint_layout->addWidget(cw);
	m_constraints.append(cw);
	m_remove_btn->setEnabled(true);
	m_ref_constraint->setRange(1, idx);

	m_constraints[1]->setRelationPlaceholder(true);

	connect(cw, &ConstraintWidget::searchRequested, this, &VoiceQualityQueryEditor::onExecute);
	connect(cw, &ConstraintWidget::modified, this, [this]() {
		if (m_save_btn) m_save_btn->setEnabled(true);
		if (m_save_as_btn) m_save_as_btn->setEnabled(true);
	});
}

void VoiceQualityQueryEditor::onRemoveConstraint()
{
	if (m_constraints.size() <= 1) return;
	auto *cw = m_constraints.take_last();
	m_constraint_layout->removeWidget(cw);
	delete cw;
	m_remove_btn->setEnabled(m_constraints.size() > 1);
	m_ref_constraint->setRange(1, (int)m_constraints.size());

	if (m_constraints.size() == 1) {
		m_constraints[1]->setRelationPlaceholder(false);
	}
}

void VoiceQualityQueryEditor::loadQuery()
{
	auto label = m_query->label();
	if (!label.empty()) m_name_edit->setText(QString::fromUtf8(label.data(), (int)label.size()));

	for (auto &mc : m_query->metaconstraints()) {
		if (auto *desc = dynamic_cast<DescMetaConstraint*>(mc.get())) {
			m_desc_op_combo->setCurrentIndex(static_cast<int>(desc->op));
			m_desc_edit->setText(QString::fromUtf8(desc->value.data(), (int)desc->value.size()));
		} else if (auto *text_mc = dynamic_cast<TextMetaConstraint*>(mc.get())) {
			for (auto *pw : m_properties) if (pw->category() == text_mc->category && pw->type() == typeid(String)) { pw->loadTextValues(text_mc->values); break; }
		} else if (auto *num_mc = dynamic_cast<NumericMetaConstraint*>(mc.get())) {
			for (auto *pw : m_properties) if (pw->category() == num_mc->category && pw->type() == typeid(double)) { pw->loadNumericValue(num_mc->op, num_mc->value); break; }
		} else if (auto *bool_mc = dynamic_cast<BooleanMetaConstraint*>(mc.get())) {
			for (auto *pw : m_properties) if (pw->category() == bool_mc->category && pw->type() == typeid(bool)) { pw->loadBoolean(bool_mc->value); break; }
		}
	}
	for (auto &file : m_query->selection())
		for (int i = 0; i < m_file_list->count(); i++)
			if (String(m_file_list->item(i)->data(Qt::UserRole).toString().toUtf8().constData()) == file->path())
				{ m_file_list->item(i)->setCheckState(Qt::Checked); break; }

	intptr_t count = m_query->constraint_count();
	for (intptr_t i = 2; i <= count; i++) onAddConstraint();
	for (intptr_t i = 1; i <= count; i++) m_constraints[i]->loadConstraint(m_query->get_constraint(i));

	m_ref_constraint->setValue(m_query->reference_constraint());

	// Duration
	m_duration_check->setChecked(m_query->include_duration());
	if (m_query->duration_in_ms()) m_duration_ms->setChecked(true);
	else m_duration_s->setChecked(true);

	switch (m_query->context()) {
		case Query::Context::Labels: m_ctx_labels->setChecked(true); break;
		case Query::Context::KWIC: m_ctx_kwic->setChecked(true); m_ctx_length->setValue(m_query->context_length()); break;
		default: m_ctx_none->setChecked(true); break;
	}

	// F0 range
	m_f0_min_edit->setText(QString::number(m_query->f0_min(), 'f', 1));
	m_f0_max_edit->setText(QString::number(m_query->f0_max(), 'f', 1));

	// Feature selection
	m_num_pulses_check      ->setChecked(m_query->output_num_pulses());
	m_voicing_check         ->setChecked(m_query->output_voicing());
	m_mean_period_check     ->setChecked(m_query->output_mean_period());
	m_mean_f0_check         ->setChecked(m_query->output_mean_f0());
	m_jitter_local_check    ->setChecked(m_query->output_jitter_local());
	m_jitter_local_abs_check->setChecked(m_query->output_jitter_local_abs());
	m_jitter_rap_check      ->setChecked(m_query->output_jitter_rap());
	m_jitter_ppq5_check     ->setChecked(m_query->output_jitter_ppq5());
	m_jitter_ddp_check      ->setChecked(m_query->output_jitter_ddp());
	m_shimmer_local_check   ->setChecked(m_query->output_shimmer_local());
	m_shimmer_local_db_check->setChecked(m_query->output_shimmer_local_db());
	m_shimmer_apq3_check    ->setChecked(m_query->output_shimmer_apq3());
	m_shimmer_apq5_check    ->setChecked(m_query->output_shimmer_apq5());
	m_shimmer_apq11_check   ->setChecked(m_query->output_shimmer_apq11());
	m_hnr_check             ->setChecked(m_query->output_hnr());

	// Per-property F0-range override
	m_pending_overrides.clear();
	for (auto &entry : m_query->override_levels()) {
		m_pending_overrides[entry.first] = entry.second;
	}
	bool override_on = m_query->override_enabled();
	{
		QSignalBlocker b_triangle(m_override_triangle);
		m_override_triangle->setChecked(override_on);
		m_override_triangle->setArrowType(override_on ? Qt::DownArrow : Qt::RightArrow);
		m_override_body->setVisible(override_on);
	}
	if (override_on)
	{
		refreshOverrideCategoryCombo();
		const String &cat = m_query->override_category();
		QString cat_qs = QString::fromUtf8(cat.data(), (int) cat.size());
		int idx = m_override_category_combo->findText(cat_qs, Qt::MatchExactly);
		if (idx >= 0) {
			QSignalBlocker b_combo(m_override_category_combo);
			m_override_category_combo->setCurrentIndex(idx);
		}
		refreshOverrideTable();
	}
	if (m_show_params_check) {
		QSignalBlocker b(m_show_params_check);
		m_show_params_check->setChecked(m_query->show_params());
	}
	applyOverrideEnabledState();
}

} // namespace phonometrica
