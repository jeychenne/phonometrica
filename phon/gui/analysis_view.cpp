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
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <numeric>
#include <algorithm>
#include <map>
#include <tuple>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QFont>
#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QGroupBox>
#include <QMainWindow>
#include <QStatusBar>
#include <QProgressBar>
#include <phon/gui/file_dialog.hpp>
#include <QClipboard>
#include <QApplication>
#include <QFile>
#include <QToolBar>
#include <QAction>
#include <QToolButton>
#include <QHeaderView>
#include <QEvent>
#include <QSet>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTextEdit>
#include <boost/math/distributions/normal.hpp>
#include <boost/math/special_functions/trigamma.hpp>
#include <phon/gui/analysis_view.hpp>
#include <phon/gui/font_helpers.hpp>
#include <phon/gui/help_browser.hpp>
#include <phon/analysis/model_comparison.hpp>
#include <phon/application/project.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

// =====================================================================
// Delegate that draws a small check mark on the right side of list items.
// The check mark is shown when Qt::UserRole + 1 data is true.
// =====================================================================

class ColumnCheckDelegate : public QStyledItemDelegate
{
public:

	explicit ColumnCheckDelegate(const QIcon &icon, QObject *parent = nullptr)
		: QStyledItemDelegate(parent), m_icon(icon) { }

	void paint(QPainter *painter, const QStyleOptionViewItem &option,
	           const QModelIndex &index) const override
	{
		QStyledItemDelegate::paint(painter, option, index);

		if (!index.data(Qt::UserRole + 1).toBool()) return;

		int size = 14;
		int margin = 4;
		QRect rect = option.rect;
		QRect icon_rect(rect.right() - size - margin,
		                rect.top() + (rect.height() - size) / 2,
		                size, size);
		m_icon.paint(painter, icon_rect);
	}

private:

	QIcon m_icon;
};

AnalysisView::AnalysisView(Handle<Analysis> analysis, QWidget *parent) :
	View(parent),
	m_analysis(std::move(analysis))
{
	m_analysis->open();
	m_check_icon = QIcon(QStringLiteral(":/icons/check.svg"));
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

void AnalysisView::setActiveTab(int index)
{
	if (m_right_tabs && index >= 0 && index < m_right_tabs->count())
		m_right_tabs->setCurrentIndex(index);
}

bool AnalysisView::save()
{
	bool firstSave = !m_analysis->has_path();

	if (firstSave)
	{
		auto path = getSaveFileName(this, tr("Save analysis as..."),
			tr("Phonometrica analysis (*.phon-analysis)"), QStringLiteral("untitled.phon-analysis"));

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
	if (m_analysis->has_path())
		m_analysis->reload();
	else
		m_analysis->discard_changes();
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

	top_bar->addWidget(new QLabel(tr("Outcome:")));

	m_family_combo = new QComboBox;
	m_family_combo->addItem(tr("Continuous"), QStringLiteral("gaussian"));
	m_family_combo->addItem(tr("Continuous (robust)"), QStringLiteral("student"));
	m_family_combo->addItem(tr("Binary"), QStringLiteral("binomial"));
	m_family_combo->addItem(tr("Count"), QStringLiteral("poisson"));
	m_family_combo->addItem(tr("Overdispersed count"), QStringLiteral("negbin"));
	m_family_combo->addItem(tr("Proportion"), QStringLiteral("beta"));
	m_family_combo->setItemData(0, tr("Gaussian family, identity link — for continuous measurements (F1, duration, VOT…)"), Qt::ToolTipRole);
	m_family_combo->setItemData(1, tr("Student t family, identity link — robust regression for continuous measurements that downweights extreme values"), Qt::ToolTipRole);
	m_family_combo->setItemData(2, tr("Binomial family, logit link — for binary outcomes (present/absent, correct/incorrect)"), Qt::ToolTipRole);
	m_family_combo->setItemData(3, tr("Poisson family, log link — for count data (number of occurrences)"), Qt::ToolTipRole);
	m_family_combo->setItemData(4, tr("Negative binomial family, log link — for count data with extra variability"), Qt::ToolTipRole);
	m_family_combo->setItemData(5, tr("Beta family, logit link — for proportions strictly between 0 and 1"), Qt::ToolTipRole);
	m_family_combo->setCurrentIndex(0);
	top_bar->addWidget(m_family_combo);

	top_bar->addWidget(new QLabel(tr("Estimation:")));

	m_estimation_combo = new QComboBox;
	m_estimation_combo->addItem(tr("Frequentist"), QStringLiteral("frequentist"));
	m_estimation_combo->addItem(tr("Bayesian"), QStringLiteral("bayesian"));
	m_estimation_combo->setItemData(0, tr("Maximum likelihood estimation with Wald-based p-values"), Qt::ToolTipRole);
	m_estimation_combo->setItemData(1, tr("Approximate Bayesian inference with weakly\n"
	                                      "informative default priors. Reports posterior\n"
	                                      "means, credible intervals, and probability\n"
	                                      "of direction (pd)."), Qt::ToolTipRole);

	// Default to the estimation method from global settings.
	int est_idx = 0; // frequentist
	try {
		auto s = Settings::get_string("statistics", "estimation");
		if (s == "bayesian") est_idx = 1;
	} catch (...) {}
	m_estimation_combo->setCurrentIndex(est_idx);

	top_bar->addWidget(m_estimation_combo);

	m_fit_button = new QPushButton(tr("Fit"));
	m_fit_button->setDefault(true);
	top_bar->addWidget(m_fit_button);

	auto *help_button = new QPushButton(QIcon(QStringLiteral(":/icons/circle-help.svg")), QString());
	help_button->setFlat(true);
	help_button->setFixedSize(24, 24);
	help_button->setIconSize(QSize(16, 16));
	help_button->setToolTip(tr("Open documentation for the Analysis view"));
	top_bar->addWidget(help_button);

	main_layout->addLayout(top_bar);

	// ── Bayesian prior panel (collapsible, hidden by default) ───

	auto *prior_header = new QHBoxLayout;
	prior_header->setSpacing(4);

	m_prior_toggle = new QToolButton;
	m_prior_toggle->setArrowType(Qt::RightArrow);
	m_prior_toggle->setCheckable(true);
	m_prior_toggle->setChecked(false);
	m_prior_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	m_prior_toggle->setText(tr("Priors"));
	m_prior_toggle->setToolTip(tr("Show or hide prior customization"));
	m_prior_toggle->setStyleSheet(QStringLiteral("QToolButton { border: none; }"));
	prior_header->addWidget(m_prior_toggle);

	m_prior_defaults_label = new QLabel;
	m_prior_defaults_label->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
	prior_header->addWidget(m_prior_defaults_label, 1);

	auto *prior_header_widget = new QWidget;
	prior_header_widget->setLayout(prior_header);
	prior_header_widget->setVisible(false);
	main_layout->addWidget(prior_header_widget);

	m_prior_panel = new QWidget;
	m_prior_panel->setVisible(false);
	auto *prior_grid = new QGridLayout(m_prior_panel);
	prior_grid->setContentsMargins(24, 0, 0, 4);
	prior_grid->setHorizontalSpacing(6);
	prior_grid->setVerticalSpacing(4);

	// ── Row 0: Fixed effects ──
	int row = 0;
	auto *fixed_label = new QLabel(tr("Fixed effects:"));
	fixed_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	prior_grid->addWidget(fixed_label, row, 0);

	m_prior_fixed_auto = new QCheckBox(tr("Auto"));
	m_prior_fixed_auto->setChecked(true);
	m_prior_fixed_auto->setToolTip(tr("When checked, priors are scaled from the data at fit time.\n"
	                                   "Intercept: N(mean(y), scale), Slopes: N(0, scale)\n"
	                                   "where scale = max(2.5, 2.5 \u00d7 sd(y))."));
	prior_grid->addWidget(m_prior_fixed_auto, row, 1);

	auto *fixed_detail = new QWidget;
	auto *fixed_hl = new QHBoxLayout(fixed_detail);
	fixed_hl->setContentsMargins(0, 0, 0, 0);
	fixed_hl->setSpacing(4);
	fixed_hl->addWidget(new QLabel(tr("N(")));
	m_prior_fixed_mean = new QDoubleSpinBox;
	m_prior_fixed_mean->setRange(-1e6, 1e6);
	m_prior_fixed_mean->setValue(0.0);
	m_prior_fixed_mean->setDecimals(2);
	m_prior_fixed_mean->setToolTip(tr("Prior mean for fixed-effect coefficients"));
	fixed_hl->addWidget(m_prior_fixed_mean);
	fixed_hl->addWidget(new QLabel(tr(",")));
	m_prior_fixed_sd = new QDoubleSpinBox;
	m_prior_fixed_sd->setRange(0.01, 1e6);
	m_prior_fixed_sd->setValue(10.0);
	m_prior_fixed_sd->setDecimals(2);
	m_prior_fixed_sd->setToolTip(tr("Prior standard deviation for fixed-effect coefficients"));
	fixed_hl->addWidget(m_prior_fixed_sd);
	fixed_hl->addWidget(new QLabel(tr(")")));
	fixed_hl->addStretch();
	fixed_detail->setEnabled(false); // auto is on by default
	prior_grid->addWidget(fixed_detail, row, 2);

	// ── Row 1: Variance components ──
	row = 1;
	auto *var_label = new QLabel(tr("Variance components:"));
	var_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	prior_grid->addWidget(var_label, row, 0);

	m_prior_variance_auto = new QCheckBox(tr("Auto"));
	m_prior_variance_auto->setChecked(true);
	m_prior_variance_auto->setToolTip(tr("When checked, the scale parameter is set from the data at fit time:\n"
	                                      "PC(u = max(2.5, 2.5 \u00d7 sd(y)), \u03b1 = 0.05)."));
	prior_grid->addWidget(m_prior_variance_auto, row, 1);

	auto *var_detail = new QWidget;
	auto *var_hl = new QHBoxLayout(var_detail);
	var_hl->setContentsMargins(0, 0, 0, 0);
	var_hl->setSpacing(4);
	m_prior_variance_type = new QComboBox;
	m_prior_variance_type->addItem(tr("PC"), QStringLiteral("pc"));
	m_prior_variance_type->addItem(tr("Half-Cauchy"), QStringLiteral("halfcauchy"));
	m_prior_variance_type->addItem(tr("Half-Normal"), QStringLiteral("halfnormal"));
	m_prior_variance_type->setToolTip(tr("Prior family for random-effect standard deviations"));
	var_hl->addWidget(m_prior_variance_type);
	var_hl->addWidget(new QLabel(tr("scale:")));
	m_prior_variance_scale = new QDoubleSpinBox;
	m_prior_variance_scale->setRange(0.01, 1e6);
	m_prior_variance_scale->setValue(1.0);
	m_prior_variance_scale->setDecimals(2);
	m_prior_variance_scale->setToolTip(tr("Scale parameter (u for PC prior, scale for Half-Cauchy/Half-Normal)"));
	var_hl->addWidget(m_prior_variance_scale);
	var_hl->addStretch();
	var_detail->setEnabled(false); // auto is on by default
	prior_grid->addWidget(var_detail, row, 2);

	// ── Row 2: Residual SD (Gaussian only) ──
	row = 2;
	auto *res_label = new QLabel(tr("Residual SD:"));
	res_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	prior_grid->addWidget(res_label, row, 0);

	m_prior_residual_auto = new QCheckBox(tr("Auto"));
	m_prior_residual_auto->setChecked(true);
	m_prior_residual_auto->setToolTip(tr("When checked, the scale parameter is set from the data at fit time:\n"
	                                      "PC(u = max(2.5, 2.5 \u00d7 sd(y)), \u03b1 = 0.05)."));
	prior_grid->addWidget(m_prior_residual_auto, row, 1);

	auto *res_detail = new QWidget;
	auto *res_hl = new QHBoxLayout(res_detail);
	res_hl->setContentsMargins(0, 0, 0, 0);
	res_hl->setSpacing(4);
	m_prior_residual_type = new QComboBox;
	m_prior_residual_type->addItem(tr("PC"), QStringLiteral("pc"));
	m_prior_residual_type->addItem(tr("Half-Cauchy"), QStringLiteral("halfcauchy"));
	m_prior_residual_type->addItem(tr("Half-Normal"), QStringLiteral("halfnormal"));
	m_prior_residual_type->setToolTip(tr("Prior family for the residual standard deviation"));
	res_hl->addWidget(m_prior_residual_type);
	res_hl->addWidget(new QLabel(tr("scale:")));
	m_prior_residual_scale = new QDoubleSpinBox;
	m_prior_residual_scale->setRange(0.01, 1e6);
	m_prior_residual_scale->setValue(1.0);
	m_prior_residual_scale->setDecimals(2);
	m_prior_residual_scale->setToolTip(tr("Scale parameter for the residual SD prior"));
	res_hl->addWidget(m_prior_residual_scale);
	res_hl->addStretch();
	res_detail->setEnabled(false); // auto is on by default
	prior_grid->addWidget(res_detail, row, 2);

	// Track all residual row widgets for visibility toggling.
	m_prior_residual_widgets = { res_label, m_prior_residual_auto, res_detail };

	// ── Row 3: Reset button ──
	row = 3;
	m_prior_reset_button = new QPushButton(tr("Reset to defaults"));
	m_prior_reset_button->setToolTip(tr("Restore all priors to their default weakly informative values"));
	prior_grid->addWidget(m_prior_reset_button, row, 0, 1, 2);

	// Column stretch: let column 2 (details) expand.
	prior_grid->setColumnStretch(2, 1);

	main_layout->addWidget(m_prior_panel);

	// Prior panel connections
	connect(m_prior_toggle, &QToolButton::toggled, this, [this](bool checked) {
		m_prior_toggle->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
		m_prior_panel->setVisible(checked);
	});
	connect(m_estimation_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, prior_header_widget](int idx) {
		bool bayesian = (idx == 1);
		prior_header_widget->setVisible(bayesian);
		if (!bayesian) {
			m_prior_toggle->setChecked(false);
			m_prior_panel->setVisible(false);
		}
		updatePriorDefaultsLabel();
	});
	connect(m_family_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		updatePriorResidualVisibility();
		updatePriorDefaultsLabel();
	});
	connect(m_prior_reset_button, &QPushButton::clicked, this, &AnalysisView::resetPriorPanel);

	// Auto checkbox toggling: enable/disable the detail widgets.
	connect(m_prior_fixed_auto, &QCheckBox::toggled, this, [this, fixed_detail](bool checked) {
		fixed_detail->setEnabled(!checked);
		updatePriorDefaultsLabel();
	});
	connect(m_prior_variance_auto, &QCheckBox::toggled, this, [this, var_detail](bool checked) {
		var_detail->setEnabled(!checked);
		updatePriorDefaultsLabel();
	});
	connect(m_prior_residual_auto, &QCheckBox::toggled, this, [this, res_detail](bool checked) {
		res_detail->setEnabled(!checked);
		updatePriorDefaultsLabel();
	});

	// Update the summary label when any value changes.
	connect(m_prior_fixed_mean, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updatePriorDefaultsLabel(); });
	connect(m_prior_fixed_sd, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updatePriorDefaultsLabel(); });
	connect(m_prior_variance_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { updatePriorDefaultsLabel(); });
	connect(m_prior_variance_scale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updatePriorDefaultsLabel(); });
	connect(m_prior_residual_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { updatePriorDefaultsLabel(); });
	connect(m_prior_residual_scale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updatePriorDefaultsLabel(); });

	updatePriorResidualVisibility();
	updatePriorDefaultsLabel();

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
	m_column_list->setItemDelegate(new ColumnCheckDelegate(m_check_icon, m_column_list));
	col_layout->addWidget(m_column_list);
	left_layout->addWidget(col_group, 1);

	auto *model_group = new QGroupBox(tr("Models"));
	auto *model_layout = new QVBoxLayout(model_group);
	model_layout->setContentsMargins(4, 4, 4, 4);
	m_model_list = new QListWidget;
	m_model_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_model_list->setToolTip(tr("Select two or more models, then click Compare for pairwise tests"));
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
	auto *save_latex_action = summary_toolbar->addAction(QIcon(":/icons/tex.svg"), tr("Save as LaTeX table..."));

	summary_toolbar->addSeparator();
	m_blup_check = new QCheckBox(tr("Show random effects"));
	m_blup_check->setToolTip(tr("Show conditional modes (BLUPs) for each level of each grouping factor"));
	m_blup_check->setEnabled(false);
	summary_toolbar->addWidget(m_blup_check);

	summary_layout->addWidget(summary_toolbar);

	m_summary = new QPlainTextEdit;
	m_summary->setReadOnly(true);
	m_summary->setFont(defaultMonoFont());
	m_summary->setPlaceholderText(tr("Fit a model to see results here."));
	summary_layout->addWidget(m_summary, 1);

	m_right_tabs->addTab(summary_widget, tr("Summary"));
	m_right_tabs->setTabToolTip(0, tr("Coefficient table and goodness-of-fit statistics for the selected model"));

	// Post-hoc tab
	auto *posthoc_widget = new QWidget;
	auto *posthoc_layout = new QVBoxLayout(posthoc_widget);
	posthoc_layout->setContentsMargins(4, 4, 4, 4);
	posthoc_layout->setSpacing(4);

	auto *posthoc_top = new QHBoxLayout;
	posthoc_top->addWidget(new QLabel(tr("Factor:")));
	m_posthoc_factor_combo = new QComboBox;
	m_posthoc_factor_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	posthoc_top->addWidget(m_posthoc_factor_combo);
	posthoc_top->addSpacing(12);
	posthoc_top->addWidget(new QLabel(tr("By:")));
	m_posthoc_by_combo = new QComboBox;
	m_posthoc_by_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_posthoc_by_combo->setToolTip(tr("Condition pairwise contrasts on each level of this factor.\n"
	                                  "Leave as \"(None)\" to marginalize over all other factors."));
	posthoc_top->addWidget(m_posthoc_by_combo);
	posthoc_top->addSpacing(12);
	posthoc_top->addWidget(new QLabel(tr("Trend:")));
	m_posthoc_trend_combo = new QComboBox;
	m_posthoc_trend_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_posthoc_trend_combo->setToolTip(tr("Select a numeric variable to estimate its slope at each level of the factor (emtrends).\n"
	                                     "Leave as \"(None)\" for estimated marginal means."));
	posthoc_top->addWidget(m_posthoc_trend_combo);
	posthoc_top->addSpacing(12);
	posthoc_top->addWidget(new QLabel(tr("Adjustment:")));
	m_posthoc_adj_combo = new QComboBox;
	m_posthoc_adj_combo->addItem(tr("Holm"), QStringLiteral("holm"));
	m_posthoc_adj_combo->addItem(tr("Bonferroni"), QStringLiteral("bonferroni"));
	m_posthoc_adj_combo->addItem(tr("None"), QStringLiteral("none"));
	posthoc_top->addWidget(m_posthoc_adj_combo);
	posthoc_top->addSpacing(12);
	posthoc_top->addWidget(new QLabel(tr("Confidence:")));
	m_posthoc_conf_spin = new QDoubleSpinBox;
	m_posthoc_conf_spin->setRange(0.80, 0.99);
	m_posthoc_conf_spin->setSingleStep(0.01);
	m_posthoc_conf_spin->setDecimals(2);
	m_posthoc_conf_spin->setValue(0.95);
	m_posthoc_conf_spin->setSuffix(QStringLiteral(" "));
	posthoc_top->addWidget(m_posthoc_conf_spin);
	posthoc_top->addStretch();
	auto *posthoc_copy_button = new QPushButton(QIcon(":/icons/clipboard-copy.svg"), tr("Copy"));
	posthoc_copy_button->setToolTip(tr("Copy post-hoc results to clipboard"));
	posthoc_top->addWidget(posthoc_copy_button);
	auto *posthoc_latex_button = new QPushButton(QIcon(":/icons/tex.svg"), tr("LaTeX"));
	posthoc_latex_button->setToolTip(tr("Copy post-hoc results as LaTeX tables to clipboard"));
	posthoc_top->addWidget(posthoc_latex_button);
	posthoc_layout->addLayout(posthoc_top);

	auto *emm_group = new QGroupBox(tr("Estimated Marginal Means"));
	auto *emm_glayout = new QVBoxLayout(emm_group);
	emm_glayout->setContentsMargins(4, 4, 4, 4);
	m_posthoc_emm_table = new QTableWidget;
	m_posthoc_emm_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_posthoc_emm_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_posthoc_emm_table->setAlternatingRowColors(true);
	m_posthoc_emm_table->verticalHeader()->setVisible(false);
	emm_glayout->addWidget(m_posthoc_emm_table);
	posthoc_layout->addWidget(emm_group, 1);

	auto *contrast_group = new QGroupBox(tr("Pairwise Contrasts"));
	auto *contrast_glayout = new QVBoxLayout(contrast_group);
	contrast_glayout->setContentsMargins(4, 4, 4, 4);
	m_posthoc_contrast_table = new QTableWidget;
	m_posthoc_contrast_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_posthoc_contrast_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_posthoc_contrast_table->setAlternatingRowColors(true);
	m_posthoc_contrast_table->verticalHeader()->setVisible(false);
	contrast_glayout->addWidget(m_posthoc_contrast_table);
	posthoc_layout->addWidget(contrast_group, 1);

	m_right_tabs->addTab(posthoc_widget, tr("Post-hoc"));
	m_right_tabs->setTabToolTip(1, tr("Estimated marginal means and pairwise contrasts for categorical factors"));

	auto *diag_widget = new QWidget;
	auto *diag_layout = new QVBoxLayout(diag_widget);
	diag_layout->setContentsMargins(4, 4, 4, 4);
	diag_layout->setSpacing(4);

	auto *diag_top = new QHBoxLayout;
	diag_top->addWidget(new QLabel(tr("Plot:")));
	m_plot_type_combo = new QComboBox;
	m_plot_type_combo->addItem(tr("Residuals vs Fitted"));
	m_plot_type_combo->addItem(tr("Normal Q-Q"));
	m_plot_type_combo->addItem(tr("Scaled Residuals vs Fitted"));
	m_plot_type_combo->addItem(tr("Scaled Residuals Q-Q"));
	m_plot_type_combo->addItem(tr("Posterior Densities"));
	diag_top->addWidget(m_plot_type_combo);
	diag_top->addStretch();
	auto *export_button = new QPushButton(tr("Export..."));
	diag_top->addWidget(export_button);
	diag_layout->addLayout(diag_top);

	m_plot = new PlotWidget;
	diag_layout->addWidget(m_plot, 1);

	// Test results panel (shown only for scaled residual plots)
	m_test_results_group = new QGroupBox(tr("Residual tests"));
	auto *test_layout = new QVBoxLayout(m_test_results_group);
	test_layout->setContentsMargins(6, 6, 6, 6);
	test_layout->setSpacing(2);
	m_test_results_text = new QTextEdit;
	m_test_results_text->setReadOnly(true);
	m_test_results_text->setFrameShape(QFrame::NoFrame);
	m_test_results_text->setMaximumHeight(60);
	test_layout->addWidget(m_test_results_text);
	m_test_results_group->setVisible(false);
	diag_layout->addWidget(m_test_results_group);

	m_right_tabs->addTab(diag_widget, tr("Diagnostics"));
	m_right_tabs->setTabToolTip(2, tr("Residual plots to check model assumptions"));

	// EDA tab
	auto *eda_widget = new QWidget;
	auto *eda_layout = new QVBoxLayout(eda_widget);
	eda_layout->setContentsMargins(4, 4, 4, 4);
	eda_layout->setSpacing(4);

	// ── EDA toolbar (export actions, matching SpectrumView) ──
	auto *eda_toolbar = new QToolBar;
	eda_toolbar->setIconSize(QSize(16, 16));
	eda_toolbar->setMovable(false);
	eda_toolbar->setContentsMargins(2, 0, 2, 0);
	eda_toolbar->setStyleSheet(QStringLiteral("QToolBar { spacing: 2px; }"));

	auto *eda_save_menu = new QMenu(this);
	eda_save_menu->addAction(tr("Save as PNG..."), this, &AnalysisView::onExportEdaPNG);
	eda_save_menu->addAction(tr("Save as PDF..."), this, &AnalysisView::onExportEdaPDF);
	eda_save_menu->addAction(tr("Save as SVG..."), this, &AnalysisView::onExportEdaSVG);

	auto *eda_save_action = new QAction(QIcon(":/icons/save.svg"), tr("Save as..."), this);
	eda_save_action->setMenu(eda_save_menu);
	eda_toolbar->addAction(eda_save_action);
	if (auto *btn = qobject_cast<QToolButton *>(eda_toolbar->widgetForAction(eda_save_action)))
		btn->setPopupMode(QToolButton::InstantPopup);

	//eda_toolbar->addSeparator();
	QWidget* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	eda_toolbar->addWidget(spacer);

	auto *detach_action = new QAction(QIcon(":/icons/maximize.svg"), tr("Detach plot"), this);
	detach_action->setToolTip(tr("Open the plot in a resizable window"));
	eda_toolbar->addAction(detach_action);

	eda_layout->addWidget(eda_toolbar);

	// ── Plot area ──
	m_eda_plot = new PlotWidget;

	// ── Controls between plot and stats ──
	// Row 1: variable selectors
	auto *eda_vars = new QHBoxLayout;
	eda_vars->setSpacing(6);
	eda_vars->addWidget(new QLabel(tr("X:")));
	m_eda_x_combo = new QComboBox;
	m_eda_x_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	eda_vars->addWidget(m_eda_x_combo);
	eda_vars->addWidget(new QLabel(tr("Y:")));
	m_eda_y_combo = new QComboBox;
	m_eda_y_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	eda_vars->addWidget(m_eda_y_combo);

	// ── Grouped scatter / formant chart controls ──
	m_eda_group_label = new QLabel(tr("Group:"));
	m_eda_group_label->setVisible(false);
	eda_vars->addWidget(m_eda_group_label);
	m_eda_group_combo = new QComboBox;
	m_eda_group_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_eda_group_combo->setToolTip(tr("Color points by a categorical variable"));
	m_eda_group_combo->setVisible(false);
	eda_vars->addWidget(m_eda_group_combo);
	m_eda_pool_label = new QLabel(tr("Pool by:"));
	m_eda_pool_label->setVisible(false);
	eda_vars->addWidget(m_eda_pool_label);
	m_eda_pool_combo = new QComboBox;
	m_eda_pool_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_eda_pool_combo->setToolTip(tr("Average X and Y values within each (group, pool) cell before plotting "
	                                "(e.g. pool by speaker to get one point per speaker per vowel)"));
	m_eda_pool_combo->setVisible(false);
	eda_vars->addWidget(m_eda_pool_combo);
	m_eda_style_label = new QLabel(tr("Style:"));
	m_eda_style_label->setVisible(false);
	eda_vars->addWidget(m_eda_style_label);
	m_eda_style_combo = new QComboBox;
	m_eda_style_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_eda_style_combo->setToolTip(tr("Differentiate ellipse line style and marker shape by a second categorical variable "
	                                 "(e.g. condition). Color still encodes the Group variable."));
	m_eda_style_combo->setVisible(false);
	eda_vars->addWidget(m_eda_style_combo);
	m_eda_label_label = new QLabel(tr("Label:"));
	m_eda_label_label->setVisible(false);
	eda_vars->addWidget(m_eda_label_label);
	m_eda_label_combo = new QComboBox;
	m_eda_label_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_eda_label_combo->setToolTip(tr("Render the value of a variable as text at each data point"));
	m_eda_label_combo->setVisible(false);
	eda_vars->addWidget(m_eda_label_combo);
	eda_vars->addStretch();

	// Row 2: secondary options (bins, checkboxes, smoothing)
	auto *eda_options = new QHBoxLayout;
	eda_options->setContentsMargins(0, 5, 0, 5);
	eda_options->setSpacing(6);
	m_bins_label = new QLabel(tr("Bins:"));
	eda_options->addWidget(m_bins_label);
	m_bins_spin = new QSpinBox;
	m_bins_spin->setRange(0, 200);
	m_bins_spin->setValue(0); // 0 = auto (Sturges' rule)
	m_bins_spin->setSpecialValueText(tr("Auto"));
	m_bins_spin->setToolTip(tr("Number of histogram bins (0 = automatic)"));
	eda_options->addWidget(m_bins_spin);
	m_eda_regline_check = new QCheckBox(tr("Regression line"));
	m_eda_regline_check->setToolTip(tr("Overlay an OLS regression line on the scatter plot"));
	m_eda_regline_check->setVisible(false);
	eda_options->addWidget(m_eda_regline_check);
	m_eda_density_check = new QCheckBox(tr("Density curve"));
	m_eda_density_check->setToolTip(tr("Overlay a kernel density estimate on the histogram"));
	m_eda_density_check->setVisible(false);
	eda_options->addWidget(m_eda_density_check);
	m_eda_bw_label = new QLabel(tr("Smoothing:"));
	m_eda_bw_label->setVisible(false);
	eda_options->addWidget(m_eda_bw_label);
	m_eda_bw_slider = new QSlider(Qt::Horizontal);
	m_eda_bw_slider->setRange(10, 500);
	m_eda_bw_slider->setValue(100);
	m_eda_bw_slider->setFixedWidth(120);
	m_eda_bw_slider->setToolTip(tr("Bandwidth adjustment factor (1.00 = default Silverman rule, "
	                                "lower = more detail, higher = smoother)"));
	m_eda_bw_slider->setVisible(false);
	eda_options->addWidget(m_eda_bw_slider);
	m_eda_bw_spin = new QDoubleSpinBox;
	m_eda_bw_spin->setRange(0.10, 5.00);
	m_eda_bw_spin->setSingleStep(0.05);
	m_eda_bw_spin->setDecimals(2);
	m_eda_bw_spin->setValue(1.00);
	m_eda_bw_spin->setFixedWidth(65);
	m_eda_bw_spin->setVisible(false);
	eda_options->addWidget(m_eda_bw_spin);
	m_eda_mean_check = new QCheckBox(tr("Means"));
	m_eda_mean_check->setToolTip(tr("Show the mean of each group"));
	m_eda_mean_check->setVisible(false);
	eda_options->addWidget(m_eda_mean_check);
	m_eda_ellipse_check = new QCheckBox(tr("Ellipses"));
	m_eda_ellipse_check->setToolTip(tr("Show confidence ellipses around each group"));
	m_eda_ellipse_check->setVisible(false);
	eda_options->addWidget(m_eda_ellipse_check);
	m_eda_ellipse_spin = new QSpinBox;
	m_eda_ellipse_spin->setRange(50, 99);
	m_eda_ellipse_spin->setValue(68);
	m_eda_ellipse_spin->setSuffix(QStringLiteral("%"));
	m_eda_ellipse_spin->setToolTip(tr("Confidence level for the ellipses (68% \u2248 1\u03C3, 95% \u2248 2\u03C3)"));
	m_eda_ellipse_spin->setVisible(false);
	eda_options->addWidget(m_eda_ellipse_spin);
	m_eda_formant_check = new QCheckBox(tr("Formant chart"));
	m_eda_formant_check->setToolTip(tr("Reverse both axes (high values at bottom-left, as in F1\u00D7F2 plots)"));
	m_eda_formant_check->setVisible(false);
	eda_options->addWidget(m_eda_formant_check);
	eda_options->addStretch();

	auto *eda_controls_layout = new QVBoxLayout;
	eda_controls_layout->setContentsMargins(0, 0, 0, 0);
	eda_controls_layout->setSpacing(2);
	eda_controls_layout->addLayout(eda_vars);
	eda_controls_layout->addLayout(eda_options);

	auto *eda_controls_widget = new QWidget;
	eda_controls_widget->setLayout(eda_controls_layout);

	// ── Summary table ──
	m_eda_summary = new QTableWidget;
	m_eda_summary->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_eda_summary->setSelectionMode(QAbstractItemView::NoSelection);
	m_eda_summary->setAlternatingRowColors(true);
	m_eda_summary->verticalHeader()->setVisible(false);
	m_eda_summary->horizontalHeader()->setStretchLastSection(false);

	// ── Assemble: splitter between (plot + controls) and stats ──
	auto *eda_top = new QWidget;
	m_eda_top_layout = new QVBoxLayout(eda_top);
	m_eda_top_layout->setContentsMargins(0, 0, 0, 0);
	m_eda_top_layout->setSpacing(4);
	m_eda_top_layout->addWidget(m_eda_plot, 1);
	m_eda_top_layout->addWidget(eda_controls_widget);

	auto *eda_splitter = new QSplitter(Qt::Vertical);
	eda_splitter->addWidget(eda_top);
	eda_splitter->addWidget(m_eda_summary);
	eda_splitter->setStretchFactor(0, 3);
	eda_splitter->setStretchFactor(1, 1);
	eda_layout->addWidget(eda_splitter, 1);

	m_right_tabs->addTab(eda_widget, tr("EDA"));
	m_right_tabs->setTabToolTip(3, tr("Exploratory Data Analysis: visualize variables before fitting a model"));

	splitter->addWidget(m_right_tabs);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	main_layout->addWidget(splitter, 1);

	// ── Connections ─────────────────────────────────────────────────

	connect(m_fit_button, &QPushButton::clicked, this, &AnalysisView::onFit);
	connect(help_button, &QPushButton::clicked, this, [this]() {
		HelpBrowser::showPage(QStringLiteral("analysis"), this);
	});
	connect(m_formula_edit, &QLineEdit::returnPressed, this, &AnalysisView::onFit);
	connect(m_model_list, &QListWidget::currentRowChanged, this, &AnalysisView::onModelSelected);
	connect(m_model_list, &QListWidget::itemSelectionChanged, this, [this]() {
		auto selected = m_model_list->selectedItems();
		if (selected.size() == 1)
		{
			int row = m_model_list->row(selected.first());
			if (row != m_current_model)
				return; // currentRowChanged will handle it
			// Current row didn't change but selection collapsed to one item
			// (e.g. after a Compare): force a display refresh.
			displayModel(row);
		}
	});
	connect(m_delete_button, &QPushButton::clicked, this, &AnalysisView::onDeleteModel);
	connect(m_compare_button, &QPushButton::clicked, this, &AnalysisView::onCompareModels);
	connect(m_model_list, &QListWidget::itemDoubleClicked, this, &AnalysisView::onRenameModel);
	connect(m_column_list, &QListWidget::itemDoubleClicked, this, &AnalysisView::onColumnDoubleClicked);
	connect(m_column_list, &QListWidget::customContextMenuRequested, this, &AnalysisView::onColumnContextMenu);
	connect(m_plot_type_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onPlotTypeChanged);
	connect(export_button, &QPushButton::clicked, this, &AnalysisView::onExportPlot);
	connect(copy_action, &QAction::triggered, this, &AnalysisView::onCopySummary);
	connect(save_txt_action, &QAction::triggered, this, &AnalysisView::onSaveSummaryText);
	connect(save_latex_action, &QAction::triggered, this, &AnalysisView::onSaveSummaryLatex);
	connect(m_blup_check, &QCheckBox::toggled, this, [this](bool) {
		if (m_current_model >= 0 && m_current_model < m_analysis->model_count()) {
			auto &m = m_analysis->model(m_current_model);
			m_summary->setPlainText(formatSummary(m));
		}
	});
	connect(m_eda_y_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onEdaChanged);
	connect(m_eda_x_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onEdaChanged);
	connect(m_bins_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AnalysisView::onEdaChanged);
	connect(m_eda_regline_check, &QCheckBox::toggled, this, &AnalysisView::onEdaChanged);
	connect(m_eda_density_check, &QCheckBox::toggled, this, &AnalysisView::onEdaChanged);
	connect(m_eda_bw_slider, &QSlider::valueChanged, this, [this](int v) {
		m_eda_bw_spin->blockSignals(true);
		m_eda_bw_spin->setValue(v / 100.0);
		m_eda_bw_spin->blockSignals(false);
		onEdaChanged();
	});
	connect(m_eda_bw_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
		m_eda_bw_slider->blockSignals(true);
		m_eda_bw_slider->setValue(qRound(v * 100.0));
		m_eda_bw_slider->blockSignals(false);
		onEdaChanged();
	});
	connect(m_eda_group_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onEdaChanged);
	connect(m_eda_pool_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onEdaChanged);
	connect(m_eda_style_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onEdaChanged);
	connect(m_eda_label_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onEdaChanged);
	connect(m_eda_mean_check, &QCheckBox::toggled, this, &AnalysisView::onEdaChanged);
	connect(m_eda_ellipse_check, &QCheckBox::toggled, this, &AnalysisView::onEdaChanged);
	connect(m_eda_ellipse_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AnalysisView::onEdaChanged);
	connect(m_eda_formant_check, &QCheckBox::toggled, this, &AnalysisView::onEdaChanged);
	connect(detach_action, &QAction::triggered, this, &AnalysisView::onDetachEdaPlot);
	connect(m_formula_edit, &QLineEdit::textChanged, this, &AnalysisView::updateColumnMarkers);
	connect(m_posthoc_factor_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onPostHocChanged);
	connect(m_posthoc_by_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onPostHocChanged);
	connect(m_posthoc_trend_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onPostHocChanged);
	connect(m_posthoc_adj_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onPostHocChanged);
	connect(m_posthoc_conf_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &AnalysisView::onPostHocChanged);
	connect(posthoc_copy_button, &QPushButton::clicked, this, &AnalysisView::onExportPostHoc);
	connect(posthoc_latex_button, &QPushButton::clicked, this, &AnalysisView::onExportPostHocLatex);
}


