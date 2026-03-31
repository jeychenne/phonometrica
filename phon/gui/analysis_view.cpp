/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <numeric>
#include <algorithm>
#include <map>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>
#include <QMenu>
#include <QMessageBox>
#include <QGroupBox>
#include <QFileDialog>
#include <QClipboard>
#include <QApplication>
#include <QFile>
#include <QToolBar>
#include <QAction>
#include <QToolButton>
#include <QHeaderView>
#include <boost/math/distributions/normal.hpp>
#include <phon/gui/analysis_view.hpp>
#include <phon/application/project.hpp>

namespace phonometrica {

AnalysisView::AnalysisView(Handle<Analysis> analysis, QWidget *parent) :
	View(parent),
	m_analysis(std::move(analysis))
{
	m_analysis->open();
	setupUi();
	populateColumns();
	populateModelList();
	updateFitEnabled();
}

QString AnalysisView::label() const
{
	QString base;
	if (m_analysis->has_path())
	{
		base = tabLabel(QString::fromUtf8(m_analysis->label().data(),
		                                   (int)m_analysis->label().size()));
	}
	else if (m_analysis->has_source())
	{
		auto src = QString::fromUtf8(m_analysis->data()->label().data(),
		                              (int)m_analysis->data()->label().size());
		base = QStringLiteral("Analysis — ") + tabLabel(src);
	}
	else
	{
		base = QStringLiteral("Analysis");
	}

	if (isModified()) {
		base += QStringLiteral(" *");
	}
	return base;
}

String AnalysisView::path() const
{
	return m_analysis->path();
}

bool AnalysisView::isModified() const
{
	return m_analysis->modified();
}

bool AnalysisView::save()
{
	bool firstSave = !m_analysis->has_path();

	if (firstSave)
	{
		auto path = QFileDialog::getSaveFileName(this, tr("Save analysis as..."),
			QStringLiteral("untitled.phon-analysis"),
			tr("Phonometrica analysis (*.phon-analysis)"));

		if (path.isEmpty())
			return false;

		auto bytes = path.toUtf8();
		m_analysis->set_path(String(bytes.constData(), bytes.size()), false);
	}

	m_analysis->save();

	if (firstSave)
	{
		auto *project = Project::get();
		auto *analyses_dir = project->analyses().get();
		analyses_dir->append(recast<Element>(m_analysis), false);
		project->register_file(m_analysis->path(), recast<Document>(m_analysis));
		project->modify();
		emit addedToProject();
	}

	emit titleChanged(label());
	return true;
}

void AnalysisView::discardChanges()
{
	// Nothing to do — the analysis is not reloaded on discard.
}


// =====================================================================
// UI setup
// =====================================================================

void AnalysisView::setupUi()
{
	auto *main_layout = new QVBoxLayout(this);
	main_layout->setContentsMargins(4, 4, 4, 4);
	main_layout->setSpacing(4);

	// ── Top bar ─────────────────────────────────────────────────────

	auto *top_bar = new QHBoxLayout;
	top_bar->setSpacing(6);

	top_bar->addWidget(new QLabel(tr("Formula:")));

	m_formula_edit = new QLineEdit;
	m_formula_edit->setPlaceholderText(tr("e.g. F1 ~ vowel + context"));
	m_formula_edit->setClearButtonEnabled(true);
	top_bar->addWidget(m_formula_edit, 1);

	top_bar->addWidget(new QLabel(tr("Family:")));

	m_family_combo = new QComboBox;
	m_family_combo->addItem(tr("Gaussian"), QStringLiteral("gaussian"));
	m_family_combo->addItem(tr("Binomial"), QStringLiteral("binomial"));
	m_family_combo->addItem(tr("Poisson"), QStringLiteral("poisson"));
	m_family_combo->addItem(tr("Negative binomial"), QStringLiteral("negbin"));
	m_family_combo->setCurrentIndex(0);
	top_bar->addWidget(m_family_combo);

	m_fit_button = new QPushButton(tr("Fit"));
	m_fit_button->setDefault(true);
	top_bar->addWidget(m_fit_button);

	main_layout->addLayout(top_bar);

	// ── Main content ────────────────────────────────────────────────

	auto *splitter = new QSplitter(Qt::Horizontal);

	// Left panel
	auto *left_widget = new QWidget;
	auto *left_layout = new QVBoxLayout(left_widget);
	left_layout->setContentsMargins(0, 0, 0, 0);
	left_layout->setSpacing(4);

	auto *col_group = new QGroupBox(tr("Columns"));
	auto *col_layout = new QVBoxLayout(col_group);
	col_layout->setContentsMargins(4, 4, 4, 4);
	m_column_list = new QListWidget;
	m_column_list->setToolTip(tr("Double-click to add to formula; right-click for options"));
	m_column_list->setContextMenuPolicy(Qt::CustomContextMenu);
	col_layout->addWidget(m_column_list);
	left_layout->addWidget(col_group, 1);

	auto *model_group = new QGroupBox(tr("Models"));
	auto *model_layout = new QVBoxLayout(model_group);
	model_layout->setContentsMargins(4, 4, 4, 4);
	m_model_list = new QListWidget;
	model_layout->addWidget(m_model_list);

	auto *model_buttons = new QHBoxLayout;
	m_delete_button = new QPushButton(tr("Delete"));
	m_delete_button->setEnabled(false);
	model_buttons->addWidget(m_delete_button);
	m_compare_button = new QPushButton(tr("Compare"));
	m_compare_button->setEnabled(false);
	model_buttons->addWidget(m_compare_button);
	model_layout->addLayout(model_buttons);

	left_layout->addWidget(model_group, 1);
	left_widget->setMinimumWidth(160);
	left_widget->setMaximumWidth(280);
	splitter->addWidget(left_widget);

	// Right panel (tabbed)
	m_right_tabs = new QTabWidget;

	// Summary tab: toolbar + text
	auto *summary_widget = new QWidget;
	auto *summary_layout = new QVBoxLayout(summary_widget);
	summary_layout->setContentsMargins(0, 0, 0, 0);
	summary_layout->setSpacing(0);

	auto *summary_toolbar = new QToolBar;
	summary_toolbar->setIconSize(QSize(16, 16));
	summary_toolbar->setMovable(false);

	auto *copy_action = summary_toolbar->addAction(QIcon(":/icons/clipboard-copy.svg"), tr("Copy to clipboard"));
	auto *save_txt_action = summary_toolbar->addAction(QIcon(":/icons/save.svg"), tr("Save as text..."));
	auto *save_latex_action = summary_toolbar->addAction(QIcon(":/icons/file-spreadsheet.svg"), tr("Save as LaTeX table..."));

	summary_layout->addWidget(summary_toolbar);

	m_summary = new QPlainTextEdit;
	m_summary->setReadOnly(true);
	QFont mono(QStringLiteral("monospace"));
	mono.setStyleHint(QFont::Monospace);
	mono.setPointSize(10);
	m_summary->setFont(mono);
	m_summary->setPlaceholderText(tr("Fit a model to see results here."));
	summary_layout->addWidget(m_summary, 1);

	m_right_tabs->addTab(summary_widget, tr("Summary"));
	m_right_tabs->setTabToolTip(0, tr("Coefficient table and goodness-of-fit statistics for the selected model"));

	auto *diag_widget = new QWidget;
	auto *diag_layout = new QVBoxLayout(diag_widget);
	diag_layout->setContentsMargins(4, 4, 4, 4);
	diag_layout->setSpacing(4);

	auto *diag_top = new QHBoxLayout;
	diag_top->addWidget(new QLabel(tr("Plot:")));
	m_plot_type_combo = new QComboBox;
	m_plot_type_combo->addItem(tr("Residuals vs Fitted"));
	m_plot_type_combo->addItem(tr("Normal Q-Q"));
	diag_top->addWidget(m_plot_type_combo);
	diag_top->addStretch();
	auto *export_button = new QPushButton(tr("Export..."));
	diag_top->addWidget(export_button);
	diag_layout->addLayout(diag_top);

	m_plot = new PlotWidget;
	diag_layout->addWidget(m_plot, 1);
	m_right_tabs->addTab(diag_widget, tr("Diagnostics"));
	m_right_tabs->setTabToolTip(1, tr("Residual plots to check model assumptions"));

	// EDA tab
	auto *eda_widget = new QWidget;
	auto *eda_layout = new QVBoxLayout(eda_widget);
	eda_layout->setContentsMargins(4, 4, 4, 4);
	eda_layout->setSpacing(4);

	// ── EDA toolbar (export actions, matching SpectrumView) ──
	auto *eda_toolbar = new QToolBar;
	eda_toolbar->setIconSize(QSize(20, 20));
	eda_toolbar->setMovable(false);

	auto *eda_save_menu = new QMenu(this);
	eda_save_menu->addAction(tr("Save as PNG..."), this, &AnalysisView::onExportEdaPNG);
	eda_save_menu->addAction(tr("Save as PDF..."), this, &AnalysisView::onExportEdaPDF);
	eda_save_menu->addAction(tr("Save as SVG..."), this, &AnalysisView::onExportEdaSVG);

	auto *eda_save_action = new QAction(QIcon(":/icons/save.svg"), tr("Save as..."), this);
	eda_save_action->setMenu(eda_save_menu);
	eda_toolbar->addAction(eda_save_action);
	if (auto *btn = qobject_cast<QToolButton *>(eda_toolbar->widgetForAction(eda_save_action)))
		btn->setPopupMode(QToolButton::InstantPopup);

	eda_layout->addWidget(eda_toolbar);

	// ── Plot area ──
	m_eda_plot = new PlotWidget;

	// ── Controls between plot and stats ──
	auto *eda_controls = new QHBoxLayout;
	eda_controls->setSpacing(6);
	eda_controls->addWidget(new QLabel(tr("X:")));
	m_eda_x_combo = new QComboBox;
	m_eda_x_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	eda_controls->addWidget(m_eda_x_combo);
	eda_controls->addWidget(new QLabel(tr("Y:")));
	m_eda_y_combo = new QComboBox;
	m_eda_y_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	eda_controls->addWidget(m_eda_y_combo);
	m_bins_label = new QLabel(tr("Bins:"));
	eda_controls->addWidget(m_bins_label);
	m_bins_spin = new QSpinBox;
	m_bins_spin->setRange(0, 200);
	m_bins_spin->setValue(0); // 0 = auto (Sturges' rule)
	m_bins_spin->setSpecialValueText(tr("Auto"));
	m_bins_spin->setToolTip(tr("Number of histogram bins (0 = automatic)"));
	eda_controls->addWidget(m_bins_spin);
	m_eda_regline_check = new QCheckBox(tr("Regression line"));
	m_eda_regline_check->setToolTip(tr("Overlay an OLS regression line on the scatter plot"));
	m_eda_regline_check->setVisible(false);
	eda_controls->addWidget(m_eda_regline_check);
	m_eda_density_check = new QCheckBox(tr("Density curve"));
	m_eda_density_check->setToolTip(tr("Overlay a kernel density estimate on the histogram"));
	m_eda_density_check->setVisible(false);
	eda_controls->addWidget(m_eda_density_check);
	eda_controls->addStretch();

	auto *eda_controls_widget = new QWidget;
	eda_controls_widget->setLayout(eda_controls);

	// ── Summary table ──
	m_eda_summary = new QTableWidget;
	m_eda_summary->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_eda_summary->setSelectionMode(QAbstractItemView::NoSelection);
	m_eda_summary->setAlternatingRowColors(true);
	m_eda_summary->verticalHeader()->setVisible(false);
	m_eda_summary->horizontalHeader()->setStretchLastSection(true);
	m_eda_summary->setMaximumHeight(200);

	// ── Assemble: plot | controls | stats in a splitter ──
	auto *eda_bottom = new QWidget;
	auto *eda_bottom_layout = new QVBoxLayout(eda_bottom);
	eda_bottom_layout->setContentsMargins(0, 0, 0, 0);
	eda_bottom_layout->setSpacing(4);
	eda_bottom_layout->addWidget(eda_controls_widget);
	eda_bottom_layout->addWidget(m_eda_summary, 1);

	auto *eda_splitter = new QSplitter(Qt::Vertical);
	eda_splitter->addWidget(m_eda_plot);
	eda_splitter->addWidget(eda_bottom);
	eda_splitter->setStretchFactor(0, 3);
	eda_splitter->setStretchFactor(1, 1);
	eda_layout->addWidget(eda_splitter, 1);

	m_right_tabs->addTab(eda_widget, tr("EDA"));
	m_right_tabs->setTabToolTip(2, tr("Exploratory Data Analysis: visualize variables before fitting a model"));

	splitter->addWidget(m_right_tabs);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	main_layout->addWidget(splitter, 1);

	// ── Connections ─────────────────────────────────────────────────

	connect(m_fit_button, &QPushButton::clicked, this, &AnalysisView::onFit);
	connect(m_formula_edit, &QLineEdit::returnPressed, this, &AnalysisView::onFit);
	connect(m_model_list, &QListWidget::currentRowChanged, this, &AnalysisView::onModelSelected);
	connect(m_delete_button, &QPushButton::clicked, this, &AnalysisView::onDeleteModel);
	connect(m_compare_button, &QPushButton::clicked, this, &AnalysisView::onCompareModels);
	connect(m_column_list, &QListWidget::itemDoubleClicked, this, &AnalysisView::onColumnDoubleClicked);
	connect(m_column_list, &QListWidget::customContextMenuRequested, this, &AnalysisView::onColumnContextMenu);
	connect(m_plot_type_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onPlotTypeChanged);
	connect(export_button, &QPushButton::clicked, this, &AnalysisView::onExportPlot);
	connect(copy_action, &QAction::triggered, this, &AnalysisView::onCopySummary);
	connect(save_txt_action, &QAction::triggered, this, &AnalysisView::onSaveSummaryText);
	connect(save_latex_action, &QAction::triggered, this, &AnalysisView::onSaveSummaryLatex);
	connect(m_eda_y_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onEdaChanged);
	connect(m_eda_x_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onEdaChanged);
	connect(m_bins_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AnalysisView::onEdaChanged);
	connect(m_eda_regline_check, &QCheckBox::toggled, this, &AnalysisView::onEdaChanged);
	connect(m_eda_density_check, &QCheckBox::toggled, this, &AnalysisView::onEdaChanged);
}

void AnalysisView::populateColumns()
{
	m_column_list->clear();
	m_eda_x_combo->clear();
	m_eda_y_combo->clear();
	m_eda_x_combo->addItem(tr("(None)"));
	m_eda_y_combo->addItem(tr("(None)"));

	if (!m_analysis->has_source()) return;

	auto names = m_analysis->column_names();
	for (intptr_t i = 1; i <= names.size(); i++) {
		auto qname = QString::fromUtf8(names[i].data(), (int)names[i].size());
		m_column_list->addItem(qname);
		m_eda_x_combo->addItem(qname);
		m_eda_y_combo->addItem(qname);
	}
}

void AnalysisView::populateModelList()
{
	m_model_list->clear();
	for (int i = 0; i < m_analysis->model_count(); i++)
	{
		auto &m = m_analysis->model(i);
		QString item_text = QStringLiteral("Model %1: %2")
			.arg(i + 1)
			.arg(QString::fromUtf8(m.formula.data(), (int)m.formula.size()));
		m_model_list->addItem(item_text);
	}
	m_delete_button->setEnabled(m_analysis->model_count() > 0);
	m_compare_button->setEnabled(m_analysis->model_count() >= 2);

	if (m_analysis->model_count() > 0) {
		m_model_list->setCurrentRow(0);
	}
}

void AnalysisView::updateFitEnabled()
{
	bool enabled = m_analysis->has_source();
	m_fit_button->setEnabled(enabled);
	m_formula_edit->setEnabled(enabled);
	m_family_combo->setEnabled(enabled);

	if (!enabled)
	{
		m_formula_edit->setPlaceholderText(tr("Source data unavailable — cannot fit new models"));
		m_column_list->setToolTip(tr("Source data is not available"));
	}
}


// =====================================================================
// Fit
// =====================================================================

void AnalysisView::onFit()
{
	QString formula_text = m_formula_edit->text().trimmed();
	if (formula_text.isEmpty())
	{
		QMessageBox::warning(this, tr("Fit model"), tr("Please enter a formula."));
		return;
	}

	try
	{
		String formula(formula_text.toUtf8().constData());
		String family(m_family_combo->currentData().toString().toUtf8().constData());

		int index = m_analysis->fit(formula, family);
		auto &m = m_analysis->model(index);

		QString item_text = QStringLiteral("Model %1: %2")
			.arg(index + 1)
			.arg(QString::fromUtf8(m.formula.data(), (int)m.formula.size()));
		m_model_list->addItem(item_text);
		m_model_list->setCurrentRow(m_model_list->count() - 1);

		m_delete_button->setEnabled(true);
		m_compare_button->setEnabled(m_analysis->model_count() >= 2);

		m_right_tabs->setCurrentIndex(0); // switch to Summary tab
		emit titleChanged(label());
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Fit model"), QString::fromUtf8(e.what()));
	}
}


// =====================================================================
// Model selection
// =====================================================================

void AnalysisView::onModelSelected(int row)
{
	if (row >= 0 && row < m_analysis->model_count())
	{
		m_current_model = row;
		displayModel(row);

		// Update the formula bar to match the selected model.
		auto &m = m_analysis->model(row);
		m_formula_edit->setText(QString::fromUtf8(m.formula.data(), (int)m.formula.size()));

		// Update the family combo to match.
		QString family = QString::fromUtf8(m.family.data(), (int)m.family.size());
		int idx = m_family_combo->findData(family);
		if (idx >= 0) m_family_combo->setCurrentIndex(idx);
	}
}

void AnalysisView::displayModel(int index)
{
	auto &m = m_analysis->model(index);
	m_summary->setPlainText(formatSummary(m));
	updateDiagnosticPlot();
}


// =====================================================================
// Delete / Compare
// =====================================================================

void AnalysisView::onDeleteModel()
{
	int row = m_model_list->currentRow();
	if (row < 0) return;

	m_analysis->remove_model(row);
	delete m_model_list->takeItem(row);

	for (int i = 0; i < m_model_list->count(); i++)
	{
		auto &m = m_analysis->model(i);
		m_model_list->item(i)->setText(
			QStringLiteral("Model %1: %2").arg(i + 1)
				.arg(QString::fromUtf8(m.formula.data(), (int)m.formula.size())));
	}

	m_delete_button->setEnabled(m_analysis->model_count() > 0);
	m_compare_button->setEnabled(m_analysis->model_count() >= 2);
	m_current_model = -1;

	if (m_model_list->count() == 0) {
		m_summary->clear();
		m_plot->clear();
	}

	emit titleChanged(label());
}

void AnalysisView::onCompareModels()
{
	if (m_analysis->model_count() < 2) return;

	QString text;
	text += QStringLiteral("Model comparison\n");
	text += QStringLiteral("================\n\n");
	text += QString::asprintf("%-8s %-40s %8s %10s %10s %12s\n",
	                           "", "Formula", "npar", "AIC", "BIC", "logLik");
	text += QStringLiteral("------------------------------------------------------------------------------------\n");

	for (int i = 0; i < m_analysis->model_count(); i++)
	{
		auto &m = m_analysis->model(i);
		QString mlabel = QStringLiteral("Model %1").arg(i + 1);
		QString formula = QString::fromUtf8(m.formula.data(), (int)m.formula.size());
		if (formula.length() > 40)
			formula = formula.left(37) + QStringLiteral("...");

		text += QString::asprintf("%-8s %-40s %8ld %10.1f %10.1f %12.1f\n",
		                           mlabel.toUtf8().constData(),
		                           formula.toUtf8().constData(),
		                           (long)m.nparams(),
		                           m.aic, m.bic, m.loglik);
	}

	m_summary->setPlainText(text);
	m_right_tabs->setCurrentIndex(0);
}


// =====================================================================
// Column interaction (formula building)
// =====================================================================

// Wrap a column name in single quotes if it contains spaces.
static QString quoteIfNeeded(const QString &name)
{
	if (name.contains(' ')) {
		return QStringLiteral("'") + name + QStringLiteral("'");
	}
	return name;
}

void AnalysisView::onColumnDoubleClicked(QListWidgetItem *item)
{
	if (!item) return;
	addPredictor(item->text());
}

void AnalysisView::onColumnContextMenu(const QPoint &pos)
{
	auto *item = m_column_list->itemAt(pos);
	if (!item) return;

	QString name = item->text();

	QMenu menu;
	auto *resp_action = menu.addAction(tr("Set as response"));
	auto *pred_action = menu.addAction(tr("Add as predictor"));
	menu.addSeparator();
	auto *interaction_action = menu.addAction(tr("Add as interaction (\303\227)"));
	auto *random_action = menu.addAction(tr("Add as random intercept"));

	auto *chosen = menu.exec(m_column_list->mapToGlobal(pos));
	if (!chosen) return;

	if (chosen == resp_action) {
		setResponse(name);
	} else if (chosen == pred_action) {
		addPredictor(name);
	} else if (chosen == interaction_action) {
		QString text = m_formula_edit->text().trimmed();
		if (!text.isEmpty() && text.contains('~'))
			m_formula_edit->setText(text + QStringLiteral(" * ") + quoteIfNeeded(name));
		m_formula_edit->setFocus();
	} else if (chosen == random_action) {
		addRandomIntercept(name);
	}
}

void AnalysisView::setResponse(const QString &name)
{
	QString quoted = quoteIfNeeded(name);
	QString text = m_formula_edit->text().trimmed();
	int tilde = text.indexOf('~');
	if (tilde >= 0) {
		m_formula_edit->setText(quoted + QStringLiteral(" ") + text.mid(tilde));
	} else {
		m_formula_edit->setText(quoted + QStringLiteral(" ~ "));
	}
	m_formula_edit->setFocus();
	m_formula_edit->setCursorPosition(m_formula_edit->text().length());
}

void AnalysisView::addPredictor(const QString &name)
{
	QString quoted = quoteIfNeeded(name);
	QString text = m_formula_edit->text().trimmed();
	if (text.isEmpty()) {
		m_formula_edit->setText(quoted + QStringLiteral(" ~ "));
	} else if (!text.contains('~')) {
		m_formula_edit->setText(text + QStringLiteral(" ~ ") + quoted);
	} else {
		QString rhs = text.mid(text.indexOf('~') + 1).trimmed();
		if (rhs.isEmpty())
			m_formula_edit->setText(text + quoted);
		else
			m_formula_edit->setText(text + QStringLiteral(" + ") + quoted);
	}
	m_formula_edit->setFocus();
	m_formula_edit->setCursorPosition(m_formula_edit->text().length());
}

void AnalysisView::addRandomIntercept(const QString &name)
{
	QString quoted = quoteIfNeeded(name);
	QString term = QStringLiteral("(1 | ") + quoted + QStringLiteral(")");
	QString text = m_formula_edit->text().trimmed();

	if (text.isEmpty() || !text.contains('~'))
	{
		// Not a valid formula yet — append the term and let the user fill in the rest.
		if (text.isEmpty())
			m_formula_edit->setText(QStringLiteral("~ ") + term);
		else
			m_formula_edit->setText(text + QStringLiteral(" ~ ") + term);
	}
	else
	{
		QString rhs = text.mid(text.indexOf('~') + 1).trimmed();
		if (rhs.isEmpty())
			m_formula_edit->setText(text + term);
		else
			m_formula_edit->setText(text + QStringLiteral(" + ") + term);
	}

	m_formula_edit->setFocus();
	m_formula_edit->setCursorPosition(m_formula_edit->text().length());
}


// =====================================================================
// Diagnostic plots
// =====================================================================

void AnalysisView::onPlotTypeChanged(int)
{
	updateDiagnosticPlot();
}

void AnalysisView::updateDiagnosticPlot()
{
	if (m_current_model < 0 || m_current_model >= m_analysis->model_count())
	{
		m_plot->clear();
		return;
	}

	auto &m = m_analysis->model(m_current_model);

	switch (m_plot_type_combo->currentIndex())
	{
	case 0:  plotResidualsVsFitted(m); break;
	case 1:  plotQQ(m);               break;
	default: m_plot->clear();         break;
	}
}

void AnalysisView::plotResidualsVsFitted(const stats::Model &m)
{
	intptr_t n = m.nobs;
	if (n == 0 || m.fitted.empty() || m.residuals.empty()) {
		m_plot->clear();
		return;
	}

	std::vector<double> x(n), y(n);
	for (intptr_t i = 0; i < n; i++) {
		x[i] = m.fitted[i + 1];
		y[i] = m.residuals[i + 1];
	}

	m_plot->setData(std::move(x), std::move(y),
	                tr("Fitted values"), tr("Residuals"),
	                tr("Residuals vs Fitted"),
	                PlotWidget::RefLine::HorizontalAtZero);
}

void AnalysisView::plotQQ(const stats::Model &m)
{
	intptr_t n = m.nobs;
	if (n == 0 || m.residuals.empty()) {
		m_plot->clear();
		return;
	}

	// Standardize residuals
	std::vector<double> resid(n);
	double sum = 0;
	for (intptr_t i = 0; i < n; i++) {
		resid[i] = m.residuals[i + 1];
		sum += resid[i];
	}
	double mean = sum / n;
	double ss = 0;
	for (intptr_t i = 0; i < n; i++) {
		resid[i] -= mean;
		ss += resid[i] * resid[i];
	}
	double sd = std::sqrt(ss / (n - 1));
	if (sd > 0) {
		for (intptr_t i = 0; i < n; i++)
			resid[i] /= sd;
	}

	// Sort
	std::vector<intptr_t> idx(n);
	std::iota(idx.begin(), idx.end(), 0);
	std::sort(idx.begin(), idx.end(), [&](intptr_t a, intptr_t b) {
		return resid[a] < resid[b];
	});

	// Theoretical quantiles
	boost::math::normal_distribution<double> norm(0.0, 1.0);
	std::vector<double> theoretical(n), sample(n);
	for (intptr_t i = 0; i < n; i++) {
		theoretical[i] = boost::math::quantile(norm, (i + 0.5) / n);
		sample[i] = resid[idx[i]];
	}

	m_plot->setData(std::move(theoretical), std::move(sample),
	                tr("Theoretical Quantiles"), tr("Sample Quantiles"),
	                tr("Normal Q-Q"),
	                PlotWidget::RefLine::Diagonal);
}


// =====================================================================
// EDA
// =====================================================================

void AnalysisView::onEdaChanged()
{
	updateEdaPlot();
	updateEdaSummary();
}

void AnalysisView::onExportEdaPNG()
{
	if (!m_eda_plot->hasData()) {
		QMessageBox::information(this, tr("Export"), tr("No plot to export."));
		return;
	}
	QString path = QFileDialog::getSaveFileName(this,
		tr("Export plot as PNG"), QString(), tr("PNG image (*.png)"));
	if (path.isEmpty()) return;
	if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
		path += QStringLiteral(".png");
	m_eda_plot->savePNG(path);
}

void AnalysisView::onExportEdaPDF()
{
	if (!m_eda_plot->hasData()) {
		QMessageBox::information(this, tr("Export"), tr("No plot to export."));
		return;
	}
	QString path = QFileDialog::getSaveFileName(this,
		tr("Export plot as PDF"), QString(), tr("PDF document (*.pdf)"));
	if (path.isEmpty()) return;
	if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
		path += QStringLiteral(".pdf");
	m_eda_plot->savePDF(path);
}

void AnalysisView::onExportEdaSVG()
{
	if (!m_eda_plot->hasData()) {
		QMessageBox::information(this, tr("Export"), tr("No plot to export."));
		return;
	}
	QString path = QFileDialog::getSaveFileName(this,
		tr("Export plot as SVG"), QString(), tr("SVG image (*.svg)"));
	if (path.isEmpty()) return;
	if (!path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive))
		path += QStringLiteral(".svg");
	m_eda_plot->saveSVG(path);
}

