/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Visual analysis workspace. Allows users to build formulas, fit models, compare results,                    *
 *          and inspect diagnostic plots starting from a concordance or dataset.                                        *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_ANALYSIS_VIEW_HPP
#define PHONOMETRICA_ANALYSIS_VIEW_HPP

#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTabWidget>
#include <QSpinBox>
#include <QLabel>
#include <phon/gui/view.hpp>
#include <phon/gui/plot_widget.hpp>
#include <phon/application/analysis.hpp>

namespace phonometrica {

class AnalysisView final : public View
{
	Q_OBJECT

public:

	// Open a new analysis from a data source (no path yet).
	explicit AnalysisView(Handle<Analysis> analysis, QWidget *parent = nullptr);

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
	void onExportEdaPlot();

private:

	void setupUi();
	void populateColumns();
	void populateModelList();
	void displayModel(int index);
	QString formatSummary(const stats::Model &m) const;
	QString formatLatex(const stats::Model &m) const;
	void setResponse(const QString &name);
	void addPredictor(const QString &name);
	void addRandomIntercept(const QString &name);
	void updateDiagnosticPlot();
	void updateFitEnabled();

	void plotResidualsVsFitted(const stats::Model &m);
	void plotQQ(const stats::Model &m);
	void updateEdaPlot();
	bool isColumnNumeric(const String &col_name) const;

	Handle<Analysis> m_analysis;
	int m_current_model = -1;

	// Top bar
	QLineEdit *m_formula_edit = nullptr;
	QComboBox *m_family_combo = nullptr;
	QPushButton *m_fit_button = nullptr;

	// Left panel
	QListWidget *m_column_list = nullptr;
	QListWidget *m_model_list = nullptr;
	QPushButton *m_delete_button = nullptr;
	QPushButton *m_compare_button = nullptr;

	// Right panel (tabbed)
	QTabWidget *m_right_tabs = nullptr;
	QPlainTextEdit *m_summary = nullptr;

	// Diagnostics tab
	QComboBox *m_plot_type_combo = nullptr;
	PlotWidget *m_plot = nullptr;

	// EDA tab
	QComboBox *m_eda_y_combo = nullptr;
	QComboBox *m_eda_x_combo = nullptr;
	QLabel *m_bins_label = nullptr;
	QSpinBox *m_bins_spin = nullptr;
	QCheckBox *m_eda_regline_check = nullptr;
	PlotWidget *m_eda_plot = nullptr;
};

} // namespace phonometrica

#endif // PHONOMETRICA_ANALYSIS_VIEW_HPP
