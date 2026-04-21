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
 * Created: 27/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QGroupBox>
#include <QButtonGroup>
#include <QScrollArea>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <phon/gui/file_dialog.hpp>
#include <QSplitter>
#include <QProgressDialog>
#include <QToolButton>
#include <phon/gui/conc/formant_query_editor.hpp>
#include <phon/gui/help_browser.hpp>
#include <phon/application/project.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

int FormantQueryEditor::s_query_id = 0;

FormantQueryEditor::FormantQueryEditor(QWidget *parent) :
	FormantQueryEditor(make_handle<FormantQuery>(nullptr, String()), parent)
{
}

FormantQueryEditor::FormantQueryEditor(Handle<FormantQuery> query, QWidget *parent) :
	QDialog(parent), m_query(std::move(query))
{
	setWindowTitle(tr("Measure formants..."));
	setMinimumSize(900, 650);
	setupUi();

	if (!m_query->empty()) {
		loadQuery();
	}

	// Default cursor to the search field, not the query name.
	if (!m_constraints.empty())
		m_constraints[1]->focusSearch();
}

void FormantQueryEditor::setupUi()
{
	auto *main_layout = new QVBoxLayout(this);

	// Name
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

	// Central area: splitter with search+formant on the left, files+properties on the right
	auto *splitter = new QSplitter(Qt::Horizontal);

	// Left: search constraints + formant settings + context
	auto *left_scroll = new QScrollArea;
	left_scroll->setWidgetResizable(true);
	auto *left_widget = new QWidget;
	auto *left_layout = new QVBoxLayout(left_widget);
	left_layout->setContentsMargins(0, 0, 0, 0);
	left_layout->addWidget(createSearchPanel());
	left_layout->addWidget(createFormantSettingsPanel());
	left_layout->addWidget(createContextPanel());
	left_layout->addStretch();
	left_scroll->setWidget(left_widget);
	splitter->addWidget(left_scroll);

	// Right: file selector + description filter + properties
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

	// Buttons
	main_layout->addWidget(createButtonPanel());
}

// ── Search panel (identical to QueryEditor) ──────────────────────────────────

QWidget *FormantQueryEditor::createSearchPanel()
{
	auto *group = new QGroupBox(tr("Search constraints"));
	auto *outer = new QVBoxLayout(group);

	m_constraint_layout = new QVBoxLayout;
	m_constraint_layout->setSpacing(2);

	auto *first = new ConstraintWidget(1, group);
	m_constraints.append(first);
	m_constraint_layout->addWidget(first);
	connect(first, &ConstraintWidget::searchRequested, this, &FormantQueryEditor::onExecute);
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

	// Reference constraint (used for context extraction and acoustic measurements)
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

	connect(m_add_btn, &QPushButton::clicked, this, &FormantQueryEditor::onAddConstraint);
	connect(m_remove_btn, &QPushButton::clicked, this, &FormantQueryEditor::onRemoveConstraint);

	return group;
}

// ── Formant settings panel ───────────────────────────────────────────────────

