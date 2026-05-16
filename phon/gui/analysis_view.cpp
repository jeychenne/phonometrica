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
#include <QRegularExpression>
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
#include <QWidgetAction>
#include <QFormLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QHeaderView>
#include <QEvent>
#include <QSet>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTextEdit>
#include <boost/math/distributions/normal.hpp>
#include <boost/math/special_functions/trigamma.hpp>
#include <phon/gui/analysis_view.hpp>
#include <phon/gui/add_model_values_dialog.hpp>
#include <phon/gui/font_helpers.hpp>
#include <phon/gui/help_browser.hpp>
#include <phon/analysis/model_comparison.hpp>
#include <phon/analysis/predict.hpp>
#include <phon/application/dataset.hpp>
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

QString AnalysisView::baseLabel() const
{
	if (m_analysis->has_path())
	{
		return tabLabel(QString::fromUtf8(m_analysis->label().data(),
		                                   (int)m_analysis->label().size()));
	}
	if (m_analysis->has_source())
	{
		auto src = QString::fromUtf8(m_analysis->data()->label().data(),
		                              (int)m_analysis->data()->label().size());
		return QStringLiteral("Analysis — ") + tabLabel(src);
	}
	return QStringLiteral("Analysis");
}

QString AnalysisView::label() const
{
	QString base = baseLabel();
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
		// Use baseLabel() rather than m_analysis->label() so the suggested
		// filename matches what the user sees in the tab header.
		// m_analysis->label() falls through to Document::label(), which
		// returns "Untitled" for any document without a path — that would
		// produce "untitled.phon-analysis" instead of e.g.
		// "Analysis — schwa coding.phon-analysis".
		auto path = getSaveFileName(this, tr("Save analysis as..."),
			tr("Phonometrica analysis (*.phon-analysis)"),
			baseLabel() + QStringLiteral(".phon-analysis"));

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

	// ── Fitting options popup ────────────────────────────────────
	m_options_button = new QToolButton;
	m_options_button->setIcon(QIcon(QStringLiteral(":/icons/settings.svg")));
	m_options_button->setFixedSize(24, 24);
	m_options_button->setIconSize(QSize(16, 16));
	m_options_button->setToolTip(tr("Fitting options"));
	m_options_button->setAutoRaise(true);
	m_options_button->setPopupMode(QToolButton::InstantPopup);

	auto *options_menu = new QMenu(m_options_button);
	auto *options_action = new QWidgetAction(options_menu);
	auto *options_widget = new QWidget;
	auto *options_form = new QFormLayout(options_widget);
	options_form->setContentsMargins(8, 8, 8, 8);

	m_default_est_combo = new QComboBox;
	m_default_est_combo->addItem(tr("Frequentist"), QStringLiteral("frequentist"));
	m_default_est_combo->addItem(tr("Bayesian"), QStringLiteral("bayesian"));
	{
		int idx = 0;
		try {
			auto s = Settings::get_string("statistics", "estimation");
			if (s == "bayesian") idx = 1;
		} catch (...) {}
		m_default_est_combo->setCurrentIndex(idx);
	}
	options_form->addRow(tr("Default estimation:"), m_default_est_combo);

	m_max_iter_spin = new QSpinBox;
	m_max_iter_spin->setRange(10, 10000);
	{
		int val = 200;
		try { val = Settings::get_int("statistics", "max_iterations"); } catch (...) {}
		m_max_iter_spin->setValue(val);
	}
	m_max_iter_spin->setToolTip(tr("Maximum number of optimizer iterations for iterative models\n"
	                                "(GLMs, mixed models, GAMs). Has no effect on OLS."));
	options_form->addRow(tr("Max iterations:"), m_max_iter_spin);

	// ── Estimation method (ML / REML) for Gaussian LMMs ─────────
	//
	// REML is an opt-in alternative to ML for Gaussian linear mixed
	// models. It applies only when the family is Gaussian AND the
	// formula contains at least one random-effects term. The row is
	// kept in the layout at all times but disabled when N/A, so the
	// dialog doesn't jitter as the user changes family/formula.
	//
	// Visible item labels are spelled out in full ("Maximum
	// likelihood", "Restricted maximum likelihood") rather than using
	// ML/REML abbreviations, which were judged cryptic for users
	// unfamiliar with the literature.
	//
	// Default: ML is ALWAYS the initial selection. The choice is not
	// persisted across sessions or even across fits within a single
	// session — REML must be re-selected for each fit that wants it.
	// This is intentional: REML is opt-in per-fit because it has
	// narrow applicability (Gaussian LMMs only, fixed effects must
	// match across compared models), and a persisted REML preference
	// would silently apply to fresh LMM fits where ML is the safer
	// default. Refits of existing models read the method from the
	// model itself (Analysis::refit), not from this combo, so dropping
	// persistence doesn't break the refit workflow.
	m_method_combo = new QComboBox;
	m_method_combo->addItem(tr("Maximum likelihood"), QStringLiteral("ML"));
	m_method_combo->addItem(tr("Restricted maximum likelihood"), QStringLiteral("REML"));
	m_method_combo->setItemData(0, tr("Maximum likelihood (default for Gaussian LMMs)."),
	                            Qt::ToolTipRole);
	m_method_combo->setItemData(1, tr("Restricted maximum likelihood: gives unbiased\n"
	                                   "variance-component estimates and matches lme4's\n"
	                                   "default. Cannot be used to compare models with\n"
	                                   "different fixed effects (refit with maximum\n"
	                                   "likelihood for that)."),
	                            Qt::ToolTipRole);
	// Index 0 (ML) is implicitly current after the first addItem; no
	// explicit setCurrentIndex needed, and no Settings load — see
	// rationale above.
	m_method_label = new QLabel(tr("Method:"));
	options_form->addRow(m_method_label, m_method_combo);

	// No currentIndexChanged connect: the choice is read directly from
	// the combo at fit time (see onFit) and is not persisted.

	options_action->setDefaultWidget(options_widget);
	options_menu->addAction(options_action);
	m_options_button->setMenu(options_menu);

	connect(m_default_est_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
		String val = (idx == 1) ? "bayesian" : "frequentist";
		Settings::set_value("statistics", "estimation", std::move(val));
		m_estimation_combo->setCurrentIndex(idx);
	});

	connect(m_max_iter_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [](int val) {
		Settings::set_value("statistics", "max_iterations", intptr_t(val));
	});

	top_bar->addWidget(m_options_button);

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
	m_prior_variance_alpha_label = new QLabel(tr("\u03b1:"));
	var_hl->addWidget(m_prior_variance_alpha_label);
	m_prior_variance_alpha = new QDoubleSpinBox;
	m_prior_variance_alpha->setRange(0.001, 0.5);
	m_prior_variance_alpha->setValue(0.05);
	m_prior_variance_alpha->setDecimals(3);
	m_prior_variance_alpha->setSingleStep(0.01);
	m_prior_variance_alpha->setToolTip(tr("Tail probability \u03b1 for the PC prior: P(\u03c3 > u) = \u03b1.\n"
	                                      "Smaller \u03b1 tightens the prior toward \u03c3 = 0; larger \u03b1 makes it more diffuse.\n"
	                                      "Convention (Simpson et al. 2017, INLA): \u03b1 = 0.05."));
	var_hl->addWidget(m_prior_variance_alpha);
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
	m_prior_residual_alpha_label = new QLabel(tr("\u03b1:"));
	res_hl->addWidget(m_prior_residual_alpha_label);
	m_prior_residual_alpha = new QDoubleSpinBox;
	m_prior_residual_alpha->setRange(0.001, 0.5);
	m_prior_residual_alpha->setValue(0.05);
	m_prior_residual_alpha->setDecimals(3);
	m_prior_residual_alpha->setSingleStep(0.01);
	m_prior_residual_alpha->setToolTip(tr("Tail probability \u03b1 for the PC prior: P(\u03c3 > u) = \u03b1.\n"
	                                      "Smaller \u03b1 tightens the prior toward \u03c3 = 0; larger \u03b1 makes it more diffuse.\n"
	                                      "Convention (Simpson et al. 2017, INLA): \u03b1 = 0.05."));
	res_hl->addWidget(m_prior_residual_alpha);
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
		updateMethodVisibility();
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
	connect(m_prior_variance_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { updatePriorPcAlphaVisibility(); updatePriorDefaultsLabel(); });
	connect(m_prior_variance_scale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updatePriorDefaultsLabel(); });
	connect(m_prior_variance_alpha, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updatePriorDefaultsLabel(); });
	connect(m_prior_residual_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { updatePriorPcAlphaVisibility(); updatePriorDefaultsLabel(); });
	connect(m_prior_residual_scale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updatePriorDefaultsLabel(); });
	connect(m_prior_residual_alpha, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updatePriorDefaultsLabel(); });

	updatePriorResidualVisibility();
	updatePriorPcAlphaVisibility();
	updatePriorDefaultsLabel();
	updateMethodVisibility();

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
	m_add_to_data_button = new QPushButton(tr("Add to data…"));
	m_add_to_data_button->setToolTip(tr("Append fitted values, residuals, or scaled "
	                                     "residuals from the selected model as new "
	                                     "columns on the source data"));
	m_add_to_data_button->setEnabled(false);
	model_buttons->addWidget(m_add_to_data_button);
	model_layout->addLayout(model_buttons);

	// Enable right-click on the model list for Rename / Add to data / Delete.
	m_model_list->setContextMenuPolicy(Qt::CustomContextMenu);

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
	// "Posterior Predictive Check" and "Posterior Densities" are appended
	// dynamically in updateDiagnosticPlot() when a Bayesian model is selected,
	// so the entries only appear when they can do something useful.
	diag_top->addWidget(m_plot_type_combo);

	// Predictor selector for the posterior-densities plot. Hidden unless that
	// plot is active on a Bayesian model.
	m_posterior_predictors_label = new QLabel(tr("Predictors:"));
	m_posterior_predictors_label->setVisible(false);
	diag_top->addWidget(m_posterior_predictors_label);
	m_posterior_predictors_combo = new CheckableComboBox;
	m_posterior_predictors_combo->setToolTip(
		tr("Select which predictors to display. Axis scales adjust to the current selection."));
	m_posterior_predictors_combo->setMinimumWidth(160);
	m_posterior_predictors_combo->setVisible(false);
	diag_top->addWidget(m_posterior_predictors_combo);

	diag_top->addStretch();

	// Save... popup menu (PNG / PDF / SVG) and Detach action, mirroring
	// the EDA toolbar idiom but rendered as inline QToolButtons in the
	// existing controls row (the Diagnostics panel doesn't have enough
	// other controls to justify a separate toolbar row).
	auto *diag_save_menu = new QMenu(this);
	diag_save_menu->addAction(tr("Save as PNG..."), this, &AnalysisView::onExportDiagPNG);
	diag_save_menu->addAction(tr("Save as PDF..."), this, &AnalysisView::onExportDiagPDF);
	diag_save_menu->addAction(tr("Save as SVG..."), this, &AnalysisView::onExportDiagSVG);
	auto *diag_save_btn = new QToolButton;
	diag_save_btn->setIcon(QIcon(":/icons/save.svg"));
	diag_save_btn->setToolTip(tr("Save the plot as PNG, PDF, or SVG"));
	diag_save_btn->setMenu(diag_save_menu);
	diag_save_btn->setPopupMode(QToolButton::InstantPopup);
	diag_top->addWidget(diag_save_btn);

	auto *diag_detach_btn = new QToolButton;
	diag_detach_btn->setIcon(QIcon(":/icons/maximize.svg"));
	diag_detach_btn->setToolTip(tr("Open the plot in a resizable window"));
	auto *diag_detach_action = new QAction(QIcon(":/icons/maximize.svg"),
	                                        tr("Detach plot"), this);
	diag_detach_btn->setDefaultAction(diag_detach_action);
	diag_top->addWidget(diag_detach_btn);

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

	// Wire the diagnostics DetachablePlot. The plot lives at index 1
	// of diag_layout (index 0 is diag_top, index 2 is the test-results
	// group); on reattach we re-insert at that same index.
	m_diag_detach.plot          = m_plot;
	m_diag_detach.home_layout   = diag_layout;
	m_diag_detach.home_index    = 1;
	m_diag_detach.home_stretch  = 1;
	m_diag_detach.window_title  = tr("Diagnostic plot");
	m_diag_detach.detach_action = diag_detach_action;
	m_diag_detach.placeholder_text = tr(
	    "Plot detached — close the floating window or click Reattach to return it here.");
	m_diag_detach.save_png = [this]() { onExportDiagPNG(); };
	m_diag_detach.save_pdf = [this]() { onExportDiagPDF(); };
	m_diag_detach.save_svg = [this]() { onExportDiagSVG(); };

	m_right_tabs->addTab(diag_widget, tr("Diagnostics"));
	m_right_tabs->setTabToolTip(2, tr("Residual plots to check model assumptions"));

	// ── Effects tab ──────────────────────────────────────────────
	auto *effects_widget = new QWidget;
	auto *effects_layout = new QVBoxLayout(effects_widget);
	effects_layout->setContentsMargins(4, 4, 4, 4);
	effects_layout->setSpacing(4);

	auto *effects_top = new QHBoxLayout;
	effects_top->addWidget(new QLabel(tr("Predictor:")));
	m_effects_focal_combo = new QComboBox;
	m_effects_focal_combo->setToolTip(
		tr("Predictor to vary on the x-axis. Other predictors are held fixed."));
	m_effects_focal_combo->setMinimumWidth(140);
	effects_top->addWidget(m_effects_focal_combo);

	effects_top->addSpacing(8);
	effects_top->addWidget(new QLabel(tr("By:")));
	m_effects_by_combo = new QComboBox;
	m_effects_by_combo->setToolTip(
		tr("Optional categorical predictor to stratify the curves. "
		   "One curve per level of this predictor."));
	m_effects_by_combo->setMinimumWidth(120);
	effects_top->addWidget(m_effects_by_combo);

	// Conditional prediction controls. The Random combobox lists the model's
	// random-effects groups (only populated for mixed-effects models; remains
	// "(None)" otherwise). Selecting a group switches the plot to one curve
	// per group level, using the saved BLUPs (Z·u contribution per row), and
	// disables the By dropdown — these two faceting modes don't compose.
	effects_top->addSpacing(8);
	effects_top->addWidget(new QLabel(tr("Random:")));
	m_effects_re_combo = new QComboBox;
	m_effects_re_combo->setToolTip(
		tr("Optional random-effects group to condition on. "
		   "Selecting a group shows one curve per level of that group, "
		   "using the saved BLUPs."));
	m_effects_re_combo->setMinimumWidth(110);
	effects_top->addWidget(m_effects_re_combo);

	effects_top->addSpacing(4);
	effects_top->addWidget(new QLabel(tr("Levels:")));
	m_effects_re_levels = new CheckableComboBox;
	m_effects_re_levels->setToolTip(
		tr("Levels of the random-effects group to draw. All levels are "
		   "selected by default."));
	m_effects_re_levels->setMinimumWidth(110);
	m_effects_re_levels->setEnabled(false);
	effects_top->addWidget(m_effects_re_levels);

	effects_top->addSpacing(8);
	m_effects_show_ci_check = new QCheckBox(tr("Show CI"));
	m_effects_show_ci_check->setChecked(true);
	m_effects_show_ci_check->setToolTip(
		tr("Toggle the confidence/credible-interval band. Useful with many "
		   "random-effects levels, where overlapping ribbons get visually "
		   "noisy."));
	effects_top->addWidget(m_effects_show_ci_check);

	m_effects_show_legend_check = new QCheckBox(tr("Show legend"));
	m_effects_show_legend_check->setChecked(true);
	m_effects_show_legend_check->setToolTip(
		tr("Toggle the legend that names each curve. Off by default when "
		   "more than eight curves are drawn — the legend would otherwise "
		   "consume too much of the plot."));
	effects_top->addWidget(m_effects_show_legend_check);

	effects_top->addStretch();

	// Save... popup menu and Detach action — same idiom as the
	// Diagnostics panel above.
	auto *effects_save_menu = new QMenu(this);
	effects_save_menu->addAction(tr("Save as PNG..."), this, &AnalysisView::onExportEffectsPNG);
	effects_save_menu->addAction(tr("Save as PDF..."), this, &AnalysisView::onExportEffectsPDF);
	effects_save_menu->addAction(tr("Save as SVG..."), this, &AnalysisView::onExportEffectsSVG);
	auto *effects_save_btn = new QToolButton;
	effects_save_btn->setIcon(QIcon(":/icons/save.svg"));
	effects_save_btn->setToolTip(tr("Save the plot as PNG, PDF, or SVG"));
	effects_save_btn->setMenu(effects_save_menu);
	effects_save_btn->setPopupMode(QToolButton::InstantPopup);
	effects_top->addWidget(effects_save_btn);

	auto *effects_detach_btn = new QToolButton;
	effects_detach_btn->setIcon(QIcon(":/icons/maximize.svg"));
	effects_detach_btn->setToolTip(tr("Open the plot in a resizable window"));
	auto *effects_detach_action = new QAction(QIcon(":/icons/maximize.svg"),
	                                           tr("Detach plot"), this);
	effects_detach_btn->setDefaultAction(effects_detach_action);
	effects_top->addWidget(effects_detach_btn);

	effects_layout->addLayout(effects_top);

	m_effects_plot = new PlotWidget;
	effects_layout->addWidget(m_effects_plot, 1);

	// Message label, shown when the plot can't be drawn (unsupported model
	// type, missing source data, etc.). Hidden by default.
	m_effects_message = new QLabel;
	m_effects_message->setWordWrap(true);
	m_effects_message->setAlignment(Qt::AlignCenter);
	m_effects_message->setStyleSheet(QStringLiteral(
		"QLabel { color: #555; padding: 12px; font-style: italic; }"));
	m_effects_message->setVisible(false);
	effects_layout->addWidget(m_effects_message);

	// Wire the effects DetachablePlot. The plot lives at index 1 of
	// effects_layout (index 0 is effects_top, index 2 is the message
	// label); on reattach we re-insert at that same index. The message
	// label and the plot are mutually exclusive — updateEffectsPlot()
	// auto-reattaches when entering message mode so the user sees the
	// explanation in the home tab rather than an empty floating window.
	m_effects_detach.plot          = m_effects_plot;
	m_effects_detach.home_layout   = effects_layout;
	m_effects_detach.home_index    = 1;
	m_effects_detach.home_stretch  = 1;
	m_effects_detach.window_title  = tr("Effects plot");
	m_effects_detach.detach_action = effects_detach_action;
	m_effects_detach.placeholder_text = tr(
	    "Plot detached — close the floating window or click Reattach to return it here.");
	m_effects_detach.save_png = [this]() { onExportEffectsPNG(); };
	m_effects_detach.save_pdf = [this]() { onExportEffectsPDF(); };
	m_effects_detach.save_svg = [this]() { onExportEffectsSVG(); };

	m_right_tabs->addTab(effects_widget, tr("Effects"));
	m_right_tabs->setTabToolTip(3, tr(
		"Model-implied effect of a focal predictor with confidence or "
		"credible intervals. Other categorical predictors held at their "
		"reference level; other numeric predictors at their observed mean. "
		"Mixed-effects models: population-level prediction (random effects "
		"set to zero). Bayesian models: posterior mean and credible interval."));

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

	// Refresh action: re-runs updateEdaPlot(), which re-reads cells from the
	// underlying DataTable. Useful workflow: click an outlier point, jump to
	// its row in the dataset view, edit the value, return here, hit refresh.
	auto *eda_refresh_action = new QAction(QIcon(":/icons/refresh-cw.svg"),
	                                        tr("Refresh plot"), this);
	eda_refresh_action->setToolTip(tr("Re-read the current data from the table "
	                                    "and redraw the plot. Use this after "
	                                    "editing values in the source dataset."));
	eda_refresh_action->setShortcut(QKeySequence::Refresh);
	eda_toolbar->addAction(eda_refresh_action);
	connect(eda_refresh_action, &QAction::triggered, this, &AnalysisView::onRefreshEdaPlot);

	auto *eda_save_menu = new QMenu(this);
	eda_save_menu->addAction(tr("Save as PNG..."), this, &AnalysisView::onExportEdaPNG);
	eda_save_menu->addAction(tr("Save as PDF..."), this, &AnalysisView::onExportEdaPDF);
	eda_save_menu->addAction(tr("Save as SVG..."), this, &AnalysisView::onExportEdaSVG);
	eda_save_menu->addSeparator();
	eda_save_menu->addAction(tr("Save summary as CSV..."), this, &AnalysisView::onSaveEdaSummary);

	auto *eda_save_action = new QAction(QIcon(":/icons/save.svg"), tr("Save as..."), this);
	eda_save_action->setMenu(eda_save_menu);
	eda_toolbar->addAction(eda_save_action);
	if (auto *btn = qobject_cast<QToolButton *>(eda_toolbar->widgetForAction(eda_save_action)))
		btn->setPopupMode(QToolButton::InstantPopup);

	//eda_toolbar->addSeparator();

	// Customize action: opens a dialog where the user can override title,
	// axis labels, axis ranges, and facet panel-per-row count. The state
	// lives in m_eda_customization and is consulted by updateEdaPlot when
	// finalizing each render. Reset whenever the plot type changes.
	auto *eda_customize_action = new QAction(QIcon(":/icons/settings.svg"),
	                                          tr("Customize plot..."), this);
	eda_customize_action->setToolTip(tr("Override title, axis labels, axis ranges, "
	                                     "and facet layout for the current plot"));
	eda_toolbar->addAction(eda_customize_action);
	connect(eda_customize_action, &QAction::triggered, this, &AnalysisView::onCustomizeEda);

	QWidget* spacer = new QWidget();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	eda_toolbar->addWidget(spacer);

	auto *eda_detach_action = new QAction(QIcon(":/icons/maximize.svg"), tr("Detach plot"), this);
	eda_detach_action->setToolTip(tr("Open the plot in a resizable window"));
	eda_toolbar->addAction(eda_detach_action);

	eda_layout->addWidget(eda_toolbar);

	// ── Plot area ──
	m_eda_plot = new PlotWidget;

	// ── Controls between plot and stats ──
	// Row 1: variable selectors
	auto *eda_vars = new QHBoxLayout;
	eda_vars->setSpacing(6);
	// Plot type combo — first element. "Auto" preserves the original
	// data-driven inference behavior; the other entries constrain the plot
	// to a specific kind, allowing plots that pure inference can't reach
	// (Formant chart, plus future Violin / Density / Ridgeline / etc.).
	m_eda_plot_type_label = new QLabel(tr("Plot type:"));
	eda_vars->addWidget(m_eda_plot_type_label);
	m_eda_plot_type_combo = new QComboBox;
	m_eda_plot_type_combo->addItem(tr("Auto"),          (int)EdaPlotType::Auto);
	m_eda_plot_type_combo->addItem(tr("Histogram"),     (int)EdaPlotType::Histogram);
	m_eda_plot_type_combo->addItem(tr("Bar chart"),     (int)EdaPlotType::BarChart);
	m_eda_plot_type_combo->addItem(tr("Boxplot"),       (int)EdaPlotType::BoxPlot);
	m_eda_plot_type_combo->addItem(tr("Scatter"),       (int)EdaPlotType::Scatter);
	m_eda_plot_type_combo->addItem(tr("Formant chart"), (int)EdaPlotType::FormantChart);
	m_eda_plot_type_combo->addItem(tr("Heatmap"),       (int)EdaPlotType::Heatmap);
	m_eda_plot_type_combo->addItem(tr("Proportion"),    (int)EdaPlotType::Proportion);
	m_eda_plot_type_combo->setToolTip(tr("Choose a plot type. \"Auto\" picks one "
	                                      "based on the variables you select."));
	eda_vars->addWidget(m_eda_plot_type_combo);

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
	m_eda_pool_combo->setToolTip(tr("Average X and Y values within each pool cell before plotting. "
	                                "Without Group: one point per pool level (e.g. one point per "
	                                "speaker). With Group: one point per (group, pool) cell (e.g. "
	                                "one point per speaker per vowel)."));
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
	m_eda_facet_label = new QLabel(tr("Facet:"));
	m_eda_facet_label->setVisible(false);
	eda_vars->addWidget(m_eda_facet_label);
	m_eda_facet_combo = new QComboBox;
	m_eda_facet_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_eda_facet_combo->setToolTip(tr("Split the plot into one panel per level of a categorical variable "
	                                  "(small multiples). Combines with Group: facet by speaker, group by "
	                                  "condition → overlaid histograms per speaker."));
	m_eda_facet_combo->setVisible(false);
	eda_vars->addWidget(m_eda_facet_combo);
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
	m_eda_regline_check->setToolTip(tr("Overlay an OLS regression line on the scatter plot. "
	                                    "With grouping, draws one line per group, clipped to "
	                                    "each group's own x-range."));
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
	// The button strip mirrors the post-hoc tab pattern: a top row with
	// stretch + icon-and-label buttons, so the save/copy paths are visible
	// at a glance rather than hidden behind a right-click.
	auto *eda_summary_widget = new QWidget;
	auto *eda_summary_layout = new QVBoxLayout(eda_summary_widget);
	eda_summary_layout->setContentsMargins(0, 0, 0, 0);
	eda_summary_layout->setSpacing(4);

	auto *eda_summary_top = new QHBoxLayout;
	eda_summary_top->setContentsMargins(2, 2, 2, 0);
	eda_summary_top->addWidget(new QLabel(tr("<b>Summary</b>")));
	eda_summary_top->addStretch();
	auto *eda_summary_copy_button = new QPushButton(QIcon(":/icons/clipboard-copy.svg"),
	                                                 tr("Copy"));
	eda_summary_copy_button->setToolTip(tr("Copy the summary table to the clipboard "
	                                        "as tab-separated values"));
	eda_summary_top->addWidget(eda_summary_copy_button);
	auto *eda_summary_save_button = new QPushButton(QIcon(":/icons/save.svg"),
	                                                 tr("Save as CSV..."));
	eda_summary_save_button->setToolTip(tr("Save the summary table to a CSV file"));
	eda_summary_top->addWidget(eda_summary_save_button);
	eda_summary_layout->addLayout(eda_summary_top);

	m_eda_summary = new QTableWidget;
	m_eda_summary->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_eda_summary->setSelectionMode(QAbstractItemView::NoSelection);
	m_eda_summary->setAlternatingRowColors(true);
	m_eda_summary->verticalHeader()->setVisible(false);
	m_eda_summary->horizontalHeader()->setStretchLastSection(false);
	// Right-click anywhere in the table for Copy / Save-as-CSV. The summary
	// is computed work the user wants to get out of the tab; without this
	// the only way to capture the numbers was to retype them.
	m_eda_summary->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_eda_summary, &QWidget::customContextMenuRequested,
	        this, &AnalysisView::onEdaSummaryContextMenu);
	connect(eda_summary_copy_button, &QPushButton::clicked,
	        this, &AnalysisView::onCopyEdaSummary);
	connect(eda_summary_save_button, &QPushButton::clicked,
	        this, &AnalysisView::onSaveEdaSummary);
	eda_summary_layout->addWidget(m_eda_summary, 1);

	// ── Assemble: splitter between (plot + controls) and stats ──
	auto *eda_top = new QWidget;
	auto *eda_top_layout = new QVBoxLayout(eda_top);
	eda_top_layout->setContentsMargins(0, 0, 0, 0);
	eda_top_layout->setSpacing(4);
	eda_top_layout->addWidget(m_eda_plot, 1);

	// Empty-state hint shown just below the plot when the current variable
	// selection doesn't fit the requested plot type. Italic gray so it reads
	// as advisory text rather than chrome; hidden when validation passes.
	m_eda_hint_label = new QLabel;
	m_eda_hint_label->setStyleSheet("color: #888; font-style: italic; padding: 2px 8px;");
	m_eda_hint_label->setVisible(false);
	m_eda_hint_label->setWordWrap(true);
	eda_top_layout->addWidget(m_eda_hint_label);

	eda_top_layout->addWidget(eda_controls_widget);

	// Wire the EDA DetachablePlot. The plot lives at index 0 of
	// eda_top_layout (above eda_controls_widget); on reattach we
	// re-insert at that same index.
	m_eda_detach.plot          = m_eda_plot;
	m_eda_detach.home_layout   = eda_top_layout;
	m_eda_detach.home_index    = 0;
	m_eda_detach.home_stretch  = 1;
	m_eda_detach.window_title  = tr("EDA plot");
	m_eda_detach.detach_action = eda_detach_action;
	m_eda_detach.placeholder_text = tr(
	    "Plot detached — close the floating window or click Reattach to return it here.");
	m_eda_detach.save_png = [this]() { onExportEdaPNG(); };
	m_eda_detach.save_pdf = [this]() { onExportEdaPDF(); };
	m_eda_detach.save_svg = [this]() { onExportEdaSVG(); };

	auto *eda_splitter = new QSplitter(Qt::Vertical);
	eda_splitter->addWidget(eda_top);
	eda_splitter->addWidget(eda_summary_widget);
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
	connect(m_add_to_data_button, &QPushButton::clicked, this, &AnalysisView::onAddToData);
	connect(m_model_list, &QListWidget::customContextMenuRequested,
	        this, &AnalysisView::onModelListContextMenu);
	connect(m_model_list, &QListWidget::itemDoubleClicked, this, &AnalysisView::onRenameModel);
	connect(m_column_list, &QListWidget::itemDoubleClicked, this, &AnalysisView::onColumnDoubleClicked);
	connect(m_column_list, &QListWidget::customContextMenuRequested, this, &AnalysisView::onColumnContextMenu);
	connect(m_plot_type_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onPlotTypeChanged);
	connect(m_posterior_predictors_combo, &CheckableComboBox::checkedItemsChanged,
	        this, [this](const QStringList &) { updateDiagnosticPlot(); });
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
	connect(m_eda_facet_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onEdaChanged);
	// Plot type uses its own slot so we can apply per-type defaults (Mean /
	// Ellipse / Density / Regline) once at the moment of change, before
	// triggering the redraw. onEdaChanged is invoked at the end of that slot.
	connect(m_eda_plot_type_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onEdaPlotTypeChanged);
	connect(m_eda_mean_check, &QCheckBox::toggled, this, &AnalysisView::onEdaChanged);
	connect(m_eda_ellipse_check, &QCheckBox::toggled, this, &AnalysisView::onEdaChanged);
	connect(m_eda_ellipse_spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AnalysisView::onEdaChanged);
	connect(m_eda_formant_check, &QCheckBox::toggled, this, &AnalysisView::onEdaChanged);

	// Click-to-source on the EDA plot: forward to MainWindow with the current
	// analysis source. PlotWidget never emits INVALID_ROW, so no extra guard
	// is needed beyond checking that we still have a source.
	connect(m_eda_plot, &PlotWidget::pointClicked, this, [this](intptr_t row) {
		if (!m_analysis->has_source())
			return;
		emit requestOpenSourceRow(m_analysis->source(), row);
	});

	// Click-to-source on the Diagnostics plot: same as EDA. Source-row
	// vectors are populated by plotResidualsVsFitted / plotQQ /
	// plotScaledResidualsVsFitted / plotScaledResidualQQ when the model
	// carries source_rows; if it doesn't (e.g. older .phon-analysis files),
	// the plot is silently inert. Posterior-density plots are line plots
	// with no per-observation points, so they're inert by construction.
	connect(m_plot, &PlotWidget::pointClicked, this, [this](intptr_t row) {
		if (!m_analysis->has_source())
			return;
		emit requestOpenSourceRow(m_analysis->source(), row);
	});

	connect(eda_detach_action, &QAction::triggered, this, &AnalysisView::onDetachEdaPlot);
	connect(diag_detach_action, &QAction::triggered, this, &AnalysisView::onDetachDiagPlot);
	connect(effects_detach_action, &QAction::triggered, this, &AnalysisView::onDetachEffectsPlot);
	connect(m_formula_edit, &QLineEdit::textChanged, this, &AnalysisView::updateColumnMarkers);
	connect(m_formula_edit, &QLineEdit::textChanged, this, [this](const QString &) {
		updateMethodVisibility();
	});
	connect(m_posthoc_factor_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onPostHocChanged);
	connect(m_posthoc_by_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onPostHocChanged);
	connect(m_posthoc_trend_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onPostHocChanged);
	connect(m_posthoc_adj_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnalysisView::onPostHocChanged);
	connect(m_posthoc_conf_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &AnalysisView::onPostHocChanged);
	connect(posthoc_copy_button, &QPushButton::clicked, this, &AnalysisView::onExportPostHoc);
	connect(posthoc_latex_button, &QPushButton::clicked, this, &AnalysisView::onExportPostHocLatex);

	connect(m_effects_focal_combo,
	        QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &AnalysisView::onEffectsFocalChanged);
	connect(m_effects_by_combo,
	        QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &AnalysisView::onEffectsFocalChanged);
	// Random-group combobox: when the user switches groups, repopulate the
	// levels checklist and toggle the By-combo enabled state. Also triggers
	// a re-render via onEffectsFocalChanged at the end.
	connect(m_effects_re_combo,
	        QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &AnalysisView::onEffectsRandomChanged);
	// Levels checklist: re-render whenever the user toggles a level.
	connect(m_effects_re_levels, &CheckableComboBox::checkedItemsChanged,
	        this, [this](const QStringList &) { updateEffectsPlot(); });
	// "Show CI" checkbox: re-render with the new flag.
	connect(m_effects_show_ci_check, &QCheckBox::toggled,
	        this, [this](bool) { updateEffectsPlot(); });
	// "Show legend" checkbox: re-render with the new flag.
	connect(m_effects_show_legend_check, &QCheckBox::toggled,
	        this, [this](bool) { updateEffectsPlot(); });

	// Initial enabled state for the three detach actions. Plots have
	// no data yet, so all three start disabled; updateDetachActionsEnabled()
	// is called whenever a plot is populated or cleared to keep this
	// in sync.
	updateDetachActionsEnabled();
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
	m_eda_facet_combo->clear();
	m_eda_x_combo->addItem(tr("(None)"));
	m_eda_y_combo->addItem(tr("(None)"));
	m_eda_group_combo->addItem(tr("(None)"));
	m_eda_pool_combo->addItem(tr("(None)"));
	m_eda_style_combo->addItem(tr("(None)"));
	m_eda_label_combo->addItem(tr("(None)"));
	m_eda_facet_combo->addItem(tr("(None)"));

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
		m_eda_facet_combo->addItem(qname);
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
	updateAddToDataButton();

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

		int max_iter = m_max_iter_spin->value();

		// Build FitOptions from the GUI. Currently only `method` (ML/REML)
		// is exposed in the UI. The struct is passed only when the user
		// has selected REML AND the Method row is enabled (i.e. the choice
		// is applicable — Gaussian LMM). Otherwise pass nullptr so
		// Analysis::fit() falls through to the legacy path. If REML is
		// selected but inapplicable, we still pass it through — the engine
		// records a fit_warning so the user knows REML was requested but
		// not honoured.
		stats::FitOptions fit_opts;
		const stats::FitOptions *opts_ptr = nullptr;
		if (m_method_combo && m_method_combo->isEnabled()
		    && m_method_combo->currentData().toString() == QStringLiteral("REML"))
		{
			fit_opts.method = stats::Method::REML;
			opts_ptr = &fit_opts;
		}

		int index = m_analysis->fit(formula, family, cb, priors_ptr, max_iter, opts_ptr);
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


