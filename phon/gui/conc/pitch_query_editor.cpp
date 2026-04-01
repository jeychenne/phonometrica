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
 * Created: 29/03/2026                                                                                                 *
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
#include <QFileDialog>
#include <QSplitter>
#include <QProgressDialog>
#include <QToolButton>
#include <phon/gui/conc/pitch_query_editor.hpp>
#include <phon/gui/help_browser.hpp>
#include <phon/application/project.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

// Algorithm-dependent voicing threshold defaults (same as PitchSettingsDialog).
struct ThresholdInfo { double min; double max; double default_value; };
static ThresholdInfo getThresholdInfo(int algo_index)
{
	switch (algo_index) {
		case 0: return {  0.0,  0.2, 0.01 }; // Harvest
		case 1: return { -0.6,  0.7, 0.0  }; // RAPT
		case 3: return {  0.2,  0.5, 0.3  }; // SWIPE
		case 4: return {  0.0,  1.0, 0.45 }; // Praat
		default: return { -0.5,  1.6, 0.9  }; // REAPER
	}
}

int PitchQueryEditor::s_query_id = 0;

PitchQueryEditor::PitchQueryEditor(QWidget *parent) :
	PitchQueryEditor(make_handle<PitchQuery>(nullptr, String()), parent) {}

PitchQueryEditor::PitchQueryEditor(Handle<PitchQuery> query, QWidget *parent) :
	QDialog(parent), m_query(std::move(query))
{
	setWindowTitle(tr("Measure pitch..."));
	setMinimumSize(900, 650);
	setupUi();
	if (!m_query->empty()) loadQuery();
	if (!m_constraints.empty()) m_constraints[1]->focusSearch();
}

void PitchQueryEditor::setupUi()
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
	left_layout->addWidget(createPitchSettingsPanel());
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

QWidget *PitchQueryEditor::createSearchPanel()
{
	auto *group = new QGroupBox(tr("Search constraints"));
	auto *outer = new QVBoxLayout(group);
	m_constraint_layout = new QVBoxLayout;
	m_constraint_layout->setSpacing(2);
	auto *first = new ConstraintWidget(1, group);
	m_constraints.append(first);
	m_constraint_layout->addWidget(first);
	connect(first, &ConstraintWidget::searchRequested, this, &PitchQueryEditor::onExecute);
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

	connect(m_add_btn, &QPushButton::clicked, this, &PitchQueryEditor::onAddConstraint);
	connect(m_remove_btn, &QPushButton::clicked, this, &PitchQueryEditor::onRemoveConstraint);
	return group;
}