QWidget *FormantQueryEditor::createFormantSettingsPanel()
{
	auto *group = new QGroupBox(tr("Formant analysis"));
	auto *outer = new QVBoxLayout(group);

	// ── Row 1: shared parameters ─────────────────────────────────────────
	auto *shared_row = new QHBoxLayout;

	shared_row->addWidget(new QLabel(tr("Number of formants:")));
	m_nformant_spin = new QSpinBox;
	m_nformant_spin->setRange(2, 7);
	m_nformant_spin->setValue(3);
	shared_row->addWidget(m_nformant_spin);

	shared_row->addSpacing(10);
	shared_row->addWidget(new QLabel(tr("Max bandwidth (Hz):")));
	m_max_bw_edit = new QLineEdit("400");
	m_max_bw_edit->setFixedWidth(60);
	shared_row->addWidget(m_max_bw_edit);

	shared_row->addSpacing(10);
	shared_row->addWidget(new QLabel(tr("Window size (s):")));
	m_win_size_edit = new QLineEdit("0.025");
	m_win_size_edit->setFixedWidth(60);
	shared_row->addWidget(m_win_size_edit);

	shared_row->addStretch();
	outer->addLayout(shared_row);

	// ── Row 2: manual / automatic radio buttons ──────────────────────────
	auto *method_row = new QHBoxLayout;
	m_manual_radio = new QRadioButton(tr("Parametric"));
	m_auto_radio = new QRadioButton(tr("Semi-parametric (Weenink's method)"));
	m_manual_radio->setChecked(true);
	method_row->addWidget(new QLabel(tr("Parameter selection:")));
	method_row->addWidget(m_manual_radio);
	method_row->addWidget(m_auto_radio);
	method_row->addStretch();
	outer->addLayout(method_row);

	// Explicit button group so these two don't interfere with measurement radios
	auto *param_group = new QButtonGroup(this);
	param_group->addButton(m_manual_radio);
	param_group->addButton(m_auto_radio);

	// ── Stacked widget: manual settings / automatic settings ─────────────
	m_method_stack = new QStackedWidget;

	// Page 0: manual
	auto *manual_page = new QWidget;
	auto *manual_layout = new QHBoxLayout(manual_page);
	manual_layout->setContentsMargins(0, 0, 0, 0);
	manual_layout->addWidget(new QLabel(tr("Max frequency (Hz):")));
	m_max_freq_edit = new QLineEdit("5500");
	m_max_freq_edit->setFixedWidth(60);
	manual_layout->addWidget(m_max_freq_edit);
	manual_layout->addSpacing(10);
	manual_layout->addWidget(new QLabel(tr("LPC order:")));
	m_lpc_order_spin = new QSpinBox;
	m_lpc_order_spin->setRange(4, 30);
	m_lpc_order_spin->setValue(10);
	manual_layout->addWidget(m_lpc_order_spin);
	manual_layout->addStretch();
	m_method_stack->addWidget(manual_page);

	// Page 1: automatic (Weenink)
	auto *auto_page = new QWidget;
	auto *auto_layout = new QVBoxLayout(auto_page);
	auto_layout->setContentsMargins(0, 0, 0, 0);

	auto *freq_row = new QHBoxLayout;
	freq_row->addWidget(new QLabel(tr("Max frequency (Hz) from:")));
	m_auto_freq_low_edit = new QLineEdit("4500");
	m_auto_freq_low_edit->setFixedWidth(60);
	freq_row->addWidget(m_auto_freq_low_edit);
	freq_row->addWidget(new QLabel(tr("to:")));
	m_auto_freq_high_edit = new QLineEdit("6500");
	m_auto_freq_high_edit->setFixedWidth(60);
	freq_row->addWidget(m_auto_freq_high_edit);
	freq_row->addWidget(new QLabel(tr("step:")));
	m_auto_freq_step_edit = new QLineEdit("100");
	m_auto_freq_step_edit->setFixedWidth(60);
	freq_row->addWidget(m_auto_freq_step_edit);
	freq_row->addStretch();
	auto_layout->addLayout(freq_row);

	auto *lpc_row = new QHBoxLayout;
	lpc_row->addWidget(new QLabel(tr("LPC order from:")));
	m_auto_lpc_low_spin = new QSpinBox;
	m_auto_lpc_low_spin->setRange(4, 30);
	m_auto_lpc_low_spin->setValue(10);
	lpc_row->addWidget(m_auto_lpc_low_spin);
	lpc_row->addWidget(new QLabel(tr("to:")));
	m_auto_lpc_high_spin = new QSpinBox;
	m_auto_lpc_high_spin->setRange(4, 30);
	m_auto_lpc_high_spin->setValue(10);
	lpc_row->addWidget(m_auto_lpc_high_spin);
	lpc_row->addStretch();
	auto_layout->addLayout(lpc_row);

	m_method_stack->addWidget(auto_page);
	outer->addWidget(m_method_stack);

	connect(m_manual_radio, &QRadioButton::toggled, this, [this](bool on) {
		if (on) m_method_stack->setCurrentIndex(0);
	});
	connect(m_auto_radio, &QRadioButton::toggled, this, [this](bool on) {
		if (on) m_method_stack->setCurrentIndex(1);
	});

	// ── Row: measurement location ────────────────────────────────────────
	auto *loc_row = new QHBoxLayout;
	loc_row->addWidget(new QLabel(tr("Measurement:")));
	m_midpoint_radio = new QRadioButton(tr("Midpoint"));
	m_npoint_radio = new QRadioButton(tr("N-point (%)"));
	m_midpoint_radio->setChecked(true);
	loc_row->addWidget(m_midpoint_radio);
	loc_row->addWidget(m_npoint_radio);
	m_npoint_edit = new QLineEdit;
	m_npoint_edit->setPlaceholderText("25 50 75");
	m_npoint_edit->setEnabled(false);
	m_npoint_edit->setFixedWidth(120);
	loc_row->addWidget(m_npoint_edit);
	loc_row->addStretch();
	outer->addLayout(loc_row);

	// Explicit button group so these don't interfere with parameter selection radios
	auto *location_group = new QButtonGroup(this);
	location_group->addButton(m_midpoint_radio);
	location_group->addButton(m_npoint_radio);

	// ── Row: output format (only enabled when N-point is selected) ───────
	auto *fmt_row = new QHBoxLayout;
	fmt_row->addWidget(new QLabel(tr("Format:")));
	m_wide_radio = new QRadioButton(tr("Wide (one row per match)"));
	m_long_radio = new QRadioButton(tr("Long (one row per time point)"));
	m_wide_radio->setChecked(true);
	m_wide_radio->setEnabled(false);
	m_long_radio->setEnabled(false);
	m_average_check = new QCheckBox(tr("Add average"));
	m_average_check->setEnabled(false);
	fmt_row->addWidget(m_wide_radio);
	fmt_row->addWidget(m_average_check);
	fmt_row->addSpacing(15);
	fmt_row->addWidget(m_long_radio);
	fmt_row->addStretch();
	outer->addLayout(fmt_row);

	// Explicit button group for Wide/Long (separate from Midpoint/N-point)
	auto *format_group = new QButtonGroup(this);
	format_group->addButton(m_wide_radio);
	format_group->addButton(m_long_radio);

	connect(m_midpoint_radio, &QRadioButton::toggled, this, [this](bool on) {
		if (on) {
			m_npoint_edit->clear();
			m_npoint_edit->setEnabled(false);
			m_wide_radio->setEnabled(false);
			m_long_radio->setEnabled(false);
			m_average_check->setEnabled(false);
		}
	});
	connect(m_npoint_radio, &QRadioButton::toggled, this, [this](bool on) {
		if (on) {
			m_npoint_edit->setText("25 50 75");
			m_npoint_edit->setEnabled(true);
			m_wide_radio->setEnabled(true);
			m_long_radio->setEnabled(true);
			m_average_check->setEnabled(m_wide_radio->isChecked());
		}
	});
	connect(m_wide_radio, &QRadioButton::toggled, this, [this](bool on) {
		m_average_check->setEnabled(on && m_npoint_radio->isChecked());
	});

	// ── Row: output options ──────────────────────────────────────────────
	auto *opt_row = new QHBoxLayout;
	m_bw_check = new QCheckBox(tr("Add bandwidth"));
	m_erb_check = new QCheckBox(tr("Add ERB"));
	m_bark_check = new QCheckBox(tr("Add Bark"));
	m_time_check = new QCheckBox(tr("Add measurement time"));
	m_time_check->setToolTip(tr("Include the absolute time (seconds) at which each measurement was taken"));
	opt_row->addWidget(m_bw_check);
	opt_row->addWidget(m_erb_check);
	opt_row->addWidget(m_bark_check);
	opt_row->addWidget(m_time_check);
	opt_row->addStretch();
	outer->addLayout(opt_row);

	return group;
}

