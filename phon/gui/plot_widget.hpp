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
 * Purpose: Reusable plot widget supporting scatter plots, grouped scatter plots with confidence ellipses,             *
 *          box plots, histograms and bar charts. Follows the same rendering pattern as SpectrumView.                  *
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
		HorizontalAtHalf,  // dashed line at y = 0.5 (for scaled residuals)
		Diagonal
	};

	explicit PlotWidget(QWidget *parent = nullptr);

	/// Scatter plot. reverse_x / reverse_y invert the corresponding axis
	/// (useful for formant charts where higher values go left/down).
	void setData(std::vector<double> x, std::vector<double> y,
	             const QString &x_label, const QString &y_label,
	             const QString &title, RefLine ref = RefLine::None,
	             bool reverse_x = false, bool reverse_y = false,
	             std::vector<QString> point_labels = {});

	/// Grouped scatter plot. Each point belongs to a named group (groups[i]).
	/// Optionally shows per-group mean markers and confidence ellipses.
	/// chi2_scale is the chi-squared(2) quantile for the desired confidence level
	/// (e.g. 2.2946 for 68.27%, 5.991 for 95%). For 2 df: -2*ln(1-p).
	/// point_labels, when non-empty, renders text at each point position
	/// instead of filled circles. point_labels[i] is the text for point i.
	void setGroupedScatterData(std::vector<QString> groups,
	                           std::vector<double> x, std::vector<double> y,
	                           const QString &x_label, const QString &y_label,
	                           const QString &title,
	                           bool show_means, bool show_ellipses,
	                           double chi2_scale = 2.2946,
	                           bool reverse_x = false, bool reverse_y = false,
	                           std::vector<QString> point_labels = {});

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

	/// Set (or clear) a density curve to overlay on histograms.
	/// curve_x/curve_y define the polyline in data coordinates;
	/// the Y values are pre-scaled to the histogram count axis.
	void setDensityCurve(std::vector<double> curve_x, std::vector<double> curve_y);
	void clearDensityCurve();

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

	enum class Mode { Empty, Scatter, GroupedScatter, BoxPlot, Histogram, BarChart };

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

	/// Per-group data for grouped scatter plot.
	struct GroupData
	{
		QString label;
		std::vector<double> x;
		std::vector<double> y;
		std::vector<QString> symbols; // per-point text labels (empty vector = draw circles)
		double mean_x = 0;
		double mean_y = 0;
		// Confidence ellipse parameters (in data coordinates).
		// Semi-axes are pre-scaled by the chi-squared quantile.
		double ellipse_angle = 0; // rotation angle in radians
		double ellipse_a = 0;     // semi-axis along principal direction
		double ellipse_b = 0;     // semi-axis along minor direction
		bool ellipse_valid = false; // false when n < 3 or singular covariance
	};

	void renderPlot(QPainter &p, int w, int h);
	void renderScatter(QPainter &p, int left, int top, int pw, int ph,
	                    double xlo, double xhi, double ylo, double yhi);
	void renderGroupedScatter(QPainter &p, int left, int top, int pw, int ph);
	void renderBoxPlot(QPainter &p, int left, int top, int pw, int ph);
	void renderHistogram(QPainter &p, int left, int top, int pw, int ph);
	void renderBarChart(QPainter &p, int left, int top, int pw, int ph);
	void renderTitle(QPainter &p, int left, int pw, int top);
	void renderLegend(QPainter &p, int left, int top, int pw, int ph);
	void rebuildCache();

	static std::vector<BoxStats> computeBoxStats(const std::vector<QString> &groups,
	                                              const std::vector<double> &values);
	static std::vector<HistBin> computeBins(const std::vector<double> &values, int nbins = 0);
	static double quantile_sorted(const std::vector<double> &sorted, double p);
	static std::vector<GroupData> buildGroups(const std::vector<QString> &labels,
	                                          const std::vector<double> &x,
	                                          const std::vector<double> &y,
	                                          double chi2_scale,
	                                          const std::vector<QString> &point_labels = {});

	Mode m_mode = Mode::Empty;

	// Scatter data
	std::vector<double> m_x;
	std::vector<double> m_y;
	std::vector<QString> m_point_labels; // per-point text labels for plain scatter (empty = draw circles)
	RefLine m_ref_line = RefLine::None;
	bool m_reverse_x = false;
	bool m_reverse_y = false;

	// Regression line overlay (scatter only)
	bool m_show_regression = false;
	double m_reg_intercept = 0;
	double m_reg_slope = 0;
	double m_reg_r2 = 0;

	// Grouped scatter data
	std::vector<GroupData> m_group_data;
	bool m_show_means = false;
	bool m_show_ellipses = false;
	bool m_use_labels = false; // true when per-point text labels are active

	// Box plot data
	std::vector<BoxStats> m_boxes;

	// Histogram data
	std::vector<HistBin> m_bins;

	// Density curve overlay (histogram only)
	bool m_show_density = false;
	std::vector<double> m_density_x;
	std::vector<double> m_density_y;

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
