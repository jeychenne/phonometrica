/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Reusable scatter plot widget with axis labels, reference lines, and PNG/PDF/SVG export.                    *
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
		HorizontalAtZero,   // y = 0 (for residuals vs fitted)
		Diagonal             // y = x (for Q-Q plot)
	};

	explicit PlotWidget(QWidget *parent = nullptr);

	/// Set the data and labels. Triggers a repaint.
	void setData(std::vector<double> x, std::vector<double> y,
	             const QString &x_label, const QString &y_label,
	             const QString &title, RefLine ref = RefLine::None);

	/// Clear all data.
	void clear();

	bool hasData() const { return !m_x.empty(); }

	/// Export to file.
	void savePNG(const QString &path);
	void savePDF(const QString &path);
	void saveSVG(const QString &path);

	QSize sizeHint() const override { return {500, 400}; }

protected:

	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:

	/// Render the full plot into the given painter at the specified logical dimensions.
	void renderPlot(QPainter &p, int w, int h);

	void rebuildCache();

	std::vector<double> m_x;
	std::vector<double> m_y;
	QString m_x_label;
	QString m_y_label;
	QString m_title;
	RefLine m_ref_line = RefLine::None;

	QPixmap m_cache;
	bool m_cache_valid = false;
};

} // namespace phonometrica

#endif // PHONOMETRICA_PLOT_WIDGET_HPP