// ── Context panel (same as QueryEditor) ──────────────────────────────────────

QWidget *FormantQueryEditor::createContextPanel()
{
	auto *group = new QGroupBox(tr("Context"));
	auto *layout = new QHBoxLayout(group);

	m_ctx_none = new QRadioButton(tr("No context"));
	m_ctx_labels = new QRadioButton(tr("Surrounding labels"));
	m_ctx_kwic = new QRadioButton(tr("Number of characters"));

	// Apply default context from preferences
	try {
		auto ctx = Settings::get_string("concordance", "default_context");
		if (ctx == "none")
			m_ctx_none->setChecked(true);
		else if (ctx == "labels")
			m_ctx_labels->setChecked(true);
		else
			m_ctx_kwic->setChecked(true);
	} catch (...) {
		m_ctx_kwic->setChecked(true);
	}

	m_ctx_length = new QSpinBox;
	m_ctx_length->setRange(1, 1000);
	m_ctx_length->setValue(Settings::get_int("concordance", "context_length"));
	m_ctx_length->setToolTip(tr("Number of characters in left/right context"));

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

	// Set initial enable state
	m_ctx_length->setEnabled(m_ctx_kwic->isChecked());

	return group;
}

// ── File selector (same as QueryEditor) ──────────────────────────────────────