bool AnalysisView::isColumnNumeric(const String &col_name) const
{
	if (!m_analysis->has_source()) return false;
	auto *dt = m_analysis->data();
	intptr_t nc = dt->column_count();
	intptr_t col = 0;
	for (intptr_t j = 1; j <= nc; j++) {
		if (dt->get_header(j) == col_name) { col = j; break; }
	}
	if (col < 1) return false;

	// Check the first few non-empty values.
	intptr_t nr = dt->row_count();
	int checked = 0;
	for (intptr_t r = 1; r <= nr && checked < 20; r++)
	{
		auto val = dt->get_cell(r, col);
		if (val.empty()) continue;
		bool ok;
		val.to_float(&ok);
		if (!ok) return false;
		checked++;
	}
	return checked > 0;
}

void AnalysisView::updateEdaPlot()
{
	// X is the primary variable; Y is the optional secondary variable.
	// When X = "(None)": nothing to show.
	if (!m_analysis->has_source() || m_eda_x_combo->currentIndex() <= 0) {
		m_eda_plot->clear();
		m_bins_label->setVisible(false);
		m_bins_spin->setVisible(false);
		m_eda_regline_check->setVisible(false);
		m_eda_density_check->setVisible(false);
		return;
	}

	auto *dt = m_analysis->data();
	auto x_name_q = m_eda_x_combo->currentText();
	auto x_name = String(x_name_q.toUtf8().constData());

	// Find X column
	intptr_t nc = dt->column_count();
	intptr_t x_col = 0;
	for (intptr_t j = 1; j <= nc; j++) {
		if (dt->get_header(j) == x_name) { x_col = j; break; }
	}
	if (x_col < 1) { m_eda_plot->clear(); return; }

	intptr_t nr = dt->row_count();

	// Check if Y is selected (index 0 is "(None)").
	bool has_y = (m_eda_y_combo->currentIndex() > 0);

	// Show bins control only for numeric histograms (univariate numeric).
	bool x_numeric = isColumnNumeric(x_name);
	bool is_histogram = !has_y && x_numeric;
	m_bins_label->setVisible(is_histogram);
	m_bins_spin->setVisible(is_histogram);

	if (!has_y)
	{
		// ── Univariate: plot X alone ──
		m_eda_regline_check->setVisible(false);
		if (x_numeric)
		{
			// Histogram of numeric X values
			std::vector<double> vals;
			vals.reserve(nr);
			for (intptr_t r = 1; r <= nr; r++)
			{
				auto v = dt->get_cell(r, x_col);
				if (v.empty()) continue;
				bool ok;
				double d = v.to_float(&ok);
				if (ok) vals.push_back(d);
			}
			if (vals.empty()) { m_eda_plot->clear(); return; }

			// Keep a copy for KDE before moving vals into the histogram.
			std::vector<double> vals_copy;
			int nbins = m_bins_spin->value();
			bool want_density = m_eda_density_check->isChecked() && vals.size() >= 2;
			if (want_density) vals_copy = vals;

			m_eda_plot->setHistogramData(std::move(vals), x_name_q, tr("Count"),
			                              x_name_q, nbins);

			m_eda_density_check->setVisible(true);

			if (want_density)
			{
				size_t n = vals_copy.size();

				// Silverman's rule of thumb for bandwidth.
				double sum = 0, sum2 = 0;
				for (double v : vals_copy) { sum += v; sum2 += v * v; }
				double mean = sum / n;
				double var  = sum2 / n - mean * mean;
				double sd   = std::sqrt(std::max(var, 0.0));

				// Also compute IQR for robustness.
				std::sort(vals_copy.begin(), vals_copy.end());
				double q1 = vals_copy[n / 4];
				double q3 = vals_copy[3 * n / 4];
				double iqr = q3 - q1;
				double s = std::min(sd, iqr / 1.34);
				if (s < 1e-15) s = sd;
				if (s < 1e-15) s = 1.0;
				double h = 0.9 * s * std::pow((double)n, -0.2);

				double xlo = vals_copy.front() - 3.0 * h;
				double xhi = vals_copy.back()  + 3.0 * h;
				constexpr int NPTS = 200;
				double dx = (xhi - xlo) / (NPTS - 1);

				double data_lo = vals_copy.front();
				double data_hi = vals_copy.back();
				double data_range = data_hi - data_lo;
				if (data_range < 1e-10) data_range = 1.0;
				int actual_nbins = nbins;
				if (actual_nbins <= 0)
					actual_nbins = std::max(5, (int)std::ceil(std::log2((double)n) + 1));
				double bin_width = data_range / actual_nbins;

				double scale = (double)n * bin_width;
				double inv_h = 1.0 / h;
				double norm = 1.0 / (std::sqrt(2.0 * M_PI) * h * (double)n);

				std::vector<double> cx(NPTS), cy(NPTS);
				for (int i = 0; i < NPTS; i++)
				{
					double x = xlo + i * dx;
					double f = 0;
					for (size_t j = 0; j < n; j++)
					{
						double u = (x - vals_copy[j]) * inv_h;
						f += std::exp(-0.5 * u * u);
					}
					cx[i] = x;
					cy[i] = f * norm * scale;
				}

				m_eda_plot->setDensityCurve(std::move(cx), std::move(cy));
			}
			else
			{
				m_eda_plot->clearDensityCurve();
			}
		}
		else
		{
			m_eda_density_check->setVisible(false);
			// Bar chart of categorical X counts
			std::map<QString, int> counts;
			std::vector<QString> order;
			for (intptr_t r = 1; r <= nr; r++)
			{
				auto v = dt->get_cell(r, x_col);
				if (v.empty()) continue;
				auto qs = QString::fromUtf8(v.data(), (int)v.size());
				if (counts.find(qs) == counts.end()) order.push_back(qs);
				counts[qs]++;
			}
			if (order.empty()) { m_eda_plot->clear(); return; }
			std::vector<int> vals;
			for (auto &lbl : order) vals.push_back(counts[lbl]);
			m_eda_plot->setBarChartData(std::move(order), std::move(vals),
			                             x_name_q, tr("Count"), x_name_q);
		}
		return;
	}

	// ── Bivariate: both X and Y selected ──

	auto y_name_q = m_eda_y_combo->currentText();
	auto y_name = String(y_name_q.toUtf8().constData());

	intptr_t y_col = 0;
	for (intptr_t j = 1; j <= nc; j++) {
		if (dt->get_header(j) == y_name) { y_col = j; break; }
	}
	if (y_col < 1) { m_eda_plot->clear(); return; }

	bool y_numeric = isColumnNumeric(y_name);

	if (x_numeric && y_numeric)
	{
		// Scatter: X continuous, Y continuous
		std::vector<double> xv, yv;
		xv.reserve(nr);
		yv.reserve(nr);
		for (intptr_t r = 1; r <= nr; r++)
		{
			auto vx = dt->get_cell(r, x_col);
			auto vy = dt->get_cell(r, y_col);
			if (vx.empty() || vy.empty()) continue;
			bool okx, oky;
			double dx = vx.to_float(&okx);
			double dy = vy.to_float(&oky);
			if (okx && oky) {
				xv.push_back(dx);
				yv.push_back(dy);
			}
		}
		if (xv.empty()) { m_eda_plot->clear(); return; }

		// Compute OLS regression before moving the vectors.
		double reg_intercept = 0, reg_slope = 0, reg_r2 = 0;
		bool reg_valid = false;
		if (m_eda_regline_check->isChecked() && xv.size() >= 2)
		{
			size_t n = xv.size();
			double sx = 0, sy = 0;
			for (size_t i = 0; i < n; i++) { sx += xv[i]; sy += yv[i]; }
			double mx = sx / n, my = sy / n;

			double sxy = 0, sxx = 0, syy = 0;
			for (size_t i = 0; i < n; i++) {
				double dx = xv[i] - mx;
				double dy = yv[i] - my;
				sxy += dx * dy;
				sxx += dx * dx;
				syy += dy * dy;
			}
			if (sxx > 1e-15)
			{
				reg_slope = sxy / sxx;
				reg_intercept = my - reg_slope * mx;
				reg_r2 = (syy > 1e-15) ? (sxy * sxy) / (sxx * syy) : 0.0;
				reg_valid = true;
			}
		}

		m_eda_plot->setData(std::move(xv), std::move(yv), x_name_q, y_name_q,
		                     y_name_q + QStringLiteral(" ~ ") + x_name_q);

		m_eda_regline_check->setVisible(true);
		m_eda_density_check->setVisible(false);
		if (reg_valid)
			m_eda_plot->setRegressionLine(reg_intercept, reg_slope, reg_r2);
		else
			m_eda_plot->clearRegressionLine();
	}
	else if (!x_numeric && y_numeric)
	{
		// Box plot: X categorical, Y continuous
		m_eda_regline_check->setVisible(false);
		m_eda_density_check->setVisible(false);

		std::vector<QString> groups;
		std::vector<double> vals;
		groups.reserve(nr);
		vals.reserve(nr);
		for (intptr_t r = 1; r <= nr; r++)
		{
			auto vx = dt->get_cell(r, x_col);
			auto vy = dt->get_cell(r, y_col);
			if (vx.empty() || vy.empty()) continue;
			bool ok;
			double dy = vy.to_float(&ok);
			if (!ok) continue;
			groups.push_back(QString::fromUtf8(vx.data(), (int)vx.size()));
			vals.push_back(dy);
		}
		if (vals.empty()) { m_eda_plot->clear(); return; }
		m_eda_plot->setBoxPlotData(std::move(groups), std::move(vals),
		                            x_name_q, y_name_q,
		                            y_name_q + QStringLiteral(" ~ ") + x_name_q);
	}
	else
	{
		// Unsupported combination (e.g. both categorical) — clear.
		m_eda_regline_check->setVisible(false);
		m_eda_density_check->setVisible(false);
		m_eda_plot->clear();
	}
}


