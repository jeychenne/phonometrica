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
 * Purpose: Visual analysis workspace. Allows users to build formulas, fit models, compare results,                    *
 *          and inspect diagnostic plots starting from a concordance or dataset.                                        *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_ANALYSIS_VIEW_HPP
#define PHONOMETRICA_ANALYSIS_VIEW_HPP

#include <optional>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QToolButton>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QLabel>
#include <QTableWidget>
#include <QGroupBox>
#include <QTextEdit>
#include <QIcon>
#include <phon/gui/view.hpp>
#include <phon/gui/plot_widget.hpp>
#include <phon/application/analysis.hpp>
#include <phon/analysis/formula.hpp>
#include <phon/analysis/scaled_residuals.hpp>
#include <phon/analysis/emmeans.hpp>

namespace phonometrica {

class AnalysisView final : public View
{
	Q_OBJECT

public:

	// Open a new analysis from a data source (no path yet).
	explicit AnalysisView(Handle<Analysis> analysis, QWidget *parent = nullptr);

	// Switch the right-panel tab widget (0=Summary, 1=Post-hoc, 2=Diagnostics, 3=EDA).
	void setActiveTab(int index);

	QString label() const override;
	String path() const override;
	bool isModified() const override;
	bool save() override;
	void discardChanges() override;

private slots:

	void onFit();
	void onModelSelected(int row);
	void onDeleteModel();
	void onCompareModels();
	void onColumnDoubleClicked(QListWidgetItem *item);
	void onColumnContextMenu(const QPoint &pos);
	void onPlotTypeChanged(int index);
	void onExportPlot();
	void onCopySummary();
	void onSaveSummaryText();
	void onSaveSummaryLatex();
	void onEdaChanged();
	void onExportEdaPNG();
	void onExportEdaPDF();
	void onExportEdaSVG();
	void onDetachEdaPlot();
	void onReattachEdaPlot();
	void onPostHocChanged();
	void onExportPostHoc();
	void onExportPostHocLatex();

private:

	void setupUi();
	void populateColumns();
	void populateModelList();
	void displayModel(int index);
	QString formatSummary(const stats::Model &m) const;
	QString formatLatex(const stats::Model &m) const;
	void updateDiagnosticPlot();
	void updateFitEnabled();
	bool eventFilter(QObject *obj, QEvent *event) override;

	// Formula building helpers
	void setResponse(const QString &name);
	void addPredictor(const QString &name);
	void addSmoothTerm(const QString &name, int k = 10, const QString &by = QString());
	void addRandomIntercept(const QString &name);
	void addInteraction(const QString &name, const QString &other, bool withMainEffects);
	void addRandomSlope(const QString &variable, const QString &group, bool correlated);
	void removeFromFormula(const QString &name);
	void updateColumnMarkers();

	// Parse the current formula bar text. Returns nullopt on parse failure.
	std::optional<stats::Formula> tryParseFormula();

	// Set the formula bar text from a Formula struct.
	void applyFormula(const stats::Formula &formula);

	void plotResidualsVsFitted(const stats::Model &m);
	void plotQQ(const stats::Model &m);
	void plotScaledResidualsVsFitted(const stats::Model &m);
	void plotScaledResidualQQ(const stats::Model &m);
	void plotPosteriorDensities(const stats::Model &m);
	const stats::ScaledResidualResult *ensureScaledResiduals(const stats::Model &m);
	void updateTestResults(const stats::ScaledResidualResult &sr);
	void clearTestResults();
	void updateEdaPlot();
	void updateEdaSummary();
	bool isColumnNumeric(const String &col_name) const;
	void updatePostHoc();
	void populatePostHocFactors();
	stats::PriorSpec buildPriorSpec() const;
	void resetPriorPanel();
	void updatePriorDefaultsLabel();
	void updatePriorResidualVisibility();

	Handle<Analysis> m_analysis;
	int m_current_model = -1;

	// Scaled residual cache (lazy, invalidated on model change).
	int m_scaled_residuals_model = -1;
	std::optional<stats::ScaledResidualResult> m_scaled_residuals;

