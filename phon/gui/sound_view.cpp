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

#include <algorithm>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMessageBox>
#include <phon/gui/sound_view.hpp>
#include <phon/gui/time_axis_widget.hpp>
#include <phon/gui/y_axis_widget.hpp>
#include <phon/gui/waveform_widget.hpp>
#include <phon/gui/spectrogram_widget.hpp>
#include <phon/gui/spectrogram_settings_dialog.hpp>
#include <phon/gui/intensity_widget.hpp>
#include <phon/gui/intensity_settings_dialog.hpp>
#include <phon/gui/wave_bar.hpp>
#include <phon/gui/sound_zoom.hpp>
#include <phon/application/audio_player.hpp>
#include <phon/application/settings.hpp>

static constexpr const char *PLAY_ICON = ":/icons/play.svg";
static constexpr const char *PAUSE_ICON = ":/icons/pause.svg";
static constexpr const char *PLAY_SEL_ICON = ":/icons/play-selection.svg";
static constexpr const char *PAUSE_SEL_ICON = ":/icons/pause-selection.svg";

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

	// Read visibility preferences from settings.
	try {
		m_show_waveform = Settings::get_boolean("sound_plots", "waveform");
	}
	catch (...) {
		m_show_waveform = true;
	}
	try {
		m_show_spectrogram = Settings::get_boolean("sound_plots", "spectrogram");
	}
	catch (...) {
		m_show_spectrogram = false;
	}
	try {
		m_show_intensity = Settings::get_boolean("sound_plots", "intensity");
	}
	catch (...) {
		m_show_intensity = false;
	}

	// All channels visible by default, average hidden.
	for (int i = 1; i <= m_sound->nchannel(); i++)
		m_visible_channels.push_back(i);

	// Hide the average waveform initially.
	if (!m_waveforms.empty())
		m_waveforms[0]->setVisible(false);

	// Apply initial plot visibility.
	updatePlotVisibility();

	// Sync the menu checkboxes with the actual state.
	if (m_show_wave_action)
		m_show_wave_action->setChecked(m_show_waveform);
	if (m_show_spectrogram_action)
		m_show_spectrogram_action->setChecked(m_show_spectrogram);
	if (m_show_intensity_action)
		m_show_intensity_action->setChecked(m_show_intensity);

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

	// Middle section: Y axis | plot column (time axis + waveforms + annotation layers).
	auto *mid_layout = new QHBoxLayout;
	mid_layout->setContentsMargins(0, 0, 0, 0);
	mid_layout->setSpacing(0);

	m_y_axis = new YAxisWidget(m_model, this);
	mid_layout->addWidget(m_y_axis);

	auto *plot_layout = new QVBoxLayout;
	plot_layout->setContentsMargins(0, 0, 0, 0);
	plot_layout->setSpacing(0);

	// Time axis: shows viewport boundaries and selection times.
	m_time_axis = new TimeAxisWidget(m_model, this);
	plot_layout->addWidget(m_time_axis);

	// Waveforms
	createWaveforms(plot_layout);

	// Spectrograms (one per channel, hidden by default).
	createSpectrograms(plot_layout);

	// Intensity tracks (one per channel, hidden by default).
	createIntensityTracks(plot_layout);

	// Register all waveforms with the Y axis (hidden ones are skipped during paint).
	for (auto *wf : m_waveforms)
		m_y_axis->addWaveform(wf);

	// Register all spectrograms with the Y axis.
	for (auto *sg : m_spectrograms)
		m_y_axis->addSpectrogram(sg);

	// Register all intensity tracks with the Y axis.
	for (auto *iw : m_intensities)
		m_y_axis->addIntensity(iw);

	// Hook for annotation layers (subclass override).
	addAnnotationLayers(plot_layout);

	mid_layout->addLayout(plot_layout, 1);
	layout->addLayout(mid_layout, 1);

	// SoundZoom: visual connector between waveforms and wavebar.
	m_zoom = new SoundZoom(this);
	m_zoom->setLeftOffset(m_y_axis->minimumWidth());
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

	setFocusPolicy(Qt::StrongFocus);
}