// =====================================================================
// EDA descriptive statistics
// =====================================================================

// Compute the median of a sorted vector.
static double sorted_median(const std::vector<double> &v)
{
	if (v.empty()) return 0;
	size_t n = v.size();
	if (n % 2 == 1) return v[n / 2];
	return (v[n / 2 - 1] + v[n / 2]) * 0.5;
}

// Compute Q1 of a sorted vector (lower quartile).
static double sorted_q1(const std::vector<double> &v)
{
	if (v.size() < 2) return v.empty() ? 0 : v[0];
	size_t n = v.size();
	double idx = 0.25 * (n - 1);
	size_t lo = (size_t)std::floor(idx);
	double frac = idx - lo;
	if (lo + 1 >= n) return v[lo];
	return v[lo] * (1.0 - frac) + v[lo + 1] * frac;
}

// Compute Q3 of a sorted vector (upper quartile).
static double sorted_q3(const std::vector<double> &v)
{
	if (v.size() < 2) return v.empty() ? 0 : v[0];
	size_t n = v.size();
	double idx = 0.75 * (n - 1);
	size_t lo = (size_t)std::floor(idx);
	double frac = idx - lo;
	if (lo + 1 >= n) return v[lo];
	return v[lo] * (1.0 - frac) + v[lo + 1] * frac;
}

