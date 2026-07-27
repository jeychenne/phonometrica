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
 * Created: 26/07/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Progress dialog shared by every query editor. A query alternates between reading files from disk and       *
 * working on what it read, and the two cost very different amounts of time on different corpora, so they get one      *
 * progress bar each instead of being averaged into a single meaningless number.                                       *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_QUERY_PROGRESS_DIALOG_HPP
#define PHONOMETRICA_QUERY_PROGRESS_DIALOG_HPP

#include <QDialog>
#include <QElapsedTimer>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <phon/application/conc/query.hpp>

namespace phonometrica {

// Tracks a query's progress on two bars: file loading on top, processing below. The dialog
// connects itself to the query on construction and disconnects on destruction, so the usual
// use is to give it the same scope as the run:
//
//     QueryProgressDialog progress(*m_query, tr("Measuring formants..."), this);
//     m_concordance = m_query->execute();
//
// The query runs on the GUI thread and reports progress by calling back into this dialog
// directly, so the dialog pumps the event loop itself to stay repainted and to let the user
// press Cancel. Repaints are throttled: a query firing one event per match would otherwise
// spend all its time in the event loop.
class QueryProgressDialog final : public QDialog
{
	Q_OBJECT

public:

	// `measuring_label` names what the processing bar is doing during Query::Stage::Measuring
	// ("Measuring formants..."). A text query never reaches that stage, so it can be left empty.
	QueryProgressDialog(Query &query, const QString &measuring_label, QWidget *parent);

	~QueryProgressDialog() override;

private:

	void setupUi();

	void onProgress(Query::Stage stage, int current, int total);

	void onCancel();

	// True once `stage` has been seen for the first time, in which case the bar it belongs to is
	// relabelled and its range reset.
	bool enterStage(Query::Stage stage, int total);

	QProgressBar *m_loading_bar = nullptr;
	QProgressBar *m_processing_bar = nullptr;
	QLabel *m_loading_label = nullptr;
	QLabel *m_processing_label = nullptr;
	QPushButton *m_cancel_button = nullptr;

	Query &m_query;

	ScopedConnection m_connection;

	QString m_measuring_label;

	// The stage of the last event, so a stage change can be detected. Starts at a value the
	// query cannot report first, so the very first event always counts as a change.
	Query::Stage m_stage = Query::Stage::Measuring;
	bool m_started = false;

	// Time since the dialog was created (to delay showing it) and since the last repaint (to
	// throttle them).
	QElapsedTimer m_since_start;
	QElapsedTimer m_since_repaint;
};

} // namespace phonometrica

#endif // PHONOMETRICA_QUERY_PROGRESS_DIALOG_HPP