QWidget *FormantQueryEditor::createFileSelector()
{
	auto *group = new QGroupBox(tr("Annotations"));
	auto *layout = new QVBoxLayout(group);

	// Description filter
	auto *desc_row = new QHBoxLayout;
	desc_row->addWidget(new QLabel(tr("Description")));
	m_desc_op_combo = new QComboBox;
	m_desc_op_combo->addItem(tr("is exactly"));
	m_desc_op_combo->addItem(tr("is not"));
	m_desc_op_combo->addItem(tr("contains"));
	m_desc_op_combo->addItem(tr("doesn't contain"));
	m_desc_op_combo->addItem(tr("matches"));
	m_desc_op_combo->addItem(tr("doesn't match"));
	m_desc_op_combo->setCurrentIndex(2);
	m_desc_op_combo->setFixedWidth(120);
	desc_row->addWidget(m_desc_op_combo);
	m_desc_edit = new QLineEdit;
	m_desc_edit->setPlaceholderText(tr("Filter by description..."));
	desc_row->addWidget(m_desc_edit, 1);
	layout->addLayout(desc_row);

	// File list
	m_file_list = new QListWidget;
	m_file_list->setMaximumHeight(180);
	m_file_list->setToolTip(tr("Check specific files, or leave all unchecked to search all files"));

	auto annotations = Project::get()->get_annotations();
	for (auto &annot : annotations)
	{
		auto qlabel = QString::fromUtf8(annot->label().data(), (int) annot->label().size());
		auto *item = new QListWidgetItem(qlabel);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Unchecked);
		item->setData(Qt::UserRole, QString::fromUtf8(annot->path().data(), (int) annot->path().size()));
		item->setToolTip(item->data(Qt::UserRole).toString());
		m_file_list->addItem(item);
	}

	layout->addWidget(m_file_list);

	return group;
}

// ── Metadata panel (same as QueryEditor) ─────────────────────────────────────

QWidget *FormantQueryEditor::createMetadataPanel()
{
	auto *group = new QGroupBox(tr("File properties"));
	auto categories = Property::get_categories();

	if (categories.empty())
	{
		auto *layout = new QVBoxLayout(group);
		auto *label = new QLabel(tr("The current project doesn't have any properties."));
		label->setStyleSheet("color: palette(disabled-text); font-style: italic;");
		layout->addWidget(label);
		return group;
	}

	auto *grid = new QGridLayout(group);
	int col = 0, row = 0;
	const int cols_per_row = 4;

	for (auto &category : categories)
	{
		const std::type_info *type;
		if (Property::is_boolean(category))
			type = &typeid(bool);
		else if (Property::is_numeric(category))
			type = &typeid(double);
		else
			type = &typeid(String);

		auto *pw = new PropertyWidget(category, *type, group);
		m_properties.append(pw);
		grid->addWidget(pw, row, col);
		connect(pw, &PropertyWidget::modified, this, [this]() {
			if (m_save_btn) m_save_btn->setEnabled(true);
			if (m_save_as_btn) m_save_as_btn->setEnabled(true);
		});

		if (++col >= cols_per_row) {
			col = 0;
			++row;
		}
	}

	return group;
}