void AnalysisView::updateEdaSummary()
{
	m_eda_summary->clear();
	m_eda_summary->setRowCount(0);
	m_eda_summary->setColumnCount(0);

	// X is the primary variable; Y is optional.
	if (!m_analysis->has_source() || m_eda_x_combo->currentIndex() <= 0)
		return;

	auto *dt = m_analysis->data();
	auto x_name_q = m_eda_x_combo->currentText();
	auto x_name = String(x_name_q.toUtf8().constData());

	intptr_t nc = dt->column_count();
	intptr_t x_col = 0;
	for (intptr_t j = 1; j <= nc; j++) {
		if (dt->get_header(j) == x_name) { x_col = j; break; }
	}
	if (x_col < 1) return;

	intptr_t nr = dt->row_count();
	bool has_y = (m_eda_y_combo->currentIndex() > 0);
	bool x_numeric = isColumnNumeric(x_name);

	if (!has_y)
	{
		if (x_numeric)
		{
			// ── Univariate numeric: N, Mean, SD, Min, Q1, Median, Q3, Max, Missing ──

			std::vector<double> vals;
			vals.reserve(nr);
			intptr_t missing = 0;
			for (intptr_t r = 1; r <= nr; r++)
			{
				auto v = dt->get_cell(r, x_col);
				if (v.empty()) { missing++; continue; }
				bool ok;
				double d = v.to_float(&ok);
				if (ok) vals.push_back(d);
				else missing++;
			}
			if (vals.empty()) return;

			std::sort(vals.begin(), vals.end());
			size_t n = vals.size();
			double sum = 0;
			for (double v : vals) sum += v;
			double mean = sum / n;
			double ss = 0;
			for (double v : vals) { double d = v - mean; ss += d * d; }
			double sd = (n > 1) ? std::sqrt(ss / (n - 1)) : 0.0;

			m_eda_summary->setColumnCount(9);
			m_eda_summary->setHorizontalHeaderLabels(
				{tr("N"), tr("Mean"), tr("SD"), tr("Min"), tr("Q1"),
				 tr("Median"), tr("Q3"), tr("Max"), tr("Missing")});
			m_eda_summary->setRowCount(1);

			auto set = [&](int col, const QString &text) {
				auto *item = new QTableWidgetItem(text);
				item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
				m_eda_summary->setItem(0, col, item);
			};

			set(0, QString::number(n));
			set(1, QString::number(mean, 'f', 4));
			set(2, QString::number(sd, 'f', 4));
			set(3, QString::number(vals.front(), 'f', 4));
			set(4, QString::number(sorted_q1(vals), 'f', 4));
			set(5, QString::number(sorted_median(vals), 'f', 4));
			set(6, QString::number(sorted_q3(vals), 'f', 4));
			set(7, QString::number(vals.back(), 'f', 4));
			set(8, QString::number(missing));

			m_eda_summary->resizeColumnsToContents();
		}
		else
		{
			// ── Univariate categorical: Level, Count ──

			std::vector<QString> order;
			std::map<QString, int> counts;
			for (intptr_t r = 1; r <= nr; r++)
			{
				auto v = dt->get_cell(r, x_col);
				if (v.empty()) continue;
				auto qs = QString::fromUtf8(v.data(), (int)v.size());
				if (counts.find(qs) == counts.end()) order.push_back(qs);
				counts[qs]++;
			}
			if (order.empty()) return;

			m_eda_summary->setColumnCount(2);
			m_eda_summary->setHorizontalHeaderLabels({x_name_q, tr("Count")});
			m_eda_summary->setRowCount((int)order.size());

			for (int i = 0; i < (int)order.size(); i++)
			{
				m_eda_summary->setItem(i, 0, new QTableWidgetItem(order[i]));
				auto *cnt = new QTableWidgetItem(QString::number(counts[order[i]]));
				cnt->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
				m_eda_summary->setItem(i, 1, cnt);
			}
			m_eda_summary->resizeColumnsToContents();
		}
		return;
	}

	// ── Bivariate: both X and Y selected ──

	auto y_name_q = m_eda_y_combo->currentText();
	auto y_name = String(y_name_q.toUtf8().constData());

	intptr_t y_col = 0;
	for (intptr_t j = 1; j <= nc; j++) {
		if (dt->get_header(j) == y_name) { y_col = j; break; }
	}
	if (y_col < 1) return;

	bool y_numeric = isColumnNumeric(y_name);

	if (x_numeric && y_numeric)
	{
		// ── Scatter (both continuous): N, r, means, SDs ──

		std::vector<double> xv, yv;
		xv.reserve(nr);
		yv.reserve(nr);
		for (intptr_t r = 1; r <= nr; r++)
		{
			auto vx = dt->get_cell(r, x_col);
			auto vy = dt->get_cell(r, y_col);
			if (vx.empty() || vy.empty()) continue;
			bool okx, oky;
			double dx = vx.to_float(&okx);
			double dy = vy.to_float(&oky);
			if (okx && oky) { xv.push_back(dx); yv.push_back(dy); }
		}
		if (xv.empty()) return;

		size_t n = xv.size();
		double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
		for (size_t i = 0; i < n; i++) { sx += xv[i]; sy += yv[i]; }
		double mx = sx / n, my = sy / n;
		for (size_t i = 0; i < n; i++) {
			double dx = xv[i] - mx;
			double dy = yv[i] - my;
			sxx += dx * dx;
			syy += dy * dy;
			sxy += dx * dy;
		}
		double sd_x = (n > 1) ? std::sqrt(sxx / (n - 1)) : 0.0;
		double sd_y = (n > 1) ? std::sqrt(syy / (n - 1)) : 0.0;
		double r = (sxx > 1e-15 && syy > 1e-15) ? sxy / std::sqrt(sxx * syy) : 0.0;

		m_eda_summary->setColumnCount(6);
		m_eda_summary->setHorizontalHeaderLabels(
			{tr("N"), tr("r"),
			 QStringLiteral("Mean(%1)").arg(x_name_q),
			 QStringLiteral("SD(%1)").arg(x_name_q),
			 QStringLiteral("Mean(%1)").arg(y_name_q),
			 QStringLiteral("SD(%1)").arg(y_name_q)});
		m_eda_summary->setRowCount(1);

		auto set = [&](int col, const QString &text) {
			auto *item = new QTableWidgetItem(text);
			item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
			m_eda_summary->setItem(0, col, item);
		};

		set(0, QString::number(n));
		set(1, QString::number(r, 'f', 4));
		set(2, QString::number(mx, 'f', 4));
		set(3, QString::number(sd_x, 'f', 4));
		set(4, QString::number(my, 'f', 4));
		set(5, QString::number(sd_y, 'f', 4));

		m_eda_summary->resizeColumnsToContents();
	}
	else if (!x_numeric && y_numeric)
	{
		// ── Box plot (X categorical, Y continuous): grouped stats ──

		std::vector<QString> group_order;
		std::map<QString, std::vector<double>> grouped;

		for (intptr_t r = 1; r <= nr; r++)
		{
			auto vx = dt->get_cell(r, x_col);
			auto vy = dt->get_cell(r, y_col);
			if (vx.empty() || vy.empty()) continue;
			bool ok;
			double dy = vy.to_float(&ok);
			if (!ok) continue;
			auto qs = QString::fromUtf8(vx.data(), (int)vx.size());
			if (grouped.find(qs) == grouped.end()) group_order.push_back(qs);
			grouped[qs].push_back(dy);
		}
		if (group_order.empty()) return;

		m_eda_summary->setColumnCount(7);
		m_eda_summary->setHorizontalHeaderLabels(
			{x_name_q, tr("N"), tr("Mean"), tr("SD"),
			 tr("Min"), tr("Median"), tr("Max")});
		m_eda_summary->setRowCount((int)group_order.size());

		for (int g = 0; g < (int)group_order.size(); g++)
		{
			auto &vals = grouped[group_order[g]];
			std::sort(vals.begin(), vals.end());
			size_t n = vals.size();

			double sum = 0;
			for (double v : vals) sum += v;
			double mean = sum / n;
			double ss = 0;
			for (double v : vals) { double d = v - mean; ss += d * d; }
			double sd = (n > 1) ? std::sqrt(ss / (n - 1)) : 0.0;

			m_eda_summary->setItem(g, 0, new QTableWidgetItem(group_order[g]));

			auto set = [&](int col, const QString &text) {
				auto *item = new QTableWidgetItem(text);
				item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
				m_eda_summary->setItem(g, col, item);
			};

			set(1, QString::number(n));
			set(2, QString::number(mean, 'f', 4));
			set(3, QString::number(sd, 'f', 4));
			set(4, QString::number(vals.front(), 'f', 4));
			set(5, QString::number(sorted_median(vals), 'f', 4));
			set(6, QString::number(vals.back(), 'f', 4));
		}
		m_eda_summary->resizeColumnsToContents();
	}
	// else: unsupported combination — table stays empty.
}


