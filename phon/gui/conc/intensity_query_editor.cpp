/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
#include <phon/gui/conc/intensity_query_editor.hpp>
#include <phon/gui/help_browser.hpp>
#include <phon/application/project.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

int IntensityQueryEditor::s_query_id = 0;

IntensityQueryEditor::IntensityQueryEditor(QWidget *parent) :
	IntensityQueryEditor(make_handle<IntensityQuery>(nullptr, String()), parent) {}

IntensityQueryEditor::IntensityQueryEditor(Handle<IntensityQuery> query, QWidget *parent) :
	QDialog(parent), m_query(std::move(query))
{
	setWindowTitle(tr("Measure intensity..."));
	setMinimumSize(900, 600);
	setupUi();
	if (!m_query->empty()) loadQuery();
	if (!m_constraints.empty()) m_constraints[1]->focusSearch();
}

void IntensityQueryEditor::setupUi()
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
	left_layout->addWidget(createIntensitySettingsPanel());
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

QWidget *IntensityQueryEditor::createSearchPanel()
{
	auto *group = new QGroupBox(tr("Search constraints"));
	auto *outer = new QVBoxLayout(group);
	m_constraint_layout = new QVBoxLayout;
	m_constraint_layout->setSpacing(2);
	auto *first = new ConstraintWidget(1, group);
	m_constraints.append(first);
	m_constraint_layout->addWidget(first);
	connect(first, &ConstraintWidget::searchRequested, this, &IntensityQueryEditor::onExecute);
	connect(first, &ConstraintWidget::modified, this, [this]() {
		if (m_save_btn) m_save_btn->setEnabled(true);
		if (m_save_as_btn) m_save_as_btn->setEnabled(true);
	});
	outer->addLayout(m_constraint_layout);
	auto *btn_layout = new QHBoxLayout;
	btn_layout->addStretch();
	m_add_btn = new QPushButton(tr("+"));
	m_add_btn->setFixedWidth(32);
	m_add_btn->setToolTip(tr("Add constraint"));
	m_remove_btn = new QPushButton(tr("\u2212"));
	m_remove_btn->setFixedWidth(32);
	m_remove_btn->setToolTip(tr("Remove last constraint"));
	m_remove_btn->setEnabled(false);
	btn_layout->addWidget(m_add_btn);
	btn_layout->addWidget(m_remove_btn);
	outer->addLayout(btn_layout);
	connect(m_add_btn, &QPushButton::clicked, this, &IntensityQueryEditor::onAddConstraint);
	connect(m_remove_btn, &QPushButton::clicked, this, &IntensityQueryEditor::onRemoveConstraint);
	return group;
}

QWidget *IntensityQueryEditor::createIntensitySettingsPanel()
{
	auto *group = new QGroupBox(tr("Intensity measurement"));
	auto *outer = new QVBoxLayout(group);

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

	return group;
}

QWidget *IntensityQueryEditor::createContextPanel()
{
	auto *group = new QGroupBox(tr("Context"));
	auto *layout = new QHBoxLayout(group);
	m_ref_constraint = new QSpinBox;
	m_ref_constraint->setRange(1, 1);
	m_ctx_none = new QRadioButton(tr("No context"));
	m_ctx_labels = new QRadioButton(tr("Surrounding labels"));
	m_ctx_kwic = new QRadioButton(tr("Number of characters"));
	m_ctx_kwic->setChecked(true);
	m_ctx_length = new QSpinBox;
	m_ctx_length->setRange(1, 1000);
	m_ctx_length->setValue(Settings::get_int("concordance", "context_length"));
	layout->addWidget(new QLabel(tr("Reference constraint:")));
	layout->addWidget(m_ref_constraint);
	layout->addSpacing(10);
	layout->addWidget(m_ctx_none);
	layout->addWidget(m_ctx_labels);
	layout->addWidget(m_ctx_kwic);
	layout->addWidget(m_ctx_length);
	layout->addStretch();
	connect(m_ctx_none, &QRadioButton::toggled, this, [this](bool on) {
		if (on) { m_ctx_length->setEnabled(false); m_ref_constraint->setEnabled(false); }
	});
	connect(m_ctx_labels, &QRadioButton::toggled, this, [this](bool on) {
		if (on) { m_ctx_length->setEnabled(false); m_ref_constraint->setEnabled(true); }
	});
	connect(m_ctx_kwic, &QRadioButton::toggled, this, [this](bool on) {
		if (on) { m_ctx_length->setEnabled(true); m_ref_constraint->setEnabled(true); }
	});
	return group;
}

QWidget *IntensityQueryEditor::createFileSelector()
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

QWidget *IntensityQueryEditor::createMetadataPanel()
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

QWidget *IntensityQueryEditor::createButtonPanel()
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
	connect(ok_btn, &QPushButton::clicked, this, &IntensityQueryEditor::onExecute);
	connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_save_btn, &QPushButton::clicked, this, &IntensityQueryEditor::onSave);
	connect(m_save_as_btn, &QPushButton::clicked, this, &IntensityQueryEditor::onSaveAs);
	return widget;
}