// ── Buttons ──────────────────────────────────────────────────────────────────

QWidget *FormantQueryEditor::createButtonPanel()
{
	auto *widget = new QWidget;
	auto *layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 4, 0, 0);

	m_save_btn = new QPushButton(tr("Save"));
	m_save_btn->setEnabled(false);
	m_save_as_btn = new QPushButton(tr("Save as..."));
	m_save_as_btn->setEnabled(false);

	auto *cancel_btn = new QPushButton(tr("Cancel"));
	auto *ok_btn = new QPushButton(tr("Search"));
	ok_btn->setDefault(true);

	layout->addWidget(m_save_btn);
	layout->addWidget(m_save_as_btn);
	layout->addStretch();
	layout->addWidget(cancel_btn);
	layout->addWidget(ok_btn);

	connect(ok_btn, &QPushButton::clicked, this, &FormantQueryEditor::onExecute);
	connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_save_btn, &QPushButton::clicked, this, &FormantQueryEditor::onSave);
	connect(m_save_as_btn, &QPushButton::clicked, this, &FormantQueryEditor::onSaveAs);

	return widget;
}

// ── Query parsing ────────────────────────────────────────────────────────────

void FormantQueryEditor::parseQuery()
{
	m_query->clear();

	// Name
	auto name = m_name_edit->text().trimmed();
	if (name.isEmpty()) {
		name = m_name_edit->placeholderText();
	}
	m_query->set_label(String(name.toUtf8().constData()), false);

	// Description metaconstraint
	if (!m_desc_edit->text().trimmed().isEmpty())
	{
		auto op = static_cast<DescMetaConstraint::Operator>(m_desc_op_combo->currentIndex());
		auto value = String(m_desc_edit->text().toUtf8().constData());
		m_query->add_metaconstraint(std::make_shared<DescMetaConstraint>(op, std::move(value)), false);
	}

	// Property metaconstraints
	for (auto *pw : m_properties)
	{
		auto mc = pw->buildMetaConstraint();
		if (mc) {
			m_query->add_metaconstraint(std::move(mc), false);
		}
	}

	// File selection
	Array<Handle<Annotation>> annotations;
	for (int i = 0; i < m_file_list->count(); i++)
	{
		if (m_file_list->item(i)->checkState() == Qt::Checked)
		{
			auto path_str = m_file_list->item(i)->data(Qt::UserRole).toString();
			String path(path_str.toUtf8().constData());
			auto vfile = Project::get()->get(path);
			auto annot = recast<Annotation>(vfile);
			if (annot) {
				annotations.append(std::move(annot));
			}
		}
	}
	m_query->set_selection(std::move(annotations));

	// Reference constraint (always set, independent of context type)
	m_query->set_reference_constraint(m_ref_constraint->value());

	// Duration
	m_query->set_include_duration(m_duration_check->isChecked());
	m_query->set_duration_in_ms(m_duration_ms->isChecked());

	// Context
	if (m_ctx_none->isChecked())
	{
		m_query->set_context(Query::Context::None);
	}
	else if (m_ctx_labels->isChecked())
	{
		m_query->set_context(Query::Context::Labels);
	}
	else if (m_ctx_kwic->isChecked())
	{
		m_query->set_context(Query::Context::KWIC);
		m_query->set_context_length(m_ctx_length->value());
		Settings::set_value("concordance", "context_length", intptr_t(m_ctx_length->value()));
	}

	// Search constraints
	for (auto *cw : m_constraints) {
		m_query->add_constraint(cw->parseConstraint(), false);
	}

	// ── Formant-specific settings ────────────────────────────────────────

	// Shared LPC parameters
	m_query->set_nformant(m_nformant_spin->value());

	bool ok;
	double win_size = m_win_size_edit->text().toDouble(&ok);
	if (!ok || win_size <= 0) {
		throw std::runtime_error("Invalid window size");
	}
	m_query->set_window_size(win_size);

	double max_bw = m_max_bw_edit->text().toDouble(&ok);
	if (!ok || max_bw <= 0) {
		throw std::runtime_error("Invalid maximum bandwidth");
	}
	m_query->set_max_bandwidth(max_bw);

	// Manual / automatic
	bool automatic = m_auto_radio->isChecked();
	m_query->set_automatic(automatic);

	if (automatic)
	{
		double freq_low = m_auto_freq_low_edit->text().toDouble(&ok);
		if (!ok || freq_low <= 0) throw std::runtime_error("Invalid minimum frequency");
		double freq_high = m_auto_freq_high_edit->text().toDouble(&ok);
		if (!ok || freq_high <= freq_low) throw std::runtime_error("Invalid maximum frequency");
		double freq_step = m_auto_freq_step_edit->text().toDouble(&ok);
		if (!ok || freq_step <= 0) throw std::runtime_error("Invalid frequency step");

		m_query->set_max_freq_low(freq_low);
		m_query->set_max_freq_high(freq_high);
		m_query->set_freq_step(freq_step);
		m_query->set_lpc_order_low(m_auto_lpc_low_spin->value());
		m_query->set_lpc_order_high(m_auto_lpc_high_spin->value());
	}
	else
	{
		double max_freq = m_max_freq_edit->text().toDouble(&ok);
		if (!ok || max_freq <= 0) throw std::runtime_error("Invalid maximum frequency");
		m_query->set_max_frequency(max_freq);
		m_query->set_lpc_order(m_lpc_order_spin->value());
	}

	// Measurement location
	if (m_midpoint_radio->isChecked())
	{
		m_query->set_method(FormantQuery::Method::Midpoint);
	}
	else
	{
		m_query->set_method(FormantQuery::Method::NPoint);

		if (m_long_radio->isChecked())
		{
			// Long format: per-point data stored (series), no average columns, long layout
			m_query->set_output_series(true);
			m_query->set_output_average(false);
			m_query->set_initial_layout(Concordance::Layout::Long);
		}
		else
		{
			// Wide format: per-point columns + optional average
			m_query->set_output_series(true);
			m_query->set_output_average(m_average_check->isChecked());
			m_query->set_initial_layout(Concordance::Layout::Wide);
		}

		Array<double> points;
		auto parts = m_npoint_edit->text().split(' ', Qt::SkipEmptyParts);
		for (auto &s : parts)
		{
			double p = s.toDouble(&ok);
			if (!ok || p < 0 || p > 100) {
				throw std::runtime_error("Invalid measurement point (must be between 0 and 100%)");
			}
			points.append(p);
		}
		if (points.empty()) {
			throw std::runtime_error("N-point measurement requires at least one measurement point");
		}
		m_query->set_measurement_points(std::move(points));
	}

	// Output options
	m_query->set_output_bandwidth(m_bw_check->isChecked());
	m_query->set_output_erb(m_erb_check->isChecked());
	m_query->set_output_bark(m_bark_check->isChecked());
	m_query->set_output_time(m_time_check->isChecked());
}