// =====================================================================
// Summary export
// =====================================================================

void AnalysisView::onCopySummary()
{
	auto text = m_summary->toPlainText();
	if (!text.isEmpty()) {
		QApplication::clipboard()->setText(text);
	}
}

void AnalysisView::onSaveSummaryText()
{
	auto text = m_summary->toPlainText();
	if (text.isEmpty()) return;

	QString path = QFileDialog::getSaveFileName(this,
		tr("Save summary"), QString(),
		tr("Text file (*.txt)"));
	if (path.isEmpty()) return;

	QFile file(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		file.write(text.toUtf8());
	}
}

void AnalysisView::onSaveSummaryLatex()
{
	if (m_current_model < 0 || m_current_model >= m_analysis->model_count()) return;

	QString path = QFileDialog::getSaveFileName(this,
		tr("Save as LaTeX"), QString(),
		tr("LaTeX file (*.tex)"));
	if (path.isEmpty()) return;

	auto &m = m_analysis->model(m_current_model);
	QString tex = formatLatex(m);

	QFile file(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		file.write(tex.toUtf8());
	}
}

QString AnalysisView::formatLatex(const stats::Model &m) const
{
	QString tex;

	tex += QStringLiteral("\\begin{table}[ht]\n");
	tex += QStringLiteral("\\centering\n");

	// Escape LaTeX special chars in formula
	QString formula = QString::fromUtf8(m.formula.data(), (int)m.formula.size());
	formula.replace('_', QStringLiteral("\\_"));
	formula.replace('~', QStringLiteral("$\\sim$"));

	tex += QStringLiteral("\\caption{%1}\n").arg(formula);
	tex += QStringLiteral("\\begin{tabular}{lrrrr}\n");
	tex += QStringLiteral("\\hline\n");

	const char *stat_label = m.is_gaussian() ? "$t$" : "$z$";
	tex += QStringLiteral(" & Estimate & Std.~Error & %1 & $p$ \\\\\n")
		.arg(QString::fromUtf8(stat_label));
	tex += QStringLiteral("\\hline\n");

	for (intptr_t i = 1; i <= m.nfixed; i++)
	{
		QString name = QString::fromUtf8(m.coef_names[i].data(), (int)m.coef_names[i].size());
		name.replace('_', QStringLiteral("\\_"));
		name.replace('&', QStringLiteral("\\&"));

		QString pval;
		if (m.p[i] < 0.001)
			pval = QStringLiteral("$<$\\,0.001");
		else
			pval = QString::number(m.p[i], 'f', 4);

		tex += QStringLiteral("%1 & %2 & %3 & %4 & %5 \\\\\n")
			.arg(name)
			.arg(m.beta[i], 0, 'f', 4)
			.arg(m.se[i], 0, 'f', 4)
			.arg(m.stat[i], 0, 'f', 3)
			.arg(pval);
	}

	tex += QStringLiteral("\\hline\n");
	tex += QStringLiteral("\\end{tabular}\n\n");

	// Random effects table (if any)
	if (m.has_random_effects())
	{
		tex += QStringLiteral("\\medskip\n");
		tex += QStringLiteral("\\begin{tabular}{lrrr}\n");
		tex += QStringLiteral("\\hline\n");
		tex += QStringLiteral("Group & Variance & Std.~Dev. & Levels \\\\\n");
		tex += QStringLiteral("\\hline\n");

		for (intptr_t g = 1; g <= m.random_effects.size(); g++)
		{
			auto &re = m.random_effects[g];
			QString gname = QString::fromUtf8(re.group_name.data(), (int)re.group_name.size());
			gname.replace('_', QStringLiteral("\\_"));

			for (intptr_t t = 1; t <= re.term_names.size(); t++)
			{
				double var = (t <= re.variance.size()) ? re.variance[t] : 0.0;
				double sd = std::sqrt(std::max(var, 0.0));

				if (t == 1) {
					tex += QStringLiteral("%1 & %2 & %3 & %4 \\\\\n")
						.arg(gname)
						.arg(var, 0, 'f', 4)
						.arg(sd, 0, 'f', 4)
						.arg(re.nlevels);
				} else {
					QString tname = QString::fromUtf8(re.term_names[t].data(),
					                                   (int)re.term_names[t].size());
					tname.replace('_', QStringLiteral("\\_"));
					tex += QStringLiteral("\\quad %1 & %2 & %3 & \\\\\n")
						.arg(tname)
						.arg(var, 0, 'f', 4)
						.arg(sd, 0, 'f', 4);
				}
			}
		}

		if (m.is_gaussian()) {
			tex += QStringLiteral("Residual & %1 & %2 & \\\\\n")
				.arg(m.rse * m.rse, 0, 'f', 4)
				.arg(m.rse, 0, 'f', 4);
		}

		tex += QStringLiteral("\\hline\n");
		tex += QStringLiteral("\\end{tabular}\n\n");
	}

	// Notes below the table
	tex += QStringLiteral("\\medskip\n");
	tex += QStringLiteral("\\footnotesize\n");
	QString family_display_tex = QString::fromUtf8(m.family.data(), (int)m.family.size());
	if (m.is_negbin()) family_display_tex = QStringLiteral("Negative binomial");

	tex += QStringLiteral("Family: %1 (%2); $N$ = %3")
		.arg(family_display_tex)
		.arg(QString::fromUtf8(m.link.data(), (int)m.link.size()))
		.arg(m.nobs);

	if (m.is_gaussian() && !m.has_random_effects()) {
		tex += QStringLiteral("; $R^2$ = %1; Adj.\\ $R^2$ = %2")
			.arg(m.r2, 0, 'f', 4)
			.arg(m.adj_r2, 0, 'f', 4);
	}
	if (m.is_negbin()) {
		tex += QStringLiteral("; $\\theta$ = %1").arg(m.theta, 0, 'f', 4);
	}

	tex += QStringLiteral("\\\\\n");
	tex += QStringLiteral("AIC = %1; BIC = %2; log-lik.\\ = %3\n")
		.arg(m.aic, 0, 'f', 1)
		.arg(m.bic, 0, 'f', 1)
		.arg(m.loglik, 0, 'f', 1);

	tex += QStringLiteral("\\end{table}\n");

	return tex;
}