void IntensityQueryEditor::parseQuery()
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

	if (m_ctx_none->isChecked()) { m_query->set_context(Query::Context::None); }
	else if (m_ctx_labels->isChecked()) {
		m_query->set_context(Query::Context::Labels);
		m_query->set_reference_constraint(m_ref_constraint->value());
	} else if (m_ctx_kwic->isChecked()) {
		m_query->set_context(Query::Context::KWIC);
		m_query->set_context_length(m_ctx_length->value());
		m_query->set_reference_constraint(m_ref_constraint->value());
		Settings::set_value("concordance", "context_length", intptr_t(m_ctx_length->value()));
	}
	for (auto *cw : m_constraints) m_query->add_constraint(cw->parseConstraint(), false);

	// Measurement location
	if (m_midpoint_radio->isChecked()) {
		m_query->set_method(IntensityQuery::Method::Midpoint);
	} else {
		m_query->set_method(IntensityQuery::Method::NPoint);
		if (m_long_radio->isChecked()) {
			m_query->set_output_series(true); m_query->set_output_average(false);
			m_query->set_initial_layout(Concordance::Layout::Long);
		} else {
			m_query->set_output_series(true); m_query->set_output_average(m_average_check->isChecked());
			m_query->set_initial_layout(Concordance::Layout::Wide);
		}
		bool ok;
		Array<double> points;
		for (auto &s : m_npoint_edit->text().split(' ', Qt::SkipEmptyParts)) {
			double p = s.toDouble(&ok);
			if (!ok || p < 0 || p > 100) throw std::runtime_error("Invalid measurement point (must be between 0 and 100%)");
			points.append(p);
		}
		if (points.empty()) throw std::runtime_error("N-point measurement requires at least one measurement point");
		m_query->set_measurement_points(std::move(points));
	}
}

bool IntensityQueryEditor::validateQuery()
{
	for (auto *cw : m_constraints) {
		if (cw->parseConstraint().target.empty()) {
			QMessageBox::warning(this, tr("Invalid query"), tr("Cannot run query with an empty search field."));
			return false;
		}
	}
	return true;
}

void IntensityQueryEditor::onExecute()
{
	if (!validateQuery()) return;
	try { parseQuery(); }
	catch (std::exception &e) { QMessageBox::warning(this, tr("Invalid settings"), QString::fromUtf8(e.what())); return; }

	auto *progress = new QProgressDialog(tr("Measuring intensity..."), tr("Cancel"), 0, 100, this);
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

void IntensityQueryEditor::onSave()
{
	if (m_query->path().empty()) { onSaveAs(); return; }
	try { parseQuery(); m_query->save(); m_save_btn->setEnabled(false); }
	catch (std::exception &e) { QMessageBox::warning(this, tr("Save error"), QString::fromUtf8(e.what())); }
}

void IntensityQueryEditor::onSaveAs()
{
	auto path = QFileDialog::getSaveFileName(this, tr("Save intensity query..."), QString(), tr("Phonometrica query (*.phon-query)"));
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

void IntensityQueryEditor::onAddConstraint()
{
	int idx = (int)m_constraints.size() + 1;
	auto *cw = new ConstraintWidget(idx, this);
	cw->setRelationVisible(true);
	m_constraint_layout->addWidget(cw);
	m_constraints.append(cw);
	m_remove_btn->setEnabled(true);
	m_ref_constraint->setRange(1, idx);
	connect(cw, &ConstraintWidget::searchRequested, this, &IntensityQueryEditor::onExecute);
	connect(cw, &ConstraintWidget::modified, this, [this]() {
		if (m_save_btn) m_save_btn->setEnabled(true);
		if (m_save_as_btn) m_save_as_btn->setEnabled(true);
	});
}

void IntensityQueryEditor::onRemoveConstraint()
{
	if (m_constraints.size() <= 1) return;
	auto *cw = m_constraints.take_last();
	m_constraint_layout->removeWidget(cw);
	delete cw;
	m_remove_btn->setEnabled(m_constraints.size() > 1);
	m_ref_constraint->setRange(1, (int)m_constraints.size());
}

void IntensityQueryEditor::loadQuery()
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

	switch (m_query->context()) {
		case Query::Context::Labels: m_ctx_labels->setChecked(true); m_ref_constraint->setValue(m_query->reference_constraint()); break;
		case Query::Context::KWIC: m_ctx_kwic->setChecked(true); m_ref_constraint->setValue(m_query->reference_constraint()); m_ctx_length->setValue(m_query->context_length()); break;
		default: m_ctx_none->setChecked(true); break;
	}

	if (m_query->method() == IntensityQuery::Method::NPoint) {
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

	m_save_btn->setEnabled(false);
	m_save_as_btn->setEnabled(false);
}

} // namespace phonometrica