void AnalysisView::populateColumns()
{
	m_column_list->clear();
	m_eda_x_combo->clear();
	m_eda_y_combo->clear();
	m_eda_group_combo->clear();
	m_eda_pool_combo->clear();
	m_eda_style_combo->clear();
	m_eda_label_combo->clear();
	m_eda_x_combo->addItem(tr("(None)"));
	m_eda_y_combo->addItem(tr("(None)"));
	m_eda_group_combo->addItem(tr("(None)"));
	m_eda_pool_combo->addItem(tr("(None)"));
	m_eda_style_combo->addItem(tr("(None)"));
	m_eda_label_combo->addItem(tr("(None)"));

	if (!m_analysis->has_source()) return;

	auto names = m_analysis->column_names();
	for (intptr_t i = 1; i <= names.size(); i++) {
		auto qname = QString::fromUtf8(names[i].data(), (int)names[i].size());
		m_column_list->addItem(qname);
		m_eda_x_combo->addItem(qname);
		m_eda_y_combo->addItem(qname);
		m_eda_group_combo->addItem(qname);
		m_eda_pool_combo->addItem(qname);
		m_eda_style_combo->addItem(qname);
		m_eda_label_combo->addItem(qname);
	}

	updateColumnMarkers();
}

QString AnalysisView::modelDisplayLabel(int index) const
{
	auto &m = m_analysis->model(index);
	if (!m.label.empty())
		return QString::fromUtf8(m.label.data(), (int)m.label.size());
	return QStringLiteral("Model %1").arg(index + 1);
}

QString AnalysisView::modelListText(int index) const
{
	auto &m = m_analysis->model(index);
	QString lbl = modelDisplayLabel(index);
	if (m.is_bayesian())
		lbl += QStringLiteral(" (B)");
	lbl += QStringLiteral(": ");
	lbl += QString::fromUtf8(m.formula.data(), (int)m.formula.size());
	return lbl;
}

void AnalysisView::populateModelList()
{
	m_model_list->clear();
	for (int i = 0; i < m_analysis->model_count(); i++)
		m_model_list->addItem(modelListText(i));
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
	m_estimation_combo->setEnabled(enabled);

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

	// ── Set up progress bar ──────────────────────────────────────
	auto *main_win = qobject_cast<QMainWindow *>(window());
	QStatusBar *status = main_win ? main_win->statusBar() : nullptr;
	QProgressBar *progress = main_win ? main_win->findChild<QProgressBar *>() : nullptr;

	if (status) status->showMessage(tr("Fitting model..."));
	QApplication::setOverrideCursor(Qt::WaitCursor);
	QApplication::processEvents();

	// Build the progress callback — passed directly through the call chain.
	// The progress bar only becomes visible on the first callback invocation,
	// so fast models (< 1 frame) never flash it.
	bool progress_shown = false;
	stats::FittingCallback cb = [progress, status, &progress_shown](int current, int maximum) {
		if (progress) {
			if (!progress_shown) {
				progress->setMaximum(maximum);
				progress->setValue(0);
				progress->setVisible(true);
				progress_shown = true;
			}
			progress->setValue(current);
		}
		QApplication::processEvents();
	};

	try
	{
		String formula(formula_text.toUtf8().constData());
		String family(m_family_combo->currentData().toString().toUtf8().constData());
		bool bayesian = (m_estimation_combo->currentData().toString() == QStringLiteral("bayesian"));

		const stats::PriorSpec *priors_ptr = nullptr;
		stats::PriorSpec priors;
		if (bayesian) {
			priors = buildPriorSpec();
			priors_ptr = &priors;
		}

		int index = m_analysis->fit(formula, family, cb, priors_ptr);
		auto &m = m_analysis->model(index);

		QApplication::restoreOverrideCursor();
		if (progress) progress->setVisible(false);
		if (status) status->showMessage(tr("Model fitted"), 2000);

		m_model_list->addItem(modelListText(index));

		// Select only the newly fitted model.
		m_model_list->clearSelection();
		m_model_list->setCurrentRow(m_model_list->count() - 1);

		m_delete_button->setEnabled(true);
		m_compare_button->setEnabled(m_analysis->model_count() >= 2);

		m_right_tabs->setCurrentIndex(0); // switch to Summary tab
		emit titleChanged(label());
	}
	catch (std::exception &e)
	{
		QApplication::restoreOverrideCursor();
		if (progress) progress->setVisible(false);
		if (status) status->clearMessage();

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
		m_scaled_residuals.reset();
		m_scaled_residuals_model = -1;
		displayModel(row);

		// Update the formula bar to match the selected model.
		auto &m = m_analysis->model(row);
		m_formula_edit->setText(QString::fromUtf8(m.formula.data(), (int)m.formula.size()));

		// Update the family combo to match.
		QString family = QString::fromUtf8(m.family.data(), (int)m.family.size());
		int idx = m_family_combo->findData(family);
		if (idx >= 0) m_family_combo->setCurrentIndex(idx);

		// Update the estimation combo to match.
		m_estimation_combo->setCurrentIndex(m.is_bayesian() ? 1 : 0);

		// Restore prior panel widgets from the model's stored priors.
		if (m.is_bayesian())
		{
			auto &pr = m.priors;

			m_prior_fixed_auto->setChecked(pr.fixed_auto);
			m_prior_fixed_mean->setValue(pr.fixed_effects.mean);
			m_prior_fixed_sd->setValue(pr.fixed_effects.sd);

			m_prior_variance_auto->setChecked(pr.variance_auto);
			switch (pr.variance_components.type)
			{
			case stats::VariancePriorType::PC:         m_prior_variance_type->setCurrentIndex(0); break;
			case stats::VariancePriorType::HalfCauchy:  m_prior_variance_type->setCurrentIndex(1); break;
			case stats::VariancePriorType::HalfNormal:  m_prior_variance_type->setCurrentIndex(2); break;
			}
			m_prior_variance_scale->setValue(pr.variance_components.param1);

			m_prior_residual_auto->setChecked(pr.residual_auto);
			switch (pr.residual.type)
			{
			case stats::VariancePriorType::PC:         m_prior_residual_type->setCurrentIndex(0); break;
			case stats::VariancePriorType::HalfCauchy:  m_prior_residual_type->setCurrentIndex(1); break;
			case stats::VariancePriorType::HalfNormal:  m_prior_residual_type->setCurrentIndex(2); break;
			}
			m_prior_residual_scale->setValue(pr.residual.param1);
		}
	}
}