QWidget *PitchQueryEditor::createPitchSettingsPanel()
{
	auto *group = new QGroupBox(tr("Pitch analysis"));
	auto *outer = new QVBoxLayout(group);

	// Row 1: algorithm + pitch range
	auto *row1 = new QHBoxLayout;
	row1->addWidget(new QLabel(tr("Algorithm:")));
	m_algorithm_combo = new QComboBox;
	m_algorithm_combo->addItem("Harvest");
	m_algorithm_combo->addItem("RAPT");
	m_algorithm_combo->addItem("REAPER");
	m_algorithm_combo->addItem("SWIPE");
	m_algorithm_combo->addItem("Praat");
	m_algorithm_combo->setCurrentIndex(2);
	row1->addWidget(m_algorithm_combo);
	row1->addSpacing(10);
	row1->addWidget(new QLabel(tr("Min pitch (Hz):")));
	m_min_pitch_edit = new QLineEdit("75");
	m_min_pitch_edit->setFixedWidth(60);
	row1->addWidget(m_min_pitch_edit);
	row1->addSpacing(10);
	row1->addWidget(new QLabel(tr("Max pitch (Hz):")));
	m_max_pitch_edit = new QLineEdit("600");
	m_max_pitch_edit->setFixedWidth(60);
	row1->addWidget(m_max_pitch_edit);
	row1->addStretch();
	outer->addLayout(row1);

	// Row 2: voicing threshold + time step
	auto *row2 = new QHBoxLayout;
	row2->addWidget(new QLabel(tr("Voicing threshold:")));
	m_threshold_edit = new QLineEdit;
	m_threshold_edit->setFixedWidth(60);
	row2->addWidget(m_threshold_edit);
	row2->addSpacing(10);
	row2->addWidget(new QLabel(tr("Time step (s):")));
	m_time_step_edit = new QLineEdit("0.01");
	m_time_step_edit->setFixedWidth(60);
	row2->addWidget(m_time_step_edit);
	row2->addStretch();
	outer->addLayout(row2);

	// Set initial threshold from REAPER default
	m_threshold_edit->setText(QString::number(getThresholdInfo(2).default_value, 'g'));

	// Praat-specific parameters (hidden unless Praat is selected)
	auto *praat_row = new QHBoxLayout;
	m_silence_label = new QLabel(tr("Silence threshold:"));
	praat_row->addWidget(m_silence_label);
	m_silence_edit = new QLineEdit("0.03");
	m_silence_edit->setFixedWidth(50);
	praat_row->addWidget(m_silence_edit);
	praat_row->addSpacing(8);
	m_octave_cost_label = new QLabel(tr("Octave cost:"));
	praat_row->addWidget(m_octave_cost_label);
	m_octave_cost_edit = new QLineEdit("0.01");
	m_octave_cost_edit->setFixedWidth(50);
	praat_row->addWidget(m_octave_cost_edit);
	praat_row->addSpacing(8);
	m_octave_jump_label = new QLabel(tr("Octave-jump cost:"));
	praat_row->addWidget(m_octave_jump_label);
	m_octave_jump_edit = new QLineEdit("0.35");
	m_octave_jump_edit->setFixedWidth(50);
	praat_row->addWidget(m_octave_jump_edit);
	praat_row->addSpacing(8);
	m_voicing_cost_label = new QLabel(tr("Voiced/unvoiced cost:"));
	praat_row->addWidget(m_voicing_cost_label);
	m_voicing_cost_edit = new QLineEdit("0.45");
	m_voicing_cost_edit->setFixedWidth(50);
	praat_row->addWidget(m_voicing_cost_edit);
	praat_row->addStretch();
	outer->addLayout(praat_row);

	// Initially hide Praat fields
	auto setPraatVisible = [this](bool v) {
		for (auto *w : {(QWidget*)m_silence_label, (QWidget*)m_silence_edit,
		               (QWidget*)m_octave_cost_label, (QWidget*)m_octave_cost_edit,
		               (QWidget*)m_octave_jump_label, (QWidget*)m_octave_jump_edit,
		               (QWidget*)m_voicing_cost_label, (QWidget*)m_voicing_cost_edit})
			w->setVisible(v);
	};
	setPraatVisible(false);

	connect(m_algorithm_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, [this, setPraatVisible](int index) {
		m_threshold_edit->setText(QString::number(getThresholdInfo(index).default_value, 'g'));
		setPraatVisible(index == 4);
	});

	// Measurement location
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
	auto *location_group = new QButtonGroup(this);
	location_group->addButton(m_midpoint_radio);
	location_group->addButton(m_npoint_radio);

	// Output format
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
	auto *format_group = new QButtonGroup(this);
	format_group->addButton(m_wide_radio);
	format_group->addButton(m_long_radio);

	connect(m_midpoint_radio, &QRadioButton::toggled, this, [this](bool on) {
		if (on) { m_npoint_edit->clear(); m_npoint_edit->setEnabled(false);
			m_wide_radio->setEnabled(false); m_long_radio->setEnabled(false); m_average_check->setEnabled(false); }
	});
	connect(m_npoint_radio, &QRadioButton::toggled, this, [this](bool on) {
		if (on) { m_npoint_edit->setText("25 50 75"); m_npoint_edit->setEnabled(true);
			m_wide_radio->setEnabled(true); m_long_radio->setEnabled(true);
			m_average_check->setEnabled(m_wide_radio->isChecked()); }
	});
	connect(m_wide_radio, &QRadioButton::toggled, this, [this](bool on) {
		m_average_check->setEnabled(on && m_npoint_radio->isChecked());
	});

	// Output options
	auto *opt_row = new QHBoxLayout;
	m_semitones_check = new QCheckBox(tr("Add semitones (ref:"));
	m_semitone_ref_edit = new QLineEdit("100");
	m_semitone_ref_edit->setFixedWidth(50);
	m_semitone_ref_edit->setEnabled(false);
	m_erb_check = new QCheckBox(tr("Add ERB"));
	opt_row->addWidget(m_semitones_check);
	opt_row->addWidget(m_semitone_ref_edit);
	opt_row->addWidget(new QLabel(tr("Hz)")));
	opt_row->addSpacing(10);
	opt_row->addWidget(m_erb_check);
	opt_row->addStretch();
	outer->addLayout(opt_row);
	connect(m_semitones_check, &QCheckBox::toggled, m_semitone_ref_edit, &QLineEdit::setEnabled);

	return group;
}

QWidget *PitchQueryEditor::createContextPanel()
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

QWidget *PitchQueryEditor::createFileSelector()
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

