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

#include <functional>
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
#include <QAction>
#include <phon/gui/view.hpp>
#include <phon/gui/plot_widget.hpp>
#include <phon/gui/checkable_combo_box.hpp>
#include <phon/application/analysis.hpp>
#include <phon/analysis/formula.hpp>
#include <phon/analysis/scaled_residuals.hpp>
#include <phon/analysis/posterior_predictive.hpp>
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

signals:

	// Emitted when the user clicks an observation in the EDA plot. MainWindow
	// connects this in openAnalysis() to find or open the source view, then
	// scroll/select the corresponding row.
	//
	// `source` is the analysis source (Dataset or Concordance, both
	// DataTable subclasses); `source_row` is a 0-based source-model row
	// index. Never emitted with INVALID_ROW.
	void requestOpenSourceRow(Handle<DataTable> source, intptr_t source_row);

private slots:

	void onFit();
	void onModelSelected(int row);
	void onDeleteModel();
	void onRefitModel(int row);
	void onRenameModel(QListWidgetItem *item);
	void onCompareModels();
	void onAddToData();
	void onModelListContextMenu(const QPoint &pos);
	void onColumnDoubleClicked(QListWidgetItem *item);
	void onColumnContextMenu(const QPoint &pos);
	void onPlotTypeChanged(int index);
	void onCopySummary();
	void onSaveSummaryText();
	void onSaveSummaryLatex();
	void onEdaChanged();
	void onEdaPlotTypeChanged();
	void onCustomizeEda();
	void onExportEdaPNG();
	void onExportEdaPDF();
	void onExportEdaSVG();
	void onDetachEdaPlot();
	void onReattachEdaPlot();
	void onExportDiagPNG();
	void onExportDiagPDF();
	void onExportDiagSVG();
	void onDetachDiagPlot();
	void onReattachDiagPlot();
	void onExportEffectsPNG();
	void onExportEffectsPDF();
	void onExportEffectsSVG();
	void onDetachEffectsPlot();
	void onReattachEffectsPlot();
	void onPostHocChanged();
	void onExportPostHoc();
	void onExportPostHocLatex();
	void onEffectsFocalChanged();
	void onEffectsRandomChanged();