	// Top bar
	QLineEdit *m_formula_edit = nullptr;
	QComboBox *m_family_combo = nullptr;
	QComboBox *m_estimation_combo = nullptr;
	QPushButton *m_fit_button = nullptr;

	// Prior customization panel (visible only when Bayesian is selected)
	QToolButton *m_prior_toggle = nullptr;
	QWidget *m_prior_panel = nullptr;
	QLabel *m_prior_defaults_label = nullptr;
	QCheckBox *m_prior_fixed_auto = nullptr;
	QDoubleSpinBox *m_prior_fixed_mean = nullptr;
	QDoubleSpinBox *m_prior_fixed_sd = nullptr;
	QCheckBox *m_prior_variance_auto = nullptr;
	QComboBox *m_prior_variance_type = nullptr;
	QDoubleSpinBox *m_prior_variance_scale = nullptr;
	QCheckBox *m_prior_residual_auto = nullptr;
	QComboBox *m_prior_residual_type = nullptr;
	QDoubleSpinBox *m_prior_residual_scale = nullptr;
	QList<QWidget *> m_prior_residual_widgets; // all widgets in the residual grid row
	QPushButton *m_prior_reset_button = nullptr;

	// Left panel
	QListWidget *m_column_list = nullptr;
	QListWidget *m_model_list = nullptr;
	QPushButton *m_delete_button = nullptr;
	QPushButton *m_compare_button = nullptr;

	// Right panel (tabbed)
	QTabWidget *m_right_tabs = nullptr;
	QPlainTextEdit *m_summary = nullptr;
	QCheckBox *m_blup_check = nullptr;

	// Diagnostics tab
	QComboBox *m_plot_type_combo = nullptr;
	PlotWidget *m_plot = nullptr;
	QGroupBox *m_test_results_group = nullptr;
	QTextEdit *m_test_results_text = nullptr;

	// EDA tab
	QComboBox *m_eda_y_combo = nullptr;
	QComboBox *m_eda_x_combo = nullptr;
	QLabel *m_bins_label = nullptr;
	QSpinBox *m_bins_spin = nullptr;
	QCheckBox *m_eda_regline_check = nullptr;
	QCheckBox *m_eda_density_check = nullptr;
	QLabel *m_eda_bw_label = nullptr;
	QSlider *m_eda_bw_slider = nullptr;
	QDoubleSpinBox *m_eda_bw_spin = nullptr;
	QLabel *m_eda_group_label = nullptr;
	QComboBox *m_eda_group_combo = nullptr;
	QLabel *m_eda_pool_label = nullptr;
	QComboBox *m_eda_pool_combo = nullptr;
	QLabel *m_eda_style_label = nullptr;
	QComboBox *m_eda_style_combo = nullptr;
	QLabel *m_eda_label_label = nullptr;
	QComboBox *m_eda_label_combo = nullptr;
	QCheckBox *m_eda_mean_check = nullptr;
	QCheckBox *m_eda_ellipse_check = nullptr;
	QSpinBox *m_eda_ellipse_spin = nullptr;
	QCheckBox *m_eda_formant_check = nullptr;
	PlotWidget *m_eda_plot = nullptr;
	QTableWidget *m_eda_summary = nullptr;
	QVBoxLayout *m_eda_top_layout = nullptr;
	QWidget *m_eda_float_window = nullptr;
	QLabel *m_eda_placeholder = nullptr;

	// Column list check mark icon for variables used in the formula.
	QIcon m_check_icon;

	// Post-hoc tab
	QComboBox *m_posthoc_factor_combo = nullptr;
	QComboBox *m_posthoc_by_combo = nullptr;
	QComboBox *m_posthoc_trend_combo = nullptr;
	QComboBox *m_posthoc_adj_combo = nullptr;
	QDoubleSpinBox *m_posthoc_conf_spin = nullptr;
	QTableWidget *m_posthoc_emm_table = nullptr;
	QTableWidget *m_posthoc_contrast_table = nullptr;
};

} // namespace phonometrica

#endif // PHONOMETRICA_ANALYSIS_VIEW_HPP