QWidget *PitchQueryEditor::createMetadataPanel()
{
	auto *group = new QGroupBox(tr("File properties"));
	auto categories = Property::get_categories();
	if (categories.empty()) {
		auto *layout = new QVBoxLayout(group);
		auto *label = new QLabel(tr("The current project doesn't have any properties."));
		label->setStyleSheet("color: gray; font-style: italic;");
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

QWidget *PitchQueryEditor::createButtonPanel()
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
	connect(ok_btn, &QPushButton::clicked, this, &PitchQueryEditor::onExecute);
	connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_save_btn, &QPushButton::clicked, this, &PitchQueryEditor::onSave);
	connect(m_save_as_btn, &QPushButton::clicked, this, &PitchQueryEditor::onSaveAs);
	return widget;
}

void PitchQueryEditor::parseQuery()
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

	// Reference constraint (always set, independent of context type)
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

	// Pitch settings
	static const speech::PitchTracker algos[] = {
		speech::PitchTracker::Harvest, speech::PitchTracker::Rapt, speech::PitchTracker::Reaper,
		speech::PitchTracker::Swipe, speech::PitchTracker::Praat
	};
	int algo_index = m_algorithm_combo->currentIndex();
	m_query->set_algorithm(algos[algo_index]);

	bool ok;
	double min_pitch = m_min_pitch_edit->text().toDouble(&ok);
	if (!ok || min_pitch <= 0) throw std::runtime_error("Invalid minimum pitch");
	m_query->set_min_pitch(min_pitch);
	double max_pitch = m_max_pitch_edit->text().toDouble(&ok);
	if (!ok || max_pitch <= min_pitch) throw std::runtime_error("Invalid maximum pitch (must be greater than minimum)");
	m_query->set_max_pitch(max_pitch);
	double threshold = m_threshold_edit->text().toDouble(&ok);
	if (!ok) throw std::runtime_error("Invalid voicing threshold");
	m_query->set_voicing_threshold(threshold);
	double time_step = m_time_step_edit->text().toDouble(&ok);
	if (!ok || time_step <= 0) throw std::runtime_error("Invalid time step");
	m_query->set_time_step(time_step);

	if (algo_index == 4) {
		double val;
		val = m_silence_edit->text().toDouble(&ok); if (!ok || val < 0) throw std::runtime_error("Invalid silence threshold");
		m_query->set_silence_threshold(val);
		val = m_octave_cost_edit->text().toDouble(&ok); if (!ok || val < 0) throw std::runtime_error("Invalid octave cost");
		m_query->set_octave_cost(val);
		val = m_octave_jump_edit->text().toDouble(&ok); if (!ok || val < 0) throw std::runtime_error("Invalid octave-jump cost");
		m_query->set_octave_jump_cost(val);
		val = m_voicing_cost_edit->text().toDouble(&ok); if (!ok || val < 0) throw std::runtime_error("Invalid voiced/unvoiced cost");
		m_query->set_voicing_cost(val);
	}

	if (m_midpoint_radio->isChecked()) {
		m_query->set_method(PitchQuery::Method::Midpoint);
	} else {
		m_query->set_method(PitchQuery::Method::NPoint);
		if (m_long_radio->isChecked()) {
			m_query->set_output_series(true); m_query->set_output_average(false);
			m_query->set_initial_layout(Concordance::Layout::Long);
		} else {
			m_query->set_output_series(true); m_query->set_output_average(m_average_check->isChecked());
			m_query->set_initial_layout(Concordance::Layout::Wide);
		}
		Array<double> points;
		for (auto &s : m_npoint_edit->text().split(' ', Qt::SkipEmptyParts)) {
			double p = s.toDouble(&ok);
			if (!ok || p < 0 || p > 100) throw std::runtime_error("Invalid measurement point (must be between 0 and 100%)");
			points.append(p);
		}
		if (points.empty()) throw std::runtime_error("N-point measurement requires at least one measurement point");
		m_query->set_measurement_points(std::move(points));
	}

	m_query->set_output_semitones(m_semitones_check->isChecked());
	if (m_semitones_check->isChecked()) {
		double ref = m_semitone_ref_edit->text().toDouble(&ok);
		if (!ok || ref <= 0) throw std::runtime_error("Invalid semitone reference frequency");
		m_query->set_semitone_reference(ref);
	}
	m_query->set_output_erb(m_erb_check->isChecked());
}

bool PitchQueryEditor::validateQuery()
{
	for (auto *cw : m_constraints) {
		if (cw->parseConstraint().target.empty()) {
			QMessageBox::warning(this, tr("Invalid query"), tr("Cannot run query with an empty search field."));
			return false;
		}
	}
	return true;
}