private:

	void setupUi();
	void populateColumns();
	void populateModelList();
	void displayModel(int index);
	QString formatSummary(const stats::Model &m) const;
	QString formatLatex(const stats::Model &m) const;

	// Tab title without the " *" modified marker. Used both by label() (for
	// the tab header) and by save() (for the default filename suggestion in
	// the Save-as dialog), so the two cannot drift apart. For an unsaved
	// analysis with a source this is "Analysis — <source-label>"; once the
	// analysis has a path on disk it is the file's base name.
	QString baseLabel() const;

	// Returns the user label if set, otherwise "Model N" (1-based).
	QString modelDisplayLabel(int index) const;
	// Returns the full text for a model list item: "label: formula" or "label (B): formula".
	QString modelListText(int index) const;
	void updateDiagnosticPlot();
	void updateFitEnabled();
	bool eventFilter(QObject *obj, QEvent *event) override;

	// Formula building helpers
	void setResponse(const QString &name);
	void addPredictor(const QString &name);
	void addSmoothTerm(const QString &name, int k = 10, const QString &by = QString());
	void addRandomIntercept(const QString &name);
	void addNestedGroupingFactor(const QString &inner, const QString &outer);
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
	void plotPosteriorPredictiveCheck(const stats::Model &m);
	const stats::ScaledResidualResult *ensureScaledResiduals(const stats::Model &m);
	const stats::PosteriorPredictiveResult *ensurePosteriorPredictive(const stats::Model &m);
	void updateTestResults(const stats::ScaledResidualResult &sr);
	void clearTestResults();
	// Inline replacement for the old modal "could not compute residuals"
	// popup. Confined to plotScaledResiduals* views — clears the plot and
	// shows m_scaled_residuals_error in the test-results text area.
	void showResidualUnavailable();

	// Effects tab plumbing.
	void populateEffectsFocalCombo();
	void updateEffectsPlot();
	void updateEdaPlot();
	void updateEdaSummary();
	bool isColumnNumeric(const String &col_name) const;

	// EDA virtual-column support. "Virtual" columns are per-observation model
	// quantities (fitted values, residuals, scaled residuals) that appear in
	// the X and Y combos of the EDA tab alongside real data columns. They are
	// resolved on the fly against the current model; they do not mutate the
	// source DataTable.
	static bool isVirtualEdaColumn(const QString &name);
	// Build an Array<String> of length source->row_count() holding the virtual
	// column's values formatted for the cell-access path ("nan" for source
	// rows that were excluded from fitting). Returns an empty Array if no
	// current model, if the model has no source_rows, or if the name is not a
	// recognized virtual column. Non-const because scaled residuals are
	// computed lazily via ensureScaledResiduals().
	Array<String> buildVirtualEdaCells(const QString &name);
	// Add the virtual-column entries (with a disabled separator) to the X
	// and Y EDA combos, preserving the current text selection if it remains
	// valid. No-op for the other EDA combos (group/pool/style/label).
	void refreshEdaVirtualColumns();

	// Enable/disable the "Add to data" button based on the current model and
	// whether the source is available. Called from onModelSelected,
	// onDeleteModel, and after fitting a model.
	void updateAddToDataButton();

	void updatePostHoc();
	void populatePostHocFactors();
	stats::PriorSpec buildPriorSpec() const;
	void resetPriorPanel();
	void updatePriorDefaultsLabel();
	void updatePriorResidualVisibility();
	void updatePriorPcAlphaVisibility();

	// ── Detachable-plot helper ───────────────────────────────────────
	//
	// All three plot panels (EDA, Diagnostics, Effects) share one
	// detach/reattach mechanism. Each panel owns one DetachablePlot
	// describing its plot widget, the layout that holds it, and the
	// per-format export callbacks used by the floating window's Save…
	// menu. detachPlot() migrates the plot into a top-level QWindow
	// with its own toolbar; reattachPlot() returns it to the home
	// layout. The detach action is gated on plot->hasData() across
	// the three panels via updateDetachActionsEnabled().
	struct DetachablePlot
	{
		PlotWidget  *plot         = nullptr;   // widget being migrated
		QBoxLayout  *home_layout  = nullptr;   // layout it lives in
		int          home_index   = 0;         // re-insertion index
		int          home_stretch = 1;         // re-insertion stretch
		QString      window_title;             // floating-window title
		QAction     *detach_action = nullptr;  // home-toolbar trigger
		QWidget     *float_window  = nullptr;  // populated when detached
		QLabel      *placeholder   = nullptr;  // shown in home tab while detached
		QString      placeholder_text;         // localised placeholder message
		// Save callbacks for the floating window's Save… menu. Each
		// bound to the corresponding per-format slot (PNG / PDF / SVG)
		// of the parent panel so the float window's exports go through
		// exactly the same paths as the home toolbar.
		std::function<void()> save_png;
		std::function<void()> save_pdf;
		std::function<void()> save_svg;
	};

	void detachPlot(DetachablePlot &dp);
	void reattachPlot(DetachablePlot &dp);
	// Enable/disable each panel's detach action based on the current
	// plot state (hasData()). Called whenever a plot is populated or
	// cleared so the action greys out cleanly.
	void updateDetachActionsEnabled();

	Handle<Analysis> m_analysis;
	int m_current_model = -1;

	// Scaled residual cache (lazy, invalidated on model change).
	int m_scaled_residuals_model = -1;
	std::optional<stats::ScaledResidualResult> m_scaled_residuals;
	// When residual computation fails, this carries the explanation that
	// is shown inline in the scaled-residual plot views (no modal popup).
	QString m_scaled_residuals_error;

	// Posterior predictive cache (lazy, invalidated on model change).
	// Lives next to the scaled-residual cache because both are derived
	// quantities that the Diagnostics tab consumes — keeping them parallel
	// makes invalidation easy: m_current_model changes → both reset.
	int m_ppc_model = -1;
	std::optional<stats::PosteriorPredictiveResult> m_ppc;
	QString m_ppc_error;

	// Top bar
	QLineEdit *m_formula_edit = nullptr;
	QComboBox *m_family_combo = nullptr;
	QComboBox *m_estimation_combo = nullptr;
	QPushButton *m_fit_button = nullptr;
	QToolButton *m_options_button = nullptr;

	// Fitting options popup
	QSpinBox *m_max_iter_spin = nullptr;
	QComboBox *m_default_est_combo = nullptr;

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
	QLabel *m_prior_variance_alpha_label = nullptr;
	QDoubleSpinBox *m_prior_variance_alpha = nullptr;
	QCheckBox *m_prior_residual_auto = nullptr;
	QComboBox *m_prior_residual_type = nullptr;
	QDoubleSpinBox *m_prior_residual_scale = nullptr;
	QLabel *m_prior_residual_alpha_label = nullptr;
	QDoubleSpinBox *m_prior_residual_alpha = nullptr;
	QList<QWidget *> m_prior_residual_widgets; // all widgets in the residual grid row
	QPushButton *m_prior_reset_button = nullptr;

	// Left panel
	QListWidget *m_column_list = nullptr;
	QListWidget *m_model_list = nullptr;
	QPushButton *m_delete_button = nullptr;
	QPushButton *m_compare_button = nullptr;
	QPushButton *m_add_to_data_button = nullptr;

	// Right panel (tabbed)
	QTabWidget *m_right_tabs = nullptr;
	QPlainTextEdit *m_summary = nullptr;
	QCheckBox *m_blup_check = nullptr;

	// Diagnostics tab
	QComboBox *m_plot_type_combo = nullptr;
	QLabel *m_posterior_predictors_label = nullptr;
	CheckableComboBox *m_posterior_predictors_combo = nullptr;
	// Tracks the predictor list (non-intercept coef names) last shown in the
	// posterior combo, so we only repopulate (resetting selection to "all")
	// when the model's predictor set actually changes.
	QStringList m_posterior_last_predictors;
	PlotWidget *m_plot = nullptr;
	QGroupBox *m_test_results_group = nullptr;
	QTextEdit *m_test_results_text = nullptr;

	// Effects tab — model-implied effect of a focal predictor with CI ribbon
	// (numeric focal) or points + error bars (categorical focal). Phase 2
	// MVP: single focal, others held fixed at reference level / mean.
	// Optional By-factor combo (Phase 2.5) shows one curve per level of a
	// second categorical predictor, revealing how the focal effect differs
	// across by-levels. Refuses cleanly for by-factor smooths and re-smooths.
	// Conditional-prediction controls (Phase B) let the user pick a random-
	// effects group to condition on, with a checkable list of levels.
	QComboBox *m_effects_focal_combo = nullptr;
	QComboBox *m_effects_by_combo = nullptr;
	QComboBox *m_effects_re_combo = nullptr;
	CheckableComboBox *m_effects_re_levels = nullptr;
	QCheckBox *m_effects_show_ci_check = nullptr;
	QCheckBox *m_effects_show_legend_check = nullptr;
	PlotWidget *m_effects_plot = nullptr;
	QLabel *m_effects_message = nullptr;

	// EDA tab
	//
	// Chart-first dispatch: the user picks a plot type up front; "Auto" preserves
	// the original data-driven inference behavior bit-for-bit. Explicit choices
	// constrain which variable slots are required (validation at render time,
	// not at column-population time — see updateEdaPlot). Each new plot type
	// adds one enum entry plus its own row in updateEdaPlot's dispatch.
	enum class EdaPlotType
	{
		Auto = 0,
		Histogram,
		BarChart,
		BoxPlot,
		Scatter,
		FormantChart
	};
	QLabel *m_eda_plot_type_label = nullptr;
	QComboBox *m_eda_plot_type_combo = nullptr;
	EdaPlotType m_last_eda_plot_type = EdaPlotType::Auto;
	void applyEdaPlotTypeDefaults(EdaPlotType type);
	// Validation hint shown under the plot when the user picks an explicit
	// plot type and the selected variables don't fit. Cleared on successful
	// render.
	void setEdaHint(const QString &msg);

	// Per-plot customization captured from the toolbar's Customize dialog.
	// Empty strings / unset optionals / 0 mean "use computed defaults"; any
	// non-default value overrides the auto-built title, axis labels, axis
	// range, or facet panel-per-row count. The struct is reset whenever the
	// plot type changes (variable changes leave it alone — labels you wrote
	// for the data you're looking at usually still apply after, say, picking
	// a different Group column).
	struct EdaCustomization
	{
		QString title;
		QString x_label;
		QString y_label;
		std::optional<double> x_min;
		std::optional<double> x_max;
		std::optional<double> y_min;
		std::optional<double> y_max;
		int facet_ncols = 0;        // 0 = auto (default cap of 4)

		bool isEmpty() const
		{
			return title.isEmpty() && x_label.isEmpty() && y_label.isEmpty()
			    && !x_min.has_value() && !x_max.has_value()
			    && !y_min.has_value() && !y_max.has_value()
			    && facet_ncols == 0;
		}
		void clear()
		{
			title.clear(); x_label.clear(); y_label.clear();
			x_min.reset(); x_max.reset();
			y_min.reset(); y_max.reset();
			facet_ncols = 0;
		}
	};
	EdaCustomization m_eda_customization;

	QComboBox *m_eda_y_combo = nullptr;
	QComboBox *m_eda_x_combo = nullptr;
	// Slot labels stored as members so applyEdaPlotTypeDefaults can rename
	// them to "F2:" / "F1:" for Formant chart and back to "X:" / "Y:" for
	// every other type.
	QLabel *m_eda_x_slot_label = nullptr;
	QLabel *m_eda_y_slot_label = nullptr;
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
	QLabel *m_eda_facet_label = nullptr;
	QComboBox *m_eda_facet_combo = nullptr;
	QCheckBox *m_eda_mean_check = nullptr;
	QCheckBox *m_eda_ellipse_check = nullptr;
	QSpinBox *m_eda_ellipse_spin = nullptr;
	QCheckBox *m_eda_formant_check = nullptr;
	PlotWidget *m_eda_plot = nullptr;
	QLabel *m_eda_hint_label = nullptr;   // empty-state hint shown under the plot
	QTableWidget *m_eda_summary = nullptr;

	// Detach state for each plot panel. The DetachablePlot struct
	// captures the home-layout / floating-window / placeholder / save-
	// callback wiring that all three panels share; see the helper
	// declarations above (detachPlot / reattachPlot).
	DetachablePlot m_eda_detach;
	DetachablePlot m_diag_detach;
	DetachablePlot m_effects_detach;

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