void AnalysisView::displayModel(int index)
{
	auto &m = m_analysis->model(index);
	m_blup_check->setEnabled(m.has_random_effects());
	m_summary->setPlainText(formatSummary(m));
	updateDiagnosticPlot();
	populatePostHocFactors();
}


// =====================================================================
// Delete / Compare
// =====================================================================

void AnalysisView::onDeleteModel()
{
	auto selected = m_model_list->selectedItems();
	if (selected.isEmpty()) return;

	// Collect rows in descending order so removals don't shift subsequent indices.
	QList<int> rows;
	for (auto *item : selected)
		rows.append(m_model_list->row(item));
	std::sort(rows.begin(), rows.end(), std::greater<int>());

	for (int row : rows)
	{
		m_analysis->remove_model(row);
		delete m_model_list->takeItem(row);
	}

	// Re-number remaining models.
	for (int i = 0; i < m_model_list->count(); i++)
		m_model_list->item(i)->setText(modelListText(i));

	m_delete_button->setEnabled(m_analysis->model_count() > 0);
	m_compare_button->setEnabled(m_analysis->model_count() >= 2);
	m_current_model = -1;
	m_scaled_residuals.reset();
	m_scaled_residuals_model = -1;

	if (m_model_list->count() == 0) {
		m_summary->clear();
		m_plot->clear();
		clearTestResults();
		m_blup_check->setEnabled(false);
		m_posthoc_factor_combo->clear();
		m_posthoc_by_combo->clear();
		m_posthoc_trend_combo->clear();
		m_posthoc_emm_table->clear();
		m_posthoc_emm_table->setRowCount(0);
		m_posthoc_emm_table->setColumnCount(0);
		m_posthoc_contrast_table->clear();
		m_posthoc_contrast_table->setRowCount(0);
		m_posthoc_contrast_table->setColumnCount(0);
	}

	emit titleChanged(label());
}

void AnalysisView::onRenameModel(QListWidgetItem *item)
{
	int row = m_model_list->row(item);
	if (row < 0 || row >= m_analysis->model_count()) return;

	auto &m = m_analysis->model(row);
	QString current = m.label.empty()
		? QStringLiteral("Model %1").arg(row + 1)
		: QString::fromUtf8(m.label.data(), (int)m.label.size());

	bool ok = false;
	QString newLabel = QInputDialog::getText(this, tr("Rename model"),
		tr("Label:"), QLineEdit::Normal, current, &ok);
	if (!ok || newLabel.isEmpty()) return;

	auto bytes = newLabel.toUtf8();
	m.label = String(bytes.constData(), bytes.size());
	item->setText(modelListText(row));
	emit titleChanged(label());
}