void PitchQueryEditor::onExecute()
{
	if (!validateQuery()) return;
	try { parseQuery(); }
	catch (std::exception &e) { QMessageBox::warning(this, tr("Invalid settings"), QString::fromUtf8(e.what())); return; }

	auto *progress = new QProgressDialog(tr("Measuring pitch..."), tr("Cancel"), 0, 100, this);
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

void PitchQueryEditor::onSave()
{
	if (m_query->path().empty()) { onSaveAs(); return; }
	try { parseQuery(); m_query->save(); m_save_btn->setEnabled(false); }
	catch (std::exception &e) { QMessageBox::warning(this, tr("Save error"), QString::fromUtf8(e.what())); }
}

void PitchQueryEditor::onSaveAs()
{
	auto path = QFileDialog::getSaveFileName(this, tr("Save pitch query..."), QString(), tr("Phonometrica query (*.phon-query)"));
	if (path.isEmpty()) return;
	bool is_new = m_query->path().empty();
	try {
		parseQuery();
		m_query->set_path(String(path.toUtf8().constData()), true);
		m_query->save();
		if (is_new) { Project::get()->add_query(m_query); Project::updated(); }
		m_save_btn->setEnabled(false);
	} catch (std::exception &e) { QMessageBox::warning(this, tr("Save error"), QString::fromUtf8(e.what())); }
}

void PitchQueryEditor::onAddConstraint()
{
	int idx = (int)m_constraints.size() + 1;
	auto *cw = new ConstraintWidget(idx, this);
	cw->setRelationVisible(true);
	m_constraint_layout->addWidget(cw);
	m_constraints.append(cw);
	m_remove_btn->setEnabled(true);
	m_ref_constraint->setRange(1, idx);

	m_constraints[1]->setRelationPlaceholder(true);

	connect(cw, &ConstraintWidget::searchRequested, this, &PitchQueryEditor::onExecute);
	connect(cw, &ConstraintWidget::modified, this, [this]() {
		if (m_save_btn) m_save_btn->setEnabled(true);
		if (m_save_as_btn) m_save_as_btn->setEnabled(true);
	});
}

void PitchQueryEditor::onRemoveConstraint()
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

void PitchQueryEditor::loadQuery()
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

	// Reference constraint (always restore)
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

	// Algorithm — setting the combo triggers the lambda which sets threshold to the default.
	// We then override the threshold with the saved value below.
	int algo_idx = 2;
	switch (m_query->algorithm()) {
		case speech::PitchTracker::Harvest: algo_idx = 0; break;
		case speech::PitchTracker::Rapt:    algo_idx = 1; break;
		case speech::PitchTracker::Reaper:  algo_idx = 2; break;
		case speech::PitchTracker::Swipe:   algo_idx = 3; break;
		case speech::PitchTracker::Praat:   algo_idx = 4; break;
	}
	m_algorithm_combo->setCurrentIndex(algo_idx);

	// Override with saved values
	m_min_pitch_edit->setText(QString::number(m_query->min_pitch()));
	m_max_pitch_edit->setText(QString::number(m_query->max_pitch()));
	m_threshold_edit->setText(QString::number(m_query->voicing_threshold()));
	m_time_step_edit->setText(QString::number(m_query->time_step()));
	m_silence_edit->setText(QString::number(m_query->silence_threshold()));
	m_octave_cost_edit->setText(QString::number(m_query->octave_cost()));
	m_octave_jump_edit->setText(QString::number(m_query->octave_jump_cost()));
	m_voicing_cost_edit->setText(QString::number(m_query->voicing_cost()));

	if (m_query->method() == PitchQuery::Method::NPoint) {
		m_npoint_radio->setChecked(true);
		QString pts;
		for (intptr_t i = 1; i <= m_query->measurement_points().size(); i++) {
			if (i > 1) pts += ' ';
			pts += QString::number(m_query->measurement_points()[i]);
		}
		m_npoint_edit->setText(pts);
		if (m_query->initial_layout() == Concordance::Layout::Long) m_long_radio->setChecked(true);
		else { m_wide_radio->setChecked(true); m_average_check->setChecked(m_query->output_average()); }
	} else {
		m_midpoint_radio->setChecked(true);
	}

	m_semitones_check->setChecked(m_query->output_semitones());
	m_semitone_ref_edit->setText(QString::number(m_query->semitone_reference()));
	m_erb_check->setChecked(m_query->output_erb());
	m_save_btn->setEnabled(false);
	m_save_as_btn->setEnabled(false);
}

} // namespace phonometrica