bool FormantQueryEditor::validateQuery()
{
	for (auto *cw : m_constraints)
	{
		auto c = cw->parseConstraint();
		if (c.target.empty())
		{
			QMessageBox::warning(this, tr("Invalid query"),
				tr("Cannot run query with an empty search field."));
			return false;
		}
	}
	return true;
}

void FormantQueryEditor::onExecute()
{
	if (!validateQuery()) return;

	try
	{
		parseQuery();
	}
	catch (std::exception &e)
	{
		QMessageBox::warning(this, tr("Invalid settings"), QString::fromUtf8(e.what()));
		return;
	}

	// Progress dialog
	auto *progress = new QProgressDialog(tr("Measuring formants..."), tr("Cancel"), 0, 100, this);
	progress->setWindowModality(Qt::WindowModal);
	progress->setMinimumDuration(500);

	auto conn = m_query->query_progress.connect([progress](int current, int total) {
		if (total > 0) {
			progress->setMaximum(total);
			progress->setValue(current);
		}
	});

	connect(progress, &QProgressDialog::canceled, this, [this]() {
		m_query->request_cancel();
	});

	try
	{
		m_concordance = m_query->execute();
		conn.disconnect();
		progress->close();

		if (m_query->modified()) {
			Project::updated();
		}

		accept();
	}
	catch (std::exception &e)
	{
		conn.disconnect();
		progress->close();
		QMessageBox::critical(this, tr("Query error"), QString::fromUtf8(e.what()));
	}
}