void SoundView::createToolBar()
{
	m_toolbar = new QToolBar(this);
	m_toolbar->setIconSize(QSize(24, 24));
	m_toolbar->setMovable(false);

	m_play_action = m_toolbar->addAction(QIcon(PLAY_ICON),
		tr("Play current window"));
	connect(m_play_action, &QAction::triggered, this, &SoundView::onPlayWindow);

	m_play_sel_action = m_toolbar->addAction(QIcon(PLAY_SEL_ICON),
		tr("Play selection"));
	m_play_sel_action->setEnabled(false);
	connect(m_play_sel_action, &QAction::triggered, this, &SoundView::onPlaySelection);

	auto *stop_action = m_toolbar->addAction(QIcon(":/icons/square.svg"),
		tr("Stop playing"));
	connect(stop_action, &QAction::triggered, this, &SoundView::onStop);

	m_toolbar->addSeparator();

	auto *back_action = m_toolbar->addAction(QIcon(":/icons/arrow-left.svg"),
		tr("Shift window backward"));
	connect(back_action, &QAction::triggered, this, &SoundView::onMoveBackward);

	auto *fwd_action = m_toolbar->addAction(QIcon(":/icons/arrow-right.svg"),
		tr("Shift window forward"));
	connect(fwd_action, &QAction::triggered, this, &SoundView::onMoveForward);

	auto *zoom_out_action = m_toolbar->addAction(QIcon(":/icons/zoom-out.svg"),
		tr("Zoom out"));
	connect(zoom_out_action, &QAction::triggered, this, &SoundView::onZoomOut);

	auto *zoom_in_action = m_toolbar->addAction(QIcon(":/icons/zoom-in.svg"),
		tr("Zoom in"));
	connect(zoom_in_action, &QAction::triggered, this, &SoundView::onZoomIn);

	m_zoom_sel_action = m_toolbar->addAction(QIcon(":/icons/minimize-2.svg"),
		tr("Zoom to selection"));
	m_zoom_sel_action->setEnabled(false);
	connect(m_zoom_sel_action, &QAction::triggered, this, &SoundView::onZoomToSelection);

	auto *view_all_action = m_toolbar->addAction(QIcon(":/icons/maximize-2.svg"),
		tr("View whole file"));
	connect(view_all_action, &QAction::triggered, this, &SoundView::onViewAll);

	m_toolbar->addSeparator();

	// ── Waveform menu button ──────────────────────────
	auto *wave_menu = new QMenu(this);

	// "Show waveform(s)" checkbox.
	auto show_wave_label = m_sound->is_mono() ? tr("Show waveform") : tr("Show waveforms");
	m_show_wave_action = wave_menu->addAction(show_wave_label);
	m_show_wave_action->setCheckable(true);
	m_show_wave_action->setChecked(true);
	connect(m_show_wave_action, &QAction::toggled, this, &SoundView::onToggleWaveform);

	// Channel selection (only for multichannel files).
	if (m_sound->nchannel() > 1)
	{
		wave_menu->addSeparator();

		auto *channel_group = new QActionGroup(this);
		channel_group->setExclusive(false);

		auto *avg_action = wave_menu->addAction(tr("Average channels"));
		avg_action->setCheckable(true);
		avg_action->setChecked(false);
		avg_action->setData(0);
		channel_group->addAction(avg_action);

		wave_menu->addSeparator();

		for (int c = 1; c <= m_sound->nchannel(); c++)
		{
			auto *ch_action = wave_menu->addAction(tr("Channel %1").arg(c));
			ch_action->setCheckable(true);
			ch_action->setChecked(true);
			ch_action->setData(c);
			channel_group->addAction(ch_action);
		}

		connect(channel_group, &QActionGroup::triggered, this, &SoundView::onChannelAction);
	}

	// Scaling options.
	wave_menu->addSeparator();
	auto *scaling_group = new QActionGroup(this);
	scaling_group->setExclusive(true);

	auto *local_action = wave_menu->addAction(tr("Local scaling"));
	local_action->setCheckable(true);
	local_action->setChecked(true);
	local_action->setData(static_cast<int>(Scaling::Local));
	scaling_group->addAction(local_action);

	auto *global_action = wave_menu->addAction(tr("Global scaling"));
	global_action->setCheckable(true);
	global_action->setData(static_cast<int>(Scaling::Global));
	scaling_group->addAction(global_action);

	auto *fixed_action = wave_menu->addAction(tr("Fixed (-1..+1)"));
	fixed_action->setCheckable(true);
	fixed_action->setData(static_cast<int>(Scaling::Fixed));
	scaling_group->addAction(fixed_action);

	connect(scaling_group, &QActionGroup::triggered, this, &SoundView::onScalingChanged);

	auto *wave_button = new QToolButton(this);
	wave_button->setPopupMode(QToolButton::MenuButtonPopup);
	connect(wave_button, &QToolButton::clicked, wave_button, &QToolButton::showMenu);
	wave_button->setIcon(QIcon(":/icons/waveform.svg"));
	wave_button->setToolTip(tr("Waveform settings"));
	wave_button->setMenu(wave_menu);
	m_toolbar->addWidget(wave_button);

	// ── Spectrogram menu button ───────────────────────
	auto *spectrum_menu = new QMenu(this);

	m_show_spectrogram_action = spectrum_menu->addAction(tr("Show spectrogram"));
	m_show_spectrogram_action->setCheckable(true);
	m_show_spectrogram_action->setChecked(false);
	connect(m_show_spectrogram_action, &QAction::toggled, this, &SoundView::onToggleSpectrogram);

	spectrum_menu->addSeparator();

	auto *spectrogram_settings_action = spectrum_menu->addAction(tr("Spectrogram settings..."));
	connect(spectrogram_settings_action, &QAction::triggered, this, &SoundView::onSpectrogramSettings);

	auto *spectrum_button = new QToolButton(this);
	spectrum_button->setPopupMode(QToolButton::MenuButtonPopup);
	connect(spectrum_button, &QToolButton::clicked, spectrum_button, &QToolButton::showMenu);
	spectrum_button->setIcon(QIcon(":/icons/spectrum.svg"));
	spectrum_button->setToolTip(tr("Spectrogram settings"));
	spectrum_button->setMenu(spectrum_menu);
	m_toolbar->addWidget(spectrum_button);

	// ── Intensity menu button ─────────────────────────
	auto *intensity_menu = new QMenu(this);

	m_show_intensity_action = intensity_menu->addAction(tr("Show intensity"));
	m_show_intensity_action->setCheckable(true);
	m_show_intensity_action->setChecked(false);
	connect(m_show_intensity_action, &QAction::toggled, this, &SoundView::onToggleIntensity);

	intensity_menu->addSeparator();

	auto *intensity_settings_action = intensity_menu->addAction(tr("Intensity settings..."));
	connect(intensity_settings_action, &QAction::triggered, this, &SoundView::onIntensitySettings);

	auto *intensity_button = new QToolButton(this);
	intensity_button->setPopupMode(QToolButton::MenuButtonPopup);
	connect(intensity_button, &QToolButton::clicked, intensity_button, &QToolButton::showMenu);
	intensity_button->setIcon(QIcon(":/icons/ear.svg"));
	intensity_button->setToolTip(tr("Intensity settings"));
	intensity_button->setMenu(intensity_menu);
	m_toolbar->addWidget(intensity_button);

	m_toolbar->addSeparator();

	// ── Mouse tracking toggle (on the right) ──────────
	auto *spacer = new QWidget(this);
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	m_toolbar->addWidget(spacer);

	auto *mouse_action = m_toolbar->addAction(QIcon(":/icons/mouse.svg"),
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

void SoundView::createSpectrograms(QLayout *layout)
{
	// Channel 0 = average of all channels.
	auto *avg = new SpectrogramWidget(m_model, m_sound, 0, this);
	avg->setVisible(false);
	m_spectrograms.push_back(avg);
	layout->addWidget(avg);

	// One spectrogram per channel.
	for (int c = 1; c <= m_sound->nchannel(); c++)
	{
		auto *sg = new SpectrogramWidget(m_model, m_sound, c, this);
		sg->setVisible(false);
		m_spectrograms.push_back(sg);
		layout->addWidget(sg);
	}
}

void SoundView::createIntensityTracks(QLayout *layout)
{
	// Channel 0 = average of all channels.
	auto *avg = new IntensityWidget(m_model, m_sound, 0, this);
	avg->setVisible(false);
	m_intensities.push_back(avg);
	layout->addWidget(avg);

	// One intensity track per channel.
	for (int c = 1; c <= m_sound->nchannel(); c++)
	{
		auto *iw = new IntensityWidget(m_model, m_sound, c, this);
		iw->setVisible(false);
		m_intensities.push_back(iw);
		layout->addWidget(iw);
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
	for (auto *sg : m_spectrograms)
		sg->setMouseTracking(checked);
	for (auto *iw : m_intensities)
		iw->setMouseTracking(checked);
}

void SoundView::onToggleWaveform(bool checked)
{
	m_show_waveform = checked;
	Settings::set_value("sound_plots", "waveform", checked);
	updatePlotVisibility();
}

void SoundView::onToggleSpectrogram(bool checked)
{
	m_show_spectrogram = checked;
	Settings::set_value("sound_plots", "spectrogram", checked);
	updatePlotVisibility();
}

void SoundView::onSpectrogramSettings()
{
	SpectrogramSettingsDialog dlg(this);

	if (dlg.exec() == QDialog::Accepted)
	{
		for (auto *sg : m_spectrograms)
		{
			sg->readSettings();
			sg->update();
		}
	}
}

void SoundView::onToggleIntensity(bool checked)
{
	m_show_intensity = checked;
	Settings::set_value("sound_plots", "intensity", checked);
	updatePlotVisibility();
}

void SoundView::onIntensitySettings()
{
	IntensitySettingsDialog dlg(this);

	if (dlg.exec() == QDialog::Accepted)
	{
		for (auto *iw : m_intensities)
		{
			iw->readSettings();
			iw->update();
		}
	}
}

void SoundView::updatePlotVisibility()
{
	// Waveforms: honour both m_show_waveform and channel visibility.
	m_waveforms[0]->setVisible(m_show_waveform && m_show_average);
	for (int c = 1; c <= m_sound->nchannel(); c++)
	{
		bool ch_visible = std::find(m_visible_channels.begin(),
			m_visible_channels.end(), c) != m_visible_channels.end();
		m_waveforms[c]->setVisible(m_show_waveform && ch_visible);
	}

	// Spectrograms: honour both m_show_spectrogram and channel visibility.
	m_spectrograms[0]->setVisible(m_show_spectrogram && m_show_average);
	for (int c = 1; c <= m_sound->nchannel(); c++)
	{
		bool ch_visible = std::find(m_visible_channels.begin(),
			m_visible_channels.end(), c) != m_visible_channels.end();
		m_spectrograms[c]->setVisible(m_show_spectrogram && ch_visible);
	}

	// Intensity tracks: honour both m_show_intensity and channel visibility.
	m_intensities[0]->setVisible(m_show_intensity && m_show_average);
	for (int c = 1; c <= m_sound->nchannel(); c++)
	{
		bool ch_visible = std::find(m_visible_channels.begin(),
			m_visible_channels.end(), c) != m_visible_channels.end();
		m_intensities[c]->setVisible(m_show_intensity && ch_visible);
	}

	m_y_axis->update();
}

void SoundView::onScalingChanged(QAction *action)
{
	auto mode = static_cast<Scaling>(action->data().toInt());
	for (auto *wf : m_waveforms)
		wf->setScaling(mode);
	m_y_axis->update();
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
	if (m_was_playing && m_active_play_action == m_play_action && !m_player->paused())
	{
		// Currently playing via this button → pause.
		m_player->pause();
		m_playback_timer->stop();
		m_play_action->setIcon(QIcon(PLAY_ICON));
		m_play_action->setToolTip(tr("Resume playback"));
		return;
	}

	if (m_was_playing && m_active_play_action == m_play_action && m_player->paused())
	{
		// Currently paused via this button → resume.
		m_player->resume();
		m_playback_timer->start();
		m_play_action->setIcon(QIcon(PAUSE_ICON));
		m_play_action->setToolTip(tr("Pause playback"));
		return;
	}

	// Not playing (or playing from the other button) → start.
	if (m_was_playing)
		stopPlayback();

	startPlayback(m_play_action, m_model->windowStart(), m_model->windowEnd());
}

void SoundView::onPlaySelection()
{
	if (m_was_playing && m_active_play_action == m_play_sel_action && !m_player->paused())
	{
		// Currently playing selection → pause.
		m_player->pause();
		m_playback_timer->stop();
		m_play_sel_action->setIcon(QIcon(PLAY_SEL_ICON));
		m_play_sel_action->setToolTip(tr("Resume selection playback"));
		return;
	}

	if (m_was_playing && m_active_play_action == m_play_sel_action && m_player->paused())
	{
		// Currently paused selection → resume.
		m_player->resume();
		m_playback_timer->start();
		m_play_sel_action->setIcon(QIcon(PAUSE_SEL_ICON));
		m_play_sel_action->setToolTip(tr("Pause selection playback"));
		return;
	}

	// Not playing (or playing from the other button) → start.
	if (m_was_playing)
		stopPlayback();

	if (m_model->hasSpanSelection())
		startPlayback(m_play_sel_action, m_model->selectionStart(), m_model->selectionEnd());
	else if (m_model->hasPointSelection())
		startPlayback(m_play_sel_action, m_model->selectionStart(), m_model->windowEnd());
}

void SoundView::onStop()
{
	stopPlayback();
}

void SoundView::startPlayback(QAction *source, double from, double to)
{
	// Stop any ongoing playback.
	if (m_player->running())
		stopPlayback();

	m_active_play_action = source;
	m_player->play(from, to);
	m_was_playing = true;

	if (source == m_play_action)
	{
		source->setIcon(QIcon(PAUSE_ICON));
		source->setToolTip(tr("Pause playback"));
	}
	else
	{
		source->setIcon(QIcon(PAUSE_SEL_ICON));
		source->setToolTip(tr("Pause selection playback"));
	}

	m_playback_timer->start();
}

void SoundView::stopPlayback()
{
	m_playback_timer->stop();
	m_player->stop();
	m_model->clearPlayback();

	// Restore the icon on whichever button was active.
	if (m_active_play_action == m_play_action)
	{
		m_play_action->setIcon(QIcon(PLAY_ICON));
		m_play_action->setToolTip(tr("Play current window"));
	}
	else if (m_active_play_action == m_play_sel_action)
	{
		m_play_sel_action->setIcon(QIcon(PLAY_SEL_ICON));
		m_play_sel_action->setToolTip(tr("Play selection"));
	}

	m_active_play_action = nullptr;
	m_was_playing = false;
}

void SoundView::onPlaybackTick()
{
	if (!m_player->running())
	{
		// Playback has ended naturally.
		stopPlayback();
		return;
	}

	double t = m_player->currentPlaybackTime();
	m_model->setPlaybackTime(t);
}

void SoundView::keyPressEvent(QKeyEvent *event)
{
	switch (event->key())
	{
	case Qt::Key_Space:
		onPlayWindow();
		break;
	case Qt::Key_Escape:
		onStop();
		break;
	case Qt::Key_Left:
		m_model->moveBackward();
		break;
	case Qt::Key_Right:
		m_model->moveForward();
		break;
	case Qt::Key_Plus:
	case Qt::Key_Equal: // unshifted + on most keyboards
		m_model->zoomIn();
		break;
	case Qt::Key_Minus:
		m_model->zoomOut();
		break;
	default:
		View::keyPressEvent(event);
		break;
	}
}

void SoundView::onChannelAction(QAction *action)
{
	int channel = action->data().toInt();
	bool show = action->isChecked();

	// Guard: don't allow unchecking everything.
	if (!show)
	{
		int visible_count = m_show_average ? 1 : 0;
		visible_count += (int)m_visible_channels.size();

		// Are we about to remove the last one?
		bool removing_last = false;
		if (channel == 0 && m_show_average)
			removing_last = (visible_count == 1);
		else if (channel != 0 && std::find(m_visible_channels.begin(), m_visible_channels.end(), channel) != m_visible_channels.end())
			removing_last = (visible_count == 1);

		if (removing_last)
		{
			QMessageBox::warning(this, tr("Channel selection"),
				tr("At least one channel must be visible."));
			action->setChecked(true);
			return;
		}
	}

	m_waveforms[channel]->setVisible(show);

	if (channel == 0)
	{
		m_show_average = show;
	}
	else
	{
		if (show)
		{
			if (std::find(m_visible_channels.begin(), m_visible_channels.end(), channel) == m_visible_channels.end())
				m_visible_channels.push_back(channel);
		}
		else
		{
			m_visible_channels.erase(
				std::remove(m_visible_channels.begin(), m_visible_channels.end(), channel),
				m_visible_channels.end());
		}
	}

	updatePlotVisibility();
}
} // namespace phonometrica