void AnalysisView::onCompareModels()
{
	if (m_analysis->model_count() < 2) return;

	// Determine which models to compare: if 2+ are selected, use those;
	// otherwise compare all models.
	std::vector<int> indices;
	auto selected = m_model_list->selectedItems();
	if (selected.size() >= 2)
	{
		for (auto *item : selected)
			indices.push_back(m_model_list->row(item));
		std::sort(indices.begin(), indices.end());
	}
	else
	{
		indices.resize(m_analysis->model_count());
		std::iota(indices.begin(), indices.end(), 0);
	}

	if ((int)indices.size() < 2) return;

	// ── Guard: check that all models share the same estimation method ──

	bool has_freq = false, has_bayes = false;
	for (int i : indices)
	{
		if (m_analysis->model(i).is_bayesian())
			has_bayes = true;
		else
			has_freq = true;
	}

	if (has_freq && has_bayes)
	{
		QMessageBox::warning(this, tr("Compare models"),
			tr("The selected models mix frequentist and Bayesian estimation. "
			   "These use different criteria and cannot be directly compared.\n\n"
			   "Please select only frequentist or only Bayesian models."));
		return;
	}

	// ── Bayesian comparison ─────────────────────────────────────────

	if (has_bayes)
	{
		// Collect model pointers.
		std::vector<const stats::Model *> models;
		models.reserve(indices.size());
		for (int i : indices)
			models.push_back(&m_analysis->model(i));

		auto result = stats::bayesian_compare(models, indices);

		// ── Determine layout flags ──────────────────────────────────
		bool any_have_waic = false;
		bool any_have_loo = false;
		for (auto &row : result.rows)
		{
			if (!std::isnan(row.waic))   any_have_waic = true;
			if (!std::isnan(row.loo_ic)) any_have_loo = true;
		}
		bool any_have_ic = any_have_waic || any_have_loo;

		// ── Format output ───────────────────────────────────────────

		QString text;
		text += QStringLiteral("Bayesian model comparison\n");
		text += QStringLiteral("=========================\n\n");

		// ── Model legend ───────────────────────────────────────────
		for (size_t i = 0; i < indices.size(); i++)
		{
			int idx = indices[i];
			auto &m = m_analysis->model(idx);
			text += modelDisplayLabel(idx) + QStringLiteral(": ")
				+ QString::fromUtf8(m.formula.data(), (int)m.formula.size())
				+ QStringLiteral("\n");
		}
		text += QStringLiteral("\n");

		if (!result.has_bayes_factors)
		{
			text += QStringLiteral("Note: Some models do not have a log marginal likelihood\n");
			text += QStringLiteral("(e.g. NB/beta/Student without random effects). Showing\n");
			text += QStringLiteral("log-likelihoods instead; Bayes factors are unavailable.\n\n");
		}

		// ── Summary table ───────────────────────────────────────────

		// Helper to map a result row back to the Analysis model index.
		auto model_idx = [&](int row_i) {
			return indices[result.rows[row_i].original_index];
		};

		// Helper for NaN-safe formatted strings.
		auto fmt_or_dash = [](double v, char f, int prec) -> QString {
			return std::isnan(v) ? QStringLiteral("--") : QString::number(v, f, prec);
		};

		// Compute label column width.
		int lbl_width = 8;
		for (int idx : indices)
		{
			int len = modelDisplayLabel(idx).toUtf8().size();
			if (len + 2 > lbl_width) lbl_width = len + 2;
		}
		std::string lbl_fmt = "%-" + std::to_string(lbl_width) + "s";

		// ── Summary table: always show logLik; optionally log p(y|M), WAIC, LOO-IC ──
		{
			// Build header dynamically.
			QString hdr = QString::asprintf((lbl_fmt + " %6s %12s").c_str(), "", "npar", "logLik");
			QString sep = QString(lbl_width + 6 + 12 + 2, QChar('-'));

			if (result.has_bayes_factors) {
				hdr += QString::asprintf(" %14s", "log p(y|M)");
				sep += QStringLiteral("---------------");
			}
			if (any_have_waic) {
				hdr += QString::asprintf(" %10s %8s", "WAIC", "p_WAIC");
				sep += QStringLiteral("-------------------");
			}
			if (any_have_loo) {
				hdr += QString::asprintf(" %10s %8s", "LOO-IC", "p_LOO");
				sep += QStringLiteral("-------------------");
			}
			text += hdr + QStringLiteral("\n") + sep + QStringLiteral("\n");

			std::string row_base = lbl_fmt + " %6ld %12.1f";

			for (size_t i = 0; i < result.rows.size(); i++)
			{
				auto &row = result.rows[i];
				QString mlabel = modelDisplayLabel(model_idx((int)i));

				QString line = QString::asprintf(row_base.c_str(),
				                                  mlabel.toUtf8().constData(),
				                                  (long)row.npar, row.loglik);
				if (result.has_bayes_factors)
					line += QString::asprintf(" %14.2f", row.log_marginal);
				if (any_have_waic)
					line += QString::asprintf(" %10s %8s",
					                           fmt_or_dash(row.waic, 'f', 1).toUtf8().constData(),
					                           fmt_or_dash(row.p_waic, 'f', 1).toUtf8().constData());
				if (any_have_loo)
					line += QString::asprintf(" %10s %8s",
					                           fmt_or_dash(row.loo_ic, 'f', 1).toUtf8().constData(),
					                           fmt_or_dash(row.p_loo, 'f', 1).toUtf8().constData());
				text += line + QStringLiteral("\n");
			}
		}

		// ── Pairwise log Bayes factors ──────────────────────────────

		if (result.has_bayes_factors)
		{
			text += QStringLiteral("\n\nPairwise log Bayes factors\n");
			text += QStringLiteral("==========================\n\n");
			text += QStringLiteral("log BF_ij = log p(y|M_i) - log p(y|M_j)\n");
			text += QStringLiteral("Positive values favour model i.\n\n");

			// Compute pair label width.
			int pair_width = 18;
			for (auto &pair : result.pairs)
			{
				int idx_a = model_idx(pair.index_a);
				int idx_b = model_idx(pair.index_b);
				int len = (modelDisplayLabel(idx_a) + " vs " + modelDisplayLabel(idx_b)).toUtf8().size();
				if (len + 2 > pair_width) pair_width = len + 2;
			}
			std::string pair_fmt = "%-" + std::to_string(pair_width) + "s";

			text += QString::asprintf((pair_fmt + " %12s   %s\n").c_str(), "", "log BF", "Evidence (Kass & Raftery 1995)");
			text += QString(pair_width + 12 + 3 + 35, QChar('-')) + QStringLiteral("\n");

			auto bayes_label = [](double log_bf) -> const char * {
				double twice = 2.0 * std::abs(log_bf);
				if (twice < 2)  return "negligible";
				if (twice < 6)  return "positive";
				if (twice < 10) return "strong";
				return "very strong";
			};

			for (auto &pair : result.pairs)
			{
				if (std::isnan(pair.log_bf)) continue;

				int idx_a = model_idx(pair.index_a);
				int idx_b = model_idx(pair.index_b);
				QString lbl_a = modelDisplayLabel(idx_a);
				QString lbl_b = modelDisplayLabel(idx_b);
				QString plabel = lbl_a + QStringLiteral(" vs ") + lbl_b;

				const char *label = bayes_label(pair.log_bf);
				QString direction = (pair.log_bf > 0)
					? QStringLiteral("favours ") + lbl_a
					: (pair.log_bf < 0)
						? QStringLiteral("favours ") + lbl_b
						: QStringLiteral("--");

				text += QString::asprintf((pair_fmt + " %12.2f   %s (%s)\n").c_str(),
				                           plabel.toUtf8().constData(),
				                           pair.log_bf,
				                           label,
				                           direction.toUtf8().constData());
			}
		}

		// ── Pairwise information criteria differences ────────────────

		if (any_have_ic && result.pairs.size() >= 1)
		{
			text += QStringLiteral("\n\nPairwise information criteria\n");
			text += QStringLiteral("=============================\n\n");
			text += QStringLiteral("Negative values favour model i.\n\n");

			// Compute pair label width.
			int pw = 18;
			for (auto &pair : result.pairs)
			{
				int idx_a = model_idx(pair.index_a);
				int idx_b = model_idx(pair.index_b);
				int len = (modelDisplayLabel(idx_a) + " vs " + modelDisplayLabel(idx_b)).toUtf8().size();
				if (len + 2 > pw) pw = len + 2;
			}
			std::string pw_fmt = "%-" + std::to_string(pw) + "s";

			// Build header based on available ICs.
			QString hdr = QString::asprintf(pw_fmt.c_str(), "");
			QString sep;
			if (any_have_waic) {
				hdr += QString::asprintf(" %10s %10s", "\u0394WAIC", "SE");
				sep += QStringLiteral("---------------------");
			}
			if (any_have_loo) {
				hdr += QString::asprintf(" %10s %10s", "\u0394LOO-IC", "SE");
				sep += QStringLiteral("---------------------");
			}
			text += hdr + QStringLiteral("\n");
			text += QString(pw, QChar('-')) + sep + QStringLiteral("\n");

			for (auto &pair : result.pairs)
			{
				bool has_waic_pair = !std::isnan(pair.delta_waic);
				bool has_loo_pair = !std::isnan(pair.delta_loo);
				if (!has_waic_pair && !has_loo_pair) continue;

				int idx_a = model_idx(pair.index_a);
				int idx_b = model_idx(pair.index_b);
				QString plabel = modelDisplayLabel(idx_a) + QStringLiteral(" vs ") + modelDisplayLabel(idx_b);

				QString line = QString::asprintf(pw_fmt.c_str(), plabel.toUtf8().constData());
				if (any_have_waic)
				{
					if (has_waic_pair)
						line += QString::asprintf(" %10.1f %10.1f", pair.delta_waic, pair.se_diff);
					else
						line += QString::asprintf(" %10s %10s", "--", "--");
				}
				if (any_have_loo)
				{
					if (has_loo_pair)
						line += QString::asprintf(" %10.1f %10.1f", pair.delta_loo, pair.se_loo_diff);
					else
						line += QString::asprintf(" %10s %10s", "--", "--");
				}
				text += line + QStringLiteral("\n");
			}
		}

		// ── Pareto k diagnostic ─────────────────────────────────────

		if (any_have_loo)
		{
			text += QStringLiteral("\n\nPareto k diagnostic\n");
			text += QStringLiteral("===================\n\n");

			for (size_t i = 0; i < result.rows.size(); i++)
			{
				auto &m = m_analysis->model(model_idx((int)i));
				if (m.pareto_k.empty()) continue;

				int n_good = 0, n_ok = 0, n_bad = 0, n_verybad = 0;
				for (intptr_t j = 1; j <= m.pareto_k.size(); j++)
				{
					double k = m.pareto_k[j];
					if (k < 0.5)      n_good++;
					else if (k < 0.7) n_ok++;
					else if (k < 1.0) n_bad++;
					else              n_verybad++;
				}
				intptr_t total = m.pareto_k.size();

				text += modelDisplayLabel(model_idx((int)i)) + QStringLiteral(": ");

				if (n_bad == 0 && n_verybad == 0 && n_ok == 0)
				{
					text += QStringLiteral("all k < 0.5 (good)\n");
				}
				else
				{
					auto pct = [&](int count) {
						return QString::number(100.0 * count / total, 'f', 1) + QStringLiteral("%");
					};
					QStringList parts;
					if (n_good > 0)    parts << pct(n_good) + QStringLiteral(" k < 0.5 (good)");
					if (n_ok > 0)      parts << pct(n_ok) + QStringLiteral(" k 0.5\u20130.7 (ok)");
					if (n_bad > 0)     parts << pct(n_bad) + QStringLiteral(" k 0.7\u20131.0 (bad)");
					if (n_verybad > 0) parts << pct(n_verybad) + QStringLiteral(" k > 1.0 (very bad)");
					text += parts.join(QStringLiteral(", ")) + QStringLiteral("\n");
				}
			}

			text += QStringLiteral("\nk < 0.7: LOO-IC is reliable.  k > 0.7: consider WAIC or refit.\n");
		}

		// ── Warnings ────────────────────────────────────────────────

		if (result.has_warnings())
		{
			text += QStringLiteral("\n\nWarnings:\n");
			for (auto &w : result.warnings)
				text += QStringLiteral("- ") + QString::fromUtf8(w.data(), (int)w.size()) + QStringLiteral("\n");
		}

		m_summary->setPlainText(text);
		m_right_tabs->setCurrentIndex(0);
		return;
	}

	// ── Frequentist comparison (existing LRT machinery) ─────────────

	// Collect model pointers.
	std::vector<const stats::Model *> models;
	models.reserve(indices.size());
	for (int i : indices)
		models.push_back(&m_analysis->model(i));

	auto result = stats::anova_compare(models, indices);

	// ── Format the output ────────────────────────────────────────────

	auto format_p = [](double p) -> QString {
		if (std::isnan(p)) return QString();
		if (p < 2.2e-16) return QStringLiteral("< 2.2e-16");
		if (p < 0.001)   return QString::asprintf("%.2e", p);
		return QString::number(p, 'f', 4);
	};

	auto signif_stars = [](double p) -> const char * {
		if (std::isnan(p)) return "";
		if (p < 0.001) return " ***";
		if (p < 0.01)  return " **";
		if (p < 0.05)  return " *";
		if (p < 0.1)   return " .";
		return "";
	};

	QString text;

	// ── Model legend ───────────────────────────────────────────────
	text += QStringLiteral("Model comparison\n");
	text += QStringLiteral("================\n\n");

	for (int idx : indices)
	{
		auto &m = m_analysis->model(idx);
		text += modelDisplayLabel(idx) + QStringLiteral(": ")
			+ QString::fromUtf8(m.formula.data(), (int)m.formula.size())
			+ QStringLiteral("\n");
	}
	text += QStringLiteral("\n");

	// ── Information criteria table ───────────────────────────────────

	// Compute label column width.
	int flbl_width = 8;
	for (int idx : indices)
	{
		int len = modelDisplayLabel(idx).toUtf8().size();
		if (len + 2 > flbl_width) flbl_width = len + 2;
	}
	std::string flbl_fmt = "%-" + std::to_string(flbl_width) + "s";

	text += QString::asprintf((flbl_fmt + " %6s %10s %10s %12s\n").c_str(),
	                           "", "npar", "AIC", "BIC", "logLik");
	text += QString(flbl_width + 6 + 10 + 10 + 12 + 4, QChar('-')) + QStringLiteral("\n");

	std::string frow_fmt = flbl_fmt + " %6ld %10.1f %10.1f %12.1f\n";

	for (auto &row : result.rows)
	{
		int midx = indices[row.original_index];
		QString mlabel = modelDisplayLabel(midx);

		text += QString::asprintf(frow_fmt.c_str(),
		                           mlabel.toUtf8().constData(),
		                           (long)row.npar,
		                           row.aic, row.bic, row.loglik);
	}

	// ── Pairwise likelihood ratio tests ──────────────────────────────

	text += QStringLiteral("\n\nPairwise likelihood ratio tests\n");
	text += QStringLiteral("===============================\n\n");

	// Compute pair label width.
	int fpw = 18;
	for (auto &pair : result.pairs)
	{
		int idx_a = indices[result.rows[pair.index_a].original_index];
		int idx_b = indices[result.rows[pair.index_b].original_index];
		int len = (modelDisplayLabel(idx_a) + " vs " + modelDisplayLabel(idx_b)).toUtf8().size();
		if (len + 2 > fpw) fpw = len + 2;
	}
	std::string fpw_fmt = "%-" + std::to_string(fpw) + "s";

	text += QString::asprintf((fpw_fmt + " %6s %10s %12s\n").c_str(),
	                           "", "Df", "Chisq", "Pr(>Chisq)");
	text += QString(fpw + 6 + 10 + 12 + 3, QChar('-')) + QStringLiteral("\n");

	for (auto &pair : result.pairs)
	{
		int idx_a = indices[result.rows[pair.index_a].original_index];
		int idx_b = indices[result.rows[pair.index_b].original_index];
		QString plabel = modelDisplayLabel(idx_a) + QStringLiteral(" vs ") + modelDisplayLabel(idx_b);

		if (pair.df_diff > 0)
		{
			QString pstr = format_p(pair.p_value);
			const char *stars = signif_stars(pair.p_value);

			text += QString::asprintf((fpw_fmt + " %6ld %10.4f   %-12s%s\n").c_str(),
			                           plabel.toUtf8().constData(),
			                           (long)pair.df_diff,
			                           pair.chisq,
			                           pstr.toUtf8().constData(),
			                           stars);
		}
		else
		{
			// Same nparams — LRT undefined.
			text += QString::asprintf((fpw_fmt + " %6s %10s   %s\n").c_str(),
			                           plabel.toUtf8().constData(),
			                           "--", "--", "(same complexity)");
		}
	}

	text += QStringLiteral("---\nSignif. codes: 0 '***' 0.001 '**' 0.01 '*' 0.05 '.' 0.1 ' ' 1\n");

	// ── Warnings (if any) ────────────────────────────────────────────

	if (result.has_warnings())
	{
		text += QStringLiteral("\nWARNING\n");
		for (auto &w : result.warnings)
		{
			text += QStringLiteral("  \u2022 "); // bullet
			text += QString::fromUtf8(w.data(), (int)w.size());
			text += QStringLiteral("\n");
		}
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

	// Try to parse the current formula for structural operations.
	auto parsed = tryParseFormula();

	// Check whether this variable is already used in the formula.
	bool in_formula = false;
	if (parsed)
	{
		auto vars = parsed->all_variables();
		auto name_s = String(name.toUtf8().constData());
		for (intptr_t i = 1; i <= vars.size(); i++) {
			if (vars[i] == name_s) { in_formula = true; break; }
		}
	}

	QMenu menu;

	// ── Response and remove ──────────────────────────────────────────
	menu.addAction(tr("Set as response"));
	auto *remove_action = menu.addAction(tr("Remove from formula"));
	remove_action->setEnabled(in_formula);

	menu.addSeparator();

	// ── Fixed effects ────────────────────────────────────────────────
	menu.addAction(tr("Add as predictor"));

	// Interaction submenus: list current fixed terms.
	auto *interaction_menu = menu.addMenu(tr("Add with main effects and interaction"));
	auto *interaction_only_menu = menu.addMenu(tr("Add interaction only with..."));

	if (parsed && !parsed->fixed.empty())
	{
		for (intptr_t i = 1; i <= parsed->fixed.size(); i++)
		{
			auto term_s = parsed->fixed[i].to_string();
			auto term_q = QString::fromUtf8(term_s.data(), (int)term_s.size());
			interaction_menu->addAction(term_q);
			interaction_only_menu->addAction(term_q);
		}
	}
	else
	{
		auto *placeholder1 = interaction_menu->addAction(tr("(no fixed effects in formula)"));
		placeholder1->setEnabled(false);
		auto *placeholder2 = interaction_only_menu->addAction(tr("(no fixed effects in formula)"));
		placeholder2->setEnabled(false);
	}

	// Smooth term: submenu for numeric columns with k choices and by-variable.
	QMenu *smooth_menu = nullptr;
	QMenu *smooth_by_menu = nullptr;
	if (m_analysis->has_source() && isColumnNumeric(String(name.toUtf8().constData())))
	{
		smooth_menu = menu.addMenu(tr("Add as smooth"));
		smooth_menu->addAction(QStringLiteral("s(%1)").arg(name))->setData(10);
		smooth_menu->addAction(QStringLiteral("s(%1, k=5)").arg(name))->setData(5);
		smooth_menu->addAction(QStringLiteral("s(%1, k=15)").arg(name))->setData(15);
		smooth_menu->addAction(QStringLiteral("s(%1, k=20)").arg(name))->setData(20);
		smooth_menu->addSeparator();
		smooth_menu->addAction(tr("Custom k..."))->setData(-1);

		// "with by variable" submenu: lists categorical columns.
		smooth_by_menu = smooth_menu->addMenu(tr("with by variable"));
		auto name_s = String(name.toUtf8().constData());
		auto *dt = m_analysis->data();
		if (dt)
		{
			intptr_t nc = dt->column_count();
			bool has_categorical = false;
			for (intptr_t j = 1; j <= nc; j++)
			{
				auto col_name = dt->get_header(j);
				// Skip the numeric column itself and skip numeric columns.
				if (col_name == name_s) continue;
				if (isColumnNumeric(col_name)) continue;

				auto col_q = QString::fromUtf8(col_name.data(), (int)col_name.size());
				auto *action = smooth_by_menu->addAction(
					QStringLiteral("s(%1, by=%2)").arg(name, col_q));
				action->setData(col_q);
				has_categorical = true;
			}
			if (!has_categorical) {
				auto *placeholder = smooth_by_menu->addAction(tr("(no categorical variables)"));
				placeholder->setEnabled(false);
			}
		}
	}

	menu.addSeparator();

	// ── Random effects ───────────────────────────────────────────────
	menu.addAction(tr("Add as grouping factor"));

	// Penalized random intercept (GAM): available for categorical columns.
	QAction *re_smooth_action = nullptr;
	QMenu *re_slope_menu = nullptr;
	if (m_analysis->has_source() && !isColumnNumeric(String(name.toUtf8().constData())))
	{
		re_smooth_action = menu.addAction(QStringLiteral("Add smooth for grouping: s(%1, bs=re)").arg(name));

		// Penalized random slope (GAM): submenu listing numeric columns as slope variables.
		re_slope_menu = menu.addMenu(tr("Add smooth for random slope in %1").arg(name));
		auto *dt = m_analysis->data();
		if (dt)
		{
			auto name_s = String(name.toUtf8().constData());
			intptr_t nc = dt->column_count();
			bool has_numeric = false;
			for (intptr_t j = 1; j <= nc; j++)
			{
				auto col_name = dt->get_header(j);
				if (col_name == name_s) continue;
				if (!isColumnNumeric(col_name)) continue;

				auto col_q = QString::fromUtf8(col_name.data(), (int)col_name.size());
				auto *action = re_slope_menu->addAction(
					QStringLiteral("s(%1, by=%2, bs=re)").arg(name, col_q));
				action->setData(col_q);
				has_numeric = true;
			}
			if (!has_numeric) {
				auto *placeholder = re_slope_menu->addAction(tr("(no numeric variables)"));
				placeholder->setEnabled(false);
			}
		}
	}

	auto *corr_slope_menu = menu.addMenu(tr("Add correlated slope in..."));
	auto *indep_slope_menu = menu.addMenu(tr("Add independent slope in..."));

	if (parsed && !parsed->random.empty())
	{
		auto name_s = String(name.toUtf8().constData());

		for (intptr_t i = 1; i <= parsed->random.size(); i++)
		{
			auto &rt = parsed->random[i];
			auto group_q = QString::fromUtf8(rt.group.data(), (int)rt.group.size());

			// Correlated slope: check if this variable is already a slope in this term.
			auto *corr_action = corr_slope_menu->addAction(group_q);
			bool already_slope = false;
			for (intptr_t s = 1; s <= rt.slopes.size(); s++) {
				if (rt.slopes[s] == name_s) { already_slope = true; break; }
			}
			corr_action->setEnabled(!already_slope);
			corr_action->setData(group_q);

			// Independent slope: always available.
			auto *indep_action = indep_slope_menu->addAction(group_q);
			indep_action->setData(group_q);
		}
	}
	else
	{
		auto *placeholder3 = corr_slope_menu->addAction(tr("(no grouping factors in formula)"));
		placeholder3->setEnabled(false);
		auto *placeholder4 = indep_slope_menu->addAction(tr("(no grouping factors in formula)"));
		placeholder4->setEnabled(false);
	}

	menu.addSeparator();

	// ── Reference level ──────────────────────────────────────────────

	QMenu *ref_menu = nullptr;
	if (m_analysis->has_source() && !isColumnNumeric(String(name.toUtf8().constData())))
	{
		ref_menu = menu.addMenu(tr("Set reference level..."));

		// Collect unique levels from the source data.
		auto *dt = m_analysis->data();
		intptr_t nc = dt->column_count();
		intptr_t col = 0;
		auto name_s = String(name.toUtf8().constData());
		for (intptr_t j = 1; j <= nc; j++) {
			if (dt->get_header(j) == name_s) { col = j; break; }
		}

		String current_ref = m_analysis->reference_level(name_s);

		// "Default (alphabetical)" option to clear the override.
		auto *default_action = ref_menu->addAction(tr("Default (alphabetical)"));
		default_action->setCheckable(true);
		default_action->setChecked(current_ref.empty());
		default_action->setData(QStringLiteral("__default__"));
		ref_menu->addSeparator();

		if (col > 0)
		{
			// Collect sorted unique levels.
			std::map<std::string, bool> seen;
			intptr_t nr = dt->row_count();
			for (intptr_t r = 1; r <= nr; r++)
			{
				auto cell = dt->get_cell(r, col);
				if (!cell.empty()) {
					seen[std::string(cell.data(), cell.size())] = true;
				}
			}

			for (auto &kv : seen)
			{
				auto qs = QString::fromUtf8(kv.first.c_str(), (int)kv.first.size());
				auto *action = ref_menu->addAction(qs);
				action->setCheckable(true);
				auto level_s = String(kv.first);
				action->setChecked(level_s == current_ref);
				action->setData(qs);
			}
		}
	}

	// ── Execute ──────────────────────────────────────────────────────

	auto *chosen = menu.exec(m_column_list->mapToGlobal(pos));
	if (!chosen) return;

	QString action_text = chosen->text();

	if (action_text == tr("Set as response")) {
		setResponse(name);
	}
	else if (chosen == remove_action) {
		removeFromFormula(name);
	}
	else if (action_text == tr("Add as predictor")) {
		addPredictor(name);
	}
	else if (smooth_by_menu && chosen->parent() == smooth_by_menu) {
		QString by_var = chosen->data().toString();
		addSmoothTerm(name, 10, by_var);
	}
	else if (smooth_menu && chosen->parent() == smooth_menu) {
		int k_val = chosen->data().toInt();
		if (k_val == -1) {
			// Custom k: prompt user.
			bool ok = false;
			int k_custom = QInputDialog::getInt(this, tr("Basis dimension"),
				tr("Number of knots (k) for s(%1):").arg(name),
				10, 3, 100, 1, &ok);
			if (ok) {
				addSmoothTerm(name, k_custom);
			}
		} else {
			addSmoothTerm(name, k_val);
		}
	}
	else if (action_text == tr("Add as grouping factor")) {
		addRandomIntercept(name);
	}
	else if (re_smooth_action && chosen == re_smooth_action) {
		// Insert s(name, bs=re) — penalized random intercept for GAM.
		QString quoted = quoteIfNeeded(name);
		QString term = QStringLiteral("s(") + quoted + QStringLiteral(", bs=re)");
		QString text = m_formula_edit->text().trimmed();
		if (text.isEmpty()) {
			m_formula_edit->setText(QStringLiteral("~ ") + term);
		} else if (!text.contains('~')) {
			m_formula_edit->setText(text + QStringLiteral(" ~ ") + term);
		} else {
			QString rhs = text.mid(text.indexOf('~') + 1).trimmed();
			if (rhs.isEmpty() || rhs == QStringLiteral("1")) {
				QString lhs = text.left(text.indexOf('~') + 1);
				m_formula_edit->setText(lhs + QStringLiteral(" ") + term);
			} else {
				m_formula_edit->setText(text + QStringLiteral(" + ") + term);
			}
		}
		m_formula_edit->setFocus();
		m_formula_edit->setCursorPosition(m_formula_edit->text().length());
	}
	else if (re_slope_menu && chosen->parent() == re_slope_menu) {
		// Insert s(name, by=slope_var, bs=re) — penalized random slope for GAM.
		QString quoted = quoteIfNeeded(name);
		QString by_var = quoteIfNeeded(chosen->data().toString());
		QString term = QStringLiteral("s(") + quoted + QStringLiteral(", by=") + by_var + QStringLiteral(", bs=re)");
		QString text = m_formula_edit->text().trimmed();
		if (text.isEmpty()) {
			m_formula_edit->setText(QStringLiteral("~ ") + term);
		} else if (!text.contains('~')) {
			m_formula_edit->setText(text + QStringLiteral(" ~ ") + term);
		} else {
			QString rhs = text.mid(text.indexOf('~') + 1).trimmed();
			if (rhs.isEmpty() || rhs == QStringLiteral("1")) {
				QString lhs = text.left(text.indexOf('~') + 1);
				m_formula_edit->setText(lhs + QStringLiteral(" ") + term);
			} else {
				m_formula_edit->setText(text + QStringLiteral(" + ") + term);
			}
		}
		m_formula_edit->setFocus();
		m_formula_edit->setCursorPosition(m_formula_edit->text().length());
	}
	else if (chosen->parent() == interaction_menu) {
		addInteraction(name, chosen->text(), true);
	}
	else if (chosen->parent() == interaction_only_menu) {
		addInteraction(name, chosen->text(), false);
	}
	else if (chosen->parent() == corr_slope_menu) {
		addRandomSlope(name, chosen->data().toString(), true);
	}
	else if (chosen->parent() == indep_slope_menu) {
		addRandomSlope(name, chosen->data().toString(), false);
	}
	else if (ref_menu && chosen->parent() == ref_menu) {
		auto name_s = String(name.toUtf8().constData());
		if (chosen->data().toString() == QStringLiteral("__default__"))
			m_analysis->clear_reference_level(name_s);
		else
			m_analysis->set_reference_level(name_s, String(chosen->data().toString().toUtf8().constData()));
	}
}

void AnalysisView::setResponse(const QString &name)
{
	QString quoted = quoteIfNeeded(name);
	QString text = m_formula_edit->text().trimmed();
	int tilde = text.indexOf('~');
	if (tilde >= 0) {
		// Keep the existing RHS, but if it's empty put an explicit intercept.
		QString rhs = text.mid(tilde + 1).trimmed();
		if (rhs.isEmpty())
			m_formula_edit->setText(quoted + QStringLiteral(" ~ 1"));
		else
			m_formula_edit->setText(quoted + QStringLiteral(" ~ ") + rhs);
	} else {
		m_formula_edit->setText(quoted + QStringLiteral(" ~ 1"));
	}
	m_formula_edit->setFocus();
	m_formula_edit->setCursorPosition(m_formula_edit->text().length());
}

void AnalysisView::addPredictor(const QString &name)
{
	QString quoted = quoteIfNeeded(name);
	QString text = m_formula_edit->text().trimmed();
	if (text.isEmpty()) {
		// No formula yet: set as response with explicit intercept.
		m_formula_edit->setText(quoted + QStringLiteral(" ~ 1"));
	} else if (!text.contains('~')) {
		m_formula_edit->setText(text + QStringLiteral(" ~ ") + quoted);
	} else {
		QString rhs = text.mid(text.indexOf('~') + 1).trimmed();
		if (rhs.isEmpty() || rhs == QStringLiteral("1")) {
			// Replace intercept-only placeholder with the predictor.
			QString lhs = text.left(text.indexOf('~') + 1);
			m_formula_edit->setText(lhs + QStringLiteral(" ") + quoted);
		} else {
			m_formula_edit->setText(text + QStringLiteral(" + ") + quoted);
		}
	}
	m_formula_edit->setFocus();
	m_formula_edit->setCursorPosition(m_formula_edit->text().length());
}

void AnalysisView::addSmoothTerm(const QString &name, int k, const QString &by)
{
	QString quoted = quoteIfNeeded(name);
	QString term = QStringLiteral("s(") + quoted;
	if (!by.isEmpty()) {
		term += QStringLiteral(", by=") + quoteIfNeeded(by);
	}
	if (k != 10) {
		term += QStringLiteral(", k=%1").arg(k);
	}
	term += QStringLiteral(")");

	QString text = m_formula_edit->text().trimmed();
	if (text.isEmpty()) {
		m_formula_edit->setText(QStringLiteral("~ ") + term);
	} else if (!text.contains('~')) {
		m_formula_edit->setText(text + QStringLiteral(" ~ ") + term);
	} else {
		QString rhs = text.mid(text.indexOf('~') + 1).trimmed();
		if (rhs.isEmpty() || rhs == QStringLiteral("1")) {
			QString lhs = text.left(text.indexOf('~') + 1);
			m_formula_edit->setText(lhs + QStringLiteral(" ") + term);
		} else {
			m_formula_edit->setText(text + QStringLiteral(" + ") + term);
		}
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
		if (rhs.isEmpty() || rhs == QStringLiteral("1")) {
			QString lhs = text.left(text.indexOf('~') + 1);
			m_formula_edit->setText(lhs + QStringLiteral(" ") + term);
		} else {
			m_formula_edit->setText(text + QStringLiteral(" + ") + term);
		}
	}

	m_formula_edit->setFocus();
	m_formula_edit->setCursorPosition(m_formula_edit->text().length());
}

void AnalysisView::addInteraction(const QString &name, const QString &other, bool withMainEffects)
{
	if (withMainEffects)
	{
		// Parse-modify-regenerate: produce "a * b" by ensuring both main effects
		// and the interaction are present, then collapsing to * notation.
		auto parsed = tryParseFormula();
		if (!parsed) {
			// No valid formula yet — just write "name * other".
			QString text = m_formula_edit->text().trimmed();
			QString term = quoteIfNeeded(name) + QStringLiteral(" * ") + quoteIfNeeded(other);
			if (text.isEmpty() || !text.contains('~'))
				m_formula_edit->setText(text.isEmpty() ? (QStringLiteral("~ ") + term) : (text + QStringLiteral(" ~ ") + term));
			else {
				QString rhs = text.mid(text.indexOf('~') + 1).trimmed();
				m_formula_edit->setText(rhs.isEmpty() ? (text + QStringLiteral(" ") + term) : (text + QStringLiteral(" + ") + term));
			}
			m_formula_edit->setFocus();
			m_formula_edit->setCursorPosition(m_formula_edit->text().length());
			return;
		}

		auto name_s = String(name.toUtf8().constData());
		auto other_s = String(other.toUtf8().constData());

		// Remove existing main effects of both variables (if present),
		// since * will re-introduce them.
		auto &fixed = parsed->fixed;
		for (intptr_t i = fixed.size(); i >= 1; i--)
		{
			if (fixed[i].variables.size() == 1 &&
			    (fixed[i].variables[1] == name_s || fixed[i].variables[1] == other_s))
			{
				fixed.remove_at(i);
			}
		}

		// Also remove any existing a:b interaction (will be re-added by *).
		for (intptr_t i = fixed.size(); i >= 1; i--)
		{
			if (fixed[i].variables.size() == 2)
			{
				auto &v = fixed[i].variables;
				if ((v[1] == name_s && v[2] == other_s) ||
				    (v[1] == other_s && v[2] == name_s))
				{
					fixed.remove_at(i);
				}
			}
		}

		// Add both main effects + interaction (the formula serialiser will
		// produce "a + b + a:b"; the user sees the expanded form, which is
		// unambiguous and correct).
		stats::FixedTerm ft_a;
		ft_a.variables.append(name_s);
		fixed.append(std::move(ft_a));

		stats::FixedTerm ft_b;
		ft_b.variables.append(other_s);
		fixed.append(std::move(ft_b));

		stats::FixedTerm ft_ab;
		ft_ab.variables.append(name_s);
		ft_ab.variables.append(other_s);
		fixed.append(std::move(ft_ab));

		applyFormula(*parsed);
	}
	else
	{
		// Interaction-only (a:b) — simple append.
		QString quoted_name = quoteIfNeeded(name);
		QString quoted_other = quoteIfNeeded(other);
		QString term = quoted_name + QStringLiteral(":") + quoted_other;
		QString text = m_formula_edit->text().trimmed();

		if (text.isEmpty() || !text.contains('~'))
		{
			if (text.isEmpty())
				m_formula_edit->setText(QStringLiteral("~ ") + term);
			else
				m_formula_edit->setText(text + QStringLiteral(" ~ ") + term);
		}
		else
		{
			QString rhs = text.mid(text.indexOf('~') + 1).trimmed();
			if (rhs.isEmpty() || rhs == QStringLiteral("1")) {
				QString lhs = text.left(text.indexOf('~') + 1);
				m_formula_edit->setText(lhs + QStringLiteral(" ") + term);
			} else {
				m_formula_edit->setText(text + QStringLiteral(" + ") + term);
			}
		}

		m_formula_edit->setFocus();
		m_formula_edit->setCursorPosition(m_formula_edit->text().length());
	}
}

void AnalysisView::addRandomSlope(const QString &variable, const QString &group, bool correlated)
{
	auto parsed = tryParseFormula();
	if (!parsed) return;

	auto var_s = String(variable.toUtf8().constData());
	auto group_s = String(group.toUtf8().constData());

	// Find the existing RandomTerm with matching group.
	for (intptr_t i = 1; i <= parsed->random.size(); i++)
	{
		if (parsed->random[i].group != group_s) continue;

		auto &rt = parsed->random[i];

		if (correlated)
		{
			// Add the slope to the existing term: (1 | group) → (1 + X | group).
			// This estimates the correlation between intercept and slope.
			rt.slopes.append(var_s);
		}
		else if (rt.slopes.empty())
		{
			// Intercept-only: replace intercept with slope in place.
			// (1 | group) → (0 + X | group).
			// The user can re-add a random intercept separately if needed.
			rt.intercept = false;
			rt.slopes.append(var_s);
		}
		else
		{
			// Already has slopes: add a separate term so the new slope
			// is estimated independently.
			// (1 + Y | group) → (1 + Y | group) + (0 + X | group).
			stats::RandomTerm new_rt;
			new_rt.group = group_s;
			new_rt.slopes.append(var_s);
			new_rt.intercept = false;
			parsed->random.append(std::move(new_rt));
		}
		break;
	}

	applyFormula(*parsed);
}

void AnalysisView::removeFromFormula(const QString &name)
{
	auto parsed = tryParseFormula();
	if (!parsed) return;

	auto name_s = String(name.toUtf8().constData());

	// Remove from response.
	if (parsed->response == name_s) {
		parsed->response = String();
	}

	// Remove from fixed effects: remove any term that contains this variable,
	// plus remove any interaction terms that reference it.
	for (intptr_t i = parsed->fixed.size(); i >= 1; i--)
	{
		auto &ft = parsed->fixed[i];
		bool contains = false;
		for (intptr_t j = 1; j <= ft.variables.size(); j++) {
			if (ft.variables[j] == name_s) { contains = true; break; }
		}
		if (contains) {
			parsed->fixed.remove_at(i);
		}
	}

	// Remove from random effects: remove the variable as a slope.
	// If it's the grouping factor, remove the entire term.
	for (intptr_t i = parsed->random.size(); i >= 1; i--)
	{
		auto &rt = parsed->random[i];

		if (rt.group == name_s) {
			parsed->random.remove_at(i);
			continue;
		}

		// Remove as slope.
		for (intptr_t s = rt.slopes.size(); s >= 1; s--) {
			if (rt.slopes[s] == name_s) {
				rt.slopes.remove_at(s);
			}
		}

		// If the term has no intercept and no slopes, restore the intercept
		// rather than dropping the random effect entirely.
		if (!rt.intercept && rt.slopes.empty()) {
			rt.intercept = true;
		}
	}

	// Remove smooth terms referencing this variable (as main covariate or by-variable).
	for (intptr_t i = parsed->smooth.size(); i >= 1; i--)
	{
		if (parsed->smooth[i].variable == name_s) {
			parsed->smooth.remove_at(i);
		}
		else if (parsed->smooth[i].by == name_s) {
			// The by-variable is being removed: drop the entire term.
			// For bs=re slopes, the intercept term s(group, bs=re) remains.
			// For bs=cr by-factor, the plain smooth s(x) may already exist.
			parsed->smooth.remove_at(i);
		}
	}

	applyFormula(*parsed);
}

std::optional<stats::Formula> AnalysisView::tryParseFormula()
{
	auto bytes = m_formula_edit->text().trimmed().toUtf8();
	if (bytes.isEmpty()) return std::nullopt;
	try {
		return stats::Formula::parse(String(bytes.constData(), bytes.size()));
	}
	catch (...) {
		return std::nullopt;
	}
}

void AnalysisView::applyFormula(const stats::Formula &formula)
{
	auto s = formula.to_string();
	m_formula_edit->setText(QString::fromUtf8(s.data(), (int)s.size()));
	m_formula_edit->setFocus();
	m_formula_edit->setCursorPosition(m_formula_edit->text().length());
}

void AnalysisView::updateColumnMarkers()
{
	QSet<QString> used;
	auto text = m_formula_edit->text();

	if (!text.isEmpty())
	{
		// Robust approach: instead of parsing the formula grammar (which fails on
		// incomplete input), scan the raw text for occurrences of known column names.
		// This works regardless of whether the formula is syntactically complete.

		auto is_ident_char = [](QChar c) {
			return c.isLetterOrNumber() || c == '_' || c == '.';
		};

		for (int i = 0; i < m_column_list->count(); i++)
		{
			QString col = m_column_list->item(i)->text();

			// Check for the column name as a whole word in the formula text.
			// It may appear bare or in single quotes.
			QString quoted = QStringLiteral("'") + col + QStringLiteral("'");

			if (text.contains(quoted))
			{
				used.insert(col);
				continue;
			}

			int pos = text.indexOf(col);
			while (pos >= 0)
			{
				int end = pos + col.length();
				// Word boundary: the char before and after must not be part of an identifier.
				bool left_ok  = (pos == 0) || !is_ident_char(text[pos - 1]);
				bool right_ok = (end >= text.length()) || !is_ident_char(text[end]);
				if (left_ok && right_ok) {
					used.insert(col);
					break;
				}
				pos = text.indexOf(col, pos + 1);
			}
		}
	}

	for (int i = 0; i < m_column_list->count(); i++) {
		auto *item = m_column_list->item(i);
		item->setData(Qt::UserRole + 1, used.contains(item->text()));
	}
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
		clearTestResults();
		return;
	}

	auto &m = m_analysis->model(m_current_model);
	int plot_type = m_plot_type_combo->currentIndex();

	switch (plot_type)
	{
	case 0:  plotResidualsVsFitted(m);       break;
	case 1:  plotQQ(m);                      break;
	case 2:  plotScaledResidualsVsFitted(m); break;
	case 3:  plotScaledResidualQQ(m);        break;
	case 4:  plotPosteriorDensities(m);      break;
	default: m_plot->clear();                break;
	}

	// Show test results only for scaled residual plots.
	if (plot_type < 2 || plot_type >= 4)
		clearTestResults();
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
	m_plot->clearFixedYTicks();
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
	m_plot->clearFixedYTicks();
}

