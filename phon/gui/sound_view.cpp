/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QFrame>
#include <algorithm>
#include <phon/gui/sound_view.hpp>
#include <phon/gui/waveform_widget.hpp>
#include <phon/gui/wave_bar.hpp>
#include <phon/gui/sound_zoom.hpp>
#include <phon/application/audio_player.hpp>
#include <phon/application/macros.hpp>

namespace phonometrica {

SoundView::SoundView(const Handle<Sound> &sound, QWidget *parent) :
	View(parent), m_sound(sound)
{
	m_sound->open();
	m_model = new TimeModel(m_sound->duration(), this);
	m_player = std::make_unique<AudioPlayer>(m_sound);

	// Playback tick timer: polls the audio player at ~30fps.
	m_playback_timer = new QTimer(this);
	m_playback_timer->setInterval(33); // ~30 FPS
	connect(m_playback_timer, &QTimer::timeout, this, &SoundView::onPlaybackTick);

	setupUi();

	// All channels visible by default, average hidden.
	for (int i = 1; i <= m_sound->nchannel(); i++)
		m_visible_channels.push_back(i);

	// Hide the average waveform initially.
	if (!m_waveforms.empty())
		m_waveforms[0]->setVisible(false);

	// Set initial viewport after all widgets are connected.
	// Show the first 10 seconds (or the whole file if shorter).
	double initial_end = std::min(10.0, m_sound->duration());
	m_model->setViewport(0, initial_end);

	updateStatusText();
}

SoundView::~SoundView()
{
	stopPlayback();
}

void SoundView::setupUi()
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	createToolBar();
	layout->addWidget(m_toolbar);

	// Waveforms
	createWaveforms(layout);

	// Hook for annotation layers (subclass override).
	addAnnotationLayers(layout);

	// SoundZoom: visual connector between waveforms and wavebar.
	m_zoom = new SoundZoom(this);
	layout->addWidget(m_zoom);

	// Wavebar at the bottom.
	m_wavebar = new WaveBar(m_model, m_sound, this);
	layout->addWidget(m_wavebar);

	// Connect wavebar pixel changes to the zoom widget.
	connect(m_wavebar, &WaveBar::viewportPixelsChanged, m_zoom, &SoundZoom::setSelection);

	// Set global magnitude on all waveforms.
	double mag = m_wavebar->globalMagnitude();
	for (auto *wf : m_waveforms)
		wf->setGlobalMagnitude(mag);

	// Status label.
	m_status_label = new QLabel(this);
	m_status_label->setContentsMargins(6, 2, 6, 2);
	layout->addWidget(m_status_label);

	// Connect model signals for UI updates.
	connect(m_model, &TimeModel::selectionChanged, this, &SoundView::onSelectionChanged);
	connect(m_model, &TimeModel::selectionCleared, this, &SoundView::onSelectionCleared);
	connect(m_model, &TimeModel::viewportChanged, this, &SoundView::onViewportChanged);
}

void SoundView::createToolBar()
{
	m_toolbar = new QToolBar(this);
	m_toolbar->setIconSize(QSize(24, 24));
	m_toolbar->setMovable(false);

	auto *play_action = m_toolbar->addAction(QIcon(":/icons/play.png"),
		tr("Play current window"));
	connect(play_action, &QAction::triggered, this, &SoundView::onPlayWindow);

	m_play_sel_action = m_toolbar->addAction(QIcon(":/icons/selection.png"),
		tr("Play selection"));
	m_play_sel_action->setEnabled(false);
	connect(m_play_sel_action, &QAction::triggered, this, &SoundView::onPlaySelection);

	auto *stop_action = m_toolbar->addAction(QIcon(":/icons/stop.png"),
		tr("Stop playing"));
	connect(stop_action, &QAction::triggered, this, &SoundView::onStop);

	m_toolbar->addSeparator();

	auto *back_action = m_toolbar->addAction(QIcon(":/icons/back.png"),
		tr("Shift window backward"));
	connect(back_action, &QAction::triggered, this, &SoundView::onMoveBackward);

	auto *fwd_action = m_toolbar->addAction(QIcon(":/icons/next.png"),
		tr("Shift window forward"));
	connect(fwd_action, &QAction::triggered, this, &SoundView::onMoveForward);

	auto *zoom_out_action = m_toolbar->addAction(QIcon(":/icons/zoom_minus.png"),
		tr("Zoom out"));
	connect(zoom_out_action, &QAction::triggered, this, &SoundView::onZoomOut);

	auto *zoom_in_action = m_toolbar->addAction(QIcon(":/icons/zoom_plus.png"),
		tr("Zoom in"));
	connect(zoom_in_action, &QAction::triggered, this, &SoundView::onZoomIn);

	m_zoom_sel_action = m_toolbar->addAction(QIcon(":/icons/collapse.png"),
		tr("Zoom to selection"));
	m_zoom_sel_action->setEnabled(false);
	connect(m_zoom_sel_action, &QAction::triggered, this, &SoundView::onZoomToSelection);

	auto *view_all_action = m_toolbar->addAction(QIcon(":/icons/expand.png"),
		tr("View whole file"));
	connect(view_all_action, &QAction::triggered, this, &SoundView::onViewAll);

	m_toolbar->addSeparator();

	auto *mouse_action = m_toolbar->addAction(QIcon(":/icons/mouse.png"),
		tr("Enable mouse tracking"));
	mouse_action->setCheckable(true);
	mouse_action->setChecked(false);
	connect(mouse_action, &QAction::toggled, this, &SoundView::onToggleMouseTracking);
}