void FormantQueryEditor::onSave()
{
	if (m_query->path().empty()) {
		onSaveAs();
		return;
	}
	try
	{
		parseQuery();
		m_query->save();
		m_save_btn->setEnabled(false);
	}
	catch (std::exception &e)
	{
		QMessageBox::warning(this, tr("Save error"), QString::fromUtf8(e.what()));
	}
}

void FormantQueryEditor::onSaveAs()
{
	auto path = getSaveFileName(this, tr("Save formant query..."),
		tr("Phonometrica query (*.phon-query)"));
	if (path.isEmpty()) return;

	bool is_new = m_query->path().empty();

	try
	{
		parseQuery();
		m_query->set_path(String(path.toUtf8().constData()), true);
		m_query->save();

		if (is_new) {
			Project::get()->add_query(m_query);
			Project::updated();
		}
		m_save_btn->setEnabled(false);
	}
	catch (std::exception &e)
	{
		QMessageBox::warning(this, tr("Save error"), QString::fromUtf8(e.what()));
	}
}

void FormantQueryEditor::onAddConstraint()
{
	int idx = (int) m_constraints.size() + 1;
	auto *cw = new ConstraintWidget(idx, this);
	cw->setRelationVisible(true);
	m_constraint_layout->addWidget(cw);
	m_constraints.append(cw);
	m_remove_btn->setEnabled(true);
	m_ref_constraint->setRange(1, idx);

	m_constraints[1]->setRelationPlaceholder(true);

	connect(cw, &ConstraintWidget::searchRequested, this, &FormantQueryEditor::onExecute);
	connect(cw, &ConstraintWidget::modified, this, [this]() {
		if (m_save_btn) m_save_btn->setEnabled(true);
		if (m_save_as_btn) m_save_as_btn->setEnabled(true);
	});
}

void FormantQueryEditor::onRemoveConstraint()
{
	if (m_constraints.size() <= 1) return;

	auto *cw = m_constraints.take_last();
	m_constraint_layout->removeWidget(cw);
	delete cw;

	m_remove_btn->setEnabled(m_constraints.size() > 1);
	m_ref_constraint->setRange(1, (int) m_constraints.size());

	if (m_constraints.size() == 1) {
		m_constraints[1]->setRelationPlaceholder(false);
	}
}

// ── Load query into the UI (for re-editing) ──────────────────────────────────