const stats::ScaledResidualResult *AnalysisView::ensureScaledResiduals(const stats::Model &m)
{
	if (m_scaled_residuals && m_scaled_residuals_model == m_current_model)
		return &*m_scaled_residuals;

	if (m.nobs == 0 || m.y.empty() || m.fitted.empty())
		return nullptr;

	try
	{
		m_scaled_residuals = stats::compute_scaled_residuals(m);
		m_scaled_residuals_model = m_current_model;
		return &*m_scaled_residuals;
	}
	catch (std::exception &e)
	{
		m_scaled_residuals.reset();
		m_scaled_residuals_model = -1;
		QMessageBox::warning(this, tr("Scaled residuals"),
			tr("Could not compute scaled residuals:\n%1").arg(QString::fromUtf8(e.what())));
		return nullptr;
	}
}

void AnalysisView::plotScaledResidualsVsFitted(const stats::Model &m)
{
	auto *sr = ensureScaledResiduals(m);
	if (!sr) {
		m_plot->clear();
		clearTestResults();
		return;
	}

	intptr_t n = m.nobs;
	std::vector<double> x(n), y(n);
	for (intptr_t i = 0; i < n; i++) {
		x[i] = m.fitted[i + 1];
		y[i] = sr->residuals[i + 1];
	}

	m_plot->setData(std::move(x), std::move(y),
	                tr("Fitted values"), tr("Scaled residual"),
	                tr("Scaled Residuals vs Fitted"),
	                PlotWidget::RefLine::HorizontalAtHalf);
	m_plot->setFixedYTicks({0.0, 0.25, 0.50, 0.75, 1.0});

	updateTestResults(*sr);
}

void AnalysisView::plotScaledResidualQQ(const stats::Model &m)
{
	auto *sr = ensureScaledResiduals(m);
	if (!sr) {
		m_plot->clear();
		clearTestResults();
		return;
	}

	intptr_t n = m.nobs;

	// Sort residuals for the QQ plot against U(0,1).
	std::vector<double> sorted(n);
	for (intptr_t i = 0; i < n; i++)
		sorted[i] = sr->residuals[i + 1];
	std::sort(sorted.begin(), sorted.end());

	// Theoretical quantiles: (i + 0.5) / n
	std::vector<double> theoretical(n);
	for (intptr_t i = 0; i < n; i++)
		theoretical[i] = (i + 0.5) / n;

	m_plot->setData(std::move(theoretical), std::move(sorted),
	                tr("Theoretical (Uniform)"), tr("Sample"),
	                tr("Scaled Residuals Q-Q"),
	                PlotWidget::RefLine::Diagonal);
	m_plot->setFixedYTicks({0.0, 0.25, 0.50, 0.75, 1.0});

	updateTestResults(*sr);
}


void AnalysisView::plotPosteriorDensities(const stats::Model &m)
{
	if (!m.is_bayesian())
	{
		m_plot->clear();
		QMessageBox::information(this, tr("Posterior Densities"),
			tr("Posterior density plots are only available for Bayesian models.\n\n"
			   "To fit a Bayesian model, select \"Bayesian\" in the Estimation dropdown\n"
			   "before fitting, or pass a Prior object to fit() in a script."));
		return;
	}

	if (m.posterior_mean.empty() || m.posterior_sd.empty())
	{
		m_plot->clear();
		return;
	}

	static const double inv_sqrt_2pi = 1.0 / std::sqrt(2.0 * M_PI);
	static const int N_POINTS = 200;

	intptr_t ncoef = m.nfixed;
	std::vector<PlotWidget::LineCurve> curves;
	curves.reserve(ncoef);

	// Check if we have a grid summary for mixture evaluation.
	bool use_grid = m.grid_summary.has_value()
	             && m.grid_summary->n_points > 0
	             && !m.grid_summary->weights.empty()
	             && !m.grid_summary->beta.empty()
	             && !m.grid_summary->vcov_diag.empty()
	             && m.grid_summary->n_beta == (int)ncoef;

	for (intptr_t j = 0; j < ncoef; j++)
	{
		double mu_j = m.posterior_mean[j + 1];
		double sd_j = m.posterior_sd[j + 1];
		if (sd_j <= 0) continue;

		// Determine evaluation range: ±4 SD from posterior mean.
		double xlo = mu_j - 4.0 * sd_j;
		double xhi = mu_j + 4.0 * sd_j;
		double dx = (xhi - xlo) / (N_POINTS - 1);

		PlotWidget::LineCurve curve;
		curve.name = (j + 1 <= m.coef_names.size())
			? QString::fromUtf8(m.coef_names[j + 1].data(), (int)m.coef_names[j + 1].size())
			: QStringLiteral("coef %1").arg(j + 1);
		curve.x.resize(N_POINTS);
		curve.y.resize(N_POINTS);

		for (int i = 0; i < N_POINTS; i++)
		{
			double x = xlo + i * dx;
			curve.x[i] = x;

			if (use_grid)
			{
				// Mixture density: f(x) = Σ_k w_k × N(x; β_k_j, σ_k_j)
				auto &gs = *m.grid_summary;
				int K = gs.n_points;
				int p = gs.n_beta;
				double density = 0;

				for (int k = 0; k < K; k++)
				{
					double w_k = gs.weights[k];
					double mu_k = gs.beta[k * p + j];
					double var_k = gs.vcov_diag[k * p + j];
					if (var_k <= 0) continue;
					double sd_k = std::sqrt(var_k);
					double z = (x - mu_k) / sd_k;
					density += w_k * inv_sqrt_2pi / sd_k * std::exp(-0.5 * z * z);
				}

				curve.y[i] = density;
			}
			else
			{
				// Single Gaussian: N(x; posterior_mean, posterior_sd)
				double z = (x - mu_j) / sd_j;
				curve.y[i] = inv_sqrt_2pi / sd_j * std::exp(-0.5 * z * z);
			}
		}

		curves.push_back(std::move(curve));
	}

	if (curves.empty())
	{
		m_plot->clear();
		return;
	}

	m_plot->setLinePlotData(std::move(curves),
	                         tr("Coefficient value"), tr("Density"),
	                         tr("Posterior Densities"));
	m_plot->clearFixedYTicks();
}


void AnalysisView::updateTestResults(const stats::ScaledResidualResult &sr)
{
	auto format_p = [](double p) -> QString {
		return (p < 0.001) ? QStringLiteral("< 0.001") : QString::number(p, 'f', 4);
	};

	QString text;

	if (sr.is_ppc)
	{
		// ── Posterior predictive checks ──────────────────────────
		auto ppc_label = [](double pp) -> const char * {
			return (pp < 0.05) ? "check model" : "good";
		};

		text += QStringLiteral("Posterior predictive checks\n\n");

		text += QStringLiteral("Uniformity:    KS D = %1  (Bayesian p-value = %2, %3)\n")
			.arg(sr.ks_statistic, 0, 'f', 4)
			.arg(format_p(sr.ks_pvalue))
			.arg(QString::fromUtf8(ppc_label(sr.ks_pvalue)));

		text += QStringLiteral("Dispersion:    ratio = %1  (Bayesian p-value = %2, %3)")
			.arg(sr.dispersion_ratio, 0, 'f', 4)
			.arg(format_p(sr.dispersion_pvalue))
			.arg(QString::fromUtf8(ppc_label(sr.dispersion_pvalue)));

		if (sr.dispersion_pvalue < 0.05)
		{
			if (sr.dispersion_ratio > 1.0)
				text += QStringLiteral("  \u2014 overdispersion");
			else if (sr.dispersion_ratio < 1.0)
				text += QStringLiteral("  \u2014 underdispersion");
		}

		text += QStringLiteral("\nOutlier test:  %1 outlier(s)  (Bayesian p-value = %2, %3)")
			.arg(sr.n_outliers)
			.arg(format_p(sr.outlier_pvalue))
			.arg(QString::fromUtf8(ppc_label(sr.outlier_pvalue)));
	}
	else
	{
		// ── Frequentist tests (existing display) ────────────────
		text += QStringLiteral("Kolmogorov\u2013Smirnov test for uniformity (H\u2080: residuals ~ U(0,1)):  D = %1,  p = %2\n")
			.arg(sr.ks_statistic, 0, 'f', 4)
			.arg(format_p(sr.ks_pvalue));

		text += QStringLiteral("Dispersion test:  ratio = %1,  p = %2")
			.arg(sr.dispersion_ratio, 0, 'f', 4)
			.arg(format_p(sr.dispersion_pvalue));

		if (sr.dispersion_pvalue < 0.05)
		{
			if (sr.dispersion_ratio > 1.0)
				text += QStringLiteral("  (potential overdispersion)");
			else if (sr.dispersion_ratio < 1.0)
				text += QStringLiteral("  (potential underdispersion)");
		}

		text += QStringLiteral("\nOutlier test:  %1 outlier(s) detected,  p = %2")
			.arg(sr.n_outliers)
			.arg(format_p(sr.outlier_pvalue));
	}

	m_test_results_text->setPlainText(text);
	m_test_results_group->setVisible(true);
}

void AnalysisView::clearTestResults()
{
	m_test_results_group->setVisible(false);
	m_test_results_text->clear();
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
	QString path = getSaveFileName(this,
		tr("Export plot as PNG"), tr("PNG image (*.png)"));
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
	QString path = getSaveFileName(this,
		tr("Export plot as PDF"), tr("PDF document (*.pdf)"));
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
	QString path = getSaveFileName(this,
		tr("Export plot as SVG"), tr("SVG image (*.svg)"));
	if (path.isEmpty()) return;
	if (!path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive))
		path += QStringLiteral(".svg");
	m_eda_plot->saveSVG(path);
}

void AnalysisView::onDetachEdaPlot()
{
	if (m_eda_float_window) {
		// Already detached — just raise the window.
		m_eda_float_window->raise();
		m_eda_float_window->activateWindow();
		return;
	}

	// Create a floating window. Parent is `this` with Qt::Window so it
	// floats independently but is destroyed when the AnalysisView closes.
	m_eda_float_window = new QWidget(this, Qt::Window);
	m_eda_float_window->setWindowTitle(tr("EDA Plot"));
	m_eda_float_window->setAttribute(Qt::WA_DeleteOnClose, false);
	m_eda_float_window->installEventFilter(this);

	auto *layout = new QVBoxLayout(m_eda_float_window);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	// Toolbar with export actions and a reattach button.
	auto *toolbar = new QToolBar;
	toolbar->setIconSize(QSize(20, 20));
	toolbar->setMovable(false);

	auto *save_menu = new QMenu(m_eda_float_window);
	save_menu->addAction(tr("Save as PNG..."), this, &AnalysisView::onExportEdaPNG);
	save_menu->addAction(tr("Save as PDF..."), this, &AnalysisView::onExportEdaPDF);
	save_menu->addAction(tr("Save as SVG..."), this, &AnalysisView::onExportEdaSVG);

	auto *save_action = new QAction(QIcon(":/icons/save.svg"), tr("Save as..."), m_eda_float_window);
	save_action->setMenu(save_menu);
	toolbar->addAction(save_action);
	if (auto *btn = qobject_cast<QToolButton *>(toolbar->widgetForAction(save_action)))
		btn->setPopupMode(QToolButton::InstantPopup);

	toolbar->addSeparator();
	auto *reattach_action = toolbar->addAction(QIcon(":/icons/minimize.svg"), tr("Reattach"));
	reattach_action->setToolTip(tr("Return the plot to the EDA tab"));
	connect(reattach_action, &QAction::triggered, this, &AnalysisView::onReattachEdaPlot);

	layout->addWidget(toolbar);

	// Move the plot widget into the floating window.
	m_eda_top_layout->removeWidget(m_eda_plot);
	layout->addWidget(m_eda_plot, 1);
	m_eda_plot->show();

	// Insert a placeholder into the EDA tab.
	m_eda_placeholder = new QLabel(tr("Plot detached — close the floating window or click Reattach to return it here."));
	m_eda_placeholder->setAlignment(Qt::AlignCenter);
	m_eda_placeholder->setWordWrap(true);
	QPalette pal = m_eda_placeholder->palette();
	pal.setColor(QPalette::WindowText, pal.color(QPalette::Disabled, QPalette::WindowText));
	m_eda_placeholder->setPalette(pal);
	m_eda_top_layout->insertWidget(0, m_eda_placeholder, 1);

	// Size the floating window to something reasonable.
	m_eda_float_window->resize(700, 500);
	m_eda_float_window->show();
	m_eda_float_window->raise();
}

void AnalysisView::onReattachEdaPlot()
{
	if (!m_eda_float_window) return;

	// Remove the plot from the floating window and return it to the EDA tab.
	auto *float_layout = m_eda_float_window->layout();
	if (float_layout)
		float_layout->removeWidget(m_eda_plot);

	// Remove the placeholder.
	if (m_eda_placeholder) {
		m_eda_top_layout->removeWidget(m_eda_placeholder);
		delete m_eda_placeholder;
		m_eda_placeholder = nullptr;
	}

	// Reinsert the plot at index 0 (above the controls bar).
	m_eda_top_layout->insertWidget(0, m_eda_plot, 1);
	m_eda_plot->show();

	// Destroy the floating window.
	m_eda_float_window->removeEventFilter(this);
	m_eda_float_window->hide();
	m_eda_float_window->deleteLater();
	m_eda_float_window = nullptr;
}