void SoundView::createWaveforms(QLayout *layout)
{
	// Channel 0 = average of all channels.
	auto *avg = new WaveformWidget(m_model, m_sound, 0, this);
	m_waveforms.push_back(avg);
	layout->addWidget(avg);

	// One waveform per channel.
	for (int c = 1; c <= m_sound->nchannel(); c++)
	{
		// Add a thin separator between waveforms.
		if (c > 1 || m_sound->nchannel() > 1)
		{
			auto *line = new QFrame(this);
			line->setFrameShape(QFrame::HLine);
			line->setFrameShadow(QFrame::Sunken);
			line->setFixedHeight(1);
			layout->addWidget(line);
		}

		auto *wf = new WaveformWidget(m_model, m_sound, c, this);
		m_waveforms.push_back(wf);
		layout->addWidget(wf);
	}
}


// ─────────────────────────────────────────────────
//  View interface
// ─────────────────────────────────────────────────

QString SoundView::label() const
{
	auto lbl = m_sound->label();
	return QString::fromUtf8(lbl.data(), (int) lbl.size());
}

String SoundView::path() const
{
	return m_sound->path();
}


// ─────────────────────────────────────────────────
//  Status
// ─────────────────────────────────────────────────

void SoundView::updateStatusText()
{
	auto start = m_model->windowStart();
	auto end = m_model->windowEnd();
	auto dur = m_model->windowDuration();

	QString text = tr("Window: %1 – %2 s (duration: %3 s)")
		.arg(start, 0, 'f', 4)
		.arg(end, 0, 'f', 4)
		.arg(dur, 0, 'f', 4);

	if (m_model->hasSpanSelection())
	{
		auto selDur = m_model->selectionEnd() - m_model->selectionStart();
		text += tr("  |  Selection: %1 – %2 s (duration: %3 s)")
			.arg(m_model->selectionStart(), 0, 'f', 4)
			.arg(m_model->selectionEnd(), 0, 'f', 4)
			.arg(selDur, 0, 'f', 4);
	}
	else if (m_model->hasPointSelection())
	{
		text += tr("  |  Cursor: %1 s")
			.arg(m_model->selectionStart(), 0, 'f', 4);
	}

	m_status_label->setText(text);
}


// ─────────────────────────────────────────────────
//  Toolbar actions
// ─────────────────────────────────────────────────

void SoundView::onViewAll()
{
	m_model->viewAll();
}

void SoundView::onZoomIn()
{
	m_model->zoomIn();
}

void SoundView::onZoomOut()
{
	m_model->zoomOut();
}

void SoundView::onZoomToSelection()
{
	m_model->zoomToSelection();
}

void SoundView::onMoveForward()
{
	m_model->moveForward();
}

void SoundView::onMoveBackward()
{
	m_model->moveBackward();
}

void SoundView::onToggleMouseTracking(bool checked)
{
	for (auto *wf : m_waveforms)
		wf->setMouseTracking(checked);
}


// ─────────────────────────────────────────────────
//  Model signal handlers
// ─────────────────────────────────────────────────

void SoundView::onSelectionChanged(double, double)
{
	bool hasSpan = m_model->hasSpanSelection();
	m_zoom_sel_action->setEnabled(hasSpan);
	m_play_sel_action->setEnabled(hasSpan);
	updateStatusText();
}

void SoundView::onSelectionCleared()
{
	m_zoom_sel_action->setEnabled(false);
	m_play_sel_action->setEnabled(false);
	updateStatusText();
}

void SoundView::onViewportChanged(double, double)
{
	updateStatusText();
}


// ─────────────────────────────────────────────────
//  Playback
// ─────────────────────────────────────────────────

void SoundView::onPlayWindow()
{
	if (m_model->hasSpanSelection())
		startPlayback(m_model->selectionStart(), m_model->selectionEnd());
	else
		startPlayback(m_model->windowStart(), m_model->windowEnd());
}

void SoundView::onPlaySelection()
{
	if (m_model->hasSpanSelection())
		startPlayback(m_model->selectionStart(), m_model->selectionEnd());
	else if (m_model->hasPointSelection())
		startPlayback(m_model->selectionStart(), m_model->windowEnd());
}

void SoundView::onStop()
{
	stopPlayback();
}

void SoundView::startPlayback(double from, double to)
{
	// Stop any ongoing playback.
	if (m_player->running())
		stopPlayback();

	m_player->play(from, to);
	m_was_playing = true;
	m_playback_timer->start();
}

void SoundView::stopPlayback()
{
	m_playback_timer->stop();
	m_player->stop();
	m_model->clearPlayback();
	m_was_playing = false;
}

void SoundView::onPlaybackTick()
{
	if (!m_player->running())
	{
		// Playback has ended (naturally or due to error).
		m_playback_timer->stop();
		m_model->clearPlayback();
		m_was_playing = false;
		return;
	}

	double t = m_player->currentPlaybackTime();
	m_model->setPlaybackTime(t);
}

} // namespace phonometrica