void FormantQueryEditor::loadQuery()
{
	// Name
	auto label = m_query->label();
	if (!label.empty()) {
		m_name_edit->setText(QString::fromUtf8(label.data(), (int) label.size()));
	}

	// Metaconstraints
	for (auto &mc : m_query->metaconstraints())
	{
		if (auto *desc = dynamic_cast<DescMetaConstraint*>(mc.get()))
		{
			m_desc_op_combo->setCurrentIndex(static_cast<int>(desc->op));
			m_desc_edit->setText(QString::fromUtf8(desc->value.data(), (int) desc->value.size()));
		}
		else if (auto *text_mc = dynamic_cast<TextMetaConstraint*>(mc.get()))
		{
			for (auto *pw : m_properties)
			{
				if (pw->category() == text_mc->category && pw->type() == typeid(String)) {
					pw->loadTextValues(text_mc->values);
					break;
				}
			}
		}
		else if (auto *num_mc = dynamic_cast<NumericMetaConstraint*>(mc.get()))
		{
			for (auto *pw : m_properties)
			{
				if (pw->category() == num_mc->category && pw->type() == typeid(double)) {
					pw->loadNumericValue(num_mc->op, num_mc->value);
					break;
				}
			}
		}
		else if (auto *bool_mc = dynamic_cast<BooleanMetaConstraint*>(mc.get()))
		{
			for (auto *pw : m_properties)
			{
				if (pw->category() == bool_mc->category && pw->type() == typeid(bool)) {
					pw->loadBoolean(bool_mc->value);
					break;
				}
			}
		}
	}

	// File selection
	for (auto &file : m_query->selection())
	{
		for (int i = 0; i < m_file_list->count(); i++)
		{
			auto item_path = m_file_list->item(i)->data(Qt::UserRole).toString();
			if (String(item_path.toUtf8().constData()) == file->path())
			{
				m_file_list->item(i)->setCheckState(Qt::Checked);
				break;
			}
		}
	}

	// Constraints
	intptr_t count = m_query->constraint_count();
	for (intptr_t i = 2; i <= count; i++) {
		onAddConstraint();
	}
	for (intptr_t i = 1; i <= count; i++) {
		m_constraints[i]->loadConstraint(m_query->get_constraint(i));
	}

	// Reference constraint (always restore, independent of context type)
	m_ref_constraint->setValue(m_query->reference_constraint());

	// Duration
	m_duration_check->setChecked(m_query->include_duration());
	if (m_query->duration_in_ms()) m_duration_ms->setChecked(true);
	else m_duration_s->setChecked(true);

	// Context
	switch (m_query->context())
	{
		case Query::Context::Labels:
			m_ctx_labels->setChecked(true);
			break;
		case Query::Context::KWIC:
			m_ctx_kwic->setChecked(true);
			m_ctx_length->setValue(m_query->context_length());
			break;
		default:
			m_ctx_none->setChecked(true);
			break;
	}

	// ── Formant settings ─────────────────────────────────────────────────

	m_nformant_spin->setValue(m_query->nformant());
	m_win_size_edit->setText(QString::number(m_query->window_size()));
	m_max_bw_edit->setText(QString::number(m_query->max_bandwidth()));

	if (m_query->automatic())
	{
		m_auto_radio->setChecked(true);
		m_auto_freq_low_edit->setText(QString::number(m_query->max_freq_low()));
		m_auto_freq_high_edit->setText(QString::number(m_query->max_freq_high()));
		m_auto_freq_step_edit->setText(QString::number(m_query->freq_step()));
		m_auto_lpc_low_spin->setValue(m_query->lpc_order_low());
		m_auto_lpc_high_spin->setValue(m_query->lpc_order_high());
	}
	else
	{
		m_manual_radio->setChecked(true);
		m_max_freq_edit->setText(QString::number(m_query->max_frequency()));
		m_lpc_order_spin->setValue(m_query->lpc_order());
	}

	// Measurement method
	if (m_query->method() == FormantQuery::Method::NPoint)
	{
		m_npoint_radio->setChecked(true);
		QString pts;
		for (intptr_t i = 1; i <= m_query->measurement_points().size(); i++)
		{
			if (i > 1) pts += ' ';
			pts += QString::number(m_query->measurement_points()[i]);
		}
		m_npoint_edit->setText(pts);

		if (m_query->initial_layout() == Concordance::Layout::Long)
		{
			m_long_radio->setChecked(true);
		}
		else
		{
			m_wide_radio->setChecked(true);
			m_average_check->setChecked(m_query->output_average());
		}
	}
	else
	{
		m_midpoint_radio->setChecked(true);
	}

	// Output options
	m_bw_check->setChecked(m_query->output_bandwidth());
	m_erb_check->setChecked(m_query->output_erb());
	m_bark_check->setChecked(m_query->output_bark());
	m_time_check->setChecked(m_query->output_time());

	m_save_btn->setEnabled(false);
	m_save_as_btn->setEnabled(false);
}

} // namespace phonometrica