bool AnalysisView::eventFilter(QObject *obj, QEvent *event)
{
	// When the floating plot window is closed, reattach the plot.
	if (obj == m_eda_float_window && event->type() == QEvent::Close) {
		onReattachEdaPlot();
		return true; // event handled
	}
	return View::eventFilter(obj, event);
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
		if (val.empty() || val == "nan" || val == "NaN" || val == "NA") continue;
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
		m_eda_bw_label->setVisible(false);
		m_eda_bw_slider->setVisible(false);
		m_eda_bw_spin->setVisible(false);
		m_eda_group_label->setVisible(false);
		m_eda_group_combo->setVisible(false);
		m_eda_pool_label->setVisible(false);
		m_eda_pool_combo->setVisible(false);
		m_eda_style_label->setVisible(false);
		m_eda_style_combo->setVisible(false);
		m_eda_label_label->setVisible(false);
		m_eda_label_combo->setVisible(false);
		m_eda_mean_check->setVisible(false);
		m_eda_ellipse_check->setVisible(false);
		m_eda_ellipse_spin->setVisible(false);
		m_eda_formant_check->setVisible(false);
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
		m_eda_group_label->setVisible(false);
		m_eda_group_combo->setVisible(false);
		m_eda_pool_label->setVisible(false);
		m_eda_pool_combo->setVisible(false);
		m_eda_style_label->setVisible(false);
		m_eda_style_combo->setVisible(false);
		m_eda_label_label->setVisible(false);
		m_eda_label_combo->setVisible(false);
		m_eda_mean_check->setVisible(false);
		m_eda_ellipse_check->setVisible(false);
		m_eda_ellipse_spin->setVisible(false);
		m_eda_formant_check->setVisible(false);

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
				if (ok && std::isfinite(d)) vals.push_back(d);
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
			{
				bool density_on = m_eda_density_check->isChecked();
				m_eda_bw_label->setVisible(density_on);
				m_eda_bw_slider->setVisible(density_on);
				m_eda_bw_spin->setVisible(density_on);
			}

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

				// Apply the user-adjustable bandwidth multiplier (R's adjust parameter).
				double adjust = m_eda_bw_spin->value();
				h *= adjust;

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
			m_eda_bw_label->setVisible(false);
			m_eda_bw_slider->setVisible(false);
			m_eda_bw_spin->setVisible(false);
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
		// ── Scatter or grouped scatter: X continuous, Y continuous ──

		bool formant = m_eda_formant_check->isChecked();
		bool has_group = (m_eda_group_combo->currentIndex() > 0);
		bool has_pool = (m_eda_pool_combo->currentIndex() > 0);
		bool has_label = (m_eda_label_combo->currentIndex() > 0);
		bool has_style = (m_eda_style_combo->currentIndex() > 0);

		// Show the group / pool / label / formant controls.
		m_eda_group_label->setVisible(true);
		m_eda_group_combo->setVisible(true);
		m_eda_pool_label->setVisible(has_group);
		m_eda_pool_combo->setVisible(has_group);
		m_eda_style_label->setVisible(has_group);
		m_eda_style_combo->setVisible(has_group);
		m_eda_label_label->setVisible(true);
		m_eda_label_combo->setVisible(true);
		m_eda_formant_check->setVisible(true);

		// Mean/Ellipse visible only when a group is selected.
		m_eda_mean_check->setVisible(has_group);
		m_eda_ellipse_check->setVisible(has_group);
		m_eda_ellipse_spin->setVisible(has_group);

		// Regression line is available only without grouping.
		m_eda_regline_check->setVisible(!has_group);
		m_eda_density_check->setVisible(false);
		m_eda_bw_label->setVisible(false);
		m_eda_bw_slider->setVisible(false);
		m_eda_bw_spin->setVisible(false);

		if (has_group)
		{
			// ── Grouped scatter ──
			auto group_name_q = m_eda_group_combo->currentText();
			auto group_name = String(group_name_q.toUtf8().constData());

			intptr_t g_col = 0;
			for (intptr_t j = 1; j <= nc; j++) {
				if (dt->get_header(j) == group_name) { g_col = j; break; }
			}
			if (g_col < 1) { m_eda_plot->clear(); return; }

			// Resolve label column (may be the same as group column).
			intptr_t l_col = 0;
			if (has_label) {
				auto label_name = String(m_eda_label_combo->currentText().toUtf8().constData());
				for (intptr_t j = 1; j <= nc; j++) {
					if (dt->get_header(j) == label_name) { l_col = j; break; }
				}
			}

			// Resolve pool column.
			intptr_t p_col = 0;
			if (has_pool) {
				auto pool_name = String(m_eda_pool_combo->currentText().toUtf8().constData());
				for (intptr_t j = 1; j <= nc; j++) {
					if (dt->get_header(j) == pool_name) { p_col = j; break; }
				}
			}

			// Resolve style column.
			intptr_t s_col = 0;
			if (has_style) {
				auto style_name = String(m_eda_style_combo->currentText().toUtf8().constData());
				for (intptr_t j = 1; j <= nc; j++) {
					if (dt->get_header(j) == style_name) { s_col = j; break; }
				}
			}

			std::vector<double> xv, yv;
			std::vector<QString> gv;
			std::vector<QString> lv;
			std::vector<QString> sv;
			xv.reserve(nr);
			yv.reserve(nr);
			gv.reserve(nr);
			if (l_col > 0) lv.reserve(nr);
			if (s_col > 0) sv.reserve(nr);
			for (intptr_t r = 1; r <= nr; r++)
			{
				auto vx = dt->get_cell(r, x_col);
				auto vy = dt->get_cell(r, y_col);
				auto vg = dt->get_cell(r, g_col);
				if (vx.empty() || vy.empty() || vg.empty()) continue;
				if (l_col > 0 && dt->get_cell(r, l_col).empty()) continue;
				if (p_col > 0 && dt->get_cell(r, p_col).empty()) continue;
				if (s_col > 0 && dt->get_cell(r, s_col).empty()) continue;
				bool okx, oky;
				double dx = vx.to_float(&okx);
				double dy = vy.to_float(&oky);
				if (okx && oky && std::isfinite(dx) && std::isfinite(dy)) {
					xv.push_back(dx);
					yv.push_back(dy);
					gv.push_back(QString::fromUtf8(vg.data(), (int)vg.size()));
					if (l_col > 0) {
						auto vl = dt->get_cell(r, l_col);
						lv.push_back(QString::fromUtf8(vl.data(), (int)vl.size()));
					}
					if (s_col > 0) {
						auto vs = dt->get_cell(r, s_col);
						sv.push_back(QString::fromUtf8(vs.data(), (int)vs.size()));
					}
				}
			}
			if (xv.empty()) { m_eda_plot->clear(); return; }

			// ── Pooling: collapse to one point per (pool, group[, style]) cell ──
			if (p_col > 0)
			{
				// Re-read raw data including pool column to build the cell map.
				// The cell key is (pool, group, style) — style is empty when s_col == 0.
				struct PoolCell {
					double sx = 0, sy = 0;
					int n = 0;
					QString group;
					QString style;
					std::map<QString, int> label_freq;
				};
				using CellKey = std::tuple<QString, QString, QString>;
				std::map<CellKey, PoolCell> cells;
				std::vector<CellKey> cell_order;

				for (intptr_t r = 1; r <= nr; r++)
				{
					auto vx = dt->get_cell(r, x_col);
					auto vy = dt->get_cell(r, y_col);
					auto vg = dt->get_cell(r, g_col);
					auto vp = dt->get_cell(r, p_col);
					if (vx.empty() || vy.empty() || vg.empty() || vp.empty()) continue;
					if (l_col > 0 && dt->get_cell(r, l_col).empty()) continue;
					if (s_col > 0 && dt->get_cell(r, s_col).empty()) continue;
					bool okx, oky;
					double dx = vx.to_float(&okx);
					double dy = vy.to_float(&oky);
					if (!okx || !oky || !std::isfinite(dx) || !std::isfinite(dy)) continue;

					QString pool_str = QString::fromUtf8(vp.data(), (int)vp.size());
					QString group_str = QString::fromUtf8(vg.data(), (int)vg.size());
					QString style_str;
					if (s_col > 0) {
						auto vs = dt->get_cell(r, s_col);
						style_str = QString::fromUtf8(vs.data(), (int)vs.size());
					}

					auto key = std::make_tuple(pool_str, group_str, style_str);
					if (cells.find(key) == cells.end()) cell_order.push_back(key);
					auto &c = cells[key];
					c.sx += dx;
					c.sy += dy;
					c.n++;
					c.group = group_str;
					c.style = style_str;
					if (l_col > 0) {
						auto vl = dt->get_cell(r, l_col);
						c.label_freq[QString::fromUtf8(vl.data(), (int)vl.size())]++;
					}
				}

				// Replace the raw vectors with one averaged point per cell.
				xv.clear(); yv.clear(); gv.clear(); lv.clear(); sv.clear();
				for (auto &key : cell_order) {
					auto &c = cells[key];
					if (c.n == 0) continue;
					xv.push_back(c.sx / c.n);
					yv.push_back(c.sy / c.n);
					gv.push_back(c.group);
					if (s_col > 0)
						sv.push_back(c.style);
					if (l_col > 0) {
						// Most frequent label within the cell.
						QString best;
						int best_n = 0;
						for (auto &kv : c.label_freq) {
							if (kv.second > best_n) { best = kv.first; best_n = kv.second; }
						}
						lv.push_back(best);
					}
				}
				if (xv.empty()) { m_eda_plot->clear(); return; }
			}

			bool show_means = m_eda_mean_check->isChecked();
			bool show_ellipses = m_eda_ellipse_check->isChecked();

			// chi-squared quantile for 2 df: F(x) = 1 - exp(-x/2), so x = -2*ln(1-p).
			double conf = m_eda_ellipse_spin->value() / 100.0;
			double chi2_scale = -2.0 * std::log(1.0 - conf);

			// Build title: Y ~ X | Group [× Style] [/ Label] [pooled by Pool].
			QString title = y_name_q + QStringLiteral(" ~ ") + x_name_q
				+ QStringLiteral(" | ") + group_name_q;
			if (has_style && s_col > 0)
				title += QStringLiteral(" \u00D7 ") + m_eda_style_combo->currentText();
			if (has_label && l_col != g_col)
				title += QStringLiteral(" / ") + m_eda_label_combo->currentText();
			if (p_col > 0)
				title += QStringLiteral(" [pooled by ") + m_eda_pool_combo->currentText() + QStringLiteral("]");

			m_eda_plot->setGroupedScatterData(
				std::move(gv), std::move(xv), std::move(yv),
				x_name_q, y_name_q, title,
				show_means, show_ellipses, chi2_scale, formant, formant,
				std::move(lv), std::move(sv));
		}
		else
		{
			// ── Plain scatter ──

			// Resolve label column if selected.
			intptr_t l_col = 0;
			if (has_label) {
				auto label_name = String(m_eda_label_combo->currentText().toUtf8().constData());
				for (intptr_t j = 1; j <= nc; j++) {
					if (dt->get_header(j) == label_name) { l_col = j; break; }
				}
			}

			std::vector<double> xv, yv;
			std::vector<QString> lv;
			xv.reserve(nr);
			yv.reserve(nr);
			if (l_col > 0) lv.reserve(nr);
			for (intptr_t r = 1; r <= nr; r++)
			{
				auto vx = dt->get_cell(r, x_col);
				auto vy = dt->get_cell(r, y_col);
				if (vx.empty() || vy.empty()) continue;
				if (l_col > 0 && dt->get_cell(r, l_col).empty()) continue;
				bool okx, oky;
				double dx = vx.to_float(&okx);
				double dy = vy.to_float(&oky);
				if (okx && oky && std::isfinite(dx) && std::isfinite(dy)) {
					xv.push_back(dx);
					yv.push_back(dy);
					if (l_col > 0) {
						auto vl = dt->get_cell(r, l_col);
						lv.push_back(QString::fromUtf8(vl.data(), (int)vl.size()));
					}
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

			QString title = y_name_q + QStringLiteral(" ~ ") + x_name_q;
			if (has_label && l_col > 0)
				title += QStringLiteral(" / ") + m_eda_label_combo->currentText();

			m_eda_plot->setData(std::move(xv), std::move(yv), x_name_q, y_name_q,
			                     title, PlotWidget::RefLine::None, formant, formant,
			                     std::move(lv));

			if (reg_valid)
				m_eda_plot->setRegressionLine(reg_intercept, reg_slope, reg_r2);
			else
				m_eda_plot->clearRegressionLine();
		}
	}
	else if (!x_numeric && y_numeric)
	{
		// Box plot: X categorical, Y continuous
		m_eda_regline_check->setVisible(false);
		m_eda_density_check->setVisible(false);
		m_eda_bw_label->setVisible(false);
		m_eda_bw_slider->setVisible(false);
		m_eda_bw_spin->setVisible(false);
		m_eda_group_label->setVisible(false);
		m_eda_group_combo->setVisible(false);
		m_eda_pool_label->setVisible(false);
		m_eda_pool_combo->setVisible(false);
		m_eda_style_label->setVisible(false);
		m_eda_style_combo->setVisible(false);
		m_eda_label_label->setVisible(false);
		m_eda_label_combo->setVisible(false);
		m_eda_mean_check->setVisible(false);
		m_eda_ellipse_check->setVisible(false);
		m_eda_ellipse_spin->setVisible(false);
		m_eda_formant_check->setVisible(false);

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
			if (!ok || !std::isfinite(dy)) continue;
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
		m_eda_bw_label->setVisible(false);
		m_eda_bw_slider->setVisible(false);
		m_eda_bw_spin->setVisible(false);
		m_eda_group_label->setVisible(false);
		m_eda_group_combo->setVisible(false);
		m_eda_pool_label->setVisible(false);
		m_eda_pool_combo->setVisible(false);
		m_eda_style_label->setVisible(false);
		m_eda_style_combo->setVisible(false);
		m_eda_label_label->setVisible(false);
		m_eda_label_combo->setVisible(false);
		m_eda_mean_check->setVisible(false);
		m_eda_ellipse_check->setVisible(false);
		m_eda_ellipse_spin->setVisible(false);
		m_eda_formant_check->setVisible(false);
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
				if (ok && std::isfinite(d)) vals.push_back(d);
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
		bool has_group = (m_eda_group_combo->currentIndex() > 0);

		if (has_group)
		{
			// ── Grouped scatter: per-group N, Mean(X), SD(X), Mean(Y), SD(Y), Missing ──

			auto group_name_q = m_eda_group_combo->currentText();
			auto group_name = String(group_name_q.toUtf8().constData());

			intptr_t g_col = 0;
			for (intptr_t j = 1; j <= nc; j++) {
				if (dt->get_header(j) == group_name) { g_col = j; break; }
			}
			if (g_col < 1) return;

			bool has_pool = (m_eda_pool_combo->currentIndex() > 0);
			intptr_t p_col = 0;
			if (has_pool) {
				auto pool_name = String(m_eda_pool_combo->currentText().toUtf8().constData());
				for (intptr_t j = 1; j <= nc; j++) {
					if (dt->get_header(j) == pool_name) { p_col = j; break; }
				}
			}

			std::vector<QString> group_order;
			struct GroupStats { std::vector<double> xv, yv; };
			std::map<QString, GroupStats> grouped;
			intptr_t included = 0;

			if (p_col > 0)
			{
				// Pool first: collapse to one mean per (pool, group) cell,
				// then feed the cell means into the per-group stats.
				struct PoolCell { double sx = 0, sy = 0; int n = 0; QString group; };
				std::map<std::pair<QString, QString>, PoolCell> cells;
				std::vector<std::pair<QString, QString>> cell_order;

				for (intptr_t r = 1; r <= nr; r++)
				{
					auto vx = dt->get_cell(r, x_col);
					auto vy = dt->get_cell(r, y_col);
					auto vg = dt->get_cell(r, g_col);
					auto vp = dt->get_cell(r, p_col);
					if (vx.empty() || vy.empty() || vg.empty() || vp.empty()) continue;
					bool okx, oky;
					double dx = vx.to_float(&okx);
					double dy = vy.to_float(&oky);
					if (!okx || !oky || !std::isfinite(dx) || !std::isfinite(dy)) continue;
					included++;

					auto key = std::make_pair(
						QString::fromUtf8(vp.data(), (int)vp.size()),
						QString::fromUtf8(vg.data(), (int)vg.size()));
					if (cells.find(key) == cells.end()) cell_order.push_back(key);
					auto &c = cells[key];
					c.sx += dx; c.sy += dy; c.n++;
					c.group = key.second;
				}

				for (auto &key : cell_order) {
					auto &c = cells[key];
					if (c.n == 0) continue;
					if (grouped.find(c.group) == grouped.end()) group_order.push_back(c.group);
					grouped[c.group].xv.push_back(c.sx / c.n);
					grouped[c.group].yv.push_back(c.sy / c.n);
				}
			}
			else
			{
				// No pooling: raw data points.
				for (intptr_t r = 1; r <= nr; r++)
				{
					auto vx = dt->get_cell(r, x_col);
					auto vy = dt->get_cell(r, y_col);
					auto vg = dt->get_cell(r, g_col);
					if (vx.empty() || vy.empty() || vg.empty()) continue;
					bool okx, oky;
					double dx = vx.to_float(&okx);
					double dy = vy.to_float(&oky);
					if (!okx || !oky || !std::isfinite(dx) || !std::isfinite(dy)) continue;
					auto qs = QString::fromUtf8(vg.data(), (int)vg.size());
					if (grouped.find(qs) == grouped.end()) group_order.push_back(qs);
					grouped[qs].xv.push_back(dx);
					grouped[qs].yv.push_back(dy);
					included++;
				}
			}
			if (group_order.empty()) return;

			intptr_t missing = nr - included;
			int ncols = missing > 0 ? 7 : 6;

			m_eda_summary->setColumnCount(ncols);
			QStringList headers = {
				group_name_q, tr("N"),
				QStringLiteral("Mean(%1)").arg(x_name_q),
				QStringLiteral("SD(%1)").arg(x_name_q),
				QStringLiteral("Mean(%1)").arg(y_name_q),
				QStringLiteral("SD(%1)").arg(y_name_q)};
			if (missing > 0) headers.append(tr("Missing"));
			m_eda_summary->setHorizontalHeaderLabels(headers);
			m_eda_summary->setRowCount((int)group_order.size());

			for (int g = 0; g < (int)group_order.size(); g++)
			{
				auto &gs = grouped[group_order[g]];
				size_t n = gs.xv.size();

				double sx = 0, sy = 0;
				for (size_t i = 0; i < n; i++) { sx += gs.xv[i]; sy += gs.yv[i]; }
				double mx = sx / n;
				double my = sy / n;
				double ssx = 0, ssy = 0;
				for (size_t i = 0; i < n; i++) {
					double dx = gs.xv[i] - mx;
					double dy = gs.yv[i] - my;
					ssx += dx * dx;
					ssy += dy * dy;
				}
				double sd_x = (n > 1) ? std::sqrt(ssx / (n - 1)) : 0.0;
				double sd_y = (n > 1) ? std::sqrt(ssy / (n - 1)) : 0.0;

				m_eda_summary->setItem(g, 0, new QTableWidgetItem(group_order[g]));

				auto set = [&](int col, const QString &text) {
					auto *item = new QTableWidgetItem(text);
					item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
					m_eda_summary->setItem(g, col, item);
				};

				set(1, QString::number(n));
				set(2, QString::number(mx, 'f', 4));
				set(3, QString::number(sd_x, 'f', 4));
				set(4, QString::number(my, 'f', 4));
				set(5, QString::number(sd_y, 'f', 4));

				// Show missing only on the first row (it's a global count).
				if (g == 0 && missing > 0)
					set(6, QString::number(missing));
			}
			m_eda_summary->resizeColumnsToContents();
		}
		else
		{
			// ── Ungrouped scatter (both continuous): N, r, means, SDs ──

			std::vector<double> xv, yv;
			xv.reserve(nr);
			yv.reserve(nr);
			intptr_t missing = 0;
			for (intptr_t r = 1; r <= nr; r++)
			{
				auto vx = dt->get_cell(r, x_col);
				auto vy = dt->get_cell(r, y_col);
				if (vx.empty() || vy.empty()) { missing++; continue; }
				bool okx, oky;
				double dx = vx.to_float(&okx);
				double dy = vy.to_float(&oky);
				if (okx && oky && std::isfinite(dx) && std::isfinite(dy)) { xv.push_back(dx); yv.push_back(dy); }
				else missing++;
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

			int ncols = missing > 0 ? 7 : 6;
			m_eda_summary->setColumnCount(ncols);
			QStringList headers = {
				tr("N"), tr("r"),
				QStringLiteral("Mean(%1)").arg(x_name_q),
				QStringLiteral("SD(%1)").arg(x_name_q),
				QStringLiteral("Mean(%1)").arg(y_name_q),
				QStringLiteral("SD(%1)").arg(y_name_q)};
			if (missing > 0) headers.append(tr("Missing"));
			m_eda_summary->setHorizontalHeaderLabels(headers);
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
			if (missing > 0) set(6, QString::number(missing));

			m_eda_summary->resizeColumnsToContents();
		}
	}
	else if (!x_numeric && y_numeric)
	{
		// ── Box plot (X categorical, Y continuous): grouped stats ──

		std::vector<QString> group_order;
		std::map<QString, std::vector<double>> grouped;
		intptr_t included = 0;

		for (intptr_t r = 1; r <= nr; r++)
		{
			auto vx = dt->get_cell(r, x_col);
			auto vy = dt->get_cell(r, y_col);
			if (vx.empty() || vy.empty()) continue;
			bool ok;
			double dy = vy.to_float(&ok);
			if (!ok || !std::isfinite(dy)) continue;
			auto qs = QString::fromUtf8(vx.data(), (int)vx.size());
			if (grouped.find(qs) == grouped.end()) group_order.push_back(qs);
			grouped[qs].push_back(dy);
			included++;
		}
		if (group_order.empty()) return;

		intptr_t missing = nr - included;
		int ncols = missing > 0 ? 8 : 7;

		m_eda_summary->setColumnCount(ncols);
		QStringList headers = {
			x_name_q, tr("N"), tr("Mean"), tr("SD"),
			tr("Min"), tr("Median"), tr("Max")};
		if (missing > 0) headers.append(tr("Missing"));
		m_eda_summary->setHorizontalHeaderLabels(headers);
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

			// Show missing only on the first row (it's a global count).
			if (g == 0 && missing > 0)
				set(7, QString::number(missing));
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

	QString path = getSaveFileName(this,
		tr("Save summary"),
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

	QString path = getSaveFileName(this,
		tr("Save as LaTeX"),
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

	if (m.is_bayesian() && !m.posterior_mean.empty())
	{
		tex += QStringLiteral("\\begin{tabular}{lrrrrr}\n");
		tex += QStringLiteral("\\hline\n");
		tex += QStringLiteral(" & Post.~Mean & Post.~SD & CI$_{2.5}$ & CI$_{97.5}$ & pd \\\\\n");
		tex += QStringLiteral("\\hline\n");

		for (intptr_t i = 1; i <= m.nfixed; i++)
		{
			QString name = QString::fromUtf8(m.coef_names[i].data(), (int)m.coef_names[i].size());
			name.replace('_', QStringLiteral("\\_"));
			name.replace('&', QStringLiteral("\\&"));

			double post_mean = (i <= m.posterior_mean.size()) ? m.posterior_mean[i] : m.beta[i];
			double post_sd = (i <= m.posterior_sd.size()) ? m.posterior_sd[i] : m.se[i];
			double ci_lo = (i <= m.ci_lower.size()) ? m.ci_lower[i] : 0.0;
			double ci_hi = (i <= m.ci_upper.size()) ? m.ci_upper[i] : 0.0;
			double pd_val = (i <= m.pd.size()) ? m.pd[i] : 0.0;

			tex += QStringLiteral("%1 & %2 & %3 & %4 & %5 & %6 \\\\\n")
				.arg(name)
				.arg(post_mean, 0, 'f', 4)
				.arg(post_sd, 0, 'f', 4)
				.arg(ci_lo, 0, 'f', 4)
				.arg(ci_hi, 0, 'f', 4)
				.arg(pd_val, 0, 'f', 4);
		}
	}
	else
	{
		tex += QStringLiteral("\\begin{tabular}{lrrrr}\n");
		tex += QStringLiteral("\\hline\n");

		const char *stat_label = (m.is_gaussian() || m.is_student()) ? "$t$" : "$z$";
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

		// Conditional modes (BLUPs) in LaTeX
		if (m_blup_check->isChecked())
		{
			for (intptr_t g = 1; g <= m.random_effects.size(); g++)
			{
				auto &re = m.random_effects[g];
				intptr_t q = re.term_names.size();
				intptr_t J = re.nlevels;

				if (re.conditional_modes.empty()) continue;

				tex += QStringLiteral("\\medskip\n");
				tex += QStringLiteral("Conditional modes for \\textit{%1}:\n\n")
					.arg(QString::fromUtf8(re.group_name.data(), (int)re.group_name.size())
					     .replace('_', QStringLiteral("\\_")));

				// Build tabular spec: l + r columns for each term
				tex += QStringLiteral("\\begin{tabular}{l");
				for (intptr_t t = 0; t < q; t++) {
					tex += QStringLiteral("r");
				}
				tex += QStringLiteral("}\n\\hline\n");

				// Header row
				tex += QStringLiteral("Level");
				for (intptr_t t = 1; t <= q; t++) {
					QString tname = QString::fromUtf8(re.term_names[t].data(), (int)re.term_names[t].size());
					tname.replace('_', QStringLiteral("\\_"));
					tex += QStringLiteral(" & %1").arg(tname);
				}
				tex += QStringLiteral(" \\\\\n\\hline\n");

				// Data rows
				for (intptr_t j = 0; j < J; j++)
				{
					QString level_label;
					if (j + 1 <= re.level_names.size())
						level_label = QString::fromUtf8(re.level_names[j + 1].data(), (int)re.level_names[j + 1].size());
					else
						level_label = QString::number(j + 1);
					level_label.replace('_', QStringLiteral("\\_"));

					tex += level_label;

					for (intptr_t t = 0; t < q; t++)
					{
						intptr_t idx = j * q + t + 1;
						double val = (idx <= re.conditional_modes.size()) ? re.conditional_modes[idx] : 0.0;
						tex += QString::asprintf(" & %.4f", val);
					}
					tex += QStringLiteral(" \\\\\n");
				}

				tex += QStringLiteral("\\hline\n");
				tex += QStringLiteral("\\end{tabular}\n\n");
			}
		}
	}

	// Notes below the table
	tex += QStringLiteral("\\medskip\n");

	// Smooth terms table (if any)
	if (m.has_smooth_terms())
	{
		tex += QStringLiteral("\\begin{tabular}{lrrr}\n");
		tex += QStringLiteral("\\hline\n");
		tex += QStringLiteral(" & edf & $F$ & $p$ \\\\\n");
		tex += QStringLiteral("\\hline\n");

		for (intptr_t i = 1; i <= m.smooth_terms.size(); i++)
		{
			auto &sm = m.smooth_terms[i];
			QString var_q = QString::fromUtf8(sm.variable.data(), (int)sm.variable.size());
			QString label;
			if (sm.basis == "re") {
				if (!sm.by.empty()) {
					QString by_q = QString::fromUtf8(sm.by.data(), (int)sm.by.size());
					label = QStringLiteral("s(%1):%2").arg(var_q, by_q);
				} else {
					label = QStringLiteral("s(%1, bs=re)").arg(var_q);
				}
			} else {
				label = QStringLiteral("s(%1)").arg(var_q);
			}
			label.replace('_', QStringLiteral("\\_"));

			QString pval;
			if (sm.p_value < 0.001)
				pval = QStringLiteral("$<$\\,0.001");
			else
				pval = QString::number(sm.p_value, 'f', 4);

			tex += QStringLiteral("%1 & %2 & %3 & %4 \\\\\n")
				.arg(label)
				.arg(sm.edf, 0, 'f', 3)
				.arg(sm.F_stat, 0, 'f', 2)
				.arg(pval);
		}

		tex += QStringLiteral("\\hline\n");
		tex += QStringLiteral("\\end{tabular}\n\n");
		tex += QStringLiteral("\\medskip\n");
	}

	tex += QStringLiteral("\\footnotesize\n");
	QString family_display_tex = QString::fromUtf8(m.family.data(), (int)m.family.size());
	if (m.is_negbin()) family_display_tex = QStringLiteral("Negative binomial");
	if (m.is_beta()) family_display_tex = QStringLiteral("Beta");
	if (m.is_student()) family_display_tex = QStringLiteral("Student $t$ (robust)");

	tex += QStringLiteral("Family: %1 (%2); $N$ = %3")
		.arg(family_display_tex)
		.arg(QString::fromUtf8(m.link.data(), (int)m.link.size()))
		.arg(m.nobs);

	if (m.is_gaussian() && !m.has_random_effects()) {
		tex += QStringLiteral("; $R^2$ = %1; Adj.\\ $R^2$ = %2")
			.arg(m.r2, 0, 'f', 4)
			.arg(m.adj_r2, 0, 'f', 4);
	}
	if (m.has_random_effects() && !std::isnan(m.r2_marginal)) {
		tex += QStringLiteral("; $R^2_m$ = %1; $R^2_c$ = %2")
			.arg(m.r2_marginal, 0, 'f', 4)
			.arg(m.r2_conditional, 0, 'f', 4);
	}
	if (m.is_negbin()) {
		tex += QStringLiteral("; $\\theta$ = %1").arg(m.theta, 0, 'f', 4);
	}
	if (m.is_beta()) {
		tex += QStringLiteral("; $\\varphi$ = %1").arg(m.phi, 0, 'f', 4);
	}
	if (m.is_student()) {
		tex += QStringLiteral("; $\\sigma$ = %1; $\\nu$ = %2").arg(m.sigma, 0, 'f', 4).arg(m.nu, 0, 'f', 4);
	}

	tex += QStringLiteral("\\\\\n");
	if (m.is_bayesian()) {
		tex += QStringLiteral("Log-marginal likelihood = %1\n")
			.arg(m.loglik, 0, 'f', 1);
	} else {
		tex += QStringLiteral("AIC = %1; BIC = %2; log-lik.\\ = %3\n")
			.arg(m.aic, 0, 'f', 1)
			.arg(m.bic, 0, 'f', 1)
			.arg(m.loglik, 0, 'f', 1);
	}

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

	QString path = getSaveFileName(this,
		tr("Export plot"),
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

// Nakagawa & Schielzeth (2013) pseudo-R² for mixed models.
// R²_marginal  = σ²_f / (σ²_f + σ²_r + σ²_d)    [fixed effects only]
// R²_conditional = (σ²_f + σ²_r) / (σ²_f + σ²_r + σ²_d)  [fixed + random]
//
// References:
//   Nakagawa, S. & Schielzeth, H. (2013). A general and simple method for
//     obtaining R² from generalized linear mixed-effects models.
//     Methods in Ecology and Evolution, 4(2), 133–142.
//   Nakagawa, S., Johnson, P.C.D. & Schielzeth, H. (2017). The coefficient
//     of determination R² and intra-class correlation coefficient from
//     generalized linear mixed-effects models revisited and expanded.
//     Journal of the Royal Society Interface, 14(134), 20170213.
struct NakagawaR2 { double marginal; double conditional; };

static std::optional<NakagawaR2> compute_nakagawa_r2(const stats::Model &m)
{
	if (!m.has_random_effects()) return std::nullopt;
	if (m.X.empty() || m.beta.empty()) return std::nullopt;
	if (m.nobs <= 0 || m.nfixed <= 0) return std::nullopt;

	// ── σ²_f: variance of the fixed-effects linear predictor ────────
	Eigen::Map<Matrix<double>> Xm(const_cast<double*>(m.X.data()), m.nobs, m.nfixed);
	Eigen::Map<Vector<double>> bm(const_cast<double*>(m.beta.data()), m.nfixed);
	Vector<double> eta_fixed = Xm * bm;
	double mean_eta = eta_fixed.mean();
	double var_f = (eta_fixed.array() - mean_eta).square().mean();

	// ── σ²_r: random-effects variance contribution ──────────────────
	// For each grouping factor:
	//   - If Z_design is available: σ²_r,g = tr(Z_g' Z_g Σ_g) / n
	//   - Otherwise: sum of diagonal variances (exact for random
	//     intercepts, approximate for random slopes).
	double var_r = 0;
	for (intptr_t gi = 1; gi <= m.random_effects.size(); gi++)
	{
		auto &re = m.random_effects[gi];
		intptr_t q = re.term_names.size();

		if (!re.Z_design.empty() && !re.cov_chol.empty() && q > 0)
		{
			// Reconstruct covariance from Cholesky: L → Σ = L L'
			// cov_chol stores the raw Cholesky factor (NOT log-diagonal).
			Eigen::MatrixXd L = Eigen::MatrixXd::Zero(q, q);
			for (intptr_t r = 0; r < q; r++)
			{
				for (intptr_t c = 0; c <= r; c++)
				{
					intptr_t idx = r * (r + 1) / 2 + c;
					L(r, c) = (idx < re.cov_chol.size()) ? re.cov_chol[idx + 1] : 0.0;
				}
			}
			Eigen::MatrixXd Sigma = L * L.transpose();

			// Z_g is n × q, row-major in re.Z_design
			Eigen::Map<const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
				Zg(re.Z_design.data(), m.nobs, q);

			// tr(Z_g' Z_g Σ_g) / n
			Eigen::MatrixXd ZtZ = (Zg.transpose() * Zg) / static_cast<double>(m.nobs);
			var_r += (ZtZ.array() * Sigma.array()).sum();
		}
		else
		{
			// Fallback: sum of diagonal variance components.
			// Exact for random intercepts, approximate for slopes.
			for (intptr_t t = 1; t <= re.variance.size(); t++)
				var_r += re.variance[t];
		}
	}

	// ── σ²_d: distribution-specific variance ────────────────────────
	double var_d = 0;
	if (m.is_gaussian())
	{
		var_d = m.rse * m.rse;
	}
	else if (m.family == "binomial")
	{
		// Logit link: π²/3
		var_d = M_PI * M_PI / 3.0;
	}
	else if (m.family == "poisson")
	{
		// Lognormal approximation: log(1 + 1/λ̄)
		// where λ̄ = exp(mean(η) + (σ²_f + σ²_r) / 2)
		double lambda = std::exp(mean_eta + (var_f + var_r) / 2.0);
		var_d = std::log(1.0 + 1.0 / std::max(lambda, 1e-10));
	}
	else if (m.is_negbin())
	{
		// NB2 lognormal approximation: log(1 + 1/λ̄ + 1/θ)
		double lambda = std::exp(mean_eta + (var_f + var_r) / 2.0);
		double theta = std::max(m.theta, 1e-10);
		var_d = std::log(1.0 + 1.0 / std::max(lambda, 1e-10) + 1.0 / theta);
	}
	else if (m.is_beta())
	{
		// Beta with logit link: trigamma-based formula (Nakagawa et al. 2017).
		// σ²_d = trigamma(μ̄φ) + trigamma((1-μ̄)φ)
		double mu_bar = 1.0 / (1.0 + std::exp(-mean_eta));
		mu_bar = std::clamp(mu_bar, 1e-6, 1.0 - 1e-6);
		double phi_val = std::max(m.phi, 1e-10);
		var_d = boost::math::trigamma(mu_bar * phi_val)
		        + boost::math::trigamma((1.0 - mu_bar) * phi_val);
	}
	else if (m.is_student())
	{
		// Student t with identity link: use σ² directly (scale mixture of normals).
		var_d = m.sigma * m.sigma;
	}
	else
	{
		return std::nullopt; // unsupported family
	}

	double denom = var_f + var_r + var_d;
	if (denom <= 0) return std::nullopt;

	NakagawaR2 result;
	result.marginal = var_f / denom;
	result.conditional = (var_f + var_r) / denom;
	return result;
}

QString AnalysisView::formatSummary(const stats::Model &m) const
{
	QString text;

	QString family_display = QString::fromUtf8(m.family.data(), (int)m.family.size());
	if (m.is_negbin()) family_display = QStringLiteral("Negative binomial");
	if (m.is_beta()) family_display = QStringLiteral("Beta");
	if (m.is_student()) family_display = QStringLiteral("Student t (robust)");

	text += QStringLiteral("Family: %1 (%2)\n")
		.arg(family_display)
		.arg(QString::fromUtf8(m.link.data(), (int)m.link.size()));
	if (m.is_negbin()) {
		text += QString::asprintf("Theta (overdispersion): %.4f\n", m.theta);
	}
	if (m.is_beta()) {
		text += QString::asprintf("Phi (precision): %.4f\n", m.phi);
	}
	if (m.is_student()) {
		text += QString::asprintf("Sigma (scale): %.4f\n", m.sigma);
		text += QString::asprintf("Nu (df): %.4f\n", m.nu);
	}
	text += QStringLiteral("Formula: %1\n")
		.arg(QString::fromUtf8(m.formula.data(), (int)m.formula.size()));
	if (m.is_bayesian()) {
		text += QStringLiteral("Estimation: Bayesian (approximate posterior)\n");
	} else {
		text += QStringLiteral("Estimation: Frequentist (maximum likelihood)\n");
	}
	text += QStringLiteral("Observations: %1\n").arg(m.nobs);

	if (m.is_bayesian())
	{
		auto prior_str = stats::format_prior_summary(m.priors, m.family);
		text += QStringLiteral("\n") + QString::fromStdString(prior_str);
	}

	text += QStringLiteral("\n");

	// ── Fixed effects ──────────────────────────────────────────────

	// Compute first-column width from the longest coefficient name.
	int name_width = 12; // minimum
	for (intptr_t i = 1; i <= m.coef_names.size(); i++)
	{
		int len = (int)m.coef_names[i].size();
		if (len > name_width) name_width = len;
	}
	name_width += 2; // padding

	// Build reusable format strings.
	std::string hdr_fmt  = "%-" + std::to_string(name_width) + "s";
	std::string name_fmt = "%-" + std::to_string(name_width) + "s";

	if (m.is_bayesian() && !m.posterior_mean.empty())
	{
		text += QStringLiteral("Fixed effects (posterior):\n");
		text += QString::asprintf((hdr_fmt + " %12s %12s %12s %12s %12s %12s %10s\n").c_str(),
		                           "", "Post.Mean", "Post.Mode", "Post.Median",
		                           "Post.SD", "CI.lower", "CI.upper", "pd");

		std::string row_fmt = name_fmt + " %12.4f %12.4f %12.4f %12.4f %12.4f %12.4f %10s%s\n";

		for (intptr_t i = 1; i <= m.nfixed; i++)
		{
			const char *name = (i <= m.coef_names.size()) ? m.coef_names[i].data() : "?";

			double pd_val = (i <= m.pd.size()) ? m.pd[i] : 0.0;

			const char *stars = "";
			if (pd_val >= 0.999) stars = " ***";
			else if (pd_val >= 0.99) stars = " **";
			else if (pd_val >= 0.975) stars = " *";
			else if (pd_val >= 0.95) stars = " .";

			char pd_buf[16];
			if (pd_val >= 0.99995)
				snprintf(pd_buf, sizeof(pd_buf), "1.0000");
			else
				snprintf(pd_buf, sizeof(pd_buf), "%.4f", pd_val);

			double post_mean = (i <= m.posterior_mean.size()) ? m.posterior_mean[i] : m.beta[i];
			double post_mode = (i <= m.posterior_mode.size()) ? m.posterior_mode[i] : m.beta[i];
			double post_median = (i <= m.posterior_median.size()) ? m.posterior_median[i] : m.beta[i];
			double post_sd = (i <= m.posterior_sd.size()) ? m.posterior_sd[i] : m.se[i];
			double ci_lo = (i <= m.ci_lower.size()) ? m.ci_lower[i] : 0.0;
			double ci_hi = (i <= m.ci_upper.size()) ? m.ci_upper[i] : 0.0;

			text += QString::asprintf(row_fmt.c_str(),
			                           name, post_mean, post_mode, post_median,
			                           post_sd, ci_lo, ci_hi, pd_buf, stars);
		}

		text += QStringLiteral("---\n");
		text += QStringLiteral("pd thresholds: 0.999 '***' 0.99 '**' 0.975 '*' 0.95 '.' (two-sided equivalents)\n\n");
	}
	else
	{
		const char *stat_label = (m.is_gaussian() || m.is_student()) ? "t value" : "z value";
		text += QStringLiteral("Fixed effects:\n");
		text += QString::asprintf((hdr_fmt + " %12s %12s %12s %12s\n").c_str(),
		                           "", "Estimate", "Std.Error", stat_label, "Pr(>|t|)");

		std::string row_fmt = name_fmt + " %12.4f %12.4f %12.3f %12s%s\n";

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

			text += QString::asprintf(row_fmt.c_str(),
			                           name, m.beta[i], m.se[i], m.stat[i], pbuf, stars);
		}

		text += QStringLiteral("---\n");
		text += QStringLiteral("Signif. codes: 0 '***' 0.001 '**' 0.01 '*' 0.05 '.' 0.1 ' ' 1\n\n");
	}

	// ── Hyperparameter posteriors (Bayesian only) ───────────────────

	if (m.is_bayesian() && !m.hyper_names.empty())
	{
		text += QStringLiteral("Hyperparameters (posterior):\n");
		text += QString::asprintf("%-36s %12s %12s %12s %12s\n",
		                           "", "Post.Mean", "Post.SD", "CI.lower", "CI.upper");

		for (intptr_t i = 1; i <= m.hyper_names.size(); i++)
		{
			const char *name = m.hyper_names[i].data();
			double mean = (i <= m.hyper_posterior_mean.size()) ? m.hyper_posterior_mean[i] : 0.0;
			double sd = (i <= m.hyper_posterior_sd.size()) ? m.hyper_posterior_sd[i] : 0.0;
			double lo = (i <= m.hyper_ci_lower.size()) ? m.hyper_ci_lower[i] : 0.0;
			double hi = (i <= m.hyper_ci_upper.size()) ? m.hyper_ci_upper[i] : 0.0;

			text += QString::asprintf("%-36s %12.4f %12.4f %12.4f %12.4f\n",
			                           name, mean, sd, lo, hi);
		}
		text += QStringLiteral("\n");
	}

	// ── Smooth terms ───────────────────────────────────────────────

	if (m.has_smooth_terms())
	{
		text += QStringLiteral("Approximate significance of smooth terms:\n");
		text += QString::asprintf("%-20s %8s %8s %10s %12s\n",
		                           "", "edf", "Ref.df", "F", "p-value");

		for (intptr_t i = 1; i <= m.smooth_terms.size(); i++)
		{
			auto &sm = m.smooth_terms[i];
			String label("s(");
			label.append(sm.variable);
			if (sm.basis == "re") {
				if (!sm.by.empty()) {
					label.append("):");
					label.append(sm.by);
				} else {
					label.append(", bs=re)");
				}
			} else {
				label.append(")");
			}

			char pbuf[16];
			if (sm.p_value < 0.001)
				snprintf(pbuf, sizeof(pbuf), "< 0.001");
			else
				snprintf(pbuf, sizeof(pbuf), "%.4f", sm.p_value);

			const char *stars = "";
			if (sm.p_value < 0.001) stars = " ***";
			else if (sm.p_value < 0.01) stars = " **";
			else if (sm.p_value < 0.05) stars = " *";
			else if (sm.p_value < 0.1) stars = " .";

			text += QString::asprintf("%-20s %8.3f %8.3f %10.2f %12s%s\n",
			                           label.data(), sm.edf, sm.ref_df, sm.F_stat, pbuf, stars);
		}

		text += QStringLiteral("---\n");
		text += QStringLiteral("Signif. codes: 0 '***' 0.001 '**' 0.01 '*' 0.05 '.' 0.1 ' ' 1\n\n");
	}

	// ── Random effects ─────────────────────────────────────────────

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

		// Nakagawa pseudo-R² (marginal and conditional)
		double r2m = m.r2_marginal;
		double r2c = m.r2_conditional;
		// Recompute on-the-fly if loaded from an old file without stored values.
		if (std::isnan(r2m) && !m.X.empty() && !m.beta.empty())
		{
			auto r2 = compute_nakagawa_r2(m);
			if (r2) { r2m = r2->marginal; r2c = r2->conditional; }
		}
		if (!std::isnan(r2m))
		{
			text += QString::asprintf("Pseudo R-squared (Nakagawa):\n");
			text += QString::asprintf("  Marginal  (fixed effects):          %.4f\n", r2m);
			text += QString::asprintf("  Conditional (fixed + random):       %.4f\n", r2c);
			text += QStringLiteral("\n");
		}

		// Conditional modes (BLUPs)
		if (m_blup_check->isChecked())
		{
			for (intptr_t g = 1; g <= m.random_effects.size(); g++)
			{
				auto &re = m.random_effects[g];
				intptr_t q = re.term_names.size();
				intptr_t J = re.nlevels;

				if (re.conditional_modes.empty()) continue;

				text += QStringLiteral("Conditional modes for %1:\n")
					.arg(QString::fromUtf8(re.group_name.data(), (int)re.group_name.size()));

				// Header: Level, then each term name
				text += QString::asprintf("%-20s", "Level");
				for (intptr_t t = 1; t <= q; t++) {
					text += QString::asprintf(" %12s", re.term_names[t].data());
				}
				text += QStringLiteral("\n");

				// One row per level
				for (intptr_t j = 0; j < J; j++)
				{
					// Level name: use level_names if available, otherwise index
					QString level_label;
					if (j + 1 <= re.level_names.size())
						level_label = QString::fromUtf8(re.level_names[j + 1].data(), (int)re.level_names[j + 1].size());
					else
						level_label = QString::number(j + 1);

					text += QString::asprintf("%-20s", level_label.toUtf8().constData());

					for (intptr_t t = 0; t < q; t++)
					{
						// conditional_modes: nlevels × nterms, row-major (j * q + t)
						intptr_t idx = j * q + t + 1;
						double val = (idx <= re.conditional_modes.size()) ? re.conditional_modes[idx] : 0.0;
						text += QString::asprintf(" %12.4f", val);
					}
					text += QStringLiteral("\n");
				}

				text += QStringLiteral("\n");
			}
		}
	}
	else if (m.is_gaussian())
	{
		text += QString::asprintf("Residual standard error: %.4f on %ld degrees of freedom\n",
		                           m.rse, (long)m.df_residual);
		text += QString::asprintf("R-squared: %.4f, Adjusted R-squared: %.4f\n", m.r2, m.adj_r2);
	}

	// ── Goodness of fit ────────────────────────────────────────────

	if (m.is_bayesian()) {
		if (!std::isnan(m.log_marginal))
			text += QString::asprintf("Log-marginal likelihood: %.2f  logLik: %.1f\n", m.log_marginal, m.loglik);
		else
			text += QString::asprintf("logLik: %.1f\n", m.loglik);
		if (!std::isnan(m.waic))
			text += QString::asprintf("WAIC: %.1f  p_WAIC: %.1f  lppd: %.1f\n", m.waic, m.p_waic, m.lppd);
		if (!std::isnan(m.loo_ic))
			text += QString::asprintf("LOO-IC: %.1f  p_LOO: %.1f\n", m.loo_ic, m.p_loo);

		// Pareto k diagnostic summary.
		if (!m.pareto_k.empty())
		{
			int n_good = 0, n_ok = 0, n_bad = 0, n_verybad = 0;
			for (intptr_t j = 1; j <= m.pareto_k.size(); j++)
			{
				double k = m.pareto_k[j];
				if (k < 0.5)      n_good++;
				else if (k < 0.7) n_ok++;
				else if (k < 1.0) n_bad++;
				else              n_verybad++;
			}
			if (n_bad == 0 && n_verybad == 0 && n_ok == 0)
				text += QStringLiteral("Pareto k: all < 0.5 (good)\n");
			else if (n_bad == 0 && n_verybad == 0)
				text += QStringLiteral("Pareto k: %1/%2 > 0.5 (ok, LOO-IC reliable)\n")
					.arg(n_ok).arg(m.pareto_k.size());
			else
				text += QStringLiteral("Pareto k: %1/%2 > 0.7 (LOO-IC may be unreliable; consider WAIC)\n")
					.arg(n_bad + n_verybad).arg(m.pareto_k.size());
		}
	} else {
		text += QString::asprintf("AIC: %.1f  BIC: %.1f  logLik: %.1f\n", m.aic, m.bic, m.loglik);
	}

	if (m.niter > 0) {
		if (m.converged)
			text += QString::asprintf("Converged in %d iterations\n", m.niter);
		else
			text += QStringLiteral("WARNING: did not converge after %1 iterations\n").arg(m.niter);
	}

	return text;
}


// =====================================================================
// Post-hoc: Estimated Marginal Means and Pairwise Contrasts
// =====================================================================

static QString formatPValue(double p)
{
	if (p < 0.001)
		return QStringLiteral("< 0.001");
	if (p < 0.01)
		return QString::asprintf("%.4f", p);
	return QString::asprintf("%.3f", p);
}

static QString formatSignificance(double p)
{
	if (p < 0.001) return QStringLiteral("***");
	if (p < 0.01)  return QStringLiteral("**");
	if (p < 0.05)  return QStringLiteral("*");
	if (p < 0.1)   return QStringLiteral(".");
	return QString();
}

// Format a probability of direction (pd) value for Bayesian contrasts.
static QString formatPd(double pd)
{
	if (pd >= 0.99995)
		return QStringLiteral("1.0000");
	return QString::asprintf("%.4f", pd);
}

// Significance stars for pd, using the same thresholds as the model summary.
static QString formatPdSignificance(double pd)
{
	if (pd >= 0.999)  return QStringLiteral("***");
	if (pd >= 0.99)   return QStringLiteral("**");
	if (pd >= 0.975)  return QStringLiteral("*");
	if (pd >= 0.95)   return QStringLiteral(".");
	return QString();
}

void AnalysisView::populatePostHocFactors()
{
	m_posthoc_factor_combo->blockSignals(true);
	m_posthoc_by_combo->blockSignals(true);
	m_posthoc_trend_combo->blockSignals(true);
	m_posthoc_factor_combo->clear();
	m_posthoc_by_combo->clear();
	m_posthoc_by_combo->addItem(tr("(None)"));
	m_posthoc_trend_combo->clear();
	m_posthoc_trend_combo->addItem(tr("(None)"));

	if (m_current_model < 0 || m_current_model >= m_analysis->model_count())
	{
		m_posthoc_factor_combo->blockSignals(false);
		m_posthoc_by_combo->blockSignals(false);
		m_posthoc_trend_combo->blockSignals(false);
		return;
	}

	auto &m = m_analysis->model(m_current_model);

	if (!m.has_variable_info())
	{
		m_posthoc_factor_combo->blockSignals(false);
		m_posthoc_by_combo->blockSignals(false);
		m_posthoc_trend_combo->blockSignals(false);
		return;
	}

	for (intptr_t i = 1; i <= m.variable_info.size(); i++)
	{
		auto &vi = m.variable_info[i];
		auto qname = QString::fromUtf8(vi.name.data(), (int)vi.name.size());

		if (!vi.numeric && vi.levels.size() >= 2) {
			m_posthoc_factor_combo->addItem(qname);
			m_posthoc_by_combo->addItem(qname);
		}
		if (vi.numeric) {
			m_posthoc_trend_combo->addItem(qname);
		}
	}

	m_posthoc_factor_combo->blockSignals(false);
	m_posthoc_by_combo->blockSignals(false);
	m_posthoc_trend_combo->blockSignals(false);

	if (m_posthoc_factor_combo->count() > 0) {
		updatePostHoc();
	}
	else {
		m_posthoc_emm_table->clear();
		m_posthoc_emm_table->setRowCount(0);
		m_posthoc_emm_table->setColumnCount(0);
		m_posthoc_contrast_table->clear();
		m_posthoc_contrast_table->setRowCount(0);
		m_posthoc_contrast_table->setColumnCount(0);
	}
}


void AnalysisView::onPostHocChanged()
{
	updatePostHoc();
}


void AnalysisView::updatePostHoc()
{
	m_posthoc_emm_table->clear();
	m_posthoc_emm_table->setRowCount(0);
	m_posthoc_emm_table->setColumnCount(0);
	m_posthoc_contrast_table->clear();
	m_posthoc_contrast_table->setRowCount(0);
	m_posthoc_contrast_table->setColumnCount(0);

	if (m_current_model < 0 || m_current_model >= m_analysis->model_count())
		return;
	if (m_posthoc_factor_combo->count() == 0 || m_posthoc_factor_combo->currentIndex() < 0)
		return;

	auto &model = m_analysis->model(m_current_model);

	if (!model.has_vcov() || !model.has_variable_info())
		return;

	QString factor_qstr = m_posthoc_factor_combo->currentText();
	String factor(factor_qstr.toUtf8().constData());

	QString adj_qstr = m_posthoc_adj_combo->currentData().toString();
	String adjustment(adj_qstr.toUtf8().constData());

	double conf_level = m_posthoc_conf_spin->value();

	bool is_identity = (model.link == "identity");
	bool bayesian = model.is_bayesian();

	// For Bayesian models, force adjustment to "none" (pd is a posterior
	// probability, not an error rate — multiplicity adjustment does not apply).
	if (bayesian) {
		adjustment = "none";
	}

	// Disable the adjustment combo for Bayesian models (pd is a posterior
	// probability — multiplicity adjustment does not apply).
	m_posthoc_adj_combo->setEnabled(!bayesian);
	if (bayesian) {
		m_posthoc_adj_combo->blockSignals(true);
		m_posthoc_adj_combo->setCurrentIndex(m_posthoc_adj_combo->findData(QStringLiteral("none")));
		m_posthoc_adj_combo->blockSignals(false);
	}

	// Determine whether we are in emtrends mode.
	bool trend_mode = (m_posthoc_trend_combo->currentIndex() > 0);
	QString trend_var_qstr;
	if (trend_mode) {
		trend_var_qstr = m_posthoc_trend_combo->currentText();
	}

	// Determine whether we have a by-factor.
	bool by_mode = (m_posthoc_by_combo->currentIndex() > 0);
	QString by_qstr;
	if (by_mode) {
		by_qstr = m_posthoc_by_combo->currentText();
		// If the by-factor is the same as the target factor, ignore it.
		if (by_qstr == factor_qstr) {
			by_mode = false;
		}
	}

	try
	{
		// Column headers for EMM table.
		QString value_header = trend_mode ? tr("Slope") : tr("EMM");
		QString value_link_header = trend_mode ? tr("Slope (link)") : tr("EMM (link)");
		bool show_link_cols = !is_identity && !trend_mode;

		// Use "CrI" (credible interval) for Bayesian, "CI" for frequentist.
		QString ci_lower = bayesian ? tr("Lower CrI") : tr("Lower CI");
		QString ci_upper = bayesian ? tr("Upper CrI") : tr("Upper CI");

		QStringList emm_headers;
		if (by_mode) {
			emm_headers << by_qstr;
		}
		emm_headers << tr("Level") << value_header << tr("SE")
		            << ci_lower << ci_upper;
		if (show_link_cols) {
			emm_headers << value_link_header << tr("SE (link)");
		}

		// Column headers for contrast table.
		QString stat_header;
		QString pval_header = bayesian ? tr("pd") : tr("p value");

		QStringList con_headers;
		if (by_mode) {
			con_headers << by_qstr;
		}
		con_headers << tr("Contrast") << tr("Estimate") << tr("SE");
		int stat_header_col = con_headers.size();
		con_headers << QString() << pval_header << QString();

		// ── Helper lambda to populate one EMMResult + ContrastResult ──

		auto appendResults = [&](const stats::EMMResult &emm, const stats::ContrastResult &contrasts,
		                         int emm_row0, int con_row0, const QString &by_label)
		{
			intptr_t K = emm.levels.size();
			int by_offset = by_mode ? 1 : 0;

			for (intptr_t i = 0; i < K; i++)
			{
				int row = emm_row0 + (int)i;

				if (by_mode) {
					m_posthoc_emm_table->setItem(row, 0, new QTableWidgetItem(by_label));
				}

				m_posthoc_emm_table->setItem(row, by_offset + 0,
					new QTableWidgetItem(QString::fromUtf8(emm.levels[i + 1].data(),
					                                       (int)emm.levels[i + 1].size())));

				auto *emm_item = new QTableWidgetItem(QString::asprintf("%.4f", emm.emmean[i + 1]));
				emm_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
				m_posthoc_emm_table->setItem(row, by_offset + 1, emm_item);

				auto *se_item = new QTableWidgetItem(QString::asprintf("%.4f", emm.se[i + 1]));
				se_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
				m_posthoc_emm_table->setItem(row, by_offset + 2, se_item);

				auto *lo_item = new QTableWidgetItem(QString::asprintf("%.4f", emm.lower_ci[i + 1]));
				lo_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
				m_posthoc_emm_table->setItem(row, by_offset + 3, lo_item);

				auto *hi_item = new QTableWidgetItem(QString::asprintf("%.4f", emm.upper_ci[i + 1]));
				hi_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
				m_posthoc_emm_table->setItem(row, by_offset + 4, hi_item);

				if (show_link_cols)
				{
					auto *emm_link_item = new QTableWidgetItem(QString::asprintf("%.4f", emm.emmean_link[i + 1]));
					emm_link_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
					m_posthoc_emm_table->setItem(row, by_offset + 5, emm_link_item);

					auto *se_link_item = new QTableWidgetItem(QString::asprintf("%.4f", emm.se_link[i + 1]));
					se_link_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
					m_posthoc_emm_table->setItem(row, by_offset + 6, se_link_item);
				}
			}

			intptr_t npairs = contrasts.label.size();
			for (intptr_t i = 0; i < npairs; i++)
			{
				int row = con_row0 + (int)i;
				double pval = contrasts.p_value[i + 1];
				bool is_bayesian_contrast = contrasts.is_bayesian;

				// Highlight: for frequentist, p < 0.05; for Bayesian, pd >= 0.975 ("*" threshold).
				bool sig = is_bayesian_contrast ? (pval >= 0.975) : (pval < 0.05);

				if (by_mode) {
					m_posthoc_contrast_table->setItem(row, 0, new QTableWidgetItem(by_label));
				}

				m_posthoc_contrast_table->setItem(row, by_offset + 0,
					new QTableWidgetItem(QString::fromUtf8(contrasts.label[i + 1].data(),
					                                       (int)contrasts.label[i + 1].size())));

				auto *est_item = new QTableWidgetItem(QString::asprintf("%.4f", contrasts.estimate[i + 1]));
				est_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
				m_posthoc_contrast_table->setItem(row, by_offset + 1, est_item);

				auto *se_item = new QTableWidgetItem(QString::asprintf("%.4f", contrasts.se[i + 1]));
				se_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
				m_posthoc_contrast_table->setItem(row, by_offset + 2, se_item);

				auto *stat_item = new QTableWidgetItem(QString::asprintf("%.3f", contrasts.stat[i + 1]));
				stat_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
				m_posthoc_contrast_table->setItem(row, by_offset + 3, stat_item);

				auto *p_item = new QTableWidgetItem(
					is_bayesian_contrast ? formatPd(pval) : formatPValue(pval));
				p_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
				m_posthoc_contrast_table->setItem(row, by_offset + 4, p_item);

				auto *sig_item = new QTableWidgetItem(
					is_bayesian_contrast ? formatPdSignificance(pval) : formatSignificance(pval));
				sig_item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
				m_posthoc_contrast_table->setItem(row, by_offset + 5, sig_item);

				if (sig) {
					QFont bold = m_posthoc_contrast_table->font();
					bold.setBold(true);
					for (int c = 0; c < m_posthoc_contrast_table->columnCount(); c++) {
						if (auto *item = m_posthoc_contrast_table->item(row, c))
							item->setFont(bold);
					}
				}
			}
		};

		if (by_mode)
		{
			// ── By-factor mode: EMMs at each level of by-factor ──
			String by_factor(by_qstr.toUtf8().constData());
			auto by_result = stats::emmeans_by(model, factor, by_factor, adjustment, conf_level);

			intptr_t B = by_result.by_levels.size();

			int total_emm_rows = 0;
			int total_con_rows = 0;
			for (intptr_t b = 1; b <= B; b++) {
				total_emm_rows += (int)by_result.emms[b].levels.size();
				total_con_rows += (int)by_result.contrasts[b].label.size();
			}

			if (B > 0) {
				stat_header = (std::isfinite(by_result.contrasts[1].df) && by_result.contrasts[1].df > 0)
				              ? tr("t value") : tr("z value");
				con_headers[stat_header_col] = stat_header;
			}

			m_posthoc_emm_table->setColumnCount(emm_headers.size());
			m_posthoc_emm_table->setHorizontalHeaderLabels(emm_headers);
			m_posthoc_emm_table->setRowCount(total_emm_rows);

			m_posthoc_contrast_table->setColumnCount(con_headers.size());
			m_posthoc_contrast_table->setHorizontalHeaderLabels(con_headers);
			m_posthoc_contrast_table->setRowCount(total_con_rows);

			int emm_row = 0;
			int con_row = 0;
			for (intptr_t b = 1; b <= B; b++)
			{
				auto &emm = by_result.emms[b];
				auto &con = by_result.contrasts[b];
				QString by_label = QString::fromUtf8(by_result.by_levels[b].data(),
				                                      (int)by_result.by_levels[b].size());

				appendResults(emm, con, emm_row, con_row, by_label);
				emm_row += (int)emm.levels.size();
				con_row += (int)con.label.size();
			}
		}
		else
		{
			// ── Standard mode (no by-factor) ──
			stats::EMMResult emm;
			if (trend_mode) {
				String trend_var(trend_var_qstr.toUtf8().constData());
				emm = stats::emtrends(model, factor, trend_var, conf_level);
			}
			else {
				emm = stats::emmeans(model, factor, conf_level);
			}

			auto contrasts = stats::pairwise_contrasts(emm, model, adjustment);

			stat_header = (std::isfinite(contrasts.df) && contrasts.df > 0)
			              ? tr("t value") : tr("z value");
			con_headers[stat_header_col] = stat_header;

			intptr_t K = emm.levels.size();
			intptr_t npairs = contrasts.label.size();

			m_posthoc_emm_table->setColumnCount(emm_headers.size());
			m_posthoc_emm_table->setHorizontalHeaderLabels(emm_headers);
			m_posthoc_emm_table->setRowCount((int)K);

			m_posthoc_contrast_table->setColumnCount(con_headers.size());
			m_posthoc_contrast_table->setHorizontalHeaderLabels(con_headers);
			m_posthoc_contrast_table->setRowCount((int)npairs);

			appendResults(emm, contrasts, 0, 0, QString());
		}

		m_posthoc_emm_table->resizeColumnsToContents();
		m_posthoc_contrast_table->resizeColumnsToContents();
	}
	catch (std::exception &e)
	{
		m_posthoc_emm_table->setColumnCount(1);
		m_posthoc_emm_table->setRowCount(1);
		m_posthoc_emm_table->setHorizontalHeaderLabels({tr("Error")});
		m_posthoc_emm_table->setItem(0, 0, new QTableWidgetItem(QString::fromUtf8(e.what())));
		m_posthoc_emm_table->resizeColumnsToContents();
	}
}


void AnalysisView::onExportPostHoc()
{
	if (m_posthoc_emm_table->rowCount() == 0) return;

	QString text;

	// EMM table
	text += tr("Estimated Marginal Means") + QStringLiteral("\n");
	for (int c = 0; c < m_posthoc_emm_table->columnCount(); c++) {
		if (c > 0) text += QStringLiteral("\t");
		text += m_posthoc_emm_table->horizontalHeaderItem(c)->text();
	}
	text += QStringLiteral("\n");
	for (int r = 0; r < m_posthoc_emm_table->rowCount(); r++) {
		for (int c = 0; c < m_posthoc_emm_table->columnCount(); c++) {
			if (c > 0) text += QStringLiteral("\t");
			if (auto *item = m_posthoc_emm_table->item(r, c))
				text += item->text();
		}
		text += QStringLiteral("\n");
	}

	// Contrast table
	if (m_posthoc_contrast_table->rowCount() > 0)
	{
		text += QStringLiteral("\n") + tr("Pairwise Contrasts") + QStringLiteral("\n");
		for (int c = 0; c < m_posthoc_contrast_table->columnCount(); c++) {
			if (c > 0) text += QStringLiteral("\t");
			text += m_posthoc_contrast_table->horizontalHeaderItem(c)->text();
		}
		text += QStringLiteral("\n");
		for (int r = 0; r < m_posthoc_contrast_table->rowCount(); r++) {
			for (int c = 0; c < m_posthoc_contrast_table->columnCount(); c++) {
				if (c > 0) text += QStringLiteral("\t");
				if (auto *item = m_posthoc_contrast_table->item(r, c))
					text += item->text();
			}
			text += QStringLiteral("\n");
		}
	}

	QApplication::clipboard()->setText(text);

	auto *main_win = qobject_cast<QMainWindow *>(window());
	if (main_win && main_win->statusBar()) {
		main_win->statusBar()->showMessage(tr("Post-hoc results copied to clipboard"), 2000);
	}
}


static QString latexEscape(const QString &s)
{
	QString out = s;
	out.replace(QStringLiteral("_"), QStringLiteral("\\_"));
	out.replace(QStringLiteral("&"), QStringLiteral("\\&"));
	out.replace(QStringLiteral("%"), QStringLiteral("\\%"));
	out.replace(QStringLiteral("#"), QStringLiteral("\\#"));
	out.replace(QStringLiteral("<"), QStringLiteral("$<$"));
	return out;
}


static QString tableToLatex(QTableWidget *table, const QString &caption)
{
	int ncols = table->columnCount();
	int nrows = table->rowCount();
	if (ncols == 0 || nrows == 0) return {};

	QString tex;
	tex += QStringLiteral("\\begin{table}[htbp]\n\\centering\n");
	tex += QStringLiteral("\\caption{") + latexEscape(caption) + QStringLiteral("}\n");

	// Column alignment: first column left, rest right.
	QString align = QStringLiteral("l");
	for (int c = 1; c < ncols; c++) {
		align += QStringLiteral("r");
	}
	tex += QStringLiteral("\\begin{tabular}{") + align + QStringLiteral("}\n");
	tex += QStringLiteral("\\hline\n");

	// Header row.
	for (int c = 0; c < ncols; c++) {
		if (c > 0) tex += QStringLiteral(" & ");
		auto *hdr = table->horizontalHeaderItem(c);
		tex += latexEscape(hdr ? hdr->text() : QString());
	}
	tex += QStringLiteral(" \\\\\n\\hline\n");

	// Data rows.
	for (int r = 0; r < nrows; r++) {
		for (int c = 0; c < ncols; c++) {
			if (c > 0) tex += QStringLiteral(" & ");
			auto *item = table->item(r, c);
			tex += latexEscape(item ? item->text() : QString());
		}
		tex += QStringLiteral(" \\\\\n");
	}

	tex += QStringLiteral("\\hline\n\\end{tabular}\n\\end{table}\n");
	return tex;
}


void AnalysisView::onExportPostHocLatex()
{
	if (m_posthoc_emm_table->rowCount() == 0) return;

	QString text;
	text += tableToLatex(m_posthoc_emm_table, tr("Estimated Marginal Means"));

	if (m_posthoc_contrast_table->rowCount() > 0) {
		text += QStringLiteral("\n");
		text += tableToLatex(m_posthoc_contrast_table, tr("Pairwise Contrasts"));
	}

	QApplication::clipboard()->setText(text);

	auto *main_win = qobject_cast<QMainWindow *>(window());
	if (main_win && main_win->statusBar()) {
		main_win->statusBar()->showMessage(tr("Post-hoc LaTeX tables copied to clipboard"), 2000);
	}
}


// =====================================================================
// Prior panel helpers
// =====================================================================

stats::PriorSpec AnalysisView::buildPriorSpec() const
{
	stats::PriorSpec spec;

	// Fixed effects
	spec.fixed_auto = m_prior_fixed_auto->isChecked();
	spec.fixed_effects.mean = m_prior_fixed_mean->value();
	spec.fixed_effects.sd = m_prior_fixed_sd->value();

	// Variance components
	spec.variance_auto = m_prior_variance_auto->isChecked();
	auto var_type = m_prior_variance_type->currentData().toString();
	if (var_type == QStringLiteral("pc")) {
		spec.variance_components.type = stats::VariancePriorType::PC;
		spec.variance_components.param1 = m_prior_variance_scale->value();
		spec.variance_components.param2 = 0.05;
	} else if (var_type == QStringLiteral("halfcauchy")) {
		spec.variance_components.type = stats::VariancePriorType::HalfCauchy;
		spec.variance_components.param1 = m_prior_variance_scale->value();
	} else {
		spec.variance_components.type = stats::VariancePriorType::HalfNormal;
		spec.variance_components.param1 = m_prior_variance_scale->value();
	}

	// Residual SD
	spec.residual_auto = m_prior_residual_auto->isChecked();
	auto res_type = m_prior_residual_type->currentData().toString();
	if (res_type == QStringLiteral("pc")) {
		spec.residual.type = stats::VariancePriorType::PC;
		spec.residual.param1 = m_prior_residual_scale->value();
		spec.residual.param2 = 0.05;
	} else if (res_type == QStringLiteral("halfcauchy")) {
		spec.residual.type = stats::VariancePriorType::HalfCauchy;
		spec.residual.param1 = m_prior_residual_scale->value();
	} else {
		spec.residual.type = stats::VariancePriorType::HalfNormal;
		spec.residual.param1 = m_prior_residual_scale->value();
	}

	return spec;
}

void AnalysisView::resetPriorPanel()
{
	m_prior_fixed_auto->setChecked(true);
	m_prior_fixed_mean->setValue(0.0);
	m_prior_fixed_sd->setValue(10.0);
	m_prior_variance_auto->setChecked(true);
	m_prior_variance_type->setCurrentIndex(0); // PC
	m_prior_variance_scale->setValue(1.0);
	m_prior_residual_auto->setChecked(true);
	m_prior_residual_type->setCurrentIndex(0); // PC
	m_prior_residual_scale->setValue(1.0);
}

void AnalysisView::updatePriorDefaultsLabel()
{
	bool bayesian = (m_estimation_combo->currentIndex() == 1);
	if (!bayesian) {
		m_prior_defaults_label->clear();
		return;
	}

	// Build a one-line summary of the current prior settings.
	QString fixed_str;
	if (m_prior_fixed_auto->isChecked()) {
		fixed_str = QStringLiteral("Fixed: auto");
	} else {
		fixed_str = QStringLiteral("Fixed: N(%1, %2)")
			.arg(m_prior_fixed_mean->value(), 0, 'g', 4)
			.arg(m_prior_fixed_sd->value(), 0, 'g', 4);
	}

	QString var_str;
	if (m_prior_variance_auto->isChecked()) {
		var_str = QStringLiteral("Variance: auto");
	} else {
		var_str = QStringLiteral("Variance: %1(%2)")
			.arg(m_prior_variance_type->currentText())
			.arg(m_prior_variance_scale->value(), 0, 'g', 4);
	}

	QString family_data = m_family_combo->currentData().toString();
	bool is_gaussian = (family_data == QStringLiteral("gaussian") || family_data == QStringLiteral("student"));

	QString summary = fixed_str + QStringLiteral("  |  ") + var_str;
	if (is_gaussian) {
		if (m_prior_residual_auto->isChecked()) {
			summary += QStringLiteral("  |  Residual: auto");
		} else {
			summary += QStringLiteral("  |  Residual: %1(%2)")
				.arg(m_prior_residual_type->currentText())
				.arg(m_prior_residual_scale->value(), 0, 'g', 4);
		}
	}

	m_prior_defaults_label->setText(summary);
}

void AnalysisView::updatePriorResidualVisibility()
{
	QString family_data = m_family_combo->currentData().toString();
	bool is_gaussian = (family_data == QStringLiteral("gaussian") || family_data == QStringLiteral("student"));
	for (auto *w : m_prior_residual_widgets)
		w->setVisible(is_gaussian);
}

} // namespace phonometrica
