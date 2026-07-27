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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QApplication>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <phon/gui/conc/query_progress_dialog.hpp>

namespace phonometrica {

// Don't flash a dialog at the user for a query that finishes instantly.
static constexpr qint64 SHOW_DELAY_MS = 400;

// Repaint at most this often. 25 fps looks continuous and leaves the query the rest of its time.
static constexpr qint64 REPAINT_INTERVAL_MS = 40;

QueryProgressDialog::QueryProgressDialog(Query &query, const QString &measuring_label, QWidget *parent) :
		QDialog(parent), m_query(query), m_measuring_label(measuring_label)
{
	setupUi();
	setWindowModality(Qt::WindowModal);

	m_since_start.start();
	m_since_repaint.start();

	m_connection = m_query.query_progress.connect(
			[this](Query::Stage stage, int current, int total) { onProgress(stage, current, total); });
}

QueryProgressDialog::~QueryProgressDialog()
{
	// m_connection disconnects here, before the bars this dialog owns are destroyed.
}

void QueryProgressDialog::setupUi()
{
	setWindowTitle(tr("Running query"));

	m_loading_label = new QLabel(tr("Loading files..."), this);
	m_loading_bar = new QProgressBar(this);
	m_loading_bar->setRange(0, 100);
	m_loading_bar->setValue(0);

	m_processing_label = new QLabel(tr("Searching..."), this);
	m_processing_bar = new QProgressBar(this);
	m_processing_bar->setRange(0, 100);
	m_processing_bar->setValue(0);

	m_cancel_button = new QPushButton(tr("Cancel"), this);
	auto *buttons = new QDialogButtonBox(this);
	buttons->addButton(m_cancel_button, QDialogButtonBox::RejectRole);
	connect(m_cancel_button, &QPushButton::clicked, this, &QueryProgressDialog::onCancel);

	auto *layout = new QVBoxLayout;
	layout->addWidget(m_loading_label);
	layout->addWidget(m_loading_bar);
	layout->addSpacing(10);
	layout->addWidget(m_processing_label);
	layout->addWidget(m_processing_bar);
	layout->addSpacing(5);
	layout->addWidget(buttons);
	setLayout(layout);

	setMinimumWidth(380);
}

bool QueryProgressDialog::enterStage(Query::Stage stage, int total)
{
	if (m_started && stage == m_stage) {
		return false;
	}
	m_stage = stage;
	m_started = true;

	// A stage with nothing to do (no sound files to load, no matches to measure) would leave an
	// empty bar sitting at zero, which reads as "stuck". Show it full instead.
	int maximum = (total > 0) ? total : 1;

	switch (stage)
	{
		case Query::Stage::LoadingAnnotations:
			m_loading_label->setText(tr("Loading annotations..."));
			m_loading_bar->setRange(0, maximum);
			break;
		case Query::Stage::Searching:
			m_processing_label->setText(tr("Searching..."));
			m_processing_bar->setRange(0, maximum);
			break;
		case Query::Stage::LoadingSounds:
			m_loading_label->setText(tr("Loading sound files..."));
			m_loading_bar->setRange(0, maximum);
			break;
		case Query::Stage::Measuring:
			m_processing_label->setText(m_measuring_label.isEmpty() ? tr("Measuring...") : m_measuring_label);
			m_processing_bar->setRange(0, maximum);
			break;
	}

	if (total == 0)
	{
		// Fill the bar for the stage we just entered, since no progress event will follow.
		auto *bar = (stage == Query::Stage::LoadingAnnotations || stage == Query::Stage::LoadingSounds)
		            ? m_loading_bar : m_processing_bar;
		bar->setValue(1);
	}

	return true;
}

void QueryProgressDialog::onProgress(Query::Stage stage, int current, int total)
{
	bool changed_stage = enterStage(stage, total);

	if (total > 0)
	{
		auto *bar = (stage == Query::Stage::LoadingAnnotations || stage == Query::Stage::LoadingSounds)
		            ? m_loading_bar : m_processing_bar;
		bar->setValue(current);
	}

	// Repainting on every event would dominate the run on a query with many matches. Always
	// repaint on a stage change, so a stage that finishes between two ticks is still seen.
	if (!changed_stage && m_since_repaint.elapsed() < REPAINT_INTERVAL_MS) {
		return;
	}
	m_since_repaint.restart();

	if (!isVisible() && m_since_start.elapsed() >= SHOW_DELAY_MS) {
		show();
	}

	// The query holds the GUI thread for its whole run, so nothing repaints and no click is
	// delivered unless the event loop is pumped from here. The editor that owns this dialog is
	// itself application-modal, so the events processed are the dialog's own.
	QApplication::processEvents();
}

void QueryProgressDialog::onCancel()
{
	m_query.request_cancel();
	m_cancel_button->setEnabled(false);
	m_cancel_button->setText(tr("Cancelling..."));
}

} // namespace phonometrica