// =====================================================================
// Export plot
// =====================================================================

void AnalysisView::onExportPlot()
{
	if (!m_plot->hasData()) {
		QMessageBox::information(this, tr("Export"), tr("No plot to export."));
		return;
	}

	QString path = QFileDialog::getSaveFileName(this,
		tr("Export plot"), QString(),
		tr("PNG image (*.png);;PDF document (*.pdf);;SVG image (*.svg)"));
	if (path.isEmpty()) return;

	if (path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
		m_plot->savePDF(path);
	else if (path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive))
		m_plot->saveSVG(path);
	else {
		if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
			path += QStringLiteral(".png");
		m_plot->savePNG(path);
	}
}


// =====================================================================
// Summary formatting
// =====================================================================

QString AnalysisView::formatSummary(const stats::Model &m) const
{
	QString text;

	QString family_display = QString::fromUtf8(m.family.data(), (int)m.family.size());
	if (m.is_negbin()) family_display = QStringLiteral("Negative binomial");

	text += QStringLiteral("Family: %1 (%2)\n")
		.arg(family_display)
		.arg(QString::fromUtf8(m.link.data(), (int)m.link.size()));
	if (m.is_negbin()) {
		text += QString::asprintf("Theta (overdispersion): %.4f\n", m.theta);
	}
	text += QStringLiteral("Formula: %1\n")
		.arg(QString::fromUtf8(m.formula.data(), (int)m.formula.size()));
	text += QStringLiteral("Observations: %1\n\n").arg(m.nobs);

	const char *stat_label = m.is_gaussian() ? "t value" : "z value";
	text += QStringLiteral("Fixed effects:\n");
	text += QString::asprintf("%-24s %12s %12s %12s %12s\n",
	                           "", "Estimate", "Std.Error", stat_label, "Pr(>|t|)");

	for (intptr_t i = 1; i <= m.nfixed; i++)
	{
		const char *name = (i <= m.coef_names.size()) ? m.coef_names[i].data() : "?";
		char pbuf[16];
		if (m.p[i] < 0.001)
			snprintf(pbuf, sizeof(pbuf), "< 0.001");
		else
			snprintf(pbuf, sizeof(pbuf), "%.4f", m.p[i]);

		const char *stars = "";
		if (m.p[i] < 0.001) stars = " ***";
		else if (m.p[i] < 0.01) stars = " **";
		else if (m.p[i] < 0.05) stars = " *";
		else if (m.p[i] < 0.1) stars = " .";

		text += QString::asprintf("%-24s %12.4f %12.4f %12.3f %12s%s\n",
		                           name, m.beta[i], m.se[i], m.stat[i], pbuf, stars);
	}

	text += QStringLiteral("---\n");
	text += QStringLiteral("Signif. codes: 0 '***' 0.001 '**' 0.01 '*' 0.05 '.' 0.1 ' ' 1\n\n");

	// Random effects
	if (m.has_random_effects())
	{
		text += QStringLiteral("Random effects:\n");
		text += QString::asprintf("%-20s %12s %12s %8s\n",
		                           "Group", "Variance", "Std.Dev.", "Levels");

		for (intptr_t g = 1; g <= m.random_effects.size(); g++)
		{
			auto &re = m.random_effects[g];
			const char *gname = re.group_name.data();

			for (intptr_t t = 1; t <= re.term_names.size(); t++)
			{
				double var = (t <= re.variance.size()) ? re.variance[t] : 0.0;
				double sd = std::sqrt(std::max(var, 0.0));

				// Show group name and level count only on the first row
				if (t == 1) {
					text += QString::asprintf("%-20s %12.4f %12.4f %8ld\n",
					                           gname, var, sd, (long)re.nlevels);
				} else {
					const char *tname = re.term_names[t].data();
					text += QString::asprintf("  %-18s %12.4f %12.4f\n",
					                           tname, var, sd);
				}
			}
		}

		if (m.is_gaussian()) {
			text += QString::asprintf("%-20s %12.4f %12.4f\n",
			                           "Residual", m.rse * m.rse, m.rse);
		}

		text += QStringLiteral("\n");
	}
	else if (m.is_gaussian())
	{
		text += QString::asprintf("Residual standard error: %.4f on %ld degrees of freedom\n",
		                           m.rse, (long)m.df_residual);
		text += QString::asprintf("R-squared: %.4f, Adjusted R-squared: %.4f\n", m.r2, m.adj_r2);
	}

	text += QString::asprintf("AIC: %.1f  BIC: %.1f  logLik: %.1f\n", m.aic, m.bic, m.loglik);

	if (m.niter > 0) {
		if (m.converged)
			text += QString::asprintf("Converged in %d iterations\n", m.niter);
		else
			text += QStringLiteral("WARNING: did not converge after %1 iterations\n").arg(m.niter);
	}

	return text;
}

} // namespace phonometrica