void AnalysisView::onRefitModel(int row)
{
	// Triggered from the model list's right-click menu. The refit reproduces
	// the model from its OWN stored spec (formula, family, estimation mode,
	// priors when Bayesian) — the formula bar and the rest of the GUI are
	// deliberately not consulted, so right-clicking model B while model A is
	// displayed refits B unambiguously. The only GUI value used is max_iter,
	// which is a global fitter ceiling rather than part of any model's
	// identity.
	if (row < 0 || row >= m_analysis->model_count())
		return;

	// ── Set up progress bar (mirrors onFit) ─────────────────────
	auto *main_win = qobject_cast<QMainWindow *>(window());
	QStatusBar *status = main_win ? main_win->statusBar() : nullptr;
	QProgressBar *progress = main_win ? main_win->findChild<QProgressBar *>() : nullptr;

	if (status) status->showMessage(tr("Refitting model..."));
	QApplication::setOverrideCursor(Qt::WaitCursor);
	QApplication::processEvents();

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
		int max_iter = m_max_iter_spin->value();
		m_analysis->refit(row, cb, max_iter);

		QApplication::restoreOverrideCursor();
		if (progress) progress->setVisible(false);
		if (status) status->showMessage(tr("Model refitted"), 2000);

		// The list-item text encodes label + estimation mode + formula; in
		// the typical case nothing changes, but rebuild it defensively in
		// case Formula::to_string() canonicalised differently this time.
		if (auto *item = m_model_list->item(row)) {
			item->setText(modelListText(row));
		}

		// Refresh the displayed view only if the refit'd model is the one
		// currently shown. Otherwise leave the GUI untouched — right-click
		// is a contextual action and must not steal display state.
		if (m_current_model == row) {
			m_scaled_residuals.reset();
			m_scaled_residuals_model = -1;
			m_ppc.reset();
			m_ppc_model = -1;
			displayModel(row);
		}

		emit titleChanged(label());
	}
	catch (std::exception &e)
	{
		QApplication::restoreOverrideCursor();
		if (progress) progress->setVisible(false);
		if (status) status->clearMessage();

		QMessageBox::critical(this, tr("Refit model"), QString::fromUtf8(e.what()));
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
		m_ppc.reset();
		m_ppc_model = -1;
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
			if (pr.variance_components.type == stats::VariancePriorType::PC) {
				m_prior_variance_alpha->setValue(pr.variance_components.param2);
			}

			m_prior_residual_auto->setChecked(pr.residual_auto);
			switch (pr.residual.type)
			{
			case stats::VariancePriorType::PC:         m_prior_residual_type->setCurrentIndex(0); break;
			case stats::VariancePriorType::HalfCauchy:  m_prior_residual_type->setCurrentIndex(1); break;
			case stats::VariancePriorType::HalfNormal:  m_prior_residual_type->setCurrentIndex(2); break;
			}
			m_prior_residual_scale->setValue(pr.residual.param1);
			if (pr.residual.type == stats::VariancePriorType::PC) {
				m_prior_residual_alpha->setValue(pr.residual.param2);
			}
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
	populateEffectsFocalCombo();
	updateEffectsPlot();
	refreshEdaVirtualColumns();
	updateAddToDataButton();
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
	m_scaled_residuals.reset();
	m_scaled_residuals_model = -1;
	m_ppc.reset();
	m_ppc_model = -1;

	if (m_model_list->count() > 0) {
		// Select the nearest remaining model.
		int select = std::min(rows.last(), m_model_list->count() - 1);
		m_model_list->setCurrentRow(select);
	}
	else {
		m_current_model = -1;
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
		// Effects panel goes through its standard "no model" path, which
		// auto-reattaches a detached float window if one is up.
		updateEffectsPlot();
		refreshEdaVirtualColumns();
		updateAddToDataButton();
		updateDetachActionsEnabled();
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
		// Label is built complex-vs-simpler (idx_b vs idx_a).
		int len = (modelDisplayLabel(idx_b) + " vs " + modelDisplayLabel(idx_a)).toUtf8().size();
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
		// Complex-vs-simpler order: significant test favours the model named first.
		QString plabel = modelDisplayLabel(idx_b) + QStringLiteral(" vs ") + modelDisplayLabel(idx_a);

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
	text += QStringLiteral("Note: a significant test favours the more complex model (named first).\n");

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

// Wrap a column name in single quotes whenever the formula tokenizer would
// not parse it as a bare name. Delegates to stats::quote_name so that GUI
// insertion and backend round-trips (Formula::to_string) agree on which
// names need quoting.
static QString quoteIfNeeded(const QString &name)
{
	auto bytes = name.toUtf8();
	auto quoted = stats::quote_name(String(bytes.constData(), bytes.size()));
	return QString::fromUtf8(quoted.data(), (int) quoted.size());
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

	// Nested grouping factor: lists existing (non-synthetic) groups in
	// the formula as candidate outer factors. Clicking one appends
	// (1 | outer:inner) — lme4-style nesting via the bare-colon
	// synthetic group, equivalent to (1|outer/inner) after parsing.
	auto *nested_menu = menu.addMenu(tr("Add as nested grouping factor in..."));
	{
		auto name_s = String(name.toUtf8().constData());
		bool any_eligible = false;
		if (parsed && !parsed->random.empty())
		{
			for (intptr_t i = 1; i <= parsed->random.size(); i++)
			{
				auto &rt = parsed->random[i];
				// Skip synthetic groups (already nested) — extending the
				// chain via the GUI is a corner case; users can edit
				// the formula directly for that.
				if (rt.is_synthetic_group) continue;
				// Skip self-nesting (outer == inner is nonsense).
				if (rt.group == name_s) continue;

				auto group_q = QString::fromUtf8(rt.group.data(), (int) rt.group.size());
				auto *action = nested_menu->addAction(group_q);
				action->setData(group_q);
				any_eligible = true;
			}
		}
		if (!any_eligible)
		{
			auto *placeholder = nested_menu->addAction(tr("(no eligible grouping factors in formula)"));
			placeholder->setEnabled(false);
		}
	}

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
			// Only a main-effect (singleton) slope counts; an interaction slope like
			// a:b does not satisfy "a is already a slope".
			auto *corr_action = corr_slope_menu->addAction(group_q);
			bool already_slope = false;
			for (intptr_t s = 1; s <= rt.slopes.size(); s++) {
				const auto &st = rt.slopes[s];
				if (st.variables.size() == 1 && st.variables[1] == name_s) {
					already_slope = true;
					break;
				}
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

	// ── Offset ──────────────────────────────────────────────────────

	auto *offset_action = menu.addAction(tr("Add as offset"));
	// Offset is only meaningful for numeric columns.
	if (m_analysis->has_source()) {
		offset_action->setEnabled(isColumnNumeric(String(name.toUtf8().constData())));
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
	else if (chosen->parent() == nested_menu) {
		addNestedGroupingFactor(name, chosen->data().toString());
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
	else if (chosen == offset_action) {
		// Insert offset(name) into the formula.
		QString quoted = quoteIfNeeded(name);
		QString term = QStringLiteral("offset(") + quoted + QStringLiteral(")");
		QString text = m_formula_edit->text().trimmed();
		if (text.isEmpty()) {
			m_formula_edit->setText(QStringLiteral("~ ") + term);
		} else if (!text.contains('~')) {
			m_formula_edit->setText(text + QStringLiteral(" ~ ") + term);
		} else {
			// Remove any existing offset() term before adding the new one.
			static QRegularExpression offset_re(QStringLiteral(R"(\+?\s*offset\([^)]*\)\s*)"));
			QString rhs = text.mid(text.indexOf('~') + 1);
			rhs.replace(offset_re, QStringLiteral(" "));
			rhs = rhs.trimmed();
			QString lhs = text.left(text.indexOf('~') + 1);
			if (rhs.isEmpty() || rhs == QStringLiteral("1")) {
				m_formula_edit->setText(lhs + QStringLiteral(" ") + term);
			} else {
				// Strip leading '+' that might be left after removal.
				if (rhs.startsWith('+')) rhs = rhs.mid(1).trimmed();
				m_formula_edit->setText(lhs + QStringLiteral(" ") + rhs + QStringLiteral(" + ") + term);
			}
		}
		m_formula_edit->setFocus();
		m_formula_edit->setCursorPosition(m_formula_edit->text().length());
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

// Append a nested random-effects term: (1 | outer:inner). This is
// the bare-colon synthetic-group form, semantically equivalent to
// the lme4 slash-sugar (1|outer/inner). The existing (1|outer)
// term (if any) is left untouched, so the resulting model is
//   ... + (1|outer) + (1|outer:inner)
// which is exactly how (1|outer/inner) desugars at parse time.
//
// If the outer term has random slopes — e.g. (1+time|outer) — the
// slopes are NOT propagated onto the synthetic group. lme4-style
// (1+time|outer/inner) would propagate, but slopes on a synthetic
// group require multiple obs per (outer, inner) cell to estimate
// and that's rarely what the user wants. If propagation is desired,
// edit the formula directly to use the slash form.
void AnalysisView::addNestedGroupingFactor(const QString &inner, const QString &outer)
{
	QString inner_q = quoteIfNeeded(inner);
	QString outer_q = quoteIfNeeded(outer);
	QString term = QStringLiteral("(1 | ") + outer_q + QStringLiteral(":") + inner_q + QStringLiteral(")");
	QString text = m_formula_edit->text().trimmed();

	if (text.isEmpty() || !text.contains('~'))
	{
		// The outer term was supposedly present in `parsed`, so the
		// formula must already contain a '~'. This branch is defensive
		// only — fall through to the same append behaviour as the
		// random-intercept helper.
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
			rt.slopes.append(stats::FixedTerm(var_s));
		}
		else if (rt.slopes.empty())
		{
			// Intercept-only: replace intercept with slope in place.
			// (1 | group) → (0 + X | group).
			// The user can re-add a random intercept separately if needed.
			rt.intercept = false;
			rt.slopes.append(stats::FixedTerm(var_s));
		}
		else
		{
			// Already has slopes: add a separate term so the new slope
			// is estimated independently.
			// (1 + Y | group) → (1 + Y | group) + (0 + X | group).
			stats::RandomTerm new_rt;
			new_rt.group = group_s;
			new_rt.slopes.append(stats::FixedTerm(var_s));
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

		// Remove as slope: drop any slope that mentions this variable, whether
		// as a main effect or as a component of an interaction. Mirrors the
		// fixed-side removal logic above.
		for (intptr_t s = rt.slopes.size(); s >= 1; s--) {
			const auto &st = rt.slopes[s];
			bool contains_s = false;
			for (intptr_t v = 1; v <= st.variables.size(); v++) {
				if (st.variables[v] == name_s) { contains_s = true; break; }
			}
			if (contains_s) {
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

	// Remove offset if this variable is the offset.
	if (parsed->offset == name_s) {
		parsed->offset = String();
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
		m_posterior_predictors_label->setVisible(false);
		m_posterior_predictors_combo->setVisible(false);
		updateDetachActionsEnabled();
		return;
	}

	auto &m = m_analysis->model(m_current_model);

	// Add or remove the Bayesian-only plot types based on the current model.
	// The two entries are appended in fixed order ("Posterior Predictive
	// Check" before "Posterior Densities") so the user sees the diagnostic
	// (does the family fit?) before the inferential view (where are the
	// coefficients?).  Removed cleanly when switching to a frequentist model.
	{
		QSignalBlocker blocker(m_plot_type_combo);
		const QString ppc_label       = tr("Posterior Predictive Check");
		const QString posterior_label = tr("Posterior Densities");

		auto remove_if_present = [&](const QString &label) {
			int idx = m_plot_type_combo->findText(label);
			if (idx >= 0) {
				if (m_plot_type_combo->currentIndex() == idx)
					m_plot_type_combo->setCurrentIndex(0);
				m_plot_type_combo->removeItem(idx);
			}
		};

		if (m.is_bayesian())
		{
			if (m_plot_type_combo->findText(ppc_label) < 0)
				m_plot_type_combo->addItem(ppc_label);
			if (m_plot_type_combo->findText(posterior_label) < 0)
				m_plot_type_combo->addItem(posterior_label);
		}
		else
		{
			remove_if_present(ppc_label);
			remove_if_present(posterior_label);
		}
	}

	const QString plot_text = m_plot_type_combo->currentText();
	const bool is_ppc       = (plot_text == tr("Posterior Predictive Check"));
	const bool is_post_dens = (plot_text == tr("Posterior Densities"));
	const bool is_scaled    = (plot_text == tr("Scaled Residuals vs Fitted"))
	                       || (plot_text == tr("Scaled Residuals Q-Q"));

	// The predictor selector is relevant only for the posterior-densities plot
	// on a Bayesian model that actually carries posterior summaries.
	bool show_predictor_combo = is_post_dens
	                         && m.is_bayesian()
	                         && !m.posterior_mean.empty()
	                         && !m.posterior_sd.empty()
	                         && m.nfixed > 0;
	m_posterior_predictors_label->setVisible(show_predictor_combo);
	m_posterior_predictors_combo->setVisible(show_predictor_combo);

	if (plot_text == tr("Residuals vs Fitted"))
		plotResidualsVsFitted(m);
	else if (plot_text == tr("Normal Q-Q"))
		plotQQ(m);
	else if (plot_text == tr("Scaled Residuals vs Fitted"))
		plotScaledResidualsVsFitted(m);
	else if (plot_text == tr("Scaled Residuals Q-Q"))
		plotScaledResidualQQ(m);
	else if (is_ppc)
		plotPosteriorPredictiveCheck(m);
	else if (is_post_dens)
		plotPosteriorDensities(m);
	else
		m_plot->clear();

	// Show the residual-test results panel only for scaled-residual plots.
	// The PPC view writes its own short caption into the same panel, so we
	// don't clear in that case.
	if (!is_scaled && !is_ppc)
		clearTestResults();

	updateDetachActionsEnabled();
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

	// Click-to-source: each plotted point is the i-th fitted observation,
	// whose source row is m.source_rows[i] (1-based DataTable row → 0-based
	// for the Qt model). Empty source_rows means the feature is silently
	// disabled (older saved analyses).
	std::vector<intptr_t> rows;
	if (m.has_source_rows()) {
		rows.reserve(n);
		for (intptr_t i = 0; i < n; i++)
			rows.push_back(m.source_rows[(size_t) i] - 1);
	}

	m_plot->setData(std::move(x), std::move(y),
	                tr("Fitted values"), tr("Residuals"),
	                tr("Residuals vs Fitted"),
	                PlotWidget::RefLine::HorizontalAtZero,
	                false, false, {}, std::move(rows));
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

	// Click-to-source: plot point i corresponds to the idx[i]-th fitted
	// observation (the one with the i-th smallest standardised residual).
	std::vector<intptr_t> rows;
	if (m.has_source_rows()) {
		rows.reserve(n);
		for (intptr_t i = 0; i < n; i++)
			rows.push_back(m.source_rows[(size_t) idx[i]] - 1);
	}

	m_plot->setData(std::move(theoretical), std::move(sample),
	                tr("Theoretical Quantiles"), tr("Sample Quantiles"),
	                tr("Normal Q-Q"),
	                PlotWidget::RefLine::Diagonal,
	                false, false, {}, std::move(rows));
	m_plot->clearFixedYTicks();
}

const stats::ScaledResidualResult *AnalysisView::ensureScaledResiduals(const stats::Model &m)
{
	// Cache hit: this model has already been processed for the current
	// session. Return the cached result if computation succeeded, or
	// nullptr if it failed previously — the failure reason was recorded
	// in m_scaled_residuals_error and will be shown inline in the
	// scaled-residual plot views.
	if (m_scaled_residuals_model == m_current_model)
		return m_scaled_residuals ? &*m_scaled_residuals : nullptr;

	if (m.nobs == 0 || m.y.empty() || m.fitted.empty())
	{
		m_scaled_residuals.reset();
		m_scaled_residuals_model = m_current_model;
		m_scaled_residuals_error = tr("Residual diagnostics are not available: this model carries no fitted data.");
		return nullptr;
	}

	try
	{
		m_scaled_residuals = stats::compute_scaled_residuals(m);
		m_scaled_residuals_model = m_current_model;
		m_scaled_residuals_error.clear();
		return &*m_scaled_residuals;
	}
	catch (std::exception &e)
	{
		m_scaled_residuals.reset();
		m_scaled_residuals_model = m_current_model;
		m_scaled_residuals_error = QString::fromUtf8(e.what());
		return nullptr;
	}
}

void AnalysisView::plotScaledResidualsVsFitted(const stats::Model &m)
{
	auto *sr = ensureScaledResiduals(m);
	if (!sr) {
		showResidualUnavailable();
		return;
	}

	intptr_t n = m.nobs;

	// Rank-transform fitted values to [0, 1], matching DHARMa's default
	// x-axis ("Model predictions, rank transformed").  This spreads
	// observations evenly across the x-axis, making patterns visible
	// even when fitted values cluster.
	std::vector<double> fitted(n), y(n);
	for (intptr_t i = 0; i < n; i++) {
		fitted[i] = m.fitted[i + 1];
		y[i] = sr->residuals[i + 1];
	}

	// Compute ranks (average rank for ties).
	std::vector<intptr_t> order(n);
	std::iota(order.begin(), order.end(), 0);
	std::sort(order.begin(), order.end(), [&](intptr_t a, intptr_t b) {
		return fitted[a] < fitted[b];
	});

	std::vector<double> x(n);
	intptr_t i = 0;
	while (i < n)
	{
		// Find run of tied values.
		intptr_t j = i + 1;
		while (j < n && fitted[order[j]] == fitted[order[i]])
			j++;
		// Average rank for this group: midpoint of [i+1, j] mapped to (0, 1).
		double avg_rank = 0.5 * (i + 1 + j);
		double rank_scaled = avg_rank / (n + 1);
		for (intptr_t k = i; k < j; k++)
			x[order[k]] = rank_scaled;
		i = j;
	}

	// Click-to-source: plot point i corresponds to the i-th fitted
	// observation, whose source row is m.source_rows[i] (1-based). Empty
	// vector means click-to-source is silently disabled.
	std::vector<intptr_t> rows;
	if (m.has_source_rows()) {
		rows.reserve(n);
		for (intptr_t k = 0; k < n; k++)
			rows.push_back(m.source_rows[(size_t) k] - 1);
	}

	m_plot->setData(std::move(x), std::move(y),
	                tr("Model predictions (rank transformed)"), tr("Scaled residual"),
	                tr("Scaled Residuals vs Predicted"),
	                PlotWidget::RefLine::HorizontalAtHalf,
	                false, false, {}, std::move(rows));
	m_plot->setFixedYTicks({0.0, 0.25, 0.50, 0.75, 1.0});

	updateTestResults(*sr);
}

void AnalysisView::plotScaledResidualQQ(const stats::Model &m)
{
	auto *sr = ensureScaledResiduals(m);
	if (!sr) {
		showResidualUnavailable();
		return;
	}

	intptr_t n = m.nobs;

	// Indexed sort so we can map each plot point back to the original
	// observation (and hence to its source row). idx[i] is the 0-based
	// fitted-observation index whose residual lands at sorted-position i.
	std::vector<intptr_t> idx(n);
	std::iota(idx.begin(), idx.end(), 0);
	std::sort(idx.begin(), idx.end(), [&](intptr_t a, intptr_t b) {
		return sr->residuals[a + 1] < sr->residuals[b + 1];
	});

	std::vector<double> sorted(n);
	for (intptr_t i = 0; i < n; i++)
		sorted[i] = sr->residuals[idx[i] + 1];

	// Theoretical quantiles: (i + 0.5) / n
	std::vector<double> theoretical(n);
	for (intptr_t i = 0; i < n; i++)
		theoretical[i] = (i + 0.5) / n;

	// Click-to-source: plot point i corresponds to fitted obs idx[i].
	std::vector<intptr_t> rows;
	if (m.has_source_rows()) {
		rows.reserve(n);
		for (intptr_t i = 0; i < n; i++)
			rows.push_back(m.source_rows[(size_t) idx[i]] - 1);
	}

	m_plot->setData(std::move(theoretical), std::move(sorted),
	                tr("Theoretical (Uniform)"), tr("Sample"),
	                tr("Scaled Residuals Q-Q"),
	                PlotWidget::RefLine::Diagonal,
	                false, false, {}, std::move(rows));
	m_plot->setFixedYTicks({0.0, 0.25, 0.50, 0.75, 1.0});

	updateTestResults(*sr);
}


void AnalysisView::plotPosteriorDensities(const stats::Model &m)
{
	// Defensive guard. The "Posterior Densities" plot type is added to the
	// combo only for Bayesian models in updateDiagnosticPlot(), so this branch
	// is normally unreachable from the GUI; keep it as a safety net.
	if (!m.is_bayesian())
	{
		m_plot->clear();
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

	// Build the current list of non-intercept coefficient names. This is the
	// universe the user picks from in m_posterior_predictors_combo.
	QStringList current_names;
	current_names.reserve((int)ncoef);
	for (intptr_t j = 0; j < ncoef; j++)
	{
		QString name = (j + 1 <= m.coef_names.size())
			? QString::fromUtf8(m.coef_names[j + 1].data(), (int)m.coef_names[j + 1].size())
			: QStringLiteral("coef %1").arg(j + 1);
		current_names << name;
	}

	// Repopulate the combo only when the predictor set changes (e.g. another
	// model was selected). In that case default to all-checked. Block signals
	// so the repopulation doesn't re-enter updateDiagnosticPlot().
	if (current_names != m_posterior_last_predictors)
	{
		QSignalBlocker blocker(m_posterior_predictors_combo);
		m_posterior_predictors_combo->setItems(current_names);
		m_posterior_predictors_combo->checkAll(true);
		m_posterior_last_predictors = current_names;
	}

	// Current user selection. NB: CheckableComboBox displays "(all)" when zero
	// items are checked as well as when all are — unchecking everything will
	// simply yield an empty plot, and the user can re-check via "Select all"
	// in the popup.
	QStringList checked = m_posterior_predictors_combo->checkedItems();
	QSet<QString> checked_set(checked.begin(), checked.end());

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
		const QString &name = current_names[(int)j];
		if (!checked_set.contains(name)) continue;

		double mu_j = m.posterior_mean[j + 1];
		double sd_j = m.posterior_sd[j + 1];
		if (sd_j <= 0) continue;

		// Determine evaluation range: ±4 SD from posterior mean.
		double xlo = mu_j - 4.0 * sd_j;
		double xhi = mu_j + 4.0 * sd_j;
		double dx = (xhi - xlo) / (N_POINTS - 1);

		PlotWidget::LineCurve curve;
		curve.name = name;
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

	// Passing only the selected curves to setLinePlotData causes
	// PlotWidget::renderLinePlot to recompute x/y ranges from the filtered
	// data, so axis scales update automatically on every selection change.
	m_plot->setLinePlotData(std::move(curves),
	                         tr("Coefficient value"), tr("Density"),
	                         tr("Posterior Densities"));
	m_plot->clearFixedYTicks();
}


// =====================================================================
// Posterior predictive check
// =====================================================================
//
// Runs a Bayesian posterior-predictive simulation (200 draws) and
// renders the result in a family-appropriate way:
//   • Continuous (Gaussian / Student t / Beta): density overlay
//   • Bernoulli binomial: bar plot with posterior-predictive intervals
//   • Poisson / negative binomial: standing rootogram on the √-scale
// Diagnostic-side caching mirrors the scaled-residuals path, so flipping
// between the PPC view and the scaled-residual views does not retrigger
// the (200-replicate) simulation.

const stats::PosteriorPredictiveResult *
AnalysisView::ensurePosteriorPredictive(const stats::Model &m)
{
	if (m_ppc_model == m_current_model)
		return m_ppc ? &*m_ppc : nullptr;

	if (!m.is_bayesian()) {
		m_ppc.reset();
		m_ppc_model = m_current_model;
		m_ppc_error = tr("Posterior predictive checks are only available for Bayesian models.");
		return nullptr;
	}
	if (m.nobs == 0 || m.y.empty()) {
		m_ppc.reset();
		m_ppc_model = m_current_model;
		m_ppc_error = tr("Posterior predictive checks are not available: this model carries no fitted data.");
		return nullptr;
	}

	try
	{
		m_ppc = stats::compute_posterior_predictive(m);
		m_ppc_model = m_current_model;
		m_ppc_error.clear();
		return &*m_ppc;
	}
	catch (std::exception &e)
	{
		m_ppc.reset();
		m_ppc_model = m_current_model;
		m_ppc_error = QString::fromUtf8(e.what());
		return nullptr;
	}
}


void AnalysisView::plotPosteriorPredictiveCheck(const stats::Model &m)
{
	// Defensive guard: the combo entry is only added for Bayesian models.
	if (!m.is_bayesian())
	{
		m_plot->clear();
		clearTestResults();
		return;
	}

	auto *ppc = ensurePosteriorPredictive(m);
	if (!ppc)
	{
		m_plot->clear();
		QString message = m_ppc_error.isEmpty()
			? tr("Posterior predictive checks are not available for this model.")
			: m_ppc_error;
		m_test_results_text->setPlainText(message);
		m_test_results_group->setTitle(tr("Posterior predictive checks"));
		m_test_results_group->setVisible(true);
		return;
	}

	const QString title = QString::fromUtf8(ppc->title.data(), (int)ppc->title.size());
	const QString xlbl  = QString::fromUtf8(ppc->x_label.data(), (int)ppc->x_label.size());
	const QString ylbl  = QString::fromUtf8(ppc->y_label.data(), (int)ppc->y_label.size());

	switch (ppc->kind)
	{
	case stats::PpcKind::Density:
	{
		std::vector<PlotWidget::LineCurve> curves;
		curves.reserve(ppc->rep_densities.size() + 1);

		// Replicates first (background); they will be painted beneath the
		// observed curve regardless of curve order — renderLinePlot
		// partitions by `highlight` and draws backgrounds first.
		for (auto &rd : ppc->rep_densities) {
			PlotWidget::LineCurve c;
			c.x = rd.x;
			c.y = rd.y;
			c.highlight = false;
			curves.push_back(std::move(c));
		}

		// Observed (highlight).
		PlotWidget::LineCurve obs;
		obs.name = tr("y");
		obs.x = ppc->obs_density.x;
		obs.y = ppc->obs_density.y;
		obs.highlight = true;
		curves.push_back(std::move(obs));

		m_plot->setLinePlotData(std::move(curves), xlbl, ylbl, title);
		m_plot->clearFixedYTicks();
		break;
	}
	case stats::PpcKind::BinomialBars:
	case stats::PpcKind::Rootogram:
	{
		std::vector<PlotWidget::PpcBarPoint> points;
		points.reserve(ppc->discrete.size());
		for (auto &pt : ppc->discrete) {
			PlotWidget::PpcBarPoint q;
			q.x = pt.x;
			q.obs = pt.obs;
			q.exp_mean = pt.exp_mean;
			q.exp_lo = pt.exp_lo;
			q.exp_hi = pt.exp_hi;
			points.push_back(q);
		}
		bool integer_ticks = (ppc->kind == stats::PpcKind::Rootogram);
		m_plot->setPpcDiscreteData(std::move(points), xlbl, ylbl, title, integer_ticks);
		m_plot->clearFixedYTicks();
		break;
	}
	}

	// Caption-style note in the test-results panel: brief and self-explanatory.
	// The wording is family-specific because the density overlay has no
	// explicit "band" (the spread of replicate curves is the band visually),
	// whereas the discrete variants do show a 5-95% I-bar at each x.
	QString caption;
	if (ppc->kind == stats::PpcKind::Density)
	{
		caption = tr("%1 posterior draws, %2 replicate curves shown.  "
		             "Dark curve: observed y.  Light curves: replicate densities "
		             "drawn from the posterior.")
			.arg(ppc->n_replicates)
			.arg(ppc->n_overlay);
	}
	else
	{
		caption = tr("%1 posterior draws.  Bars: observed.  "
		             "Intervals span the 5\u201395% posterior predictive range.")
			.arg(ppc->n_replicates);
	}
	m_test_results_text->setPlainText(caption);
	m_test_results_group->setTitle(tr("Posterior predictive checks"));
	m_test_results_group->setVisible(true);
}


void AnalysisView::updateTestResults(const stats::ScaledResidualResult &sr)
{
	auto format_p = [](double p) -> QString {
		return (p < 0.001) ? QStringLiteral("< 0.001") : QString::number(p, 'f', 4);
	};

	QString text;

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

	m_test_results_text->setPlainText(text);
	m_test_results_group->setTitle(tr("Residual tests"));
	m_test_results_group->setVisible(true);
}

void AnalysisView::clearTestResults()
{
	m_test_results_group->setVisible(false);
	m_test_results_text->clear();
}

void AnalysisView::showResidualUnavailable()
{
	// Inline message used in place of a modal popup when scaled residuals
	// can't be computed for the current model. Confined to the scaled-residual
	// plot views; other diagnostics and the model summary are unaffected.
	m_plot->clear();

	QString message = m_scaled_residuals_error.isEmpty()
		? tr("Residual diagnostics are not available for this model.")
		: m_scaled_residuals_error;

	m_test_results_text->setPlainText(message);
	m_test_results_group->setTitle(tr("Residual diagnostics"));
	m_test_results_group->setVisible(true);
}


// =====================================================================
// EDA
// =====================================================================

void AnalysisView::onEdaPlotTypeChanged()
{
	// Read the new selection from the combo's userData (set via addItem with
	// (int)EdaPlotType::*). Falling back to Auto on any unexpected value
	// keeps the function robust to combo reorderings.
	auto data = m_eda_plot_type_combo->currentData();
	auto new_type = data.isValid() ? static_cast<EdaPlotType>(data.toInt())
	                                : EdaPlotType::Auto;

	// Apply defaults only when the type genuinely changes — otherwise every
	// updateEdaPlot would clobber per-session user toggles.
	if (new_type != m_last_eda_plot_type)
	{
		// Reset every variable slot to (None) so the user starts fresh.
		// Without this, switching from a grouped Scatter to a Histogram
		// silently inherits the prior Group selection, producing an
		// unexpected overlay; the user's complaint was exactly this.
		// blockSignals prevents an avalanche of onEdaChanged from each
		// individual combo reset — updateEdaPlot will fire once at the end
		// via onEdaChanged.
		auto reset = [](QComboBox *c) {
			if (!c) return;
			c->blockSignals(true);
			c->setCurrentIndex(0);
			c->blockSignals(false);
		};
		reset(m_eda_x_combo);
		reset(m_eda_y_combo);
		reset(m_eda_group_combo);
		reset(m_eda_pool_combo);
		reset(m_eda_style_combo);
		reset(m_eda_label_combo);
		reset(m_eda_facet_combo);

		// Drop any user customizations — the title/labels/ranges were tied
		// to the previous plot and no longer fit. The user can re-open the
		// dialog after picking new variables.
		m_eda_customization.clear();
		setEdaHint(QString());

		applyEdaPlotTypeDefaults(new_type);
		m_last_eda_plot_type = new_type;
	}
	onEdaChanged();
}

void AnalysisView::applyEdaPlotTypeDefaults(EdaPlotType type)
{
	// Block signals on the checkboxes we're touching so the cascade of
	// `toggled` signals doesn't trigger updateEdaPlot mid-update — we want
	// onEdaChanged to fire once at the end (in onEdaPlotTypeChanged).
	auto guard = [](QCheckBox *c, bool checked) {
		c->blockSignals(true);
		c->setChecked(checked);
		c->blockSignals(false);
	};

	// Slot labels reflect the chart's variable conventions. Formant chart
	// renames "X:" / "Y:" to "F2:" / "F1:" to match phonetic practice; every
	// other type uses the neutral labels. Always reset back to X/Y when
	// leaving Formant chart so the labels don't get stuck.
	if (m_eda_x_slot_label) {
		m_eda_x_slot_label->setText(type == EdaPlotType::FormantChart
		                            ? tr("F2:") : tr("X:"));
	}
	if (m_eda_y_slot_label) {
		m_eda_y_slot_label->setText(type == EdaPlotType::FormantChart
		                            ? tr("F1:") : tr("Y:"));
	}

	switch (type)
	{
	case EdaPlotType::FormantChart:
		// FormantChart's identity is reversed axes (handled inside
		// updateEdaPlot via is_formant_chart), not extra overlays.
		// Raw formant clouds are useful too — leave Mean and Ellipse OFF
		// by default so the user opts in. Same for the other overlays.
		guard(m_eda_mean_check, false);
		guard(m_eda_ellipse_check, false);
		guard(m_eda_formant_check, false);
		guard(m_eda_regline_check, false);
		guard(m_eda_density_check, false);
		if (m_eda_ellipse_spin) m_eda_ellipse_spin->setValue(95);
		break;
	case EdaPlotType::Scatter:
		// Fresh scatter: all overlays off so the user opts in.
		guard(m_eda_mean_check, false);
		guard(m_eda_ellipse_check, false);
		guard(m_eda_formant_check, false);
		guard(m_eda_regline_check, false);
		guard(m_eda_density_check, false);
		break;
	case EdaPlotType::Histogram:
	case EdaPlotType::BarChart:
	case EdaPlotType::BoxPlot:
		guard(m_eda_density_check, false);
		guard(m_eda_regline_check, false);
		break;
	case EdaPlotType::Heatmap:
		// Heatmap is a 2-D contingency table — no overlays apply. Group /
		// Facet are not used (the two categorical axes already saturate
		// the visual dimensions); they're hidden by updateEdaPlot's
		// visibility logic, but turning off any leftover toggles keeps
		// the customize state clean.
		guard(m_eda_density_check, false);
		guard(m_eda_regline_check, false);
		guard(m_eda_mean_check, false);
		guard(m_eda_ellipse_check, false);
		guard(m_eda_formant_check, false);
		break;
	case EdaPlotType::Proportion:
		// Proportion plot computes mean(Y) per X level with Wilson CIs.
		// Optional Group splits each X level into one curve per Group
		// level. Other overlays don't apply.
		guard(m_eda_density_check, false);
		guard(m_eda_regline_check, false);
		guard(m_eda_mean_check, false);
		guard(m_eda_ellipse_check, false);
		guard(m_eda_formant_check, false);
		break;
	case EdaPlotType::Auto:
		// Auto preserves whatever state the user had: the existing
		// inference takes over and we don't second-guess their toggles.
		break;
	}
}

void AnalysisView::onEdaChanged()
{
	updateEdaPlot();
	updateEdaSummary();
	updateDetachActionsEnabled();
}

void AnalysisView::setEdaHint(const QString &msg)
{
	// The hint label lives between the plot widget and the controls strip
	// (see setupUi). Hidden when empty so the layout doesn't reserve dead
	// vertical space when nothing's wrong.
	if (!m_eda_hint_label) return;
	if (msg.isEmpty()) {
		m_eda_hint_label->clear();
		m_eda_hint_label->setVisible(false);
	} else {
		m_eda_hint_label->setText(msg);
		m_eda_hint_label->setVisible(true);
	}
}

void AnalysisView::onCustomizeEda()
{
	// Opens a modal dialog with seven form fields. Pre-fills from the
	// current m_eda_customization so the user sees their last settings.
	// On Accept, copy field values back into m_eda_customization and
	// trigger a re-render. On Reset, clear customization in-place.
	// Cancel discards the form edits.

	QDialog dlg(this);
	dlg.setWindowTitle(tr("Customize plot"));
	dlg.setMinimumWidth(360);

	auto *form = new QFormLayout;
	form->setLabelAlignment(Qt::AlignRight);

	auto *title_edit = new QLineEdit(m_eda_customization.title);
	title_edit->setPlaceholderText(tr("(use auto-generated title)"));
	form->addRow(tr("Title:"), title_edit);

	auto *xlabel_edit = new QLineEdit(m_eda_customization.x_label);
	xlabel_edit->setPlaceholderText(tr("(use variable name)"));
	form->addRow(tr("X label:"), xlabel_edit);

	auto *ylabel_edit = new QLineEdit(m_eda_customization.y_label);
	ylabel_edit->setPlaceholderText(tr("(use variable name)"));
	form->addRow(tr("Y label:"), ylabel_edit);

	// Axis range fields. Empty = auto-fit. A locale-tolerant validator
	// accepts dot/comma decimal separators without locking the user out
	// while they're still typing (Intermediate is treated as acceptable).
	auto make_range_field = [](const std::optional<double> &v) {
		auto *e = new QLineEdit;
		e->setPlaceholderText(QObject::tr("required"));
		auto *val = new QDoubleValidator(e);
		val->setNotation(QDoubleValidator::StandardNotation);
		e->setValidator(val);
		if (v.has_value()) e->setText(QString::number(*v));
		return e;
	};
	auto *xmin_edit = make_range_field(m_eda_customization.x_min);
	auto *xmax_edit = make_range_field(m_eda_customization.x_max);
	auto *ymin_edit = make_range_field(m_eda_customization.y_min);
	auto *ymax_edit = make_range_field(m_eda_customization.y_max);

	// X/Y range live on one row each as min/max pairs so the dialog stays
	// compact. Layout: [min] – [max].
	auto make_range_row = [](QLineEdit *lo, QLineEdit *hi) {
		auto *w = new QWidget;
		auto *l = new QHBoxLayout(w);
		l->setContentsMargins(0, 0, 0, 0);
		l->addWidget(lo);
		l->addWidget(new QLabel(QStringLiteral(" \u2013 ")));
		l->addWidget(hi);
		return w;
	};

	// "Custom X range" / "Custom Y range" checkboxes sit in the form's
	// label slot and gate the min/max fields. Unchecked = auto-fit
	// (fields disabled, greyed out). Checked = override (fields enabled
	// and both required at Accept time, enforced below). Pre-check the
	// box if the user already had a range saved from a previous open.
	bool x_initial = m_eda_customization.x_min.has_value()
	              && m_eda_customization.x_max.has_value();
	bool y_initial = m_eda_customization.y_min.has_value()
	              && m_eda_customization.y_max.has_value();

	auto *xrange_check = new QCheckBox(tr("Custom X range:"));
	xrange_check->setChecked(x_initial);
	xmin_edit->setEnabled(x_initial);
	xmax_edit->setEnabled(x_initial);
	connect(xrange_check, &QCheckBox::toggled, &dlg,
	        [xmin_edit, xmax_edit](bool on) {
		xmin_edit->setEnabled(on);
		xmax_edit->setEnabled(on);
		if (on) xmin_edit->setFocus();
	});

	auto *yrange_check = new QCheckBox(tr("Custom Y range:"));
	yrange_check->setChecked(y_initial);
	ymin_edit->setEnabled(y_initial);
	ymax_edit->setEnabled(y_initial);
	connect(yrange_check, &QCheckBox::toggled, &dlg,
	        [ymin_edit, ymax_edit](bool on) {
		ymin_edit->setEnabled(on);
		ymax_edit->setEnabled(on);
		if (on) ymin_edit->setFocus();
	});

	// QFormLayout::addRow accepts a QWidget* as the left-hand cell, so
	// the checkbox can replace the usual QLabel.
	form->addRow(xrange_check, make_range_row(xmin_edit, xmax_edit));
	form->addRow(yrange_check, make_range_row(ymin_edit, ymax_edit));

	// Facet columns spinbox. 0 means auto.
	auto *facet_ncols_spin = new QSpinBox;
	facet_ncols_spin->setRange(0, 8);
	facet_ncols_spin->setSpecialValueText(tr("auto"));
	facet_ncols_spin->setValue(m_eda_customization.facet_ncols);
	facet_ncols_spin->setToolTip(tr("Columns per row in faceted layouts. "
	                                 "0 = auto (capped at 4 for readability)."));
	form->addRow(tr("Facet columns:"), facet_ncols_spin);

	// Buttons: Reset, then standard Cancel/OK.
	auto *bbox = new QDialogButtonBox(
	    QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset);
	// Validate ranges before accepting: when a "Custom range" box is
	// checked, both min and max must parse to finite numbers. If not,
	// warn and keep the dialog open so the user can fix the field. We
	// route accepted -> our lambda (instead of QDialog::accept directly)
	// so we control the gate.
	auto parse_check = [](QLineEdit *e) -> std::optional<double> {
		QString s = e->text().trimmed();
		if (s.isEmpty()) return std::nullopt;
		s.replace(',', '.');
		bool ok = false;
		double d = s.toDouble(&ok);
		if (!ok || !std::isfinite(d)) return std::nullopt;
		return d;
	};
	auto try_accept = [&]() {
		if (xrange_check->isChecked()) {
			auto lo = parse_check(xmin_edit);
			auto hi = parse_check(xmax_edit);
			if (!lo.has_value() || !hi.has_value()) {
				QMessageBox::warning(&dlg, tr("Custom X range"),
				    tr("Custom X range needs both min and max set to "
				       "valid numbers. Uncheck \u201cCustom X range\u201d "
				       "to use the auto range instead."));
				return;
			}
		}
		if (yrange_check->isChecked()) {
			auto lo = parse_check(ymin_edit);
			auto hi = parse_check(ymax_edit);
			if (!lo.has_value() || !hi.has_value()) {
				QMessageBox::warning(&dlg, tr("Custom Y range"),
				    tr("Custom Y range needs both min and max set to "
				       "valid numbers. Uncheck \u201cCustom Y range\u201d "
				       "to use the auto range instead."));
				return;
			}
		}
		dlg.accept();
	};
	connect(bbox, &QDialogButtonBox::accepted, &dlg, try_accept);
	connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	connect(bbox->button(QDialogButtonBox::Reset), &QPushButton::clicked,
	        &dlg, [&]() {
		// Reset clears the form fields and (on Accept) the customization.
		// Unchecking the range boxes also disables the now-empty min/max
		// fields, giving consistent visual state.
		title_edit->clear();
		xlabel_edit->clear();
		ylabel_edit->clear();
		xmin_edit->clear();
		xmax_edit->clear();
		ymin_edit->clear();
		ymax_edit->clear();
		xrange_check->setChecked(false);
		yrange_check->setChecked(false);
		facet_ncols_spin->setValue(0);
	});

	auto *dlg_layout = new QVBoxLayout(&dlg);
	dlg_layout->addLayout(form);
	dlg_layout->addWidget(bbox);

	if (dlg.exec() != QDialog::Accepted) return;

	// Parse field values into the customization struct. A blank field is
	// "use auto"; a value that fails to parse is treated as blank rather
	// than producing a user-hostile error. Range fields are only consulted
	// when the matching checkbox is on — try_accept already verified both
	// fields parse when the checkbox is on, so the parses here can't fail.
	auto parse_opt = [](QLineEdit *e) -> std::optional<double> {
		QString s = e->text().trimmed();
		if (s.isEmpty()) return std::nullopt;
		// Tolerate comma decimals from FR locales.
		s.replace(',', '.');
		bool ok = false;
		double d = s.toDouble(&ok);
		if (!ok || !std::isfinite(d)) return std::nullopt;
		return d;
	};

	m_eda_customization.title    = title_edit->text().trimmed();
	m_eda_customization.x_label  = xlabel_edit->text().trimmed();
	m_eda_customization.y_label  = ylabel_edit->text().trimmed();
	if (xrange_check->isChecked()) {
		m_eda_customization.x_min = parse_opt(xmin_edit);
		m_eda_customization.x_max = parse_opt(xmax_edit);
	} else {
		m_eda_customization.x_min.reset();
		m_eda_customization.x_max.reset();
	}
	if (yrange_check->isChecked()) {
		m_eda_customization.y_min = parse_opt(ymin_edit);
		m_eda_customization.y_max = parse_opt(ymax_edit);
	} else {
		m_eda_customization.y_min.reset();
		m_eda_customization.y_max.reset();
	}
	m_eda_customization.facet_ncols = facet_ncols_spin->value();

	// If the user inverted min/max (e.g. typed 200 in min, 100 in max),
	// silently swap so the renderer doesn't get a degenerate range.
	auto fix = [](std::optional<double> &lo, std::optional<double> &hi) {
		if (lo.has_value() && hi.has_value() && *lo > *hi)
			std::swap(lo, hi);
	};
	fix(m_eda_customization.x_min, m_eda_customization.x_max);
	fix(m_eda_customization.y_min, m_eda_customization.y_max);

	// Re-render with new customization applied.
	updateEdaPlot();
}

void AnalysisView::onRefreshEdaPlot()
{
	// updateEdaPlot() reads cells fresh from m_analysis->data()->get_cell()
	// on every call, so simply re-running it picks up any edits the user
	// made to the source dataset since the last render. No invalidation
	// needed: the DataTable is the single source of truth.
	updateEdaPlot();
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

QString AnalysisView::edaSummaryToText(QChar sep, bool csv_quote) const
{
	int nrows = m_eda_summary->rowCount();
	int ncols = m_eda_summary->columnCount();
	if (nrows == 0 || ncols == 0) return QString();

	auto quote = [&](const QString &s) -> QString {
		if (!csv_quote) return s;
		bool need = s.contains(sep) || s.contains(QLatin1Char('"'))
		         || s.contains(QLatin1Char('\n')) || s.contains(QLatin1Char('\r'));
		if (!need) return s;
		QString out = s;
		out.replace(QStringLiteral("\""), QStringLiteral("\"\""));
		return QLatin1Char('"') + out + QLatin1Char('"');
	};

	QString text;
	// Header row.
	for (int c = 0; c < ncols; c++) {
		if (c > 0) text += sep;
		auto *hdr = m_eda_summary->horizontalHeaderItem(c);
		text += quote(hdr ? hdr->text() : QString());
	}
	text += QLatin1Char('\n');

	// Data rows.
	for (int r = 0; r < nrows; r++) {
		for (int c = 0; c < ncols; c++) {
			if (c > 0) text += sep;
			auto *item = m_eda_summary->item(r, c);
			text += quote(item ? item->text() : QString());
		}
		text += QLatin1Char('\n');
	}
	return text;
}

void AnalysisView::onCopyEdaSummary()
{
	QString text = edaSummaryToText(QLatin1Char('\t'), /*csv_quote=*/false);
	if (text.isEmpty()) return;
	QApplication::clipboard()->setText(text);

	auto *main_win = qobject_cast<QMainWindow *>(window());
	if (main_win && main_win->statusBar()) {
		main_win->statusBar()->showMessage(tr("Summary copied to clipboard"), 2000);
	}
}

void AnalysisView::onSaveEdaSummary()
{
	if (m_eda_summary->rowCount() == 0 || m_eda_summary->columnCount() == 0) {
		QMessageBox::information(this, tr("Save summary"),
		                          tr("The summary table is empty."));
		return;
	}
	QString path = getSaveFileName(this,
		tr("Save summary as CSV"), tr("CSV file (*.csv)"));
	if (path.isEmpty()) return;
	if (!path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
		path += QStringLiteral(".csv");

	QString text = edaSummaryToText(QLatin1Char(','), /*csv_quote=*/true);
	QFile file(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		file.write(text.toUtf8());
	} else {
		QMessageBox::warning(this, tr("Save summary"),
		                      tr("Could not open file for writing: %1").arg(path));
	}
}

void AnalysisView::onEdaSummaryContextMenu(const QPoint &pos)
{
	if (m_eda_summary->rowCount() == 0 || m_eda_summary->columnCount() == 0)
		return;

	QMenu menu(m_eda_summary);
	auto *copy_action = menu.addAction(tr("Copy"));
	auto *save_action = menu.addAction(tr("Save as CSV..."));

	QAction *chosen = menu.exec(m_eda_summary->viewport()->mapToGlobal(pos));
	if (chosen == copy_action) onCopyEdaSummary();
	else if (chosen == save_action) onSaveEdaSummary();
}

// ── Detach / reattach implementation ─────────────────────────────────
//
// The detachPlot()/reattachPlot() helpers carry the full mechanism.
// The per-panel slots are thin wrappers: they hand the corresponding
// DetachablePlot struct to the helper, which migrates the plot widget
// into a floating QWidget (with its own toolbar exposing the same
// Save... menu the home toolbar provides, plus a Reattach action) and
// inserts a placeholder into the home layout. eventFilter dispatches
// Close events on any of the three floating windows back to the
// matching reattach slot, so closing the window mirrors clicking
// Reattach.

void AnalysisView::detachPlot(DetachablePlot &dp)
{
	if (dp.float_window) {
		// Already detached — just raise the window.
		dp.float_window->raise();
		dp.float_window->activateWindow();
		return;
	}

	// Floating window. Parent is `this` with Qt::Window so it floats
	// independently but is destroyed when the AnalysisView closes.
	dp.float_window = new QWidget(this, Qt::Window);
	dp.float_window->setWindowTitle(dp.window_title);
	dp.float_window->setAttribute(Qt::WA_DeleteOnClose, false);
	dp.float_window->installEventFilter(this);

	auto *layout = new QVBoxLayout(dp.float_window);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	// Toolbar: Save... popup menu + Reattach action.
	auto *toolbar = new QToolBar;
	toolbar->setIconSize(QSize(20, 20));
	toolbar->setMovable(false);

	auto *save_menu = new QMenu(dp.float_window);
	if (dp.save_png)
		save_menu->addAction(tr("Save as PNG..."), this, [&dp]() { if (dp.save_png) dp.save_png(); });
	if (dp.save_pdf)
		save_menu->addAction(tr("Save as PDF..."), this, [&dp]() { if (dp.save_pdf) dp.save_pdf(); });
	if (dp.save_svg)
		save_menu->addAction(tr("Save as SVG..."), this, [&dp]() { if (dp.save_svg) dp.save_svg(); });

	auto *save_action = new QAction(QIcon(":/icons/save.svg"), tr("Save as..."), dp.float_window);
	save_action->setMenu(save_menu);
	toolbar->addAction(save_action);
	if (auto *btn = qobject_cast<QToolButton *>(toolbar->widgetForAction(save_action)))
		btn->setPopupMode(QToolButton::InstantPopup);

	toolbar->addSeparator();
	auto *reattach_action = toolbar->addAction(QIcon(":/icons/minimize.svg"), tr("Reattach"));
	reattach_action->setToolTip(tr("Return the plot to the analysis tab"));
	// Dispatch through the matching slot so eventFilter and explicit
	// reattach clicks share one code path.
	if (&dp == &m_eda_detach)
		connect(reattach_action, &QAction::triggered, this, &AnalysisView::onReattachEdaPlot);
	else if (&dp == &m_diag_detach)
		connect(reattach_action, &QAction::triggered, this, &AnalysisView::onReattachDiagPlot);
	else if (&dp == &m_effects_detach)
		connect(reattach_action, &QAction::triggered, this, &AnalysisView::onReattachEffectsPlot);

	layout->addWidget(toolbar);

	// Migrate the plot widget into the floating window.
	dp.home_layout->removeWidget(dp.plot);
	layout->addWidget(dp.plot, 1);
	dp.plot->show();

	// Placeholder in the home tab where the plot used to live.
	dp.placeholder = new QLabel(dp.placeholder_text);
	dp.placeholder->setAlignment(Qt::AlignCenter);
	dp.placeholder->setWordWrap(true);
	QPalette pal = dp.placeholder->palette();
	pal.setColor(QPalette::WindowText, pal.color(QPalette::Disabled, QPalette::WindowText));
	dp.placeholder->setPalette(pal);
	dp.home_layout->insertWidget(dp.home_index, dp.placeholder, dp.home_stretch);

	dp.float_window->resize(700, 500);
	dp.float_window->show();
	dp.float_window->raise();

	updateDetachActionsEnabled();
}

void AnalysisView::reattachPlot(DetachablePlot &dp)
{
	if (!dp.float_window) return;

	auto *float_layout = dp.float_window->layout();
	if (float_layout)
		float_layout->removeWidget(dp.plot);

	if (dp.placeholder) {
		dp.home_layout->removeWidget(dp.placeholder);
		delete dp.placeholder;
		dp.placeholder = nullptr;
	}

	dp.home_layout->insertWidget(dp.home_index, dp.plot, dp.home_stretch);
	dp.plot->show();

	dp.float_window->removeEventFilter(this);
	dp.float_window->hide();
	dp.float_window->deleteLater();
	dp.float_window = nullptr;

	updateDetachActionsEnabled();
}

void AnalysisView::updateDetachActionsEnabled()
{
	// Enable each detach action iff the corresponding plot has data
	// AND it is not already detached. While detached we leave the
	// action enabled so it acts as a "raise the window" shortcut
	// (see the early-out in detachPlot()).
	auto gate = [](const DetachablePlot &dp) {
		if (!dp.detach_action || !dp.plot) return;
		bool detached = (dp.float_window != nullptr);
		dp.detach_action->setEnabled(detached || dp.plot->hasData());
	};
	gate(m_eda_detach);
	gate(m_diag_detach);
	gate(m_effects_detach);
}

void AnalysisView::onDetachEdaPlot()      { detachPlot(m_eda_detach); }
void AnalysisView::onReattachEdaPlot()    { reattachPlot(m_eda_detach); }
void AnalysisView::onDetachDiagPlot()     { detachPlot(m_diag_detach); }
void AnalysisView::onReattachDiagPlot()   { reattachPlot(m_diag_detach); }
void AnalysisView::onDetachEffectsPlot()  { detachPlot(m_effects_detach); }
void AnalysisView::onReattachEffectsPlot(){ reattachPlot(m_effects_detach); }

bool AnalysisView::eventFilter(QObject *obj, QEvent *event)
{
	// When any of the floating plot windows is closed, reattach the
	// matching plot — closing the window is treated as an implicit
	// Reattach.
	if (event->type() == QEvent::Close) {
		if (obj == m_eda_detach.float_window) {
			onReattachEdaPlot();
			return true;
		}
		if (obj == m_diag_detach.float_window) {
			onReattachDiagPlot();
			return true;
		}
		if (obj == m_effects_detach.float_window) {
			onReattachEffectsPlot();
			return true;
		}
	}
	return View::eventFilter(obj, event);
}

bool AnalysisView::isColumnNumeric(const String &col_name) const
{
	// Virtual model columns are always numeric.
	QString qname = QString::fromUtf8(col_name.data(), (int)col_name.size());
	if (isVirtualEdaColumn(qname)) return true;

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

// =====================================================================
// EDA virtual model columns
// =====================================================================

bool AnalysisView::isVirtualEdaColumn(const QString &name)
{
	return name == QStringLiteral("(fitted)")
	    || name == QStringLiteral("(residuals)")
	    || name == QStringLiteral("(scaled residuals)");
}

Array<String> AnalysisView::buildVirtualEdaCells(const QString &name)
{
	// Early-out cases: no model, no source, or unknown virtual name.
	if (m_current_model < 0 || m_current_model >= m_analysis->model_count())
		return Array<String>();
	if (!m_analysis->has_source())
		return Array<String>();
	if (!isVirtualEdaColumn(name))
		return Array<String>();

	auto &m = m_analysis->model(m_current_model);
	if (!m.has_source_rows())
		return Array<String>();

	intptr_t nr = m_analysis->data()->row_count();

	// Resolve the source array of per-observation values for this virtual name.
	Array<double> aligned;
	if (name == QStringLiteral("(fitted)"))
	{
		if (m.fitted.empty()) return Array<String>();
		aligned = m.fitted_aligned(nr);
	}
	else if (name == QStringLiteral("(residuals)"))
	{
		if (m.residuals.empty()) return Array<String>();
		aligned = m.residuals_aligned(nr);
	}
	else // (scaled residuals)
	{
		auto *sr = ensureScaledResiduals(m);
		if (!sr || sr->residuals.empty()) return Array<String>();
		aligned = m.align_to_source(sr->residuals, nr);
	}

	if (aligned.empty()) return Array<String>();

	// Format each aligned value as a String, using "nan" for NaN entries so
	// downstream to_float / empty-cell checks treat excluded rows as missing,
	// matching the convention at fitting.cpp:145.
	Array<String> cells(nr, String());
	for (intptr_t r = 1; r <= nr; r++)
	{
		double v = aligned[r];
		if (std::isnan(v))
			cells[r] = String("nan");
		else
			cells[r] = String::format("%.17g", v);
	}
	return cells;
}

void AnalysisView::refreshEdaVirtualColumns()
{
	// Remember the current text selection so we can preserve it if still valid.
	QString x_selected = m_eda_x_combo->currentText();
	QString y_selected = m_eda_y_combo->currentText();

	// Strip any existing virtual entries and their separator. Real columns
	// plus the leading "(None)" stay; virtual entries always sit at the end.
	auto strip_virtuals = [](QComboBox *combo) {
		for (int i = combo->count() - 1; i >= 0; i--)
		{
			QString t = combo->itemText(i);
			if (t.isEmpty() || isVirtualEdaColumn(t)) {
				combo->removeItem(i);
			}
		}
	};

	// We need to block signals on the combos during restructuring, otherwise
	// removeItem() / addItem() will fire currentIndexChanged mid-rebuild and
	// re-enter updateEdaPlot with inconsistent state.
	QSignalBlocker bx(m_eda_x_combo);
	QSignalBlocker by(m_eda_y_combo);

	strip_virtuals(m_eda_x_combo);
	strip_virtuals(m_eda_y_combo);

	// Only add virtual entries if there's a current model with aligned data.
	bool offer_virtuals = false;
	if (m_current_model >= 0 && m_current_model < m_analysis->model_count()
	    && m_analysis->has_source())
	{
		auto &m = m_analysis->model(m_current_model);
		offer_virtuals = m.has_source_rows() && !m.fitted.empty();
	}

	if (offer_virtuals)
	{
		auto add_virtual_block = [](QComboBox *combo) {
			combo->insertSeparator(combo->count());
			combo->addItem(QStringLiteral("(fitted)"));
			combo->addItem(QStringLiteral("(residuals)"));
			combo->addItem(QStringLiteral("(scaled residuals)"));
		};
		add_virtual_block(m_eda_x_combo);
		add_virtual_block(m_eda_y_combo);
	}

	// Restore previous selection if still present; otherwise fall back to
	// index 0 ("(None)").
	auto restore = [](QComboBox *combo, const QString &sel) {
		int idx = combo->findText(sel);
		combo->setCurrentIndex(idx >= 0 ? idx : 0);
	};
	restore(m_eda_x_combo, x_selected);
	restore(m_eda_y_combo, y_selected);
}

void AnalysisView::updateEdaPlot()
{
	auto hideAllControls = [&]() {
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
		m_eda_facet_label->setVisible(false);
		m_eda_facet_combo->setVisible(false);
		m_eda_mean_check->setVisible(false);
		m_eda_ellipse_check->setVisible(false);
		m_eda_ellipse_spin->setVisible(false);
		m_eda_formant_check->setVisible(false);
		// Plot-type combo stays visible — user can keep picking. The hint
		// label is cleared here because we have no useful diagnostic to
		// show when X itself isn't selected.
		m_eda_hint_label->clear();
		m_eda_hint_label->setVisible(false);
	};

	// Early-out: no source, or X = (None).
	if (!m_analysis->has_source() || m_eda_x_combo->currentIndex() <= 0) {
		m_eda_plot->clear();
		hideAllControls();
		return;
	}

	auto *dt = m_analysis->data();
	intptr_t nc = dt->column_count();
	intptr_t nr = dt->row_count();

	// ── Resolve X column ───────────────────────────────────────────
	auto x_name_q = m_eda_x_combo->currentText();
	auto x_name = String(x_name_q.toUtf8().constData());
	intptr_t x_col = 0;
	for (intptr_t j = 1; j <= nc; j++) {
		if (dt->get_header(j) == x_name) { x_col = j; break; }
	}
	Array<String> x_virtual_cells;
	bool x_virtual = false;
	if (x_col < 1) {
		if (isVirtualEdaColumn(x_name_q)) {
			x_virtual_cells = buildVirtualEdaCells(x_name_q);
			x_virtual = !x_virtual_cells.empty();
		}
		if (!x_virtual) { m_eda_plot->clear(); hideAllControls(); return; }
	}
	auto xc = [&](intptr_t r) -> String {
		return x_virtual ? x_virtual_cells[r] : dt->get_cell(r, x_col);
	};

	// ── Resolve Y column (optional) ─────────────────────────────────
	bool has_y = (m_eda_y_combo->currentIndex() > 0);
	QString y_name_q;
	String y_name;
	intptr_t y_col = 0;
	Array<String> y_virtual_cells;
	bool y_virtual = false;
	if (has_y) {
		y_name_q = m_eda_y_combo->currentText();
		y_name = String(y_name_q.toUtf8().constData());
		for (intptr_t j = 1; j <= nc; j++) {
			if (dt->get_header(j) == y_name) { y_col = j; break; }
		}
		if (y_col < 1) {
			if (isVirtualEdaColumn(y_name_q)) {
				y_virtual_cells = buildVirtualEdaCells(y_name_q);
				y_virtual = !y_virtual_cells.empty();
			}
			if (!y_virtual) { m_eda_plot->clear(); hideAllControls(); return; }
		}
	}
	auto yc = [&](intptr_t r) -> String {
		return y_virtual ? y_virtual_cells[r]
		                 : (y_col >= 1 ? dt->get_cell(r, y_col) : String());
	};

	// ── Plot-type + Kind dispatch ───────────────────────────────────
	// `ptype` is the user's intent. "Auto" preserves the original
	// data-driven inference bit-for-bit. Each explicit type maps to a
	// Kind and runs validation against the selected variable types; if
	// validation fails, we set `hint` so the user sees a one-line
	// diagnostic under the plot instead of silently getting an empty
	// canvas. Variable combos stay populated so they can fix it without
	// losing context.
	auto ptype_data = m_eda_plot_type_combo->currentData();
	EdaPlotType ptype = ptype_data.isValid()
		? static_cast<EdaPlotType>(ptype_data.toInt())
		: EdaPlotType::Auto;

	bool x_numeric = isColumnNumeric(x_name);
	bool y_numeric = has_y && isColumnNumeric(y_name);
	enum Kind { KHistogram, KBarChart, KScatter, KBoxPlot, KHeatmap, KProportion, KUnsupported };
	Kind kind = KUnsupported;
	QString hint;

	auto type_label = [&]() -> QString {
		switch (ptype) {
		case EdaPlotType::Histogram:    return tr("Histogram");
		case EdaPlotType::BarChart:     return tr("Bar chart");
		case EdaPlotType::BoxPlot:      return tr("Boxplot");
		case EdaPlotType::Scatter:      return tr("Scatter");
		case EdaPlotType::FormantChart: return tr("Formant chart");
		case EdaPlotType::Heatmap:      return tr("Heatmap");
		case EdaPlotType::Proportion:   return tr("Proportion");
		case EdaPlotType::Auto:         return tr("Auto");
		}
		return QString();
	}();

	if (ptype == EdaPlotType::Auto)
	{
		// Original data-driven inference, extended: when both X and Y are
		// categorical, dispatch to a heatmap of counts instead of leaving
		// the case unsupported. Auto never picks Proportion — proportion
		// of what counts as a success isn't unambiguous from a numeric
		// column without the user opting in.
		if (!has_y) kind = x_numeric ? KHistogram : KBarChart;
		else if (x_numeric && y_numeric) kind = KScatter;
		else if (!x_numeric && y_numeric) kind = KBoxPlot;
		else if (!x_numeric && !y_numeric) kind = KHeatmap;
	}
	else
	{
		switch (ptype)
		{
		case EdaPlotType::Histogram:
			kind = KHistogram;
			if (has_y) hint = tr("%1 is univariate — clear Y to plot.").arg(type_label);
			else if (!x_numeric) hint = tr("%1 needs a numeric X.").arg(type_label);
			break;
		case EdaPlotType::BarChart:
			kind = KBarChart;
			if (has_y) hint = tr("%1 is univariate — clear Y to plot.").arg(type_label);
			else if (x_numeric) hint = tr("%1 needs a categorical X.").arg(type_label);
			break;
		case EdaPlotType::BoxPlot:
			kind = KBoxPlot;
			if (!has_y) hint = tr("%1 needs both X (categorical) and Y (numeric).").arg(type_label);
			else if (x_numeric) hint = tr("%1 needs a categorical X.").arg(type_label);
			else if (!y_numeric) hint = tr("%1 needs a numeric Y.").arg(type_label);
			break;
		case EdaPlotType::Scatter:
		case EdaPlotType::FormantChart:
			kind = KScatter;
			if (!has_y) hint = tr("%1 needs both X and Y.").arg(type_label);
			else if (!x_numeric) hint = tr("%1 needs a numeric X.").arg(type_label);
			else if (!y_numeric) hint = tr("%1 needs a numeric Y.").arg(type_label);
			break;
		case EdaPlotType::Heatmap:
			kind = KHeatmap;
			if (!has_y) hint = tr("%1 needs both X (categorical) and Y (categorical).").arg(type_label);
			else if (x_numeric) hint = tr("%1 needs a categorical X.").arg(type_label);
			else if (y_numeric) hint = tr("%1 needs a categorical Y.").arg(type_label);
			break;
		case EdaPlotType::Proportion:
			kind = KProportion;
			if (!has_y) hint = tr("%1 needs both X (categorical) and Y (binary 0/1 numeric).").arg(type_label);
			else if (x_numeric) hint = tr("%1 needs a categorical X.").arg(type_label);
			else if (!y_numeric) hint = tr("%1 needs a numeric Y (binary 0/1).").arg(type_label);
			break;
		case EdaPlotType::Auto:
			break;
		}
	}

	bool is_formant_chart = (ptype == EdaPlotType::FormantChart);

	// Surface or clear the hint label. Visible only on validation failure.
	if (!hint.isEmpty()) {
		m_eda_hint_label->setText(hint);
		m_eda_hint_label->setVisible(true);
	} else {
		m_eda_hint_label->clear();
		m_eda_hint_label->setVisible(false);
	}

	// Auto-mode KUnsupported (e.g., numeric X with categorical Y) is treated
	// like before — hide controls, bail. For explicit types, kind is always
	// set; we keep controls visible so the user can fix any validation issue.
	if (kind == KUnsupported) {
		m_eda_plot->clear();
		hideAllControls();
		return;
	}

	// ── Resolve Group / Facet ───────────────────────────────────────
	auto resolveCombo = [&](QComboBox *combo) -> intptr_t {
		if (combo->currentIndex() <= 0) return 0;
		auto qname = combo->currentText();
		String name(qname.toUtf8().constData());
		for (intptr_t j = 1; j <= nc; j++) {
			if (dt->get_header(j) == name) return j;
		}
		return 0;
	};
	intptr_t g_col = resolveCombo(m_eda_group_combo);
	intptr_t f_col = resolveCombo(m_eda_facet_combo);
	QString group_name_q = (g_col > 0) ? m_eda_group_combo->currentText() : QString();
	QString facet_name_q = (f_col > 0) ? m_eda_facet_combo->currentText() : QString();

	// ── Visibility (uniform across all supported modes) ─────────────
	// Heatmap saturates the visual plane with two categorical axes — Group
	// has no defined meaning there (Group + cat × cat would just duplicate
	// Facet semantics) but Facet itself is exposed so the user can split
	// the table into small multiples by a third variable. Proportion uses
	// Group as a colour-dodged second factor and Facet as a panel splitter.
	bool is_heatmap   = (kind == KHeatmap);
	bool is_proportion = (kind == KProportion);
	bool show_group = !is_heatmap;
	bool show_facet = true;
	m_eda_group_label->setVisible(show_group);
	m_eda_group_combo->setVisible(show_group);
	m_eda_facet_label->setVisible(show_facet);
	m_eda_facet_combo->setVisible(show_facet);
	m_bins_label->setVisible(kind == KHistogram);
	m_bins_spin->setVisible(kind == KHistogram);
	bool density_eligible = (kind == KHistogram);
	m_eda_density_check->setVisible(density_eligible);
	{
		bool density_on = density_eligible && m_eda_density_check->isChecked();
		m_eda_bw_label->setVisible(density_on);
		m_eda_bw_slider->setVisible(density_on);
		m_eda_bw_spin->setVisible(density_on);
	}
	m_eda_regline_check->setVisible(kind == KScatter && !is_formant_chart);
	m_eda_pool_label->setVisible(kind == KScatter);
	m_eda_pool_combo->setVisible(kind == KScatter);
	m_eda_style_label->setVisible(kind == KScatter && g_col > 0);
	m_eda_style_combo->setVisible(kind == KScatter && g_col > 0);
	m_eda_label_label->setVisible(kind == KScatter);
	m_eda_label_combo->setVisible(kind == KScatter);
	// For FormantChart we want Mean and Ellipse exposed even with no Group
	// — a global formant centroid + CI is meaningful, and applyEdaPlotType
	// Defaults turned them on so the user must be able to see/toggle them.
	m_eda_mean_check->setVisible(kind == KScatter && (g_col > 0 || is_formant_chart));
	m_eda_ellipse_check->setVisible(kind == KScatter && (g_col > 0 || is_formant_chart));
	m_eda_ellipse_spin->setVisible(kind == KScatter && (g_col > 0 || is_formant_chart));
	// Formant checkbox is hidden when the plot type is explicitly
	// FormantChart — the formant flag is implied by that selection,
	// so a redundant checkbox would just confuse users.
	m_eda_formant_check->setVisible(kind == KScatter && !is_formant_chart);

	// Effective formant flag: implicit for FormantChart, explicit for Scatter.
	bool formant = is_formant_chart
		|| (kind == KScatter && m_eda_formant_check->isChecked());
	int nbins = m_bins_spin->value();

	// On validation failure, keep the visibility set above so the user can
	// fix the variable selection without losing form state — but don't
	// render anything.
	if (!hint.isEmpty()) {
		m_eda_plot->clear();
		return;
	}

	// ── Helper: build one KDE curve from a sample ──
	// Pure curve-builder. The optional bin_lo/bin_hi clip the kernel
	// support to the histogram's shared bin edges (used in the faceted
	// path where panels share X); when not supplied, the curve runs from
	// data_min-3h to data_max+3h. The optional ref_n is the count used to
	// scale density → counts; defaults to the actual sample size. For
	// grouped overlays each group passes its own count so its curve
	// matches its bar heights.
	auto buildDensityCurve = [&](std::vector<double> vals_copy, int nbins_,
	                              double clip_lo, double clip_hi,
	                              int ref_n, int color_index) -> PlotWidget::DensityCurve {
		PlotWidget::DensityCurve out;
		out.color_index = color_index;
		size_t n = vals_copy.size();
		if (n < 2) return out;
		double sum = 0, sum2 = 0;
		for (double v : vals_copy) { sum += v; sum2 += v * v; }
		double mean = sum / n;
		double var = sum2 / n - mean * mean;
		double sd = std::sqrt(std::max(var, 0.0));
		std::sort(vals_copy.begin(), vals_copy.end());
		double q1 = vals_copy[n / 4];
		double q3 = vals_copy[3 * n / 4];
		double iqr = q3 - q1;
		double s = std::min(sd, iqr / 1.34);
		if (s < 1e-15) s = sd;
		if (s < 1e-15) s = 1.0;
		double h = 0.9 * s * std::pow((double)n, -0.2);
		double adjust = m_eda_bw_spin->value();
		h *= adjust;
		double xlo = vals_copy.front() - 3.0 * h;
		double xhi = vals_copy.back() + 3.0 * h;
		// Clip the support to the shared histogram x-range when one is
		// supplied; without this, two curves in a faceted layout might
		// span different x-ranges and look misaligned.
		if (std::isfinite(clip_lo)) xlo = std::max(xlo, clip_lo);
		if (std::isfinite(clip_hi)) xhi = std::min(xhi, clip_hi);
		if (xhi <= xlo) return out;
		constexpr int NPTS = 200;
		double dx = (xhi - xlo) / (NPTS - 1);
		double data_lo = vals_copy.front();
		double data_hi = vals_copy.back();
		double data_range = data_hi - data_lo;
		if (data_range < 1e-10) data_range = 1.0;
		int actual_nbins = nbins_;
		if (actual_nbins <= 0)
			actual_nbins = std::max(5, (int)std::ceil(std::log2((double)n) + 1));
		double bin_width = data_range / actual_nbins;
		// Scale to counts: density × N × binwidth, where N is the count
		// the curve should match (group's own count for grouped overlays,
		// total sample for single-series). ref_n <= 0 means "use n".
		double scale_n = (ref_n > 0) ? (double)ref_n : (double)n;
		double scale = scale_n * bin_width;
		double inv_h = 1.0 / h;
		double norm = 1.0 / (std::sqrt(2.0 * M_PI) * h * (double)n);
		out.x.resize(NPTS);
		out.y.resize(NPTS);
		for (int i = 0; i < NPTS; i++) {
			double x = xlo + i * dx;
			double f = 0;
			for (size_t j = 0; j < n; j++) {
				double u = (x - vals_copy[j]) * inv_h;
				f += std::exp(-0.5 * u * u);
			}
			out.x[i] = x;
			out.y[i] = f * norm * scale;
		}
		return out;
	};
	auto attachDensityCurve = [&](std::vector<double> vals_copy, int nbins_) {
		// Single-curve convenience wrapper for the ungrouped non-facet
		// path. color_index == -1 → default density color.
		auto curve = buildDensityCurve(std::move(vals_copy), nbins_,
		                                std::nan(""), std::nan(""),
		                                0, -1);
		std::vector<PlotWidget::DensityCurve> v;
		v.push_back(std::move(curve));
		m_eda_plot->setDensityCurves(std::move(v));
	};

	// ── Helper: partition row indices by facet level ───────────────
	auto partitionByFacet = [&]() -> std::vector<std::pair<QString, std::vector<intptr_t>>> {
		std::vector<std::pair<QString, std::vector<intptr_t>>> facets;
		std::map<QString, size_t> idx;
		for (intptr_t r = 1; r <= nr; r++) {
			auto vf = dt->get_cell(r, f_col);
			if (vf.empty()) continue;
			auto qf = QString::fromUtf8(vf.data(), (int)vf.size());
			auto it = idx.find(qf);
			if (it == idx.end()) {
				idx[qf] = facets.size();
				facets.push_back({qf, {}});
			}
			facets[idx[qf]].second.push_back(r);
		}
		return facets;
	};

	// ====================================================================
	// Faceted dispatch — when Facet is active, partition rows by facet
	// level, build a per-cell data structure, then ship the whole thing
	// to PlotWidget::setFacetedData() which lays them out as small
	// multiples with shared axes. Group remains live inside each panel
	// (overlaid histograms / dodged bars / dodged boxes / grouped scatter
	// / dodged proportion curves).
	// ====================================================================
	if (f_col > 0) {
		auto facets = partitionByFacet();
		if (facets.empty()) { m_eda_plot->clear(); return; }

		// ── Faceted Heatmap ─────────────────────────────────────────
		// Every panel must share the same X/Y level ordering so cells line
		// up column-by-column and row-by-row; we also share max_count so
		// the colour scale is comparable. The shared state lives at the
		// grid level (m_facet_heatmap_*) and is populated via the helper
		// before setFacetedData.
		if (kind == KHeatmap)
		{
			std::vector<QString> x_levels, y_levels;
			std::map<QString, int> x_idx, y_idx;
			auto ensure_x = [&](const QString &lvl) -> int {
				auto it = x_idx.find(lvl);
				if (it != x_idx.end()) return it->second;
				int i = (int)x_levels.size();
				x_idx[lvl] = i; x_levels.push_back(lvl);
				return i;
			};
			auto ensure_y = [&](const QString &lvl) -> int {
				auto it = y_idx.find(lvl);
				if (it != y_idx.end()) return it->second;
				int i = (int)y_levels.size();
				y_idx[lvl] = i; y_levels.push_back(lvl);
				return i;
			};

			// First pass: discover the union of X and Y levels across all
			// facet partitions, in source-row order. This guarantees a
			// stable, intuitive ordering and that every cell can address
			// the same row / column index space.
			for (auto &fp : facets) {
				for (intptr_t r : fp.second) {
					auto vx = xc(r); auto vy = yc(r);
					if (vx.empty() || vy.empty()) continue;
					ensure_x(QString::fromUtf8(vx.data(), (int)vx.size()));
					ensure_y(QString::fromUtf8(vy.data(), (int)vy.size()));
				}
			}
			if (x_levels.empty() || y_levels.empty()) {
				m_eda_plot->clear(); return;
			}
			int nx = (int)x_levels.size();
			int ny = (int)y_levels.size();

			// Second pass: per-panel counts, plus track the global max for
			// the shared gradient.
			std::vector<PlotWidget::FacetCell> cells;
			cells.reserve(facets.size());
			int max_count = 0;
			for (auto &fp : facets) {
				PlotWidget::FacetCell cell;
				cell.label = fp.first;
				cell.heatmap_counts.assign(ny, std::vector<int>(nx, 0));
				for (intptr_t r : fp.second) {
					auto vx = xc(r); auto vy = yc(r);
					if (vx.empty() || vy.empty()) continue;
					auto qx = QString::fromUtf8(vx.data(), (int)vx.size());
					auto qy = QString::fromUtf8(vy.data(), (int)vy.size());
					int ci = x_idx[qx];
					int ri = y_idx[qy];
					cell.heatmap_counts[ri][ci]++;
					max_count = std::max(max_count, cell.heatmap_counts[ri][ci]);
				}
				cells.push_back(std::move(cell));
			}

			QString title = y_name_q + QStringLiteral(" ~ ") + x_name_q
			              + QStringLiteral(" | facet: ") + facet_name_q;

			m_eda_plot->setFacetHeatmapShared(x_levels, y_levels, max_count);
			m_eda_plot->setFacetedData(std::move(cells),
			                            PlotWidget::FacetInnerMode::Heatmap,
			                            x_name_q, y_name_q, title, facet_name_q,
			                            {std::nan(""), std::nan("")},
			                            {std::nan(""), std::nan("")},
			                            /*shared_y_count=*/true);

			if (!m_eda_customization.title.isEmpty())
				m_eda_plot->setTitle(m_eda_customization.title);
			if (!m_eda_customization.x_label.isEmpty())
				m_eda_plot->setXLabel(m_eda_customization.x_label);
			if (!m_eda_customization.y_label.isEmpty())
				m_eda_plot->setYLabel(m_eda_customization.y_label);
			m_eda_plot->setFacetNColsOverride(m_eda_customization.facet_ncols);
			return;
		}

		// ── Faceted Proportion ──────────────────────────────────────
		// Categorical X levels and (optional) Group levels are pooled
		// across panels so panels share their x-axis ticks and per-Group
		// curve identity. Wilson 95% intervals computed per (facet, X,
		// Group) cell. Y range fixed to [0, 1] for comparability.
		if (kind == KProportion)
		{
			// Validate binary Y once, on the pooled data — same logic as
			// the non-faceted path.
			double v0 = std::nan(""), v1 = std::nan("");
			int n_unique = 0;
			for (intptr_t r = 1; r <= nr; r++)
			{
				auto vy = yc(r);
				if (vy.empty()) continue;
				bool ok; double d = vy.to_float(&ok);
				if (!ok || !std::isfinite(d)) continue;
				if (n_unique == 0) { v0 = d; n_unique = 1; }
				else if (n_unique == 1 && d != v0) { v1 = d; n_unique = 2; }
				else if (n_unique == 2 && d != v0 && d != v1) { n_unique = 3; break; }
			}
			if (n_unique != 2) {
				setEdaHint(tr("Proportion needs a binary Y (exactly two unique numeric values; "
				              "found %1).").arg(n_unique));
				m_eda_plot->clear();
				return;
			}
			double success = std::max(v0, v1);

			std::vector<QString> x_levels, g_levels;
			std::map<QString, int> x_idx, g_idx;
			auto ensure_x = [&](const QString &lvl) -> int {
				auto it = x_idx.find(lvl);
				if (it != x_idx.end()) return it->second;
				int i = (int)x_levels.size();
				x_idx[lvl] = i; x_levels.push_back(lvl);
				return i;
			};
			auto ensure_g = [&](const QString &lvl) -> int {
				auto it = g_idx.find(lvl);
				if (it != g_idx.end()) return it->second;
				int i = (int)g_levels.size();
				g_idx[lvl] = i; g_levels.push_back(lvl);
				return i;
			};
			if (g_col == 0) ensure_g(QString());

			// First pass: discover all X / Group levels for shared axes.
			for (auto &fp : facets) {
				for (intptr_t r : fp.second) {
					auto vx = xc(r); auto vy = yc(r);
					if (vx.empty() || vy.empty()) continue;
					bool ok; double d = vy.to_float(&ok);
					if (!ok || !std::isfinite(d)) continue;
					if (g_col > 0 && dt->get_cell(r, g_col).empty()) continue;
					ensure_x(QString::fromUtf8(vx.data(), (int)vx.size()));
					if (g_col > 0) {
						auto vg = dt->get_cell(r, g_col);
						ensure_g(QString::fromUtf8(vg.data(), (int)vg.size()));
					}
				}
			}
			if (x_levels.empty()) { m_eda_plot->clear(); return; }
			int nx = (int)x_levels.size();
			int ng = (int)g_levels.size();

			constexpr double z = 1.95996398454005423;
			auto wilson = [&](int k, int n) -> std::tuple<double, double, double> {
				if (n <= 0) return {std::nan(""), std::nan(""), std::nan("")};
				double phat  = (double)k / (double)n;
				double denom = 1.0 + z*z / n;
				double center = (phat + z*z / (2.0*n)) / denom;
				double half   = z * std::sqrt(phat*(1-phat)/n + z*z/(4.0*(double)n*n)) / denom;
				return {phat, center - half, center + half};
			};

			auto dodge = [&](int gi) -> double {
				if (ng <= 1) return 0.0;
				double span = 0.60;
				return -span / 2.0 + span * (double)gi / (double)(ng - 1);
			};

			// Second pass: build per-cell curves, tracking the global y
			// range so we can pass tight comparable bounds.
			std::vector<PlotWidget::FacetCell> cells;
			cells.reserve(facets.size());
			double y_lo = 0.0, y_hi = 1.0;
			bool any_data = false;
			for (auto &fp : facets) {
				PlotWidget::FacetCell cell;
				cell.label = fp.first;
				std::vector<std::vector<int>> n_cell(ng, std::vector<int>(nx, 0));
				std::vector<std::vector<int>> k_cell(ng, std::vector<int>(nx, 0));
				for (intptr_t r : fp.second) {
					auto vx = xc(r); auto vy = yc(r);
					if (vx.empty() || vy.empty()) continue;
					bool ok; double d = vy.to_float(&ok);
					if (!ok || !std::isfinite(d)) continue;
					if (g_col > 0 && dt->get_cell(r, g_col).empty()) continue;
					auto qx = QString::fromUtf8(vx.data(), (int)vx.size());
					int ci = x_idx[qx];
					int gi = 0;
					if (g_col > 0) {
						auto vg = dt->get_cell(r, g_col);
						auto qg = QString::fromUtf8(vg.data(), (int)vg.size());
						gi = g_idx[qg];
					}
					n_cell[gi][ci]++;
					if (d == success) k_cell[gi][ci]++;
				}
				cell.eff_curves.reserve(ng);
				for (int gi = 0; gi < ng; gi++) {
					PlotWidget::EffectsCurve curve;
					curve.label = (g_col > 0) ? g_levels[gi] : QString();
					for (int ci = 0; ci < nx; ci++) {
						auto [p, lo, hi] = wilson(k_cell[gi][ci], n_cell[gi][ci]);
						curve.x.push_back((double)ci + dodge(gi));
						curve.fit.push_back(p);
						curve.ci_lower.push_back(lo);
						curve.ci_upper.push_back(hi);
						if (!std::isnan(p)) {
							any_data = true;
							if (!std::isnan(lo)) y_lo = std::min(y_lo, lo);
							if (!std::isnan(hi)) y_hi = std::max(y_hi, hi);
						}
					}
					cell.eff_curves.push_back(std::move(curve));
				}
				cells.push_back(std::move(cell));
			}
			if (!any_data) { m_eda_plot->clear(); return; }

			// Clamp y to a slightly padded [0, 1] envelope unless the
			// data CIs push outside it (which can happen at small n).
			double y_pad = 0.04;
			std::pair<double, double> y_range{
				std::min(0.0, y_lo) - y_pad,
				std::max(1.0, y_hi) + y_pad};
			// X range covers all levels with a small margin so dodged
			// curves don't get clipped at the edges.
			std::pair<double, double> x_range{
				-0.5, (double)nx - 0.5};

			QString ylab = tr("Proportion (%1 = %2)").arg(y_name_q).arg(success);
			QString title = ylab + QStringLiteral(" by ") + x_name_q;
			if (g_col > 0) title += QStringLiteral(" | ") + group_name_q;
			title += QStringLiteral(" | facet: ") + facet_name_q;

			QString caption = tr("Error bars are Wilson 95%% intervals");
			m_eda_plot->setFacetEffectsShared(x_levels,
			                                   /*curve_labels=*/(g_col > 0 ? g_levels
			                                                                : std::vector<QString>{}),
			                                   caption);
			m_eda_plot->setFacetedData(std::move(cells),
			                            PlotWidget::FacetInnerMode::EffectsPlot,
			                            x_name_q, ylab, title, facet_name_q,
			                            x_range, y_range, true);

			if (!m_eda_customization.title.isEmpty())
				m_eda_plot->setTitle(m_eda_customization.title);
			if (!m_eda_customization.x_label.isEmpty())
				m_eda_plot->setXLabel(m_eda_customization.x_label);
			if (!m_eda_customization.y_label.isEmpty())
				m_eda_plot->setYLabel(m_eda_customization.y_label);
			m_eda_plot->setFacetNColsOverride(m_eda_customization.facet_ncols);
			return;
		}

		std::vector<PlotWidget::FacetCell> cells;
		cells.reserve(facets.size());
		PlotWidget::FacetInnerMode inner_mode = PlotWidget::FacetInnerMode::Histogram;
		double nan_d = std::nan("");
		std::pair<double, double> x_range{nan_d, nan_d};
		std::pair<double, double> y_range{nan_d, nan_d};
		std::vector<QString> hist_group_labels;
		std::vector<QString> box_secondary_labels;
		std::vector<QString> bar_group_labels;
		std::vector<QString> color_labels;
		std::vector<QString> style_labels;
		bool show_means = m_eda_mean_check->isChecked();
		bool show_ellipses = m_eda_ellipse_check->isChecked();
		double conf = m_eda_ellipse_spin->value() / 100.0;
		double chi2_scale = -2.0 * std::log(1.0 - conf);

		if (kind == KHistogram)
		{
			inner_mode = PlotWidget::FacetInnerMode::Histogram;

			// Pooled values & groups → shared bin edges, stable group order.
			std::vector<double> all_vals;
			std::vector<QString> all_groups;
			for (auto &fp : facets) {
				for (intptr_t r : fp.second) {
					auto v = xc(r);
					if (v.empty()) continue;
					if (g_col > 0 && dt->get_cell(r, g_col).empty()) continue;
					bool ok; double d = v.to_float(&ok);
					if (!ok || !std::isfinite(d)) continue;
					all_vals.push_back(d);
					if (g_col > 0) {
						auto vg = dt->get_cell(r, g_col);
						all_groups.push_back(QString::fromUtf8(vg.data(), (int)vg.size()));
					}
				}
			}
			if (all_vals.empty()) { m_eda_plot->clear(); return; }

			std::vector<PlotWidget::HistBin> ref_bins;
			if (g_col > 0) {
				ref_bins = PlotWidget::computeGroupedBins(all_vals, all_groups,
				                                          hist_group_labels, nbins);
			} else {
				ref_bins = PlotWidget::computeBins(all_vals, nbins);
			}
			if (ref_bins.empty()) { m_eda_plot->clear(); return; }
			double rlo = ref_bins.front().lo;
			double rhi = ref_bins.back().hi;
			int rnb = (int)ref_bins.size();
			double rbw = (rhi - rlo) / rnb;
			x_range = {rlo, rhi};

			// Per-cell binning against the shared edges. Walk row indices
			// directly rather than calling computeBins (which would recompute
			// edges from per-cell mins/maxes — defeats the purpose).
			std::map<QString, int> g_idx;
			for (int i = 0; i < (int)hist_group_labels.size(); i++)
				g_idx[hist_group_labels[i]] = i;
			int ng = (int)hist_group_labels.size();

			bool want_density = m_eda_density_check->isChecked();

			for (auto &fp : facets) {
				PlotWidget::FacetCell cell;
				cell.label = fp.first;
				cell.bins.resize(rnb);
				for (int i = 0; i < rnb; i++) {
					cell.bins[i].lo = rlo + i * rbw;
					cell.bins[i].hi = rlo + (i + 1) * rbw;
					cell.bins[i].count = 0;
					if (g_col > 0) cell.bins[i].group_counts.assign(ng, 0);
				}
				// Density inputs: when density is requested, collect raw
				// values (ungrouped) or per-group raw values (grouped) in
				// the same row pass so we don't iterate the cell's rows
				// twice. Empty when density is off.
				std::vector<double> dens_vals;
				std::vector<std::vector<double>> dens_vals_by_group;
				if (want_density) {
					if (g_col == 0) dens_vals.reserve(fp.second.size());
					else dens_vals_by_group.assign(ng, std::vector<double>{});
				}

				for (intptr_t r : fp.second) {
					auto v = xc(r);
					if (v.empty()) continue;
					if (g_col > 0 && dt->get_cell(r, g_col).empty()) continue;
					bool ok; double d = v.to_float(&ok);
					if (!ok || !std::isfinite(d)) continue;
					int bi = (int)((d - rlo) / rbw);
					if (bi < 0) bi = 0;
					if (bi >= rnb) bi = rnb - 1;
					cell.bins[bi].count++;
					int g_for_density = -1;
					if (g_col > 0) {
						auto vg = dt->get_cell(r, g_col);
						auto qg = QString::fromUtf8(vg.data(), (int)vg.size());
						auto it = g_idx.find(qg);
						if (it != g_idx.end()) {
							cell.bins[bi].group_counts[it->second]++;
							g_for_density = it->second;
						}
					}
					if (want_density) {
						if (g_col == 0) dens_vals.push_back(d);
						else if (g_for_density >= 0)
							dens_vals_by_group[g_for_density].push_back(d);
					}
				}

				// Build density curve(s) for this cell. Each curve is
				// scaled to the local count so the curve heights match
				// the bars in the SAME cell — densities don't get
				// renormalized across panels.
				if (want_density) {
					if (g_col == 0) {
						if (dens_vals.size() >= 2) {
							int local_n = (int)dens_vals.size();
							auto c = buildDensityCurve(std::move(dens_vals), nbins,
							                            rlo, rhi, local_n, -1);
							if (!c.x.empty()) cell.density_curves.push_back(std::move(c));
						}
					} else {
						for (int gi = 0; gi < ng; gi++) {
							auto &gv = dens_vals_by_group[gi];
							if (gv.size() < 2) continue;
							int gn = (int)gv.size();
							auto c = buildDensityCurve(std::move(gv), nbins,
							                            rlo, rhi, gn, gi);
							if (!c.x.empty()) cell.density_curves.push_back(std::move(c));
						}
					}
				}
				cells.push_back(std::move(cell));
			}
		}
		else if (kind == KBarChart)
		{
			inner_mode = PlotWidget::FacetInnerMode::BarChart;

			// Discover X categories and (optional) group categories across
			// pooled data so every panel has the same bar layout.
			std::vector<QString> cats_order;
			std::map<QString, int> cats_idx;
			std::vector<QString> groups_order;
			std::map<QString, int> groups_idx;
			for (auto &fp : facets) {
				for (intptr_t r : fp.second) {
					auto v = xc(r);
					if (v.empty()) continue;
					if (g_col > 0 && dt->get_cell(r, g_col).empty()) continue;
					auto qs = QString::fromUtf8(v.data(), (int)v.size());
					if (cats_idx.find(qs) == cats_idx.end()) {
						cats_idx[qs] = (int)cats_order.size();
						cats_order.push_back(qs);
					}
					if (g_col > 0) {
						auto vg = dt->get_cell(r, g_col);
						auto qg = QString::fromUtf8(vg.data(), (int)vg.size());
						if (groups_idx.find(qg) == groups_idx.end()) {
							groups_idx[qg] = (int)groups_order.size();
							groups_order.push_back(qg);
						}
					}
				}
			}
			if (cats_order.empty()) { m_eda_plot->clear(); return; }
			if (g_col > 0) bar_group_labels = groups_order;

			int nc_ = (int)cats_order.size();
			int ng = std::max(1, (int)groups_order.size());
			for (auto &fp : facets) {
				PlotWidget::FacetCell cell;
				cell.label = fp.first;
				cell.bar_labels = cats_order;
				if (g_col == 0) {
					cell.bar_counts.assign(nc_, 0);
					for (intptr_t r : fp.second) {
						auto v = xc(r);
						if (v.empty()) continue;
						auto qs = QString::fromUtf8(v.data(), (int)v.size());
						cell.bar_counts[cats_idx[qs]]++;
					}
				} else {
					cell.bar_grouped_counts.assign(ng, std::vector<int>(nc_, 0));
					for (intptr_t r : fp.second) {
						auto v = xc(r);
						auto vg = dt->get_cell(r, g_col);
						if (v.empty() || vg.empty()) continue;
						auto qs = QString::fromUtf8(v.data(), (int)v.size());
						auto qg = QString::fromUtf8(vg.data(), (int)vg.size());
						cell.bar_grouped_counts[groups_idx[qg]][cats_idx[qs]]++;
					}
				}
				cells.push_back(std::move(cell));
			}
		}
		else if (kind == KBoxPlot)
		{
			inner_mode = PlotWidget::FacetInnerMode::BoxPlot;

			double ylo_pool = std::numeric_limits<double>::infinity();
			double yhi_pool = -std::numeric_limits<double>::infinity();
			for (auto &fp : facets) {
				std::vector<QString> groups, style_groups;
				std::vector<double> vals;
				std::vector<intptr_t> rows;
				for (intptr_t r : fp.second) {
					auto vx = xc(r);
					auto vy = yc(r);
					if (vx.empty() || vy.empty()) continue;
					if (g_col > 0 && dt->get_cell(r, g_col).empty()) continue;
					bool ok; double dy = vy.to_float(&ok);
					if (!ok || !std::isfinite(dy)) continue;
					groups.push_back(QString::fromUtf8(vx.data(), (int)vx.size()));
					vals.push_back(dy);
					rows.push_back((intptr_t)(r - 1));
					if (g_col > 0) {
						auto vg = dt->get_cell(r, g_col);
						style_groups.push_back(QString::fromUtf8(vg.data(), (int)vg.size()));
					}
					ylo_pool = std::min(ylo_pool, dy);
					yhi_pool = std::max(yhi_pool, dy);
				}
				PlotWidget::FacetCell cell;
				cell.label = fp.first;
				cell.boxes = PlotWidget::computeBoxStats(groups, vals, rows, style_groups);
				cells.push_back(std::move(cell));
			}
			if (std::isfinite(ylo_pool) && std::isfinite(yhi_pool) && yhi_pool > ylo_pool) {
				double pad = (yhi_pool - ylo_pool) * 0.06;
				y_range = {ylo_pool - pad, yhi_pool + pad};
			}
			if (g_col > 0) {
				std::map<QString, int> seen;
				for (auto &cell : cells) {
					for (auto &b : cell.boxes) {
						if (!b.secondary_label.isEmpty()
						    && seen.find(b.secondary_label) == seen.end()) {
							seen[b.secondary_label] = (int)box_secondary_labels.size();
							box_secondary_labels.push_back(b.secondary_label);
						}
					}
				}
			}
		}
		else if (kind == KScatter)
		{
			inner_mode = g_col > 0 ? PlotWidget::FacetInnerMode::GroupedScatter
			                       : PlotWidget::FacetInnerMode::Scatter;

			intptr_t l_col = 0;
			if (m_eda_label_combo->currentIndex() > 0) {
				String label_name(m_eda_label_combo->currentText().toUtf8().constData());
				for (intptr_t j = 1; j <= nc; j++) {
					if (dt->get_header(j) == label_name) { l_col = j; break; }
				}
			}

			// Discover pooled color labels first so colors stay consistent
			// across facets (same group always gets the same palette slot).
			if (g_col > 0) {
				std::map<QString, int> seen;
				for (auto &fp : facets) {
					for (intptr_t r : fp.second) {
						auto vg = dt->get_cell(r, g_col);
						if (vg.empty()) continue;
						auto qg = QString::fromUtf8(vg.data(), (int)vg.size());
						if (seen.find(qg) == seen.end()) {
							seen[qg] = (int)color_labels.size();
							color_labels.push_back(qg);
						}
					}
				}
			}

			double xlo_pool = std::numeric_limits<double>::infinity();
			double xhi_pool = -std::numeric_limits<double>::infinity();
			double ylo_pool = std::numeric_limits<double>::infinity();
			double yhi_pool = -std::numeric_limits<double>::infinity();

			for (auto &fp : facets) {
				PlotWidget::FacetCell cell;
				cell.label = fp.first;
				if (g_col == 0) {
					for (intptr_t r : fp.second) {
						auto vx = xc(r);
						auto vy = yc(r);
						if (vx.empty() || vy.empty()) continue;
						if (l_col > 0 && dt->get_cell(r, l_col).empty()) continue;
						bool okx, oky;
						double dx = vx.to_float(&okx);
						double dy = vy.to_float(&oky);
						if (!okx || !oky || !std::isfinite(dx) || !std::isfinite(dy)) continue;
						cell.x.push_back(dx);
						cell.y.push_back(dy);
						cell.source_rows.push_back((intptr_t)(r - 1));
						if (l_col > 0) {
							auto vl = dt->get_cell(r, l_col);
							cell.point_labels.push_back(QString::fromUtf8(vl.data(), (int)vl.size()));
						}
						xlo_pool = std::min(xlo_pool, dx); xhi_pool = std::max(xhi_pool, dx);
						ylo_pool = std::min(ylo_pool, dy); yhi_pool = std::max(yhi_pool, dy);
					}
				} else {
					std::vector<double> xv, yv;
					std::vector<QString> gv, lv;
					std::vector<intptr_t> rows;
					for (intptr_t r : fp.second) {
						auto vx = xc(r);
						auto vy = yc(r);
						auto vg = dt->get_cell(r, g_col);
						if (vx.empty() || vy.empty() || vg.empty()) continue;
						if (l_col > 0 && dt->get_cell(r, l_col).empty()) continue;
						bool okx, oky;
						double dx = vx.to_float(&okx);
						double dy = vy.to_float(&oky);
						if (!okx || !oky || !std::isfinite(dx) || !std::isfinite(dy)) continue;
						xv.push_back(dx);
						yv.push_back(dy);
						gv.push_back(QString::fromUtf8(vg.data(), (int)vg.size()));
						rows.push_back((intptr_t)(r - 1));
						if (l_col > 0) {
							auto vl = dt->get_cell(r, l_col);
							lv.push_back(QString::fromUtf8(vl.data(), (int)vl.size()));
						}
						xlo_pool = std::min(xlo_pool, dx); xhi_pool = std::max(xhi_pool, dx);
						ylo_pool = std::min(ylo_pool, dy); yhi_pool = std::max(yhi_pool, dy);
					}
					cell.group_data = PlotWidget::buildGroups(gv, xv, yv, chi2_scale, lv, {}, rows);
				}
				cells.push_back(std::move(cell));
			}

			if (std::isfinite(xlo_pool) && std::isfinite(xhi_pool) && xhi_pool > xlo_pool) {
				double pad = (xhi_pool - xlo_pool) * 0.06;
				x_range = {xlo_pool - pad, xhi_pool + pad};
			}
			if (std::isfinite(ylo_pool) && std::isfinite(yhi_pool) && yhi_pool > ylo_pool) {
				double pad = (yhi_pool - ylo_pool) * 0.06;
				y_range = {ylo_pool - pad, yhi_pool + pad};
			}
		}

		// Build axis labels and title.
		QString axis_x_label = x_name_q;
		QString axis_y_label = (kind == KBoxPlot || kind == KScatter)
			? y_name_q : tr("Count");
		QString global_title;
		if (kind == KScatter || kind == KBoxPlot)
			global_title = y_name_q + QStringLiteral(" ~ ") + x_name_q;
		else
			global_title = x_name_q;
		if (g_col > 0) global_title += QStringLiteral(" by ") + group_name_q;
		global_title += QStringLiteral(" | facet: ") + facet_name_q;

		// Fold user customization range into the facet shared range before
		// shipping to setFacetedData. Without this, renderFacetGrid would
		// use the auto-computed range during the inner pass and the user's
		// override would be invisible in faceted layouts. The dialog only
		// allows setting both bounds at once (gated by a "Custom range"
		// checkbox), so checking either is_value() is sufficient — if one
		// is set, both are.
		bool x_user_set = m_eda_customization.x_min.has_value()
		               && m_eda_customization.x_max.has_value();
		bool y_user_set = m_eda_customization.y_min.has_value()
		               && m_eda_customization.y_max.has_value();
		if (x_user_set) {
			x_range.first  = *m_eda_customization.x_min;
			x_range.second = *m_eda_customization.x_max;
		}
		if (y_user_set) {
			y_range.first  = *m_eda_customization.y_min;
			y_range.second = *m_eda_customization.y_max;
		}

		m_eda_plot->setFacetedData(std::move(cells), inner_mode,
		                            axis_x_label, axis_y_label,
		                            global_title, facet_name_q,
		                            x_range, y_range, true,
		                            std::move(hist_group_labels),
		                            std::move(box_secondary_labels),
		                            std::move(bar_group_labels),
		                            std::move(color_labels),
		                            std::move(style_labels),
		                            formant, formant,
		                            show_means, show_ellipses,
		                            m_eda_regline_check->isChecked());

		// Apply remaining user customization for the faceted path. Mirrors
		// the same block at the end of updateEdaPlot — the facet branch
		// returns early so we need to apply twice. Title/labels are direct
		// overrides; ranges also go through setForcedX/YRange so that
		// renderFacetGrid's end-of-pass restore puts back the user values.
		if (!m_eda_customization.title.isEmpty())
			m_eda_plot->setTitle(m_eda_customization.title);
		if (!m_eda_customization.x_label.isEmpty())
			m_eda_plot->setXLabel(m_eda_customization.x_label);
		if (!m_eda_customization.y_label.isEmpty())
			m_eda_plot->setYLabel(m_eda_customization.y_label);
		if (x_user_set)
			m_eda_plot->setForcedXRange(*m_eda_customization.x_min, *m_eda_customization.x_max);
		else
			m_eda_plot->clearForcedXRange();
		if (y_user_set)
			m_eda_plot->setForcedYRange(*m_eda_customization.y_min, *m_eda_customization.y_max);
		else
			m_eda_plot->clearForcedYRange();
		m_eda_plot->setFacetNColsOverride(m_eda_customization.facet_ncols);
		return;
	}

	// ====================================================================
	// Non-faceted dispatch — single-panel plot with optional Group.
	// ====================================================================

	if (kind == KHistogram)
	{
		std::vector<double> vals;
		std::vector<QString> groups;
		vals.reserve(nr);
		if (g_col > 0) groups.reserve(nr);
		for (intptr_t r = 1; r <= nr; r++) {
			auto v = xc(r);
			if (v.empty()) continue;
			if (g_col > 0 && dt->get_cell(r, g_col).empty()) continue;
			bool ok; double d = v.to_float(&ok);
			if (!ok || !std::isfinite(d)) continue;
			vals.push_back(d);
			if (g_col > 0) {
				auto vg = dt->get_cell(r, g_col);
				groups.push_back(QString::fromUtf8(vg.data(), (int)vg.size()));
			}
		}
		if (vals.empty()) { m_eda_plot->clear(); return; }

		QString title = x_name_q;
		if (g_col > 0) title += QStringLiteral(" by ") + group_name_q;

		if (g_col > 0) {
			bool want_density = m_eda_density_check->isChecked();
			// Build per-group density curves before moving vals/groups
			// into the plot — each curve uses its group's count for
			// scaling so the curve heights match the (overlaid) bars.
			std::vector<PlotWidget::DensityCurve> curves;
			if (want_density) {
				// Discover groups in first-seen order, matching how
				// computeGroupedBins assigns palette slots.
				std::vector<QString> g_order;
				std::map<QString, int> g_idx;
				for (size_t i = 0; i < groups.size(); i++) {
					if (g_idx.find(groups[i]) == g_idx.end()) {
						g_idx[groups[i]] = (int)g_order.size();
						g_order.push_back(groups[i]);
					}
				}
				// Pool the X support so every group's curve spans the
				// same range — keeps visual comparison meaningful.
				double pool_lo = *std::min_element(vals.begin(), vals.end());
				double pool_hi = *std::max_element(vals.begin(), vals.end());
				for (int gi = 0; gi < (int)g_order.size(); gi++) {
					std::vector<double> gv;
					for (size_t i = 0; i < vals.size(); i++)
						if (groups[i] == g_order[gi]) gv.push_back(vals[i]);
					if (gv.size() < 2) continue;
					int gn = (int)gv.size();
					auto c = buildDensityCurve(std::move(gv), nbins,
					                            pool_lo, pool_hi, gn, gi);
					if (!c.x.empty()) curves.push_back(std::move(c));
				}
			}
			m_eda_plot->setHistogramData(std::move(vals), std::move(groups),
			                              x_name_q, tr("Count"), title, nbins);
			if (!curves.empty()) m_eda_plot->setDensityCurves(std::move(curves));
			else m_eda_plot->clearDensityCurve();
		} else {
			std::vector<double> vals_copy;
			bool want_density = m_eda_density_check->isChecked() && vals.size() >= 2;
			if (want_density) vals_copy = vals;
			m_eda_plot->setHistogramData(std::move(vals), x_name_q, tr("Count"),
			                              title, nbins);
			if (want_density) attachDensityCurve(std::move(vals_copy), nbins);
			else m_eda_plot->clearDensityCurve();
		}
	}
	else if (kind == KBarChart)
	{
		std::vector<QString> cats_order;
		std::map<QString, int> cats_idx;
		if (g_col == 0) {
			std::vector<int> counts;
			for (intptr_t r = 1; r <= nr; r++) {
				auto v = xc(r);
				if (v.empty()) continue;
				auto qs = QString::fromUtf8(v.data(), (int)v.size());
				auto it = cats_idx.find(qs);
				if (it == cats_idx.end()) {
					cats_idx[qs] = (int)cats_order.size();
					cats_order.push_back(qs);
					counts.push_back(0);
				}
				counts[cats_idx[qs]]++;
			}
			if (cats_order.empty()) { m_eda_plot->clear(); return; }
			m_eda_plot->setBarChartData(std::move(cats_order), std::move(counts),
			                             x_name_q, tr("Count"), x_name_q);
		} else {
			std::vector<QString> groups_order;
			std::map<QString, int> groups_idx;
			std::vector<std::pair<int, int>> rows;
			for (intptr_t r = 1; r <= nr; r++) {
				auto v = xc(r);
				auto vg = dt->get_cell(r, g_col);
				if (v.empty() || vg.empty()) continue;
				auto qs = QString::fromUtf8(v.data(), (int)v.size());
				auto qg = QString::fromUtf8(vg.data(), (int)vg.size());
				if (cats_idx.find(qs) == cats_idx.end()) {
					cats_idx[qs] = (int)cats_order.size();
					cats_order.push_back(qs);
				}
				if (groups_idx.find(qg) == groups_idx.end()) {
					groups_idx[qg] = (int)groups_order.size();
					groups_order.push_back(qg);
				}
				rows.push_back({cats_idx[qs], groups_idx[qg]});
			}
			if (cats_order.empty()) { m_eda_plot->clear(); return; }
			int nc_ = (int)cats_order.size();
			int ng = (int)groups_order.size();
			std::vector<std::vector<int>> counts(ng, std::vector<int>(nc_, 0));
			for (auto &p : rows) counts[p.second][p.first]++;
			QString title = x_name_q + QStringLiteral(" by ") + group_name_q;
			m_eda_plot->setBarChartData(std::move(cats_order),
			                             std::move(groups_order),
			                             std::move(counts),
			                             x_name_q, tr("Count"), title);
		}
	}
	else if (kind == KBoxPlot)
	{
		std::vector<QString> groups, style_groups;
		std::vector<double> vals;
		std::vector<intptr_t> rows;
		groups.reserve(nr);
		vals.reserve(nr);
		rows.reserve(nr);
		if (g_col > 0) style_groups.reserve(nr);
		for (intptr_t r = 1; r <= nr; r++) {
			auto vx = xc(r);
			auto vy = yc(r);
			if (vx.empty() || vy.empty()) continue;
			if (g_col > 0 && dt->get_cell(r, g_col).empty()) continue;
			bool ok; double dy = vy.to_float(&ok);
			if (!ok || !std::isfinite(dy)) continue;
			groups.push_back(QString::fromUtf8(vx.data(), (int)vx.size()));
			vals.push_back(dy);
			rows.push_back((intptr_t)(r - 1));
			if (g_col > 0) {
				auto vg = dt->get_cell(r, g_col);
				style_groups.push_back(QString::fromUtf8(vg.data(), (int)vg.size()));
			}
		}
		if (vals.empty()) { m_eda_plot->clear(); return; }
		QString title = y_name_q + QStringLiteral(" ~ ") + x_name_q;
		if (g_col > 0) title += QStringLiteral(" | ") + group_name_q;
		m_eda_plot->setBoxPlotData(std::move(groups), std::move(vals),
		                            x_name_q, y_name_q, title,
		                            std::move(rows), std::move(style_groups));
	}
	else if (kind == KHeatmap)
	{
		// Two-categorical contingency. Walk rows in source order so first-
		// seen level becomes the leftmost column / top row — same convention
		// the bar-chart path uses, so users see the same orderings across
		// related plots of the same variables.
		std::vector<QString> x_levels, y_levels;
		std::map<QString, int> x_idx, y_idx;
		std::vector<std::vector<int>> counts;

		auto ensure_x = [&](const QString &lvl) -> int {
			auto it = x_idx.find(lvl);
			if (it != x_idx.end()) return it->second;
			int i = (int)x_levels.size();
			x_idx[lvl] = i;
			x_levels.push_back(lvl);
			for (auto &row : counts) row.push_back(0);
			return i;
		};
		auto ensure_y = [&](const QString &lvl) -> int {
			auto it = y_idx.find(lvl);
			if (it != y_idx.end()) return it->second;
			int i = (int)y_levels.size();
			y_idx[lvl] = i;
			y_levels.push_back(lvl);
			counts.emplace_back((int)x_levels.size(), 0);
			return i;
		};

		for (intptr_t r = 1; r <= nr; r++)
		{
			auto vx = xc(r);
			auto vy = yc(r);
			if (vx.empty() || vy.empty()) continue;
			auto qx = QString::fromUtf8(vx.data(), (int)vx.size());
			auto qy = QString::fromUtf8(vy.data(), (int)vy.size());
			int ci = ensure_x(qx);
			int ri = ensure_y(qy);
			counts[ri][ci]++;
		}

		if (x_levels.empty() || y_levels.empty()) {
			m_eda_plot->clear();
			return;
		}

		QString title = y_name_q + QStringLiteral(" ~ ") + x_name_q;
		m_eda_plot->setHeatmapData(std::move(x_levels), std::move(y_levels),
		                            std::move(counts),
		                            x_name_q, y_name_q, title);
	}
	else if (kind == KProportion)
	{
		// Proportion of Y=success per X level, optionally split by Group.
		// "Success" is the larger of the two unique numeric Y values (if
		// the column has exactly two unique non-missing values). For Y that
		// is already 0/1 this is just "% of 1s"; for arbitrary binary 2-
		// level numeric coding (e.g. 1/2) the larger level is the success.
		// Non-binary Y triggers the validation hint earlier — by the time
		// we get here Y is guaranteed numeric, so we just gate on the
		// two-unique-values check and surface a runtime hint if not.

		// First pass: discover the two unique Y values across the whole
		// column, ignoring missing and non-finite. If we find more than
		// two, abort with a hint (the column isn't binary).
		double v0 = std::nan(""), v1 = std::nan("");
		int n_unique = 0;
		for (intptr_t r = 1; r <= nr; r++)
		{
			auto vy = yc(r);
			if (vy.empty()) continue;
			bool ok; double d = vy.to_float(&ok);
			if (!ok || !std::isfinite(d)) continue;
			if (n_unique == 0) { v0 = d; n_unique = 1; }
			else if (n_unique == 1 && d != v0) { v1 = d; n_unique = 2; }
			else if (n_unique == 2 && d != v0 && d != v1) { n_unique = 3; break; }
		}
		if (n_unique != 2) {
			setEdaHint(tr("Proportion needs a binary Y (exactly two unique numeric values; "
			              "found %1).").arg(n_unique));
			m_eda_plot->clear();
			return;
		}
		double success = std::max(v0, v1);

		// Discover X levels and (optional) Group levels in first-seen order
		// so the on-screen ordering matches every other plot of the same
		// columns. counts[g][x] = total n; succ[g][x] = successes.
		std::vector<QString> x_levels, g_levels;
		std::map<QString, int> x_idx, g_idx;
		std::vector<std::vector<int>> n_cell, k_cell;

		auto ensure_x = [&](const QString &lvl) -> int {
			auto it = x_idx.find(lvl);
			if (it != x_idx.end()) return it->second;
			int i = (int)x_levels.size();
			x_idx[lvl] = i;
			x_levels.push_back(lvl);
			for (auto &row : n_cell) row.push_back(0);
			for (auto &row : k_cell) row.push_back(0);
			return i;
		};
		auto ensure_g = [&](const QString &lvl) -> int {
			auto it = g_idx.find(lvl);
			if (it != g_idx.end()) return it->second;
			int i = (int)g_levels.size();
			g_idx[lvl] = i;
			g_levels.push_back(lvl);
			n_cell.emplace_back((int)x_levels.size(), 0);
			k_cell.emplace_back((int)x_levels.size(), 0);
			return i;
		};
		// Always have at least one group row, even when ungrouped.
		if (g_col == 0) ensure_g(QString());

		for (intptr_t r = 1; r <= nr; r++)
		{
			auto vx = xc(r);
			auto vy = yc(r);
			if (vx.empty() || vy.empty()) continue;
			bool ok; double d = vy.to_float(&ok);
			if (!ok || !std::isfinite(d)) continue;
			if (g_col > 0 && dt->get_cell(r, g_col).empty()) continue;
			auto qx = QString::fromUtf8(vx.data(), (int)vx.size());
			int ci = ensure_x(qx);
			int gi = 0;
			if (g_col > 0) {
				auto vg = dt->get_cell(r, g_col);
				auto qg = QString::fromUtf8(vg.data(), (int)vg.size());
				gi = ensure_g(qg);
			}
			n_cell[gi][ci]++;
			if (d == success) k_cell[gi][ci]++;
		}

		if (x_levels.empty()) {
			m_eda_plot->clear();
			return;
		}

		// Wilson 95% interval (z = 1.96). Single-CI level is plenty for v1
		// — a future enhancement can reuse m_eda_ellipse_spin as a generic
		// confidence-level control across plot types.
		constexpr double z = 1.95996398454005423;
		auto wilson = [&](int k, int n) -> std::tuple<double, double, double> {
			if (n <= 0) return {std::nan(""), std::nan(""), std::nan("")};
			double phat  = (double)k / (double)n;
			double denom = 1.0 + z*z / n;
			double center = (phat + z*z / (2.0*n)) / denom;
			double half   = z * std::sqrt(phat*(1-phat)/n + z*z/(4.0*(double)n*n)) / denom;
			return {phat, center - half, center + half};
		};

		std::vector<PlotWidget::EffectsCurve> curves;
		curves.reserve(g_levels.size());
		// Per-curve x dodge so error bars don't overlap when Group is used.
		// Spread over ±0.30 of a categorical-x unit, mirroring how the
		// Effects tab dodges its by-curves.
		int ng = (int)g_levels.size();
		auto dodge = [&](int gi) -> double {
			if (ng <= 1) return 0.0;
			double span = 0.60;
			return -span / 2.0 + span * (double)gi / (double)(ng - 1);
		};

		for (int gi = 0; gi < ng; gi++)
		{
			PlotWidget::EffectsCurve curve;
			curve.label = (g_col > 0) ? g_levels[gi] : QString();
			for (int ci = 0; ci < (int)x_levels.size(); ci++) {
				auto [p, lo, hi] = wilson(k_cell[gi][ci], n_cell[gi][ci]);
				curve.x.push_back((double)ci + dodge(gi));
				curve.fit.push_back(p);
				curve.ci_lower.push_back(lo);
				curve.ci_upper.push_back(hi);
			}
			curves.push_back(std::move(curve));
		}

		// Reasonable axis title for "what's being proportioned".
		// Show as e.g. "% schwa = 1" when success is a numeric literal.
		QString ylab = tr("Proportion (%1 = %2)")
		    .arg(y_name_q)
		    .arg(success);
		QString title = ylab + QStringLiteral(" by ") + x_name_q;
		if (g_col > 0) title += QStringLiteral(" | ") + group_name_q;

		std::vector<QString> level_labels = x_levels;
		QString caption = tr("Error bars are Wilson 95%% intervals");
		m_eda_plot->setEffectsPlotData(std::move(curves),
		                                x_name_q, ylab, title,
		                                caption,
		                                std::move(level_labels),
		                                /*show_ci=*/true,
		                                /*show_legend=*/g_col > 0);
	}
	else if (kind == KScatter)
	{
		// Adapt the original scatter logic with the same Group/Pool/Style/
		// Label/Mean/Ellipse/Formant semantics as before.
		bool has_pool = (m_eda_pool_combo->currentIndex() > 0);
		bool has_label = (m_eda_label_combo->currentIndex() > 0);
		bool has_style = (m_eda_style_combo->currentIndex() > 0);

		if (g_col > 0)
		{
			// ── Grouped scatter (original code) ──
			intptr_t l_col = 0;
			if (has_label) {
				auto label_name = String(m_eda_label_combo->currentText().toUtf8().constData());
				for (intptr_t j = 1; j <= nc; j++) {
					if (dt->get_header(j) == label_name) { l_col = j; break; }
				}
			}

			intptr_t p_col = 0;
			if (has_pool) {
				auto pool_name = String(m_eda_pool_combo->currentText().toUtf8().constData());
				for (intptr_t j = 1; j <= nc; j++) {
					if (dt->get_header(j) == pool_name) { p_col = j; break; }
				}
			}

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
			std::vector<intptr_t> rows;
			xv.reserve(nr);
			yv.reserve(nr);
			gv.reserve(nr);
			rows.reserve(nr);
			if (l_col > 0) lv.reserve(nr);
			if (s_col > 0) sv.reserve(nr);
			for (intptr_t r = 1; r <= nr; r++)
			{
				auto vx = xc(r);
				auto vy = yc(r);
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
					rows.push_back((intptr_t)(r - 1));
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

			// ── Pooling ──
			if (p_col > 0)
			{
				struct PoolCell {
					double sx = 0, sy = 0;
					int n = 0;
					QString group;
					QString style;
					std::map<QString, int> label_freq;
				};
				using CellKey = std::tuple<QString, QString, QString>;
				std::map<CellKey, PoolCell> cells_pool;
				std::vector<CellKey> cell_order;

				for (intptr_t r = 1; r <= nr; r++)
				{
					auto vx = xc(r);
					auto vy = yc(r);
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
					if (cells_pool.find(key) == cells_pool.end()) cell_order.push_back(key);
					auto &c = cells_pool[key];
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

				xv.clear(); yv.clear(); gv.clear(); lv.clear(); sv.clear();
				rows.clear();
				for (auto &key : cell_order) {
					auto &c = cells_pool[key];
					if (c.n == 0) continue;
					xv.push_back(c.sx / c.n);
					yv.push_back(c.sy / c.n);
					gv.push_back(c.group);
					if (s_col > 0)
						sv.push_back(c.style);
					if (l_col > 0) {
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

			bool show_means_local = m_eda_mean_check->isChecked();
			bool show_ellipses_local = m_eda_ellipse_check->isChecked();
			bool show_regression_lines = m_eda_regline_check->isChecked();
			double conf_local = m_eda_ellipse_spin->value() / 100.0;
			double chi2_scale_local = -2.0 * std::log(1.0 - conf_local);

			QString title = y_name_q + QStringLiteral(" ~ ") + x_name_q
				+ QStringLiteral(" | ") + group_name_q;
			if (has_style && s_col > 0)
				title += QStringLiteral(" \u00D7 ") + m_eda_style_combo->currentText();
			if (has_label && l_col != g_col)
				title += QStringLiteral(" / ") + m_eda_label_combo->currentText();
			if (p_col > 0)
				title += QStringLiteral(" [pooled by ") + m_eda_pool_combo->currentText()
				       + QStringLiteral("]");

			m_eda_plot->setGroupedScatterData(
				std::move(gv), std::move(xv), std::move(yv),
				x_name_q, y_name_q, title,
				show_means_local, show_ellipses_local, chi2_scale_local,
				formant, formant,
				std::move(lv), std::move(sv), std::move(rows),
				show_regression_lines);
		}
		else
		{
			// ── Plain scatter (no Group) ──
			intptr_t l_col = 0;
			if (has_label) {
				auto label_name = String(m_eda_label_combo->currentText().toUtf8().constData());
				for (intptr_t j = 1; j <= nc; j++) {
					if (dt->get_header(j) == label_name) { l_col = j; break; }
				}
			}

			intptr_t p_col = 0;
			if (has_pool) {
				auto pool_name = String(m_eda_pool_combo->currentText().toUtf8().constData());
				for (intptr_t j = 1; j <= nc; j++) {
					if (dt->get_header(j) == pool_name) { p_col = j; break; }
				}
			}

			std::vector<double> xv, yv;
			std::vector<QString> lv;
			std::vector<intptr_t> rows;
			xv.reserve(nr);
			yv.reserve(nr);
			rows.reserve(nr);
			if (l_col > 0) lv.reserve(nr);
			for (intptr_t r = 1; r <= nr; r++)
			{
				auto vx = xc(r);
				auto vy = yc(r);
				if (vx.empty() || vy.empty()) continue;
				if (l_col > 0 && dt->get_cell(r, l_col).empty()) continue;
				if (p_col > 0 && dt->get_cell(r, p_col).empty()) continue;
				bool okx, oky;
				double dx = vx.to_float(&okx);
				double dy = vy.to_float(&oky);
				if (okx && oky && std::isfinite(dx) && std::isfinite(dy)) {
					xv.push_back(dx);
					yv.push_back(dy);
					rows.push_back((intptr_t)(r - 1));
					if (l_col > 0) {
						auto vl = dt->get_cell(r, l_col);
						lv.push_back(QString::fromUtf8(vl.data(), (int)vl.size()));
					}
				}
			}
			if (xv.empty()) { m_eda_plot->clear(); return; }

			// ── Pooling ──
			// When a pool variable is set without Group, each plotted point is
			// the (x, y) mean across all rows sharing the same pool level —
			// e.g. one point per speaker. Mirrors the grouped pool path at the
			// top of this branch, minus the Group/Style keying. Source rows
			// are left empty for pooled points: each one represents N rows,
			// so click-through to a single source row isn't meaningful.
			if (p_col > 0)
			{
				struct PoolCell {
					double sx = 0, sy = 0;
					int n = 0;
					std::map<QString, int> label_freq;
				};
				std::map<QString, PoolCell> cells_pool;
				std::vector<QString> cell_order;

				for (intptr_t r = 1; r <= nr; r++)
				{
					auto vx = xc(r);
					auto vy = yc(r);
					auto vp = dt->get_cell(r, p_col);
					if (vx.empty() || vy.empty() || vp.empty()) continue;
					if (l_col > 0 && dt->get_cell(r, l_col).empty()) continue;
					bool okx, oky;
					double dx = vx.to_float(&okx);
					double dy = vy.to_float(&oky);
					if (!okx || !oky || !std::isfinite(dx) || !std::isfinite(dy)) continue;

					QString pool_str = QString::fromUtf8(vp.data(), (int)vp.size());
					if (cells_pool.find(pool_str) == cells_pool.end()) cell_order.push_back(pool_str);
					auto &c = cells_pool[pool_str];
					c.sx += dx;
					c.sy += dy;
					c.n++;
					if (l_col > 0) {
						auto vl = dt->get_cell(r, l_col);
						c.label_freq[QString::fromUtf8(vl.data(), (int)vl.size())]++;
					}
				}

				xv.clear(); yv.clear(); lv.clear();
				rows.clear();
				for (auto &key : cell_order) {
					auto &c = cells_pool[key];
					if (c.n == 0) continue;
					xv.push_back(c.sx / c.n);
					yv.push_back(c.sy / c.n);
					if (l_col > 0) {
						// Use the most frequent label among the rows in
						// this pool cell. Ties broken by first-seen.
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

			double reg_intercept = 0, reg_slope = 0, reg_r2 = 0;
			bool reg_valid = false;
			if (m_eda_regline_check->isChecked() && xv.size() >= 2)
			{
				size_t n_ = xv.size();
				double sx = 0, sy = 0;
				for (size_t i = 0; i < n_; i++) { sx += xv[i]; sy += yv[i]; }
				double mx = sx / n_, my = sy / n_;

				double sxy = 0, sxx = 0, syy = 0;
				for (size_t i = 0; i < n_; i++) {
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
			if (p_col > 0)
				title += QStringLiteral(" [pooled by ") + m_eda_pool_combo->currentText()
				       + QStringLiteral("]");

			m_eda_plot->setData(std::move(xv), std::move(yv), x_name_q, y_name_q,
			                     title, PlotWidget::RefLine::None, formant, formant,
			                     std::move(lv), std::move(rows));

			if (reg_valid)
				m_eda_plot->setRegressionLine(reg_intercept, reg_slope, reg_r2);
			else
				m_eda_plot->clearRegressionLine();
		}
	}

	// ── Apply user customization (overrides auto title/labels/range) ────
	// Done at the very end so it overrides whatever the dispatch above
	// just set. Empty strings and unset optionals are no-ops; the facet
	// branch also calls this before its early-return so the two paths
	// produce identical results from the user's perspective.
	if (!m_eda_customization.title.isEmpty())
		m_eda_plot->setTitle(m_eda_customization.title);
	if (!m_eda_customization.x_label.isEmpty())
		m_eda_plot->setXLabel(m_eda_customization.x_label);
	if (!m_eda_customization.y_label.isEmpty())
		m_eda_plot->setYLabel(m_eda_customization.y_label);
	// Ranges: the dialog gates these on a "Custom X/Y range" checkbox and
	// requires both bounds when the checkbox is checked, so x_min and
	// x_max are either both set or both unset (same for y). Either-or-
	// nothing means a single has_value() check suffices.
	if (m_eda_customization.x_min.has_value() && m_eda_customization.x_max.has_value())
		m_eda_plot->setForcedXRange(*m_eda_customization.x_min, *m_eda_customization.x_max);
	else
		m_eda_plot->clearForcedXRange();
	if (m_eda_customization.y_min.has_value() && m_eda_customization.y_max.has_value())
		m_eda_plot->setForcedYRange(*m_eda_customization.y_min, *m_eda_customization.y_max);
	else
		m_eda_plot->clearForcedYRange();
	m_eda_plot->setFacetNColsOverride(m_eda_customization.facet_ncols);
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
	Array<String> x_virtual_cells;
	bool x_virtual = false;
	if (x_col < 1)
	{
		if (isVirtualEdaColumn(x_name_q))
		{
			x_virtual_cells = buildVirtualEdaCells(x_name_q);
			x_virtual = !x_virtual_cells.empty();
		}
		if (!x_virtual) return;
	}
	auto xc = [&](intptr_t r) -> String {
		return x_virtual ? x_virtual_cells[r] : dt->get_cell(r, x_col);
	};

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
				auto v = xc(r);
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
				auto v = xc(r);
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
	Array<String> y_virtual_cells;
	bool y_virtual = false;
	if (y_col < 1)
	{
		if (isVirtualEdaColumn(y_name_q))
		{
			y_virtual_cells = buildVirtualEdaCells(y_name_q);
			y_virtual = !y_virtual_cells.empty();
		}
		if (!y_virtual) return;
	}
	auto yc = [&](intptr_t r) -> String {
		return y_virtual ? y_virtual_cells[r] : dt->get_cell(r, y_col);
	};

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
					auto vx = xc(r);
					auto vy = yc(r);
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
					auto vx = xc(r);
					auto vy = yc(r);
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
			// When a pool variable is set, the stats are computed over pool
			// cell means rather than raw observations — so the user sees
			// grand means, in keeping with the plot itself (one point per
			// pool level). N then reports the number of pool cells; the
			// Missing column still reports raw rows that couldn't make any
			// cell because of an NA in X, Y, or the pool column.

			bool has_pool = (m_eda_pool_combo->currentIndex() > 0);
			intptr_t p_col = 0;
			if (has_pool) {
				auto pool_name = String(m_eda_pool_combo->currentText().toUtf8().constData());
				for (intptr_t j = 1; j <= nc; j++) {
					if (dt->get_header(j) == pool_name) { p_col = j; break; }
				}
			}

			std::vector<double> xv, yv;
			xv.reserve(nr);
			yv.reserve(nr);
			intptr_t missing = 0;

			if (p_col > 0)
			{
				struct PoolCell { double sx = 0, sy = 0; int n = 0; };
				std::map<QString, PoolCell> cells;
				std::vector<QString> cell_order;
				for (intptr_t r = 1; r <= nr; r++)
				{
					auto vx = xc(r);
					auto vy = yc(r);
					auto vp = dt->get_cell(r, p_col);
					if (vx.empty() || vy.empty() || vp.empty()) { missing++; continue; }
					bool okx, oky;
					double dx = vx.to_float(&okx);
					double dy = vy.to_float(&oky);
					if (!okx || !oky || !std::isfinite(dx) || !std::isfinite(dy)) { missing++; continue; }
					QString key = QString::fromUtf8(vp.data(), (int)vp.size());
					if (cells.find(key) == cells.end()) cell_order.push_back(key);
					auto &c = cells[key];
					c.sx += dx; c.sy += dy; c.n++;
				}
				for (auto &key : cell_order) {
					auto &c = cells[key];
					if (c.n == 0) continue;
					xv.push_back(c.sx / c.n);
					yv.push_back(c.sy / c.n);
				}
			}
			else
			{
				for (intptr_t r = 1; r <= nr; r++)
				{
					auto vx = xc(r);
					auto vy = yc(r);
					if (vx.empty() || vy.empty()) { missing++; continue; }
					bool okx, oky;
					double dx = vx.to_float(&okx);
					double dy = vy.to_float(&oky);
					if (okx && oky && std::isfinite(dx) && std::isfinite(dy)) { xv.push_back(dx); yv.push_back(dy); }
					else missing++;
				}
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
		// If the user has explicitly picked Proportion, replace the boxplot-
		// style summary with per-(X level [, Group level]) counts, observed
		// proportion, and Wilson 95% interval. Mirrors what the plot shows
		// so the numbers can be copied / saved as a table.
		auto ptype_data = m_eda_plot_type_combo->currentData();
		EdaPlotType ptype = ptype_data.isValid()
			? static_cast<EdaPlotType>(ptype_data.toInt())
			: EdaPlotType::Auto;
		bool proportion_mode = (ptype == EdaPlotType::Proportion);
		bool has_group = (m_eda_group_combo->currentIndex() > 0);

		if (proportion_mode)
		{
			// Resolve Group column.
			intptr_t g_col = 0;
			QString group_name_q;
			if (has_group) {
				group_name_q = m_eda_group_combo->currentText();
				auto group_name = String(group_name_q.toUtf8().constData());
				for (intptr_t j = 1; j <= nc; j++) {
					if (dt->get_header(j) == group_name) { g_col = j; break; }
				}
				if (g_col < 1) has_group = false;
			}

			// Determine the "success" value: larger of the two unique Y
			// values across the column. Bail if there aren't exactly two.
			double v0 = std::nan(""), v1 = std::nan("");
			int n_unique = 0;
			for (intptr_t r = 1; r <= nr; r++)
			{
				auto vy = yc(r);
				if (vy.empty()) continue;
				bool ok; double d = vy.to_float(&ok);
				if (!ok || !std::isfinite(d)) continue;
				if (n_unique == 0) { v0 = d; n_unique = 1; }
				else if (n_unique == 1 && d != v0) { v1 = d; n_unique = 2; }
				else if (n_unique == 2 && d != v0 && d != v1) { n_unique = 3; break; }
			}
			if (n_unique != 2) return; // plot already shows the hint
			double success = std::max(v0, v1);

			// Two-pass aggregation matching the plot logic.
			std::vector<QString> x_levels, g_levels;
			std::map<QString, int> x_idx, g_idx;
			std::vector<std::vector<int>> n_cell, k_cell;
			auto ensure_x = [&](const QString &lvl) -> int {
				auto it = x_idx.find(lvl);
				if (it != x_idx.end()) return it->second;
				int i = (int)x_levels.size();
				x_idx[lvl] = i;
				x_levels.push_back(lvl);
				for (auto &row : n_cell) row.push_back(0);
				for (auto &row : k_cell) row.push_back(0);
				return i;
			};
			auto ensure_g = [&](const QString &lvl) -> int {
				auto it = g_idx.find(lvl);
				if (it != g_idx.end()) return it->second;
				int i = (int)g_levels.size();
				g_idx[lvl] = i;
				g_levels.push_back(lvl);
				n_cell.emplace_back((int)x_levels.size(), 0);
				k_cell.emplace_back((int)x_levels.size(), 0);
				return i;
			};
			if (!has_group) ensure_g(QString());
			for (intptr_t r = 1; r <= nr; r++)
			{
				auto vx = xc(r);
				auto vy = yc(r);
				if (vx.empty() || vy.empty()) continue;
				bool ok; double d = vy.to_float(&ok);
				if (!ok || !std::isfinite(d)) continue;
				if (has_group && dt->get_cell(r, g_col).empty()) continue;
				auto qx = QString::fromUtf8(vx.data(), (int)vx.size());
				int ci = ensure_x(qx);
				int gi = 0;
				if (has_group) {
					auto vg = dt->get_cell(r, g_col);
					auto qg = QString::fromUtf8(vg.data(), (int)vg.size());
					gi = ensure_g(qg);
				}
				n_cell[gi][ci]++;
				if (d == success) k_cell[gi][ci]++;
			}
			if (x_levels.empty()) return;

			constexpr double z = 1.95996398454005423;
			auto wilson = [&](int k, int n) -> std::tuple<double, double, double> {
				if (n <= 0) return {std::nan(""), std::nan(""), std::nan("")};
				double phat  = (double)k / (double)n;
				double denom = 1.0 + z*z / n;
				double center = (phat + z*z / (2.0*n)) / denom;
				double half   = z * std::sqrt(phat*(1-phat)/n + z*z/(4.0*(double)n*n)) / denom;
				return {phat, center - half, center + half};
			};

			QStringList headers;
			if (has_group) headers << group_name_q;
			headers << x_name_q << tr("n") << tr("k")
			        << QStringLiteral("p\u0302") // p-hat
			        << QStringLiteral("CI\u2082\u2024\u2085")  // CI 2.5%
			        << QStringLiteral("CI\u2089\u2087\u2024\u2085"); // CI 97.5%
			int ncols = headers.size();

			int total_rows = (int)g_levels.size() * (int)x_levels.size();
			m_eda_summary->setColumnCount(ncols);
			m_eda_summary->setHorizontalHeaderLabels(headers);
			m_eda_summary->setRowCount(total_rows);

			auto set = [&](int row, int col, const QString &text, bool right=true) {
				auto *item = new QTableWidgetItem(text);
				if (right) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
				m_eda_summary->setItem(row, col, item);
			};

			int row = 0;
			for (int gi = 0; gi < (int)g_levels.size(); gi++)
			{
				for (int ci = 0; ci < (int)x_levels.size(); ci++)
				{
					int col = 0;
					if (has_group) set(row, col++, g_levels[gi], /*right=*/false);
					set(row, col++, x_levels[ci], /*right=*/false);
					auto [p, lo, hi] = wilson(k_cell[gi][ci], n_cell[gi][ci]);
					set(row, col++, QString::number(n_cell[gi][ci]));
					set(row, col++, QString::number(k_cell[gi][ci]));
					if (std::isnan(p)) {
						set(row, col++, QStringLiteral("\u2014")); // em-dash
						set(row, col++, QStringLiteral("\u2014"));
						set(row, col++, QStringLiteral("\u2014"));
					} else {
						set(row, col++, QString::number(p,  'f', 4));
						set(row, col++, QString::number(lo, 'f', 4));
						set(row, col++, QString::number(hi, 'f', 4));
					}
					row++;
				}
			}
			m_eda_summary->resizeColumnsToContents();
			return;
		}

		// ── Box plot (X categorical, Y continuous): grouped stats ──

		std::vector<QString> group_order;
		std::map<QString, std::vector<double>> grouped;
		intptr_t included = 0;

		for (intptr_t r = 1; r <= nr; r++)
		{
			auto vx = xc(r);
			auto vy = yc(r);
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
	else if (!x_numeric && !y_numeric)
	{
		// ── Contingency table (X categorical, Y categorical) ──
		// Rows = Y levels, columns = X levels, plus a rightmost "Total"
		// column and a bottom "Total" row. Cells show "count (row%)" —
		// the most common reading is "of all Y=y_i observations, what %
		// fall into X=x_j", which is the row percentage. Column and grand
		// totals stay as raw counts to avoid double-percentage clutter.

		std::vector<QString> x_levels, y_levels;
		std::map<QString, int> x_idx, y_idx;
		std::vector<std::vector<int>> counts;
		auto ensure_x = [&](const QString &lvl) -> int {
			auto it = x_idx.find(lvl);
			if (it != x_idx.end()) return it->second;
			int i = (int)x_levels.size();
			x_idx[lvl] = i; x_levels.push_back(lvl);
			for (auto &row : counts) row.push_back(0);
			return i;
		};
		auto ensure_y = [&](const QString &lvl) -> int {
			auto it = y_idx.find(lvl);
			if (it != y_idx.end()) return it->second;
			int i = (int)y_levels.size();
			y_idx[lvl] = i; y_levels.push_back(lvl);
			counts.emplace_back((int)x_levels.size(), 0);
			return i;
		};

		intptr_t included = 0;
		for (intptr_t r = 1; r <= nr; r++)
		{
			auto vx = xc(r);
			auto vy = yc(r);
			if (vx.empty() || vy.empty()) continue;
			auto qx = QString::fromUtf8(vx.data(), (int)vx.size());
			auto qy = QString::fromUtf8(vy.data(), (int)vy.size());
			int ci = ensure_x(qx);
			int ri = ensure_y(qy);
			counts[ri][ci]++;
			included++;
		}
		if (x_levels.empty() || y_levels.empty()) return;

		int nx = (int)x_levels.size();
		int ny = (int)y_levels.size();
		intptr_t missing = nr - included;
		// Columns: [Y \ X], one per X level, then Total. We embed the
		// Y-axis label in the (0,0) header cell so the user sees "vowel \
		// dialect" clearly.
		int ncols = 1 + nx + 1;
		m_eda_summary->setColumnCount(ncols);
		QStringList headers;
		headers << QStringLiteral("%1 \\ %2").arg(y_name_q, x_name_q);
		for (int c = 0; c < nx; c++) headers << x_levels[c];
		headers << tr("Total");
		m_eda_summary->setHorizontalHeaderLabels(headers);
		// Rows: one per Y level, plus a Total row, plus a Missing row when
		// any rows were skipped due to NA in X or Y.
		int nrows_out = ny + 1 + (missing > 0 ? 1 : 0);
		m_eda_summary->setRowCount(nrows_out);

		auto set = [&](int row, int col, const QString &text, bool right) {
			auto *item = new QTableWidgetItem(text);
			if (right) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
			m_eda_summary->setItem(row, col, item);
		};

		std::vector<int> col_totals(nx, 0);
		int grand_total = 0;
		for (int r = 0; r < ny; r++)
		{
			int row_total = 0;
			for (int c = 0; c < nx; c++) {
				row_total    += counts[r][c];
				col_totals[c] += counts[r][c];
			}
			grand_total += row_total;
			set(r, 0, y_levels[r], /*right=*/false);
			for (int c = 0; c < nx; c++) {
				int n = counts[r][c];
				QString txt = (row_total > 0)
					? QStringLiteral("%1 (%2%)")
					      .arg(n)
					      .arg(100.0 * n / row_total, 0, 'f', 1)
					: QString::number(n);
				set(r, 1 + c, txt, /*right=*/true);
			}
			set(r, 1 + nx, QString::number(row_total), /*right=*/true);
		}

		// Totals row.
		set(ny, 0, tr("Total"), /*right=*/false);
		for (int c = 0; c < nx; c++)
			set(ny, 1 + c, QString::number(col_totals[c]), /*right=*/true);
		set(ny, 1 + nx, QString::number(grand_total), /*right=*/true);

		// Missing row, when applicable.
		if (missing > 0) {
			set(ny + 1, 0, tr("Missing"), /*right=*/false);
			set(ny + 1, 1 + nx, QString::number((int)missing), /*right=*/true);
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

	// Caption: formula followed by the estimation method, so that
	// readers of the exported table can tell at a glance whether the
	// fit was frequentist (ML or REML) or Bayesian. Mirrors the
	// "Estimation:" line in the in-app and scripting summaries.
	QString estimation_text;
	if (m.is_bayesian()) {
		estimation_text = QStringLiteral("Bayesian (Gaussian approximation)");
	} else if (m.method == stats::Method::REML) {
		estimation_text = QStringLiteral("Frequentist (restricted maximum likelihood)");
	} else {
		estimation_text = QStringLiteral("Frequentist (maximum likelihood)");
	}
	tex += QStringLiteral("\\caption{%1. Estimation: %2.}\n").arg(formula, estimation_text);

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
		// Same Corr-column logic as the text display: only show if any
		// group has random slopes (q > 1). For q = 1 throughout we keep
		// the legacy 4-column tabular.
		bool show_corr = false;
		for (intptr_t g = 1; g <= m.random_effects.size(); g++) {
			if (m.random_effects[g].term_names.size() > 1) {
				show_corr = true;
				break;
			}
		}

		// cov_chol is the packed lower-triangular raw Cholesky factor L
		// (NOT log-diagonal), stored 1-indexed, row by row. Element (r, c)
		// with 0-indexed r ≥ c lives at cov_chol[r*(r+1)/2 + c + 1].
		auto chol_at = [](const Array<double> &cc, intptr_t r0, intptr_t c0) -> double {
			intptr_t idx = r0 * (r0 + 1) / 2 + c0 + 1;
			return (idx <= cc.size()) ? cc[idx] : 0.0;
		};
		auto cov_st = [&](const Array<double> &cc, intptr_t s0, intptr_t t0) -> double {
			if (s0 > t0) std::swap(s0, t0);
			double sum = 0.0;
			for (intptr_t k = 0; k <= s0; k++) {
				sum += chol_at(cc, s0, k) * chol_at(cc, t0, k);
			}
			return sum;
		};

		tex += QStringLiteral("\\medskip\n");
		if (show_corr) {
			tex += QStringLiteral("\\begin{tabular}{lrrrr}\n");
		} else {
			tex += QStringLiteral("\\begin{tabular}{lrrr}\n");
		}
		tex += QStringLiteral("\\hline\n");
		if (show_corr) {
			tex += QStringLiteral("Group & Variance & Std.~Dev. & Levels & Corr \\\\\n");
		} else {
			tex += QStringLiteral("Group & Variance & Std.~Dev. & Levels \\\\\n");
		}
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
					if (show_corr) {
						tex += QStringLiteral("%1 & %2 & %3 & %4 & \\\\\n")
							.arg(gname)
							.arg(var, 0, 'f', 4)
							.arg(sd, 0, 'f', 4)
							.arg(re.nlevels);
					} else {
						tex += QStringLiteral("%1 & %2 & %3 & %4 \\\\\n")
							.arg(gname)
							.arg(var, 0, 'f', 4)
							.arg(sd, 0, 'f', 4)
							.arg(re.nlevels);
					}
				} else {
					QString tname = QString::fromUtf8(re.term_names[t].data(),
					                                   (int)re.term_names[t].size());
					tname.replace('_', QStringLiteral("\\_"));

					if (show_corr) {
						// For q ≥ 3, multiple corrs go in a single Corr cell,
						// comma-separated. For q = 2, just one value.
						QString corrs;
						for (intptr_t s = 1; s < t; s++) {
							double var_s = (s <= re.variance.size()) ? re.variance[s] : 0.0;
							double denom = std::sqrt(std::max(var_s, 1e-30)
							                       * std::max(var,   1e-30));
							double corr = cov_st(re.cov_chol, s - 1, t - 1) / denom;
							if (s > 1) corrs += QStringLiteral(", ");
							corrs += QString::number(corr, 'f', 4);
						}
						tex += QStringLiteral("\\quad %1 & %2 & %3 & & %4 \\\\\n")
							.arg(tname)
							.arg(var, 0, 'f', 4)
							.arg(sd, 0, 'f', 4)
							.arg(corrs);
					} else {
						tex += QStringLiteral("\\quad %1 & %2 & %3 & \\\\\n")
							.arg(tname)
							.arg(var, 0, 'f', 4)
							.arg(sd, 0, 'f', 4);
					}
				}
			}
		}

		if (m.is_gaussian()) {
			if (show_corr) {
				tex += QStringLiteral("Residual & %1 & %2 & & \\\\\n")
					.arg(m.rse * m.rse, 0, 'f', 4)
					.arg(m.rse, 0, 'f', 4);
			} else {
				tex += QStringLiteral("Residual & %1 & %2 & \\\\\n")
					.arg(m.rse * m.rse, 0, 'f', 4)
					.arg(m.rse, 0, 'f', 4);
			}
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
		if (!m.laplace_method.empty()) {
			QString label = (m.laplace_method == "exact")
			    ? QStringLiteral("exact")
			    : QStringLiteral("Fisher-info");
			tex += QStringLiteral("; Laplace = %1").arg(label);
		}
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
// Export diagnostic plot (Save... menu actions: PNG / PDF / SVG)
// =====================================================================
//
// Mirrors the EDA panel: each format has its own slot, both wired into
// the home toolbar's Save... popup menu and into the floating window's
// toolbar (via the DetachablePlot save_* callbacks).

void AnalysisView::onExportDiagPNG()
{
	if (!m_plot->hasData()) {
		QMessageBox::information(this, tr("Export"), tr("No plot to export."));
		return;
	}
	QString path = getSaveFileName(this,
		tr("Export plot as PNG"), tr("PNG image (*.png)"));
	if (path.isEmpty()) return;
	if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
		path += QStringLiteral(".png");
	m_plot->savePNG(path);
}

void AnalysisView::onExportDiagPDF()
{
	if (!m_plot->hasData()) {
		QMessageBox::information(this, tr("Export"), tr("No plot to export."));
		return;
	}
	QString path = getSaveFileName(this,
		tr("Export plot as PDF"), tr("PDF document (*.pdf)"));
	if (path.isEmpty()) return;
	if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
		path += QStringLiteral(".pdf");
	m_plot->savePDF(path);
}

void AnalysisView::onExportDiagSVG()
{
	if (!m_plot->hasData()) {
		QMessageBox::information(this, tr("Export"), tr("No plot to export."));
		return;
	}
	QString path = getSaveFileName(this,
		tr("Export plot as SVG"), tr("SVG image (*.svg)"));
	if (path.isEmpty()) return;
	if (!path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive))
		path += QStringLiteral(".svg");
	m_plot->saveSVG(path);
}


// =====================================================================
// Effects tab
// =====================================================================
//
// Phase 2 MVP: numeric focal → line + CI ribbon (100-point grid across
// the observed range); categorical focal → markers + error bars (one
// per level). All other categorical predictors held at their reference
// level (vi.levels[1]); other numerics held at their observed mean.
// Mixed-effects, Bayesian, by-factor smooths, and re-smooths are
// refused with a message in the plot area.

namespace {

// Iterate the source DataTable and compute, for each numeric column,
// the mean of parseable values together with the observed range
// (min/max) used to lay out the focal grid. Empty / "nan" / "NaN" /
// "NA" cells are skipped. Categorical columns are not summarised here:
// they are held at their reference level (vi.levels[1]), which the
// model already knows about and which matches ggpredict's default.
struct SourceStats
{
	std::map<std::string, double> numeric_mean;
	std::map<std::string, double> numeric_lo;
	std::map<std::string, double> numeric_hi;
};

static bool parse_cell_double(const String &cell, double &out)
{
	if (cell.empty() || cell == "nan" || cell == "NaN" || cell == "NA") return false;
	bool ok = false;
	out = cell.to_float(&ok);
	return ok && !std::isnan(out);
}

static SourceStats compute_source_stats(const DataTable &dt,
                                        const std::vector<String> &numeric_cols)
{
	SourceStats s;

	auto find_col = [&](const String &name) -> intptr_t {
		intptr_t nc = dt.column_count();
		for (intptr_t j = 1; j <= nc; j++) {
			if (dt.get_header(j) == name) return j;
		}
		return 0;
	};

	intptr_t nr = dt.row_count();

	for (auto &name : numeric_cols)
	{
		intptr_t col = find_col(name);
		if (col == 0) continue;
		std::string key(name.data(), name.size());

		double sum = 0;
		intptr_t cnt = 0;
		double lo = 1e300, hi = -1e300;

		for (intptr_t r = 1; r <= nr; r++)
		{
			double v;
			if (!parse_cell_double(dt.get_cell(r, col), v)) continue;
			sum += v;
			cnt++;
			if (v < lo) lo = v;
			if (v > hi) hi = v;
		}
		if (cnt > 0) {
			s.numeric_mean[key] = sum / (double) cnt;
			s.numeric_lo[key]   = lo;
			s.numeric_hi[key]   = hi;
		}
	}

	return s;
}

} // anonymous namespace


void AnalysisView::populateEffectsFocalCombo()
{
	m_effects_focal_combo->blockSignals(true);
	m_effects_by_combo->blockSignals(true);
	m_effects_re_combo->blockSignals(true);
	m_effects_re_levels->blockSignals(true);
	m_effects_focal_combo->clear();
	m_effects_by_combo->clear();
	m_effects_re_combo->clear();
	m_effects_re_levels->setItems(QStringList());
	m_effects_re_levels->setEnabled(false);
	m_effects_by_combo->addItem(tr("(None)"));
	m_effects_re_combo->addItem(tr("(None)"));

	if (m_current_model < 0 || m_current_model >= m_analysis->model_count()) {
		m_effects_focal_combo->blockSignals(false);
		m_effects_by_combo->blockSignals(false);
		m_effects_re_combo->blockSignals(false);
		m_effects_re_levels->blockSignals(false);
		return;
	}

	auto &m = m_analysis->model(m_current_model);

	// Focal: fixed-effects predictors (categorical with ≥ 2 levels, or numeric).
	for (intptr_t i = 1; i <= m.variable_info.size(); i++)
	{
		auto &vi = m.variable_info[i];
		if (!vi.numeric && vi.levels.size() < 2) continue;
		auto qname = QString::fromUtf8(vi.name.data(), (int) vi.name.size());
		m_effects_focal_combo->addItem(qname);
	}

	// Focal: smooth covariates. Not in variable_info (fitting.cpp expands
	// only the fixed-effects loop into variable_info), but they are valid
	// focal predictors — the smooth basis is replayed by predict().
	// Skip names already added.
	for (intptr_t i = 1; i <= m.smooth_terms.size(); i++)
	{
		auto &sm = m.smooth_terms[i];
		auto qname = QString::fromUtf8(sm.variable.data(), (int) sm.variable.size());
		if (m_effects_focal_combo->findText(qname) < 0) {
			m_effects_focal_combo->addItem(qname);
		}
	}

	// By: every categorical predictor with ≥ 2 levels (whether or not the
	// formula contains an interaction with the focal). For purely additive
	// models this produces parallel curves — informative as a visual sanity
	// check that there's no interaction.
	for (intptr_t i = 1; i <= m.variable_info.size(); i++)
	{
		auto &vi = m.variable_info[i];
		if (vi.numeric || vi.levels.size() < 2) continue;
		auto qname = QString::fromUtf8(vi.name.data(), (int) vi.name.size());
		m_effects_by_combo->addItem(qname);
	}

	// Random: every random-effects group on the model. Only meaningful for
	// mixed-effects models; for fixed-effects-only models the combo stays at
	// "(None)". Switching to a group repopulates the levels checklist via
	// onEffectsRandomChanged().
	for (intptr_t g = 1; g <= m.random_effects.size(); g++)
	{
		auto &re = m.random_effects[g];
		auto qname = QString::fromUtf8(re.group_name.data(), (int) re.group_name.size());
		m_effects_re_combo->addItem(qname);
	}

	m_effects_focal_combo->blockSignals(false);
	m_effects_by_combo->blockSignals(false);
	m_effects_re_combo->blockSignals(false);
	m_effects_re_levels->blockSignals(false);
}


void AnalysisView::onEffectsRandomChanged()
{
	// Repopulate the levels checklist based on the chosen group, toggle the
	// By-combo enabled state (Random and By are mutually exclusive faceting
	// modes), set a sensible default for the Show CI checkbox, and refresh
	// the plot.
	m_effects_re_levels->blockSignals(true);
	m_effects_re_levels->setItems(QStringList());

	bool re_active = (m_effects_re_combo->currentIndex() > 0);

	if (!re_active) {
		// Population-level mode: levels disabled, By enabled, CI default on,
		// legend default on (typically 1-2 curves, well within the readable
		// legend threshold).
		m_effects_re_levels->setEnabled(false);
		m_effects_by_combo->setEnabled(true);
		m_effects_show_ci_check->blockSignals(true);
		m_effects_show_ci_check->setChecked(true);
		m_effects_show_ci_check->blockSignals(false);
		m_effects_show_legend_check->blockSignals(true);
		m_effects_show_legend_check->setChecked(true);
		m_effects_show_legend_check->blockSignals(false);
	}
	else {
		// Conditional mode: populate levels (all checked), disable By,
		// default CI off (busy plots). Legend default is auto-rule based
		// on level count: ≤ 8 → on, > 8 → off. Once the user expresses
		// an opinion (toggles the checkbox), we don't override it again
		// until they switch random group or back to (None).
		intptr_t n_levels_default = 0;
		if (m_current_model >= 0 && m_current_model < m_analysis->model_count())
		{
			auto &m = m_analysis->model(m_current_model);
			QString gname = m_effects_re_combo->currentText();

			intptr_t found = 0;
			for (intptr_t g = 1; g <= m.random_effects.size(); g++) {
				auto &re = m.random_effects[g];
				QString rg_q = QString::fromUtf8(re.group_name.data(),
				                                (int) re.group_name.size());
				if (rg_q == gname) { found = g; break; }
			}
			if (found > 0) {
				auto &re = m.random_effects[found];
				QStringList lvls;
				for (intptr_t l = 1; l <= re.level_names.size(); l++) {
					lvls << QString::fromUtf8(re.level_names[l].data(),
					                          (int) re.level_names[l].size());
				}
				m_effects_re_levels->setItems(lvls);
				m_effects_re_levels->setCheckedItems(lvls);
				n_levels_default = (intptr_t) lvls.size();
			}
		}
		m_effects_re_levels->setEnabled(true);
		m_effects_by_combo->setEnabled(false);
		m_effects_show_ci_check->blockSignals(true);
		m_effects_show_ci_check->setChecked(false);
		m_effects_show_ci_check->blockSignals(false);
		m_effects_show_legend_check->blockSignals(true);
		m_effects_show_legend_check->setChecked(n_levels_default <= 8);
		m_effects_show_legend_check->blockSignals(false);
	}

	m_effects_re_levels->blockSignals(false);
	updateEffectsPlot();
}


void AnalysisView::onEffectsFocalChanged()
{
	updateEffectsPlot();
}


void AnalysisView::updateEffectsPlot()
{
	auto show_message = [this](const QString &msg) {
		// If the plot is currently detached, reattach it before
		// showing the message — otherwise the user sees an empty
		// floating window and the explanation renders out of view in
		// the home tab. The message label and the plot are mutually
		// exclusive, and the message lives only in the home tab.
		if (m_effects_detach.float_window)
			reattachPlot(m_effects_detach);
		m_effects_plot->clear();
		m_effects_plot->setVisible(false);
		m_effects_message->setText(msg);
		m_effects_message->setVisible(true);
		updateDetachActionsEnabled();
	};
	auto show_plot = [this]() {
		m_effects_message->setVisible(false);
		m_effects_plot->setVisible(true);
		updateDetachActionsEnabled();
	};

	if (m_current_model < 0 || m_current_model >= m_analysis->model_count()) {
		show_message(tr("Select a model to view effects plots."));
		return;
	}

	auto &m = m_analysis->model(m_current_model);

	// Refusals — match the underlying predict_at() refusals so the user
	// sees a clear explanation rather than an obscure error. Mixed-effects
	// and Bayesian models are now both supported (population-level prediction
	// with u=0; posterior mean and credible interval for Bayesian); the
	// caption below adapts to the model type.
	for (intptr_t i = 1; i <= m.smooth_terms.size(); i++) {
		auto &sm = m.smooth_terms[i];
		if (!sm.by.empty()) {
			show_message(tr(
				"Effects plots are not yet supported for models with "
				"by-factor smooths (s(x, by=...)). "
				"This will be added in a future release."));
			return;
		}
		if (sm.basis == "re") {
			show_message(tr(
				"Effects plots are not yet supported for models with "
				"random-effect smooths (s(g, bs=\"re\"))."));
			return;
		}
	}

	if (!m_analysis->has_source()) {
		show_message(tr(
			"Effects plots require access to the source data. "
			"Reopen the dataset that was used to fit this model."));
		return;
	}

	if (m_effects_focal_combo->count() == 0
	    || m_effects_focal_combo->currentIndex() < 0) {
		show_message(tr(
			"This model has no predictors that can be used as a focal "
			"variable for an effects plot."));
		return;
	}

	QString focal_qname = m_effects_focal_combo->currentText();
	String focal_name(focal_qname.toUtf8().constData());

	// Resolve focal type (numeric / categorical) and, for categorical, the
	// level set. Smooth covariates are always numeric and not in
	// variable_info, so we have to check both.
	bool focal_is_numeric = true;
	Array<String> focal_levels;

	bool found = false;
	for (intptr_t i = 1; i <= m.variable_info.size(); i++) {
		if (m.variable_info[i].name == focal_name) {
			focal_is_numeric = m.variable_info[i].numeric;
			focal_levels = m.variable_info[i].levels;
			found = true;
			break;
		}
	}
	if (!found) {
		// Smooth covariate — numeric by construction.
		focal_is_numeric = true;
	}

	auto *src = m_analysis->data();

	// Collect numeric predictor names (we'll need their means / observed
	// ranges from the source data). Categoricals don't need a source-data
	// pass — they are held at vi.levels[1] (treatment-contrast reference),
	// which is already on the model.
	std::vector<String> all_numeric;
	for (intptr_t i = 1; i <= m.variable_info.size(); i++) {
		auto &vi = m.variable_info[i];
		if (vi.numeric) all_numeric.push_back(vi.name);
	}
	for (intptr_t i = 1; i <= m.smooth_terms.size(); i++) {
		auto &sm = m.smooth_terms[i];
		bool already = false;
		for (auto &n : all_numeric) if (n == sm.variable) { already = true; break; }
		if (!already) all_numeric.push_back(sm.variable);
	}

	auto src_stats = compute_source_stats(*src, all_numeric);

	// ── Resolve the by-factor (optional categorical predictor) ──────
	// "(None)" at index 0 means no by-factor; everything else is a level
	// from a categorical predictor in variable_info. We refuse silently if
	// the user picked the same variable for focal and by — defensive check;
	// the combo could in principle offer focal=A, by=A simultaneously.
	String by_name;
	Array<String> by_levels;
	bool has_by = (m_effects_by_combo->currentIndex() > 0);
	if (has_by)
	{
		String candidate(m_effects_by_combo->currentText().toUtf8().constData());
		if (candidate == focal_name) {
			has_by = false;  // ignore — same variable on both axes
		} else {
			for (intptr_t i = 1; i <= m.variable_info.size(); i++) {
				auto &vi = m.variable_info[i];
				if (vi.name == candidate && !vi.numeric && vi.levels.size() >= 2) {
					by_name = vi.name;
					by_levels = vi.levels;
					break;
				}
			}
			if (by_levels.empty()) has_by = false;
		}
	}

	// ── Resolve the random-effects group (optional, mutually exclusive
	//    with the By-factor: when re_active, has_by is forced false). The
	//    selected levels come from the checklist; the predict() call below
	//    receives re_form = re_group_name and includes the group's BLUPs in
	//    each row's prediction. By-curves and RE-curves don't compose in
	//    this release — that's what the deferred "subplots per level"
	//    feature is for.
	bool re_active = (m_effects_re_combo->currentIndex() > 0);
	String re_group_name;
	Array<String> re_selected_levels;
	if (re_active)
	{
		has_by = false;
		by_name = String();
		by_levels = Array<String>();

		QString gname_q = m_effects_re_combo->currentText();
		re_group_name = String(gname_q.toUtf8().constData());

		QStringList sel = m_effects_re_levels->checkedItems();
		if (sel.isEmpty()) {
			show_message(tr(
				"No random-effects levels selected. "
				"Pick at least one level from the Levels list, or set "
				"Random back to (None) for a population-level plot."));
			return;
		}
		for (auto &q : sel) {
			re_selected_levels.append(String(q.toUtf8().constData()));
		}
	}

	// ── Build the reference grid ────────────────────────────────────
	// We build a single Dataset with n_grid × n_by_levels rows (or n_grid
	// rows when there's no by-factor). Each "block" of n_grid consecutive
	// rows corresponds to one by-level; the by-column carries that level
	// across all rows in the block. After predict() runs, we slice the
	// returned vectors back into per-curve segments.
	intptr_t n_grid = focal_is_numeric ? 100
	                                   : (intptr_t) focal_levels.size();
	if (n_grid < 2) {
		show_message(tr("Cannot construct a reference grid for this predictor."));
		return;
	}
	intptr_t n_by;
	if (re_active)      n_by = (intptr_t) re_selected_levels.size();
	else if (has_by)    n_by = (intptr_t) by_levels.size();
	else                n_by = 1;
	intptr_t n_rows = n_grid * n_by;

	auto grid = Dataset::create_empty(n_rows);

	// Focal column (repeated n_by times across blocks).
	std::vector<double> focal_x(n_grid, 0.0);
	if (focal_is_numeric)
	{
		std::string key(focal_name.data(), focal_name.size());
		auto lo_it = src_stats.numeric_lo.find(key);
		auto hi_it = src_stats.numeric_hi.find(key);
		if (lo_it == src_stats.numeric_lo.end() || hi_it == src_stats.numeric_hi.end()
		    || hi_it->second <= lo_it->second) {
			show_message(tr(
				"Cannot determine the range of '%1' from the source data.")
				.arg(focal_qname));
			return;
		}
		double lo = lo_it->second;
		double hi = hi_it->second;
		double step = (hi - lo) / (double) (n_grid - 1);
		for (intptr_t i = 0; i < n_grid; i++) focal_x[(size_t) i] = lo + step * i;

		std::vector<double> col_vals((size_t) n_rows);
		for (intptr_t b = 0; b < n_by; b++)
			for (intptr_t i = 0; i < n_grid; i++)
				col_vals[(size_t)(b * n_grid + i)] = focal_x[(size_t) i];
		grid->add_numeric_column(focal_name, col_vals);
	}
	else
	{
		std::vector<String> col_vals((size_t) n_rows);
		for (intptr_t i = 0; i < n_grid; i++) focal_x[(size_t) i] = (double) i;
		for (intptr_t b = 0; b < n_by; b++)
			for (intptr_t i = 0; i < n_grid; i++)
				col_vals[(size_t)(b * n_grid + i)] = focal_levels[i + 1];
		grid->add_text_column(focal_name, col_vals);
	}

	// By-factor column: each block fills with that block's by-level.
	if (has_by)
	{
		std::vector<String> col_vals((size_t) n_rows);
		for (intptr_t b = 0; b < n_by; b++) {
			String level = by_levels[b + 1];
			for (intptr_t i = 0; i < n_grid; i++)
				col_vals[(size_t)(b * n_grid + i)] = level;
		}
		grid->add_text_column(by_name, col_vals);
	}
	// Random-effects grouping column: each block carries the level identifier
	// for that block. predict() reads this column to look up the BLUP row to
	// add to η for each grid point (Z·u contribution).
	else if (re_active)
	{
		std::vector<String> col_vals((size_t) n_rows);
		for (intptr_t b = 0; b < n_by; b++) {
			String level = re_selected_levels[b + 1];
			for (intptr_t i = 0; i < n_grid; i++)
				col_vals[(size_t)(b * n_grid + i)] = level;
		}
		grid->add_text_column(re_group_name, col_vals);
	}

	// Other numeric predictors at their mean (constant across all rows).
	for (auto &name : all_numeric)
	{
		if (name == focal_name) continue;
		std::string key(name.data(), name.size());
		auto mit = src_stats.numeric_mean.find(key);
		if (mit == src_stats.numeric_mean.end()) {
			show_message(tr(
				"Could not compute mean for '%1' (no numeric values in source).")
				.arg(QString::fromUtf8(name.data(), (int) name.size())));
			return;
		}
		std::vector<double> vals((size_t) n_rows, mit->second);
		grid->add_numeric_column(name, vals);
	}

	// Other categorical predictors at their reference level (vi.levels[1]).
	// Skip the focal and by-variable; for everything else, fill the same
	// reference value across all n_rows.
	for (intptr_t i = 1; i <= m.variable_info.size(); i++)
	{
		auto &vi = m.variable_info[i];
		if (vi.numeric) continue;
		if (vi.name == focal_name) continue;
		if (has_by && vi.name == by_name) continue;
		if (vi.levels.empty()) {
			show_message(tr(
				"Categorical predictor '%1' has no recorded levels.")
				.arg(QString::fromUtf8(vi.name.data(), (int) vi.name.size())));
			return;
		}
		std::vector<String> vals((size_t) n_rows, vi.levels[1]);
		grid->add_text_column(vi.name, vals);
	}
	grid->mark_loaded();

	// ── Run predict ─────────────────────────────────────────────────
	stats::PredictOptions opts;
	opts.scale = "response";
	opts.bare = true;
	if (re_active) {
		// Tell predict() to add Z·u from this group's BLUPs per row.
		opts.re_form = re_group_name;
	}
	auto pr = stats::predict_at(m, *grid, opts);
	if (!pr.ok) {
		show_message(QString::fromUtf8(pr.error.data(), (int) pr.error.size()));
		return;
	}

	// ── Build the curve list, slicing by block ──────────────────────
	// Each block in the grid produced n_grid prediction rows. We slice
	// pr.fit / ci_lower / ci_upper into one EffectsCurve per block. The
	// curve's label depends on which mode is active: by-level when has_by,
	// random-effects level when re_active, empty otherwise (suppresses
	// legend for the single-curve population-level case).

	std::vector<PlotWidget::EffectsCurve> curves;
	curves.reserve((size_t) n_by);

	for (intptr_t b = 0; b < n_by; b++)
	{
		PlotWidget::EffectsCurve cv;
		if (has_by) {
			cv.label = QString::fromUtf8(by_levels[b + 1].data(),
			                             (int) by_levels[b + 1].size());
		} else if (re_active) {
			cv.label = QString::fromUtf8(re_selected_levels[b + 1].data(),
			                             (int) re_selected_levels[b + 1].size());
		}

		cv.x.resize((size_t) n_grid);
		cv.fit.resize((size_t) n_grid);
		cv.ci_lower.resize((size_t) n_grid);
		cv.ci_upper.resize((size_t) n_grid);

		intptr_t off = b * n_grid;
		for (intptr_t i = 0; i < n_grid; i++) {
			cv.x[(size_t) i] = focal_x[(size_t) i];
			cv.fit[(size_t) i]      = pr.fit.data()[off + i];
			cv.ci_lower[(size_t) i] = pr.ci_lower.data()[off + i];
			cv.ci_upper[(size_t) i] = pr.ci_upper.data()[off + i];
		}
		curves.push_back(std::move(cv));
	}

	// ── Feed PlotWidget ─────────────────────────────────────────────
	QString y_label = tr("Predicted ") + QString::fromUtf8(
		m.formula.data(), (int) m.formula.size()).section('~', 0, 0).trimmed();

	QString title;
	if (re_active) {
		title = tr("Effect of %1 by %2 (conditional)")
			.arg(focal_qname)
			.arg(QString::fromUtf8(re_group_name.data(),
			                       (int) re_group_name.size()));
	} else if (has_by) {
		title = tr("Effect of %1 by %2")
			.arg(focal_qname)
			.arg(QString::fromUtf8(by_name.data(), (int) by_name.size()));
	} else {
		title = tr("Effect of %1").arg(focal_qname);
	}
	// Caption adapts to the model type and to whether conditional prediction
	// is active. The "fixed-others" rule (categoricals at reference level,
	// numerics at mean) applies in all cases. Conditional mode displaces the
	// usual population-level wording with a "Conditional on <group>" prefix.
	QString caption;
	bool bayesian = m.is_bayesian();
	bool mixed    = m.has_random_effects();
	if (re_active)
	{
		QString gq = QString::fromUtf8(re_group_name.data(),
		                               (int) re_group_name.size());
		int sel = (int) re_selected_levels.size();
		QString head = tr("Conditional on %1 (%2 level%3)")
			.arg(gq).arg(sel).arg(sel == 1 ? QString() : tr("s"));
		QString interval = bayesian
			? tr("posterior mean")
			: tr("predicted mean");
		caption = tr("%1 — %2; other categoricals at reference level; "
		             "numerics at mean")
			.arg(head).arg(interval);
	}
	else if (bayesian && mixed) {
		caption = tr("Population-level posterior mean (95% credible interval); "
		             "other categoricals at reference level; numerics at mean");
	} else if (bayesian) {
		caption = tr("Posterior mean (95% credible interval); "
		             "other categoricals at reference level; numerics at mean");
	} else if (mixed) {
		caption = tr("Population-level (random effects = 0); "
		             "other categoricals at reference level; numerics at mean");
	} else {
		caption = tr("Other categorical predictors at reference level; "
		             "numerics at mean");
	}

	std::vector<QString> level_labels;
	if (!focal_is_numeric) {
		level_labels.reserve((size_t) n_grid);
		for (intptr_t i = 1; i <= focal_levels.size(); i++) {
			level_labels.push_back(QString::fromUtf8(
				focal_levels[i].data(), (int) focal_levels[i].size()));
		}
	}

	bool show_ci = m_effects_show_ci_check->isChecked();
	bool show_legend = m_effects_show_legend_check->isChecked();

	m_effects_plot->setEffectsPlotData(
		std::move(curves),
		focal_qname, y_label, title, caption,
		std::move(level_labels), show_ci, show_legend);

	show_plot();
}


void AnalysisView::onExportEffectsPNG()
{
	if (!m_effects_plot->hasData()) {
		QMessageBox::information(this, tr("Export"), tr("No plot to export."));
		return;
	}
	QString path = getSaveFileName(this,
		tr("Export plot as PNG"), tr("PNG image (*.png)"));
	if (path.isEmpty()) return;
	if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
		path += QStringLiteral(".png");
	m_effects_plot->savePNG(path);
}

void AnalysisView::onExportEffectsPDF()
{
	if (!m_effects_plot->hasData()) {
		QMessageBox::information(this, tr("Export"), tr("No plot to export."));
		return;
	}
	QString path = getSaveFileName(this,
		tr("Export plot as PDF"), tr("PDF document (*.pdf)"));
	if (path.isEmpty()) return;
	if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
		path += QStringLiteral(".pdf");
	m_effects_plot->savePDF(path);
}

void AnalysisView::onExportEffectsSVG()
{
	if (!m_effects_plot->hasData()) {
		QMessageBox::information(this, tr("Export"), tr("No plot to export."));
		return;
	}
	QString path = getSaveFileName(this,
		tr("Export plot as SVG"), tr("SVG image (*.svg)"));
	if (path.isEmpty()) return;
	if (!path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive))
		path += QStringLiteral(".svg");
	m_effects_plot->saveSVG(path);
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
		if (!m.laplace_method.empty()) {
			const char *method_label =
				(m.laplace_method == "exact")
				    ? "exact"
				    : "Fisher-information (robust fallback)";
			text += QString::asprintf("Laplace correction: %s\n", method_label);
		}
	}
	text += QStringLiteral("Formula: %1\n")
		.arg(QString::fromUtf8(m.formula.data(), (int)m.formula.size()));
	if (m.is_bayesian()) {
		text += QStringLiteral("Estimation: Bayesian (Gaussian approximation)\n");
	} else if (m.method == stats::Method::REML) {
		text += QStringLiteral("Estimation: Frequentist (restricted maximum likelihood)\n");
	} else {
		text += QStringLiteral("Estimation: Frequentist (maximum likelihood)\n");
	}
	text += QStringLiteral("Observations: %1\n").arg(m.nobs);

	// Experimental notice for models containing smooth terms.  Mirrors
	// the notice in print_model_summary (data_table.cpp) so users see
	// the same caveat whether they inspect the model in the GUI or via
	// summarize() in scripting.
	if (m.smooth_terms.size() > 0) {
		text += QStringLiteral(
			"\nNote: GAM support (s() smooth terms) is experimental in this release.\n"
			"      Fitted curves and inference are qualitatively reliable, but smooth\n"
			"      EDF and lambda values may differ numerically from reference\n"
			"      implementations such as R's mgcv\n");
	}

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
		// First-column width: fit longest hyperparameter name (e.g.
		// "cor(Intercept,man.dist:subsystem[vowels]|language)") so that
		// value columns don't get pushed out of alignment.
		int hyper_w = 36; // minimum
		for (intptr_t i = 1; i <= m.hyper_names.size(); i++)
		{
			int len = (int)m.hyper_names[i].size() + 2;
			if (len > hyper_w) hyper_w = len;
		}
		std::string hyper_fmt = "%-" + std::to_string(hyper_w) + "s";

		text += QStringLiteral("Hyperparameters (posterior):\n");
		text += QString::asprintf((hyper_fmt + " %12s %12s %12s %12s\n").c_str(),
		                           "", "Post.Mean", "Post.SD", "CI.lower", "CI.upper");

		std::string hyper_row = hyper_fmt + " %12.4f %12.4f %12.4f %12.4f\n";

		for (intptr_t i = 1; i <= m.hyper_names.size(); i++)
		{
			const char *name = m.hyper_names[i].data();
			double mean = (i <= m.hyper_posterior_mean.size()) ? m.hyper_posterior_mean[i] : 0.0;
			double sd = (i <= m.hyper_posterior_sd.size()) ? m.hyper_posterior_sd[i] : 0.0;
			double lo = (i <= m.hyper_ci_lower.size()) ? m.hyper_ci_lower[i] : 0.0;
			double hi = (i <= m.hyper_ci_upper.size()) ? m.hyper_ci_upper[i] : 0.0;

			text += QString::asprintf(hyper_row.c_str(),
			                           name, mean, sd, lo, hi);
		}
		text += QStringLiteral("\n");
	}

	// ── Smooth terms ───────────────────────────────────────────────

	if (m.has_smooth_terms())
	{
		// Build labels first so we can both size the column and reuse them.
		auto build_label = [](const auto &sm) -> String {
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
			return label;
		};

		Array<String> labels;
		int sm_w = 20;
		for (intptr_t i = 1; i <= m.smooth_terms.size(); i++) {
			labels.append(build_label(m.smooth_terms[i]));
			int len = (int)labels[i].size() + 2;
			if (len > sm_w) sm_w = len;
		}
		std::string sm_fmt = "%-" + std::to_string(sm_w) + "s";

		text += QStringLiteral("Approximate significance of smooth terms:\n");
		text += QString::asprintf((sm_fmt + " %8s %8s %10s %12s\n").c_str(),
		                           "", "edf", "Ref.df", "F", "p-value");

		std::string sm_row = sm_fmt + " %8.3f %8.3f %10.2f %12s%s\n";

		for (intptr_t i = 1; i <= m.smooth_terms.size(); i++)
		{
			auto &sm = m.smooth_terms[i];

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

			text += QString::asprintf(sm_row.c_str(),
			                           labels[i].data(), sm.edf, sm.ref_df, sm.F_stat, pbuf, stars);
		}

		text += QStringLiteral("---\n");
		text += QStringLiteral("Signif. codes: 0 '***' 0.001 '**' 0.01 '*' 0.05 '.' 0.1 ' ' 1\n\n");
	}

	// ── Random effects ─────────────────────────────────────────────

	if (m.has_random_effects())
	{
		// Show a "Corr" column when any group has q > 1 (random slopes).
		// Pure intercept-only models keep the legacy 4-column header.
		bool show_corr = false;
		for (intptr_t g = 1; g <= m.random_effects.size(); g++) {
			if (m.random_effects[g].term_names.size() > 1) {
				show_corr = true;
				break;
			}
		}

		// cov_chol is the packed lower-triangular raw Cholesky factor L
		// (NOT log-diagonal), stored 1-indexed, row by row. Element (r, c)
		// with 0-indexed r ≥ c lives at cov_chol[r*(r+1)/2 + c + 1].
		// Covariance Σ(s, t) = Σ_{k ≤ min(s,t)} L(s,k) · L(t,k).
		// Same helpers as the scripting summarize() in data_table.cpp.
		auto chol_at = [](const Array<double> &cc, intptr_t r0, intptr_t c0) -> double {
			intptr_t idx = r0 * (r0 + 1) / 2 + c0 + 1;
			return (idx <= cc.size()) ? cc[idx] : 0.0;
		};
		auto cov_st = [&](const Array<double> &cc, intptr_t s0, intptr_t t0) -> double {
			if (s0 > t0) std::swap(s0, t0);
			double sum = 0.0;
			for (intptr_t k = 0; k <= s0; k++) {
				sum += chol_at(cc, s0, k) * chol_at(cc, t0, k);
			}
			return sum;
		};

		// Compute first-column width.  Two kinds of labels share this slot:
		// the (un-indented) group name and the (indented by 2) term names.
		// We need w ≥ max(min, longest_group + pad, 2 + longest_term + pad)
		// so values stay aligned regardless of which row holds the longest
		// label.  "Residual" (8 chars) trivially fits any min ≥ 10.
		constexpr int re_min_w = 20;
		constexpr int re_pad = 2;
		constexpr int re_indent = 2;
		int re_w = re_min_w;
		for (intptr_t g = 1; g <= m.random_effects.size(); g++)
		{
			auto &re = m.random_effects[g];
			int gw = (int)re.group_name.size() + re_pad;
			if (gw > re_w) re_w = gw;
			for (intptr_t t = 1; t <= re.term_names.size(); t++) {
				int tw = re_indent + (int)re.term_names[t].size() + re_pad;
				if (tw > re_w) re_w = tw;
			}
		}
		int re_term_w = re_w - re_indent;
		std::string re_grp_fmt  = "%-" + std::to_string(re_w) + "s";
		std::string re_term_fmt = "%-" + std::to_string(re_term_w) + "s";

		text += QStringLiteral("Random effects:\n");
		if (show_corr) {
			text += QString::asprintf((re_grp_fmt + " %12s %12s %8s   %s\n").c_str(),
			                           "Group", "Variance", "Std.Dev.", "Levels", "Corr");
		} else {
			text += QString::asprintf((re_grp_fmt + " %12s %12s %8s\n").c_str(),
			                           "Group", "Variance", "Std.Dev.", "Levels");
		}

		std::string re_grp_row  = re_grp_fmt + " %12.4f %12.4f %8ld\n";
		std::string re_term_row = "  " + re_term_fmt + " %12.4f %12.4f %8s";

		for (intptr_t g = 1; g <= m.random_effects.size(); g++)
		{
			auto &re = m.random_effects[g];
			const char *gname = re.group_name.data();

			for (intptr_t t = 1; t <= re.term_names.size(); t++)
			{
				double var = (t <= re.variance.size()) ? re.variance[t] : 0.0;
				double sd = std::sqrt(std::max(var, 0.0));

				if (t == 1) {
					// First row: group name, level count, no corr.
					text += QString::asprintf(re_grp_row.c_str(),
					                           gname, var, sd, (long)re.nlevels);
				} else {
					// Slope row: indented term name, blank Levels slot,
					// then one corr per previous term in the same group.
					const char *tname = re.term_names[t].data();
					text += QString::asprintf(re_term_row.c_str(),
					                           tname, var, sd, "");
					for (intptr_t s = 1; s < t; s++) {
						double var_s = (s <= re.variance.size()) ? re.variance[s] : 0.0;
						double denom = std::sqrt(std::max(var_s, 1e-30)
						                       * std::max(var,   1e-30));
						double corr = cov_st(re.cov_chol, s - 1, t - 1) / denom;
						text += QString::asprintf(" %+7.4f", corr);
					}
					text += QStringLiteral("\n");
				}
			}
		}

		if (m.is_gaussian()) {
			std::string re_res_fmt = re_grp_fmt + " %12.4f %12.4f\n";
			text += QString::asprintf(re_res_fmt.c_str(),
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

				// First-column width: fit "Level" header plus longest level
				// label.  Level labels are e.g. subject IDs or language names
				// that can easily run past the legacy 20-char field.
				int lvl_w = 20;
				for (intptr_t j = 1; j <= re.level_names.size(); j++) {
					int len = (int)re.level_names[j].size() + 2;
					if (len > lvl_w) lvl_w = len;
				}
				std::string lvl_fmt = "%-" + std::to_string(lvl_w) + "s";

				// Header: Level, then each term name
				text += QString::asprintf(lvl_fmt.c_str(), "Level");
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

					text += QString::asprintf(lvl_fmt.c_str(), level_label.toUtf8().constData());

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
		const char *opt_suffix = "";
		if (m.optimizer == "newton")     opt_suffix = " (Newton)";
		else if (m.optimizer == "lbfgs") opt_suffix = " (L-BFGS)";

		if (m.converged)
			text += QString::asprintf("Converged in %d iterations%s\n", m.niter, opt_suffix);
		else
			text += QString::asprintf("WARNING: did not converge after %d iterations%s\n", m.niter, opt_suffix);
	}

	// ── Diagnostic warnings ──────────────────────────────────────
	// Identifiability warning (fit_warning): set by the fitting engine
	// when the outer Hessian is ill-conditioned, e.g. for a random slope
	// aliased with its intercept.  Independent of the convergence flag.
	if (!m.well_identified && !m.fit_warning.empty())
	{
		text += QStringLiteral("\nNote: ");
		text += QString::fromUtf8(m.fit_warning.data(), (int)m.fit_warning.size());
		text += QStringLiteral("\n");
	}

	// Prior-scale warning (prior_warning): set by the Bayesian fit when
	// the residual scale is anomalously large relative to sd(y), suggesting
	// the fixed-effects prior is too tight for the response scale.  See
	// check_prior_scale_mismatch() in fitting.cpp.
	if (!m.prior_warning.empty())
	{
		text += QStringLiteral("\nWarning (prior scale): ");
		text += QString::fromUtf8(m.prior_warning.data(), (int)m.prior_warning.size());
		text += QStringLiteral("\n");
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
		spec.variance_components.param2 = m_prior_variance_alpha->value();
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
		spec.residual.param2 = m_prior_residual_alpha->value();
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
	m_prior_variance_alpha->setValue(0.05);
	m_prior_residual_auto->setChecked(true);
	m_prior_residual_type->setCurrentIndex(0); // PC
	m_prior_residual_scale->setValue(1.0);
	m_prior_residual_alpha->setValue(0.05);
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
		bool is_pc = (m_prior_variance_type->currentData().toString() == QStringLiteral("pc"));
		if (is_pc) {
			var_str = QStringLiteral("Variance: PC(%1, %2)")
				.arg(m_prior_variance_scale->value(), 0, 'g', 4)
				.arg(m_prior_variance_alpha->value(), 0, 'g', 3);
		} else {
			var_str = QStringLiteral("Variance: %1(%2)")
				.arg(m_prior_variance_type->currentText())
				.arg(m_prior_variance_scale->value(), 0, 'g', 4);
		}
	}

	QString family_data = m_family_combo->currentData().toString();
	bool is_gaussian = (family_data == QStringLiteral("gaussian") || family_data == QStringLiteral("student"));

	QString summary = fixed_str + QStringLiteral("  |  ") + var_str;
	if (is_gaussian) {
		if (m_prior_residual_auto->isChecked()) {
			summary += QStringLiteral("  |  Residual: auto");
		} else {
			bool is_pc = (m_prior_residual_type->currentData().toString() == QStringLiteral("pc"));
			if (is_pc) {
				summary += QStringLiteral("  |  Residual: PC(%1, %2)")
					.arg(m_prior_residual_scale->value(), 0, 'g', 4)
					.arg(m_prior_residual_alpha->value(), 0, 'g', 3);
			} else {
				summary += QStringLiteral("  |  Residual: %1(%2)")
					.arg(m_prior_residual_type->currentText())
					.arg(m_prior_residual_scale->value(), 0, 'g', 4);
			}
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

// Enable the ML/REML row only when the model is a Gaussian linear mixed
// model: family == "gaussian" AND the formula contains a random-effects
// term. For all other cases REML doesn't apply and the engine would
// coerce to ML anyway. The row is always *visible* so the user can see
// the option exists; it's *disabled* when not applicable, with a tooltip
// explaining why, so the absence isn't mistaken for "feature not
// implemented".
//
// The detection of "(|)" is heuristic — a substring search for the "(|"
// pattern won't catch malformed formulas, but Formula::parse would catch
// those at fit time. The intent here is just to grey out the option in
// configurations where it can't possibly be honoured.
void AnalysisView::updateMethodVisibility()
{
	if (!m_method_combo || !m_method_label) return;
	QString family_data = m_family_combo->currentData().toString();
	bool is_gaussian = (family_data == QStringLiteral("gaussian"));
	bool has_re = m_formula_edit->text().contains(QStringLiteral("(")) &&
	              m_formula_edit->text().contains(QStringLiteral("|"));
	bool applicable = is_gaussian && has_re;
	m_method_combo->setEnabled(applicable);
	m_method_label->setEnabled(applicable);
	if (applicable) {
		m_method_combo->setToolTip(QString());
		m_method_label->setToolTip(QString());
	} else {
		QString reason = !is_gaussian
			? tr("REML applies to Gaussian (continuous) outcomes only.")
			: tr("REML applies to mixed-effects models — add a random-effects term, e.g. (1 | speaker), to enable.");
		m_method_combo->setToolTip(reason);
		m_method_label->setToolTip(reason);
		// Snap the disabled combo back to ML so the greyed-out row
		// always reads "Maximum likelihood", consistent with what the
		// engine actually uses for non-Gaussian families and for
		// fixed-effects-only models. Leaving a previously-selected
		// REML visible in a greyed-out row would falsely suggest the
		// fit will use REML; in fact the engine would coerce to ML.
		m_method_combo->setCurrentIndex(0);
	}
}

// Show the α spinbox (and its label) only when the prior type is "PC".
// Half-Cauchy and Half-Normal are single-parameter priors, so α is hidden
// to avoid suggesting it has any effect on those families.
void AnalysisView::updatePriorPcAlphaVisibility()
{
	bool var_is_pc = (m_prior_variance_type->currentData().toString() == QStringLiteral("pc"));
	m_prior_variance_alpha_label->setVisible(var_is_pc);
	m_prior_variance_alpha->setVisible(var_is_pc);

	bool res_is_pc = (m_prior_residual_type->currentData().toString() == QStringLiteral("pc"));
	m_prior_residual_alpha_label->setVisible(res_is_pc);
	m_prior_residual_alpha->setVisible(res_is_pc);
}


// =====================================================================
// Add to data (Layer 4)
// =====================================================================

void AnalysisView::updateAddToDataButton()
{
	bool can_add = false;
	if (m_current_model >= 0 && m_current_model < m_analysis->model_count()
	    && m_analysis->has_source())
	{
		auto &m = m_analysis->model(m_current_model);
		can_add = m.has_source_rows() && !m.fitted.empty();
	}
	m_add_to_data_button->setEnabled(can_add);
}

void AnalysisView::onAddToData()
{
	if (m_current_model < 0 || m_current_model >= m_analysis->model_count())
		return;
	if (!m_analysis->has_source())
		return;

	auto &m = m_analysis->model(m_current_model);
	if (!m.has_source_rows()) {
		QMessageBox::warning(this, tr("Add to data"),
			tr("This model does not carry source-row indices (it may have been "
			   "loaded from a file saved by an older version of Phonometrica). "
			   "Refit the model to enable this action."));
		return;
	}

	// Probe scaled-residual availability cheaply by attempting to compute it
	// now — the result is cached on m_scaled_residuals via ensureScaledResiduals
	// so the subsequent call on accept is free.
	const stats::ScaledResidualResult *pre_sr = ensureScaledResiduals(m);
	bool scaled_available = (pre_sr != nullptr) && !pre_sr->residuals.empty();

	// Gather existing source headers for collision checking.
	QStringList existing;
	auto names = m_analysis->column_names();
	for (intptr_t i = 1; i <= names.size(); i++)
		existing << QString::fromUtf8(names[i].data(), (int)names[i].size());

	QString model_label = modelDisplayLabel(m_current_model);
	QString source_name = m_analysis->has_source()
		? QString::fromUtf8(m_analysis->data()->label().data(),
		                     (int)m_analysis->data()->label().size())
		: tr("source");

	AddModelValuesDialog dlg(model_label, source_name, existing,
	                          scaled_available, this);
	if (dlg.exec() != QDialog::Accepted)
		return;

	// Build the request. For each selected value type, fetch an aligned vector
	// of length source->row_count() (NaN for rows excluded from fitting).
	intptr_t nr = m_analysis->data()->row_count();
	Analysis::AppendColumnsRequest req;

	auto q_to_string = [](const QString &s) -> String {
		QByteArray utf8 = s.toUtf8();
		return String(utf8.constData(), utf8.size());
	};

	if (dlg.wantsFitted())
	{
		Analysis::AppendColumnsRequest::Column c;
		c.header = q_to_string(dlg.fittedColumnName());
		c.values = m.fitted_aligned(nr);
		if (c.values.empty()) {
			QMessageBox::warning(this, tr("Add to data"),
				tr("Could not align fitted values to source rows."));
			return;
		}
		req.columns.push_back(std::move(c));
	}
	if (dlg.wantsResiduals())
	{
		Analysis::AppendColumnsRequest::Column c;
		c.header = q_to_string(dlg.residualsColumnName());
		c.values = m.residuals_aligned(nr);
		if (c.values.empty()) {
			QMessageBox::warning(this, tr("Add to data"),
				tr("Could not align residuals to source rows."));
			return;
		}
		req.columns.push_back(std::move(c));
	}
	if (dlg.wantsScaledResiduals())
	{
		// Scaled residuals were cached above via ensureScaledResiduals.
		auto *sr = ensureScaledResiduals(m);
		if (!sr || sr->residuals.empty()) {
			QMessageBox::warning(this, tr("Add to data"),
				tr("Scaled residuals are not available for this model."));
			return;
		}
		Analysis::AppendColumnsRequest::Column c;
		c.header = q_to_string(dlg.scaledResidualsColumnName());
		c.values = m.align_to_source(sr->residuals, nr);
		if (c.values.empty()) {
			QMessageBox::warning(this, tr("Add to data"),
				tr("Could not align scaled residuals to source rows."));
			return;
		}
		req.columns.push_back(std::move(c));
	}

	if (req.columns.empty())
		return; // dialog OK would have been disabled, but be defensive

	try
	{
		m_analysis->append_columns_to_source(req);
	}
	catch (std::exception &e)
	{
		QMessageBox::warning(this, tr("Add to data"),
			tr("Could not add columns:\n%1").arg(QString::fromUtf8(e.what())));
		return;
	}

	// Status-bar feedback rather than a modal — the operation is silent by
	// nature (only a flag flip on the source, not a visible change here). The
	// source view, if open in another tab, will not refresh until reopened;
	// that's a known v1 limitation worth surfacing.
	QString status_msg = tr("Added %n column(s) to %1. Close and reopen the source "
	                         "to see them.", nullptr, (int)req.columns.size())
	                      .arg(source_name);
	if (auto *mw = qobject_cast<QMainWindow*>(window())) {
		if (auto *bar = mw->statusBar()) {
			bar->showMessage(status_msg, 6000);
		}
	}
}

void AnalysisView::onModelListContextMenu(const QPoint &pos)
{
	auto *item = m_model_list->itemAt(pos);
	if (!item) return;

	int row = m_model_list->row(item);
	// Make sure the right-clicked row is the current selection, so slot
	// handlers operate on the intended model.
	if (m_model_list->currentRow() != row)
		m_model_list->setCurrentRow(row);

	QMenu menu(this);
	QAction *refit_act = menu.addAction(tr("Refit"));
	menu.addSeparator();
	QAction *rename_act = menu.addAction(tr("Rename…"));
	QAction *add_act = menu.addAction(tr("Add to data…"));
	add_act->setEnabled(m_add_to_data_button->isEnabled());
	menu.addSeparator();
	QAction *delete_act = menu.addAction(tr("Delete"));

	QAction *chosen = menu.exec(m_model_list->viewport()->mapToGlobal(pos));
	if (chosen == refit_act) {
		onRefitModel(row);
	}
	else if (chosen == rename_act) {
		onRenameModel(item);
	}
	else if (chosen == add_act) {
		onAddToData();
	}
	else if (chosen == delete_act) {
		onDeleteModel();
	}
}

} // namespace phonometrica
