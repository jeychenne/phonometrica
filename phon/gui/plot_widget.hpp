/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Reusable plot widget supporting scatter plots, box plots, and histograms.                                  *
 *          Follows the same rendering pattern as SpectrumView.                                                        *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PLOT_WIDGET_HPP
#define PHONOMETRICA_PLOT_WIDGET_HPP

#include <vector>
#include <QWidget>
#include <QPixmap>

namespace phonometrica {

class PlotWidget : public QWidget
{
	Q_OBJECT

public:

	enum class RefLine
	{
		None,
		HorizontalAtZero,
		Diagonal
	};

	explicit PlotWidget(QWidget *parent = nullptr);

	/// Scatter plot.
	void setData(std::vector<double> x, std::vector<double> y,
	             const QString &x_label, const QString &y_label,
	             const QString &title, RefLine ref = RefLine::None);

	/// Box plot: groups[i] is the group label for values[i].
	void setBoxPlotData(std::vector<QString> groups, std::vector<double> values,
	                    const QString &x_label, const QString &y_label,
	                    const QString &title);

	/// Histogram: a single array of numeric values. nbins=0 means auto (Sturges' rule).
	void setHistogramData(std::vector<double> values,
	                      const QString &x_label, const QString &y_label,
	                      const QString &title, int nbins = 0);

	/// Bar chart: category labels and their counts.
	void setBarChartData(std::vector<QString> labels, std::vector<int> counts,
	                     const QString &x_label, const QString &y_label,
	                     const QString &title);

	/// Set (or clear) an OLS regression line to overlay on scatter plots.
	/// The line is y = intercept + slope * x, and r2 is displayed as an annotation.
	void setRegressionLine(double intercept, double slope, double r2);
	void clearRegressionLine();

	void clear();
	bool hasData() const;

	void savePNG(const QString &path);
	void savePDF(const QString &path);
	void saveSVG(const QString &path);

	QSize sizeHint() const override { return {500, 400}; }

protected:

	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:

	enum class Mode { Empty, Scatter, BoxPlot, Histogram, BarChart };

	struct BoxStats
	{
		QString label;
		double median = 0;
		double q1 = 0;
		double q3 = 0;
		double whisker_lo = 0;
		double whisker_hi = 0;
		std::vector<double> outliers;
	};

	struct HistBin
	{
		double lo = 0;
		double hi = 0;
		int count = 0;
	};

	void renderPlot(QPainter &p, int w, int h);
	void renderScatter(QPainter &p, int left, int top, int pw, int ph,
	                    double xlo, double xhi, double ylo, double yhi);
	void renderBoxPlot(QPainter &p, int left, int top, int pw, int ph);
	void renderHistogram(QPainter &p, int left, int top, int pw, int ph);
	void renderBarChart(QPainter &p, int left, int top, int pw, int ph);
	void renderTitle(QPainter &p, int left, int pw, int top);
	void rebuildCache();

	static std::vector<BoxStats> computeBoxStats(const std::vector<QString> &groups,
	                                              const std::vector<double> &values);
	static std::vector<HistBin> computeBins(const std::vector<double> &values, int nbins = 0);
	static double quantile_sorted(const std::vector<double> &sorted, double p);

	Mode m_mode = Mode::Empty;

	// Scatter data
	std::vector<double> m_x;
	std::vector<double> m_y;
	RefLine m_ref_line = RefLine::None;

	// Regression line overlay (scatter only)
	bool m_show_regression = false;
	double m_reg_intercept = 0;
	double m_reg_slope = 0;
	double m_reg_r2 = 0;

	// Box plot data
	std::vector<BoxStats> m_boxes;

	// Histogram data
	std::vector<HistBin> m_bins;

	// Bar chart data
	std::vector<QString> m_bar_labels;
	std::vector<int> m_bar_counts;

	// Shared
	QString m_x_label;
	QString m_y_label;
	QString m_title;

	QPixmap m_cache;
	bool m_cache_valid = false;
};

} // namespace phonometrica

#endif // PHONOMETRICA_PLOT_WIDGET_HPP
