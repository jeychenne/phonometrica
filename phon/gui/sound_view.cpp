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
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <algorithm>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QKeyEvent>
#include <QMessageBox>
#include <QLabel>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QApplication>
#include <phon/gui/sound_view.hpp>
#include <phon/gui/spectrum_view.hpp>
#include <phon/application/spectral_moments.hpp>
#include <phon/gui/time_axis_widget.hpp>
#include <phon/gui/y_axis_widget.hpp>
#include <phon/gui/waveform_widget.hpp>
#include <phon/gui/spectrogram_widget.hpp>
#include <phon/gui/spectrogram_settings_dialog.hpp>
#include <phon/gui/intensity_widget.hpp>
#include <phon/gui/intensity_settings_dialog.hpp>
#include <phon/gui/pitch_widget.hpp>
#include <phon/gui/pitch_settings_dialog.hpp>
#include <phon/gui/formant_settings_dialog.hpp>
#include <phon/gui/output_panel.hpp>
#include <phon/gui/wave_bar.hpp>
#include <phon/gui/sound_zoom.hpp>
#include <phon/gui/help_browser.hpp>
#include <phon/application/audio_player.hpp>
#include <phon/application/settings.hpp>
#include <phon/analysis/voice_quality.hpp>

static constexpr const char *PLAY_ICON = ":/icons/play.svg";
static constexpr const char *PAUSE_ICON = ":/icons/pause.svg";
static constexpr const char *PLAY_SEL_ICON = ":/icons/play-selection.svg";
static constexpr const char *PAUSE_SEL_ICON = ":/icons/pause-selection.svg";

namespace phonometrica {

// Private helper: shared initialization code for both constructors.
static void initSoundViewCore(SoundView *self, const Handle<Sound> &sound, TimeModel *&model,
	std::unique_ptr<AudioPlayer> &player, QTimer *&timer)
{
	sound->open();
	model = new TimeModel(sound->duration(), self);
	player = std::make_unique<AudioPlayer>(sound);
	timer = new QTimer(self);
	timer->setInterval(33); // ~30 FPS
}

SoundView::SoundView(const Handle<Sound> &sound, QWidget *parent) :
	View(parent), m_sound(sound)
{
	initSoundViewCore(this, m_sound, m_model, m_player, m_playback_timer);
	connect(m_playback_timer, &QTimer::timeout, this, &SoundView::onPlaybackTick);
	initialize();
}

SoundView::SoundView(const Handle<Sound> &sound, DeferInit, QWidget *parent) :
	View(parent), m_sound(sound)
{
	initSoundViewCore(this, m_sound, m_model, m_player, m_playback_timer);
	connect(m_playback_timer, &QTimer::timeout, this, &SoundView::onPlaybackTick);
	// initialize() will be called by the subclass.
}

void SoundView::initialize()
{
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
	try {
		m_show_pitch = Settings::get_boolean("sound_plots", "pitch");
	}
	catch (...) {
		m_show_pitch = false;
	}
	try {
		m_show_formants = Settings::get_boolean("sound_plots", "formants");
	}
	catch (...) {
		m_show_formants = false;
	}
	try {
		m_show_glottal_pulses = Settings::get_boolean("sound_plots", "glottal_pulses");
	}
	catch (...) {
		m_show_glottal_pulses = false;
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
	if (m_show_pitch_action)
		m_show_pitch_action->setChecked(m_show_pitch);
	if (m_show_formants_action)
		m_show_formants_action->setChecked(m_show_formants);
	if (m_show_glottal_pulses_action)
		m_show_glottal_pulses_action->setChecked(m_show_glottal_pulses);

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

	// Pitch tracks (one per channel, hidden by default).
	createPitchTracks(plot_layout);

	// Intensity tracks (one per channel, hidden by default).
	createIntensityTracks(plot_layout);

	// Register all waveforms with the Y axis (hidden ones are skipped during paint).
	for (auto *wf : m_waveforms)
		m_y_axis->addWaveform(wf);

	// Register all spectrograms with the Y axis.
	for (auto *sg : m_spectrograms)
		m_y_axis->addSpectrogram(sg);

	// Register all pitch tracks with the Y axis.
	for (auto *pw : m_pitches)
		m_y_axis->addPitch(pw);

	// Register all intensity tracks with the Y axis.
	for (auto *iw : m_intensities)
		m_y_axis->addIntensity(iw);

	// Hook for annotation layers (subclass override).
	addAnnotationLayers(plot_layout);

	mid_layout->addLayout(plot_layout, 1);

	// Wrap the mid section (Y axis + plots + layers) in a scroll area so that the
	// view can shrink when the user resizes the bottom dock panel. Without this,
	// the accumulated minimum heights of all fixed-size sub-widgets (toolbar, time axis,
	// waveforms, annotation layers, zoom, wavebar) prevent the central widget from
	// shrinking and the dock divider gets stuck.
	auto *mid_container = new QWidget(this);
	mid_container->setLayout(mid_layout);

	m_scroll_area = new QScrollArea(this);
	m_scroll_area->setWidget(mid_container);
	m_scroll_area->setWidgetResizable(true);
	m_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scroll_area->setFrameShape(QFrame::NoFrame);

	layout->addWidget(m_scroll_area, 1);

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
	// Use Ignored horizontal policy so text changes don't trigger layout recalculations
	// (which cause the dock widget divider to move when the cursor hovers over sound plots).
	m_status_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
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

	m_zoom_sel_action = m_toolbar->addAction(QIcon(":/icons/minimize.svg"),
		tr("Zoom to selection"));
	m_zoom_sel_action->setEnabled(false);
	connect(m_zoom_sel_action, &QAction::triggered, this, &SoundView::onZoomToSelection);

	auto *view_all_action = m_toolbar->addAction(QIcon(":/icons/maximize.svg"),
		tr("View whole file"));
	connect(view_all_action, &QAction::triggered, this, &SoundView::onViewAll);

	m_toolbar->addSeparator();

	auto *goto_action = m_toolbar->addAction(QIcon(":/icons/text-cursor.svg"),
		tr("Go to time..."));
	connect(goto_action, &QAction::triggered, this, &SoundView::onGoToTime);

	auto *sel_window_action = m_toolbar->addAction(QIcon(":/icons/select-window.svg"),
	tr("Select window"));
	connect(sel_window_action, &QAction::triggered, this, &SoundView::onSelectWindow);

	m_toolbar->addSeparator();

	// ── Waveform menu button ──────────────────────────
	auto *wave_menu = new QMenu(this);

	// "Show waveform(s)" checkbox.
	auto show_wave_label = m_sound->is_mono() ? tr("Show waveform") : tr("Show waveforms");
	m_show_wave_action = wave_menu->addAction(show_wave_label);
	m_show_wave_action->setCheckable(true);
	m_show_wave_action->setChecked(true);
	connect(m_show_wave_action, &QAction::toggled, this, &SoundView::onToggleWaveform);

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

	auto *wave_action = new QAction(QIcon(":/icons/waveform.svg"), tr("Waveform settings"), this);
	wave_action->setMenu(wave_menu);
	m_toolbar->addAction(wave_action);
	if (auto *wb = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(wave_action)))
		wb->setPopupMode(QToolButton::InstantPopup);

	// ── Spectrogram menu button ───────────────────────
	auto *spectrum_menu = new QMenu(this);

	m_show_spectrogram_action = spectrum_menu->addAction(tr("Show spectrogram"));
	m_show_spectrogram_action->setCheckable(true);
	m_show_spectrogram_action->setChecked(false);
	connect(m_show_spectrogram_action, &QAction::toggled, this, &SoundView::onToggleSpectrogram);
	spectrum_menu->addSeparator();

	auto *view_fft_action = spectrum_menu->addAction(tr("View FFT spectrum"));
	view_fft_action->setShortcut(QKeySequence(Qt::Key_F5));
	view_fft_action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	addAction(view_fft_action);
	connect(view_fft_action, &QAction::triggered, this,
		[this]() { onViewSpectralSlice(SpectrumDisplayMode::FFT); });

	auto *view_lpc_action = spectrum_menu->addAction(tr("View LPC spectrum"));
	connect(view_lpc_action, &QAction::triggered, this,
		[this]() { onViewSpectralSlice(SpectrumDisplayMode::LPC); });

	auto *view_both_action = spectrum_menu->addAction(tr("View FFT && LPC spectrum"));
	view_both_action->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
	view_both_action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	addAction(view_both_action);
	connect(view_both_action, &QAction::triggered, this,
		[this]() { onViewSpectralSlice(SpectrumDisplayMode::Both); });

	auto *view_moments_action = spectrum_menu->addAction(tr("Get spectral moments"));
	connect(view_moments_action, &QAction::triggered, this,
		[this]() { onViewSpectralMoments(); });

	spectrum_menu->addSeparator();

	auto *spectrogram_settings_action = spectrum_menu->addAction(tr("Spectrogram settings..."));
	connect(spectrogram_settings_action, &QAction::triggered, this, &SoundView::onSpectrogramSettings);
	spectrum_menu->addSeparator();

	auto *spectrum_action = new QAction(QIcon(":/icons/spectrum.svg"), tr("Spectrogram settings"), this);
	spectrum_action->setMenu(spectrum_menu);
	m_toolbar->addAction(spectrum_action);
	if (auto *sb = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(spectrum_action)))
		sb->setPopupMode(QToolButton::InstantPopup);


	// ── Formant menu button ──────────────────────────
	auto *formant_menu = new QMenu(this);

	m_show_formants_action = formant_menu->addAction(tr("Show formants"));
	m_show_formants_action->setCheckable(true);
	m_show_formants_action->setChecked(false);
	connect(m_show_formants_action, &QAction::toggled, this, &SoundView::onToggleFormants);

	formant_menu->addSeparator();

	auto *get_formants_action = formant_menu->addAction(tr("Get formants"));
	get_formants_action->setShortcut(QKeySequence(Qt::Key_F6));
	get_formants_action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	addAction(get_formants_action);
	connect(get_formants_action, &QAction::triggered, this, &SoundView::onGetFormants);

	auto *get_mean_formants_action = formant_menu->addAction(tr("Get mean formants"));
	get_mean_formants_action->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F6));
	get_mean_formants_action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	addAction(get_mean_formants_action);
	connect(get_mean_formants_action, &QAction::triggered, this, &SoundView::onGetMeanFormants);

	formant_menu->addSeparator();

	auto *formant_settings_action = formant_menu->addAction(tr("Formant settings..."));
	connect(formant_settings_action, &QAction::triggered, this, &SoundView::onFormantSettings);

	auto *formant_action = new QAction(QIcon(":/icons/waves.svg"), tr("Formant settings"), this);
	formant_action->setMenu(formant_menu);
	m_toolbar->addAction(formant_action);
	if (auto *fb = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(formant_action)))
		fb->setPopupMode(QToolButton::InstantPopup);

	// ── Pitch menu button ─────────────────────────────
	auto *pitch_menu = new QMenu(this);

	m_show_pitch_action = pitch_menu->addAction(tr("Show pitch"));
	m_show_pitch_action->setCheckable(true);
	m_show_pitch_action->setChecked(false);
	connect(m_show_pitch_action, &QAction::toggled, this, &SoundView::onTogglePitch);

	pitch_menu->addSeparator();

	auto *get_pitch_action = pitch_menu->addAction(tr("Get pitch"));
	get_pitch_action->setShortcut(QKeySequence(Qt::Key_F7));
	get_pitch_action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	addAction(get_pitch_action);
	connect(get_pitch_action, &QAction::triggered, this, &SoundView::onGetPitch);

	auto *get_mean_pitch_action = pitch_menu->addAction(tr("Get mean pitch"));
	get_mean_pitch_action->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F7));
	get_mean_pitch_action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	addAction(get_mean_pitch_action);
	connect(get_mean_pitch_action, &QAction::triggered, this, &SoundView::onGetMeanPitch);

	pitch_menu->addSeparator();

	auto *pitch_settings_action = pitch_menu->addAction(tr("Pitch settings..."));
	connect(pitch_settings_action, &QAction::triggered, this, &SoundView::onPitchSettings);

	auto *pitch_action = new QAction(QIcon(":/icons/pitch.svg"), tr("Pitch settings"), this);
	pitch_action->setMenu(pitch_menu);
	m_toolbar->addAction(pitch_action);
	if (auto *pb = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(pitch_action)))
		pb->setPopupMode(QToolButton::InstantPopup);

	// ── Voice quality menu button ─────────────────────
	// Houses the glottal-pulse overlay in v1.0; HNR track and full voice
	// report (jitter/shimmer/HNR over selection) join in follow-up commits.
	auto *voice_menu = new QMenu(this);

	m_show_glottal_pulses_action = voice_menu->addAction(tr("Show glottal pulses"));
	m_show_glottal_pulses_action->setCheckable(true);
	m_show_glottal_pulses_action->setChecked(false);
	connect(m_show_glottal_pulses_action, &QAction::toggled, this, &SoundView::onToggleGlottalPulses);

	voice_menu->addSeparator();

	auto *voice_report_action = voice_menu->addAction(tr("Get voice report"));
	voice_report_action->setShortcut(QKeySequence(Qt::Key_F9));
	voice_report_action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	addAction(voice_report_action);
	connect(voice_report_action, &QAction::triggered, this, &SoundView::onVoiceReport);

	auto *voice_action = new QAction(QIcon(":/icons/voice.svg"), tr("Voice quality"), this);
	voice_action->setMenu(voice_menu);
	m_toolbar->addAction(voice_action);
	if (auto *vb = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(voice_action)))
		vb->setPopupMode(QToolButton::InstantPopup);

	// ── Intensity menu button ─────────────────────────
	auto *intensity_menu = new QMenu(this);

	m_show_intensity_action = intensity_menu->addAction(tr("Show intensity"));
	m_show_intensity_action->setCheckable(true);
	m_show_intensity_action->setChecked(false);
	connect(m_show_intensity_action, &QAction::toggled, this, &SoundView::onToggleIntensity);

	intensity_menu->addSeparator();

	auto *get_intensity_action = intensity_menu->addAction(tr("Get intensity"));
	get_intensity_action->setShortcut(QKeySequence(Qt::Key_F8));
	get_intensity_action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	addAction(get_intensity_action);
	connect(get_intensity_action, &QAction::triggered, this, &SoundView::onGetIntensity);

	auto *get_mean_intensity_action = intensity_menu->addAction(tr("Get mean intensity"));
	get_mean_intensity_action->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F8));
	get_mean_intensity_action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	addAction(get_mean_intensity_action);
	connect(get_mean_intensity_action, &QAction::triggered, this, &SoundView::onGetMeanIntensity);

	intensity_menu->addSeparator();

	auto *intensity_settings_action = intensity_menu->addAction(tr("Intensity settings..."));
	connect(intensity_settings_action, &QAction::triggered, this, &SoundView::onIntensitySettings);

	auto *intensity_action = new QAction(QIcon(":/icons/ear.svg"), tr("Intensity settings"), this);
	intensity_action->setMenu(intensity_menu);
	m_toolbar->addAction(intensity_action);
	if (auto *ib = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(intensity_action)))
		ib->setPopupMode(QToolButton::InstantPopup);

	m_toolbar->addSeparator();

	// ── Display menu button (channels + subclass entries) ──
	auto *display_menu = new QMenu(this);

	auto *channel_group = new QActionGroup(this);
	channel_group->setExclusive(false);

	auto *avg_action = display_menu->addAction(tr("Average channels"));
	avg_action->setCheckable(true);
	avg_action->setChecked(false);
	avg_action->setData(0);
	channel_group->addAction(avg_action);

	// For mono files, averaging is meaningless: disable the action.
	if (m_sound->is_mono())
		avg_action->setEnabled(false);

	display_menu->addSeparator();

	for (int c = 1; c <= m_sound->nchannel(); c++)
	{
		auto *ch_action = display_menu->addAction(tr("Channel %1").arg(c));
		ch_action->setCheckable(true);
		ch_action->setChecked(true);
		ch_action->setData(c);
		channel_group->addAction(ch_action);
	}

	connect(channel_group, &QActionGroup::triggered, this, &SoundView::onChannelAction);

	// Let subclasses (AnnotationView) add their entries.
	addDisplayMenuEntries(display_menu);

	auto *display_action = new QAction(QIcon(":/icons/display.svg"), tr("Display settings"), this);
	display_action->setMenu(display_menu);
	m_toolbar->addAction(display_action);
	if (auto *db = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(display_action)))
		db->setPopupMode(QToolButton::InstantPopup);
	m_toolbar->addSeparator();

	// ── Mouse tracking toggle ─────────────────────────
	auto *mouse_action = m_toolbar->addAction(QIcon(":/icons/mouse.svg"),
		tr("Enable mouse tracking"));
	mouse_action->setCheckable(true);
	mouse_action->setChecked(false);
	connect(mouse_action, &QAction::toggled, this, &SoundView::onToggleMouseTracking);

	// ── Annotation toolbar actions (subclass hook) ───
	addAnnotationToolbar(m_toolbar);

	// ── Right-aligned help button ─────────────────────
	auto *spacer = new QWidget(this);
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	m_toolbar->addWidget(spacer);

	auto *help_action = m_toolbar->addAction(QIcon(":/icons/circle-help.svg"),
		tr("Help"));
	connect(help_action, &QAction::triggered, this, [this]() {
		HelpBrowser::showPage(helpAnchor(), this);
	});
}

void SoundView::createWaveforms(QVBoxLayout *layout)
{
	// Stretch factor for large plots (waveforms, spectrograms).
	const int large_stretch = 2;

	// Channel 0 = average of all channels.
	auto *avg = new WaveformWidget(m_model, m_sound, 0, this);
	connect(avg, &WaveformWidget::yValueDescription, this, &SoundView::onYValueDescription);
	connect(avg, &WaveformWidget::anchorRequested, this, &SoundView::onAnchorRequested);
	m_waveforms.push_back(avg);
	layout->addWidget(avg, large_stretch);
	auto *avg_line = createSeparator();
	m_wave_lines.push_back(avg_line);
	layout->addWidget(avg_line);

	// One waveform per channel.
	for (int c = 1; c <= m_sound->nchannel(); c++)
	{
		auto *wf = new WaveformWidget(m_model, m_sound, c, this);
		connect(wf, &WaveformWidget::yValueDescription, this, &SoundView::onYValueDescription);
		connect(wf, &WaveformWidget::anchorRequested, this, &SoundView::onAnchorRequested);
		m_waveforms.push_back(wf);
		layout->addWidget(wf, large_stretch);
		auto *line = createSeparator();
		m_wave_lines.push_back(line);
		layout->addWidget(line);
	}
}

void SoundView::createSpectrograms(QVBoxLayout *layout)
{
	const int large_stretch = 2;

	auto *avg = new SpectrogramWidget(m_model, m_sound, 0, this);
	avg->setVisible(false);
	connect(avg, &SpectrogramWidget::yValueDescription, this, &SoundView::onYValueDescription);
	connect(avg, &SpectrogramWidget::anchorRequested, this, &SoundView::onAnchorRequested);
	m_spectrograms.push_back(avg);
	layout->addWidget(avg, large_stretch);
	auto *avg_line = createSeparator();
	m_spectrogram_lines.push_back(avg_line);
	layout->addWidget(avg_line);

	for (int c = 1; c <= m_sound->nchannel(); c++)
	{
		auto *sg = new SpectrogramWidget(m_model, m_sound, c, this);
		sg->setVisible(false);
		connect(sg, &SpectrogramWidget::yValueDescription, this, &SoundView::onYValueDescription);
		connect(sg, &SpectrogramWidget::anchorRequested, this, &SoundView::onAnchorRequested);
		m_spectrograms.push_back(sg);
		layout->addWidget(sg, large_stretch);
		auto *line = createSeparator();
		m_spectrogram_lines.push_back(line);
		layout->addWidget(line);
	}
}

void SoundView::createPitchTracks(QVBoxLayout *layout)
{
	const int small_stretch = 1;

	auto *avg = new PitchWidget(m_model, m_sound, 0, this);
	avg->setVisible(false);
	connect(avg, &PitchWidget::yValueDescription, this, &SoundView::onYValueDescription);
	connect(avg, &PitchWidget::anchorRequested, this, &SoundView::onAnchorRequested);
	m_pitches.push_back(avg);
	layout->addWidget(avg, small_stretch);
	auto *avg_line = createSeparator();
	m_pitch_lines.push_back(avg_line);
	layout->addWidget(avg_line);

	for (int c = 1; c <= m_sound->nchannel(); c++)
	{
		auto *pw = new PitchWidget(m_model, m_sound, c, this);
		pw->setVisible(false);
		connect(pw, &PitchWidget::yValueDescription, this, &SoundView::onYValueDescription);
		connect(pw, &PitchWidget::anchorRequested, this, &SoundView::onAnchorRequested);
		m_pitches.push_back(pw);
		layout->addWidget(pw, small_stretch);
		auto *line = createSeparator();
		m_pitch_lines.push_back(line);
		layout->addWidget(line);
	}
}

void SoundView::createIntensityTracks(QVBoxLayout *layout)
{
	const int small_stretch = 1;

	auto *avg = new IntensityWidget(m_model, m_sound, 0, this);
	avg->setVisible(false);
	connect(avg, &IntensityWidget::yValueDescription, this, &SoundView::onYValueDescription);
	connect(avg, &IntensityWidget::anchorRequested, this, &SoundView::onAnchorRequested);
	m_intensities.push_back(avg);
	layout->addWidget(avg, small_stretch);
	auto *avg_line = createSeparator();
	m_intensity_lines.push_back(avg_line);
	layout->addWidget(avg_line);

	for (int c = 1; c <= m_sound->nchannel(); c++)
	{
		auto *iw = new IntensityWidget(m_model, m_sound, c, this);
		iw->setVisible(false);
		connect(iw, &IntensityWidget::yValueDescription, this, &SoundView::onYValueDescription);
		connect(iw, &IntensityWidget::anchorRequested, this, &SoundView::onAnchorRequested);
		m_intensities.push_back(iw);
		layout->addWidget(iw, small_stretch);
		auto *line = createSeparator();
		m_intensity_lines.push_back(line);
		layout->addWidget(line);
	}
}

QFrame *SoundView::createSeparator()
{
	// Paint the separator as a plain 1 px coloured strip via autoFillBackground
	// rather than relying on QFrame::HLine. The macOS native style ignores
	// palette role changes on QFrame decoration (both Sunken and Plain shadows),
	// so any palette-based approach renders invisibly against the sound widget
	// backgrounds. autoFillBackground + an explicit Window colour bypasses the
	// style entirely — it's just a rectangle the widget paints itself, which
	// mirrors how LayerWidget draws its own bottom separator.
	//
	// The return type is kept as QFrame* to avoid churn in the m_*_lines
	// vectors; QFrame's frame shape is left at its default NoFrame.
	auto *line = new QFrame(this);
	line->setFrameShape(QFrame::NoFrame);
	line->setAutoFillBackground(true);
	QPalette pal = line->palette();
	pal.setColor(QPalette::Window, QColor(0xA0, 0xA0, 0xA0));
	line->setPalette(pal);
	line->setFixedHeight(1);
	return line;
}


// ─────────────────────────────────────────────────
//  View interface
// ─────────────────────────────────────────────────

QString SoundView::label() const
{
	auto lbl = m_sound->label();
	return tabLabel(QString::fromUtf8(lbl.data(), (int) lbl.size()));
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

	if (!m_y_value_text.isEmpty())
	{
		text += QStringLiteral("  |  ") + m_y_value_text;
	}

	if (!m_annot_status_text.isEmpty())
	{
		text += QStringLiteral("  |  ") + m_annot_status_text;
	}

	m_status_label->setText(text);
}

void SoundView::onYValueDescription(const QString &text)
{
	m_y_value_text = text;
	updateStatusText();
}

void SoundView::setAnnotationStatus(const QString &text)
{
	m_annot_status_text = text;
	updateStatusText();
}

void SoundView::clearAnnotationStatus()
{
	m_annot_status_text.clear();
	updateStatusText();
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
	for (auto *pw : m_pitches)
		pw->setMouseTracking(checked);
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
	if (m_spectrogram_settings_dialog) {
		m_spectrogram_settings_dialog->show();
		m_spectrogram_settings_dialog->raise();
		m_spectrogram_settings_dialog->activateWindow();
		return;
	}

	auto *dlg = new SpectrogramSettingsDialog(this);
	dlg->setAttribute(Qt::WA_DeleteOnClose);

	connect(dlg, &SpectrogramSettingsDialog::settingsApplied, this, [this]() {
		for (auto *sg : m_spectrograms) {
			sg->readSettings();
			sg->update();
		}
	});

	m_spectrogram_settings_dialog = dlg;
	dlg->show();
}

void SoundView::onViewSpectralSlice(SpectrumDisplayMode mode)
{
	using namespace speech;

	double t1, t2;

	if (m_model->hasSpanSelection())
	{
		t1 = m_model->selectionStart();
		t2 = m_model->selectionEnd();
	}
	else if (m_model->hasPointSelection())
	{
		// Cursor at a single time point: ask the user for an analysis window duration.
		double cursor = m_model->selectionStart();

		// Default to 25 ms for spectral analysis (longer than the spectrogram window).
		double default_dur = 0.025;

		QDialog dlg(this);
		dlg.setWindowTitle(tr("FFT window duration"));
		dlg.setMinimumWidth(280);

		auto *layout = new QVBoxLayout(&dlg);
		layout->addWidget(new QLabel(
			tr("Window duration (seconds), centered at %1 s:").arg(cursor, 0, 'f', 4)));
		auto *edit = new QLineEdit(QString::number(default_dur, 'f', 4), &dlg);
		layout->addWidget(edit);
		layout->addStretch();
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
		layout->addWidget(buttons);
		connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
		connect(edit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);

		if (dlg.exec() != QDialog::Accepted)
			return;

		bool ok;
		double dur = edit->text().toDouble(&ok);
		if (!ok || dur <= 0)
		{
			QMessageBox::critical(this, tr("FFT spectrum"),
				tr("Invalid window duration."));
			return;
		}

		double half = dur / 2.0;
		t1 = cursor - half;
		t2 = cursor + half;

		// Clamp to the sound boundaries.
		if (t1 < 0) { t2 -= t1; t1 = 0; }
		if (t2 > m_sound->duration()) { t1 -= (t2 - m_sound->duration()); t2 = m_sound->duration(); }
		if (t1 < 0) t1 = 0;
	}
	else
	{
		QMessageBox::warning(this, tr("FFT spectrum"),
			tr("Please select a portion of the signal or place the cursor first."));
		return;
	}

	// Read analysis parameters from spectrogram settings.
	WindowType window_type = WindowType::Gaussian;
	double preemph = 50.0;
	double max_freq = 0.0;
	double dynamic_range = 70.0;

	try
	{
		String category("spectrogram");
		preemph = Settings::get_number(category, "preemphasis_threshold");
		dynamic_range = Settings::get_number(category, "dynamic_range");
		max_freq = Settings::get_number(category, "frequency_range");

		String win = Settings::get_string(category, "window_type");
		if (win == "Bartlett")        window_type = WindowType::Bartlett;
		else if (win == "Blackman")   window_type = WindowType::Blackman;
		else if (win == "Gaussian")   window_type = WindowType::Gaussian;
		else if (win == "Hamming")    window_type = WindowType::Hamming;
		else if (win == "Hann")       window_type = WindowType::Hann;
		else if (win == "Rectangular") window_type = WindowType::Rectangular;
	}
	catch (...) {}

	// Use the first visible channel (or 0 = average if showing the average).
	int channel = 0;
	if (!m_show_average && !m_visible_channels.empty())
		channel = m_visible_channels.front();

	// Determine LPC order if an LPC spectrum is requested.
	int lpc_order = 0;
	if (mode != SpectrumDisplayMode::FFT)
	{
		// Try to read the LPC order from the formant settings; otherwise use a
		// standard heuristic: 2 × (max_frequency / 1000) + 2.
		try {
			lpc_order = static_cast<int>(Settings::get_number("formants", "lpc_order"));
		}
		catch (...) {
			double effective_max = (max_freq > 0) ? max_freq : (m_sound->sample_rate() / 2.0);
			lpc_order = static_cast<int>(2.0 * effective_max / 1000.0) + 2;
		}
		if (lpc_order < 2) lpc_order = 2;
	}

	try
	{
		auto spec = make_handle<Spectrum>(nullptr, m_sound, channel, t1, t2,
			window_type, 2, preemph, max_freq, dynamic_range, lpc_order);
		auto *view = new SpectrumView(spec, mode, this);
		view->setAttribute(Qt::WA_DeleteOnClose);
		view->show();
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Spectral slice"),
			QString::fromUtf8(e.what()));
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
	if (m_intensity_settings_dialog) {
		m_intensity_settings_dialog->show();
		m_intensity_settings_dialog->raise();
		m_intensity_settings_dialog->activateWindow();
		return;
	}

	auto *dlg = new IntensitySettingsDialog(this);
	dlg->setAttribute(Qt::WA_DeleteOnClose);

	connect(dlg, &IntensitySettingsDialog::settingsApplied, this, [this]() {
		for (auto *iw : m_intensities) {
			iw->readSettings();
			iw->update();
		}
	});

	m_intensity_settings_dialog = dlg;
	dlg->show();
}

void SoundView::onToggleFormants(bool checked)
{
	m_show_formants = checked;
	Settings::set_value("sound_plots", "formants", checked);
	for (auto *sg : m_spectrograms)
		sg->setShowFormants(checked);
}

void SoundView::onToggleGlottalPulses(bool checked)
{
	m_show_glottal_pulses = checked;
	Settings::set_value("sound_plots", "glottal_pulses", checked);
	// Pulses are overlaid on each waveform widget, not on the average track.
	// The average channel still receives the toggle so the user can opt in
	// from the average view too (it computes on the per-frame mean).
	for (auto *wf : m_waveforms)
		wf->setShowGlottalPulses(checked);
}

// ─────────────────────────────────────────────────
//  Voice quality report
// ─────────────────────────────────────────────────

namespace {

// Format a relative jitter / shimmer measure as a percentage with three
// fractional digits, or "(undefined)" if NaN.
QString fmtPercent(double x)
{
	if (std::isnan(x)) return QObject::tr("(undefined)");
	return QString::number(100.0 * x, 'f', 3) + QStringLiteral(" %");
}

QString fmtMicroseconds(double seconds)
{
	if (std::isnan(seconds)) return QObject::tr("(undefined)");
	return QString::number(seconds * 1e6, 'f', 2) + QStringLiteral(" µs");
}

QString fmtDb(double db)
{
	if (std::isnan(db)) return QObject::tr("(undefined)");
	return QString::number(db, 'f', 3) + QStringLiteral(" dB");
}

} // namespace

void SoundView::onVoiceReport()
{
	if (!m_model->hasSpanSelection())
	{
		QMessageBox::warning(this, tr("Cannot compute voice report"),
			tr("Please select a time interval first.\n\nA single-point selection is not enough — "
			   "the voice report needs a span of voiced speech to estimate jitter, shimmer and HNR."));
		return;
	}

	double t1 = m_model->selectionStart();
	double t2 = m_model->selectionEnd();

	QString heading = tr("Voice report (%1 s – %2 s, duration %3 s)")
		.arg(t1, 0, 'f', 4).arg(t2, 0, 'f', 4).arg(t2 - t1, 0, 'f', 4);

	QString body;
	QApplication::setOverrideCursor(Qt::WaitCursor);
	try
	{
		const bool mono = m_sound->is_mono();
		const QString ind = mono ? QString() : QStringLiteral("  ");

		for (int ch : m_visible_channels)
		{
			// Single-shot computation: the kernel owns pulse detection,
			// jitter/shimmer aggregation, period summary and HNR. The GUI
			// only formats.
			auto r = m_sound->compute_voice_report(ch, t1, t2);

			if (!mono)
				body += tr("Channel %1:\n").arg(ch);

			body += ind + tr("Pulses\n");
			body += ind + tr("  Number of pulses: %1\n").arg(r.num_pulses);
			if (!std::isnan(r.mean_period))
			{
				body += ind + tr("  Mean period: %1 ms\n")
					.arg(r.mean_period * 1000.0, 0, 'f', 3);
			}
			else
			{
				body += ind + tr("  Mean period: (undefined — too few in-range periods)\n");
			}

			body += ind + tr("Voicing\n");
			if (!std::isnan(r.voiced_frame_fraction))
			{
				body += ind + tr("  Fraction of voiced frames: %1 %\n")
					.arg(100.0 * r.voiced_frame_fraction, 0, 'f', 1);
			}
			else
			{
				body += ind + tr("  Fraction of voiced frames: (undefined — pitch tracker produced no frames)\n");
			}

			body += ind + tr("Pitch\n");
			if (!std::isnan(r.mean_f0))
			{
				body += ind + tr("  Mean F0: %1 Hz\n")
					.arg(r.mean_f0, 0, 'f', 1);
			}
			else
			{
				body += ind + tr("  Mean F0: (undefined — no voiced frames)\n");
			}

			body += ind + tr("Jitter\n");
			body += ind + tr("  Local:                %1\n").arg(fmtPercent(r.jitter_local));
			body += ind + tr("  Local, absolute:      %1\n").arg(fmtMicroseconds(r.jitter_local_abs));
			body += ind + tr("  RAP:                  %1\n").arg(fmtPercent(r.jitter_rap));
			body += ind + tr("  PPQ5:                 %1\n").arg(fmtPercent(r.jitter_ppq5));
			body += ind + tr("  DDP:                  %1\n").arg(fmtPercent(r.jitter_ddp));

			body += ind + tr("Shimmer\n");
			body += ind + tr("  Local:                %1\n").arg(fmtPercent(r.shimmer_local));
			body += ind + tr("  Local, in dB:         %1\n").arg(fmtDb(r.shimmer_local_db));
			body += ind + tr("  APQ3:                 %1\n").arg(fmtPercent(r.shimmer_apq3));
			body += ind + tr("  APQ5:                 %1\n").arg(fmtPercent(r.shimmer_apq5));
			body += ind + tr("  APQ11:                %1\n").arg(fmtPercent(r.shimmer_apq11));

			body += ind + tr("Harmonics-to-noise ratio (mean over voiced frames):\n");
			body += ind + tr("  HNR:                  %1\n").arg(fmtDb(r.hnr));
			body += QStringLiteral("\n");
		}

		QApplication::restoreOverrideCursor();
	}
	catch (std::exception &e)
	{
		QApplication::restoreOverrideCursor();
		QMessageBox::critical(this, tr("Measurement error"), QString::fromUtf8(e.what()));
		return;
	}

	// Inlined writeToOutput: that helper is defined later in the file, so
	// calling it from here would be a forward-reference error. The body
	// is the same: trim trailing newlines and forward to the output panel.
	while (body.endsWith('\n'))
		body.chop(1);
	if (auto *output = OutputPanel::instance())
		output->appendResult(heading, body);
}

void SoundView::onFormantSettings()
{
	// If a dialog is already open, raise it instead of creating a second one.
	// This matches the expected UX for a persistent tool palette: one panel
	// per SoundView, reused across multiple "Formant settings..." invocations.
	if (m_formant_settings_dialog) {
		m_formant_settings_dialog->show();
		m_formant_settings_dialog->raise();
		m_formant_settings_dialog->activateWindow();
		return;
	}

	auto *dlg = new FormantSettingsDialog(this);
	dlg->setAttribute(Qt::WA_DeleteOnClose);

	// Each Apply click refreshes the spectrograms. The dialog stays open so
	// the user can tweak values, observe the effect, and tweak again — the
	// whole point of the non-modal design. readFormantSettings() re-reads
	// Settings into the widget's cached members and invalidates the formant
	// cache; update() triggers the repaint.
	connect(dlg, &FormantSettingsDialog::settingsApplied, this, [this]() {
		for (auto *sg : m_spectrograms) {
			sg->readFormantSettings();
			sg->update();
		}
	});

	m_formant_settings_dialog = dlg;
	dlg->show();
}

void SoundView::onTogglePitch(bool checked)
{
	m_show_pitch = checked;
	Settings::set_value("sound_plots", "pitch", checked);
	updatePlotVisibility();
}

void SoundView::onPitchSettings()
{
	if (m_pitch_settings_dialog) {
		m_pitch_settings_dialog->show();
		m_pitch_settings_dialog->raise();
		m_pitch_settings_dialog->activateWindow();
		return;
	}

	auto *dlg = new PitchSettingsDialog(this);
	dlg->setAttribute(Qt::WA_DeleteOnClose);

	connect(dlg, &PitchSettingsDialog::settingsApplied, this, [this]() {
		for (auto *pw : m_pitches) {
			pw->readSettings();
			pw->update();
		}
	});

	m_pitch_settings_dialog = dlg;
	dlg->show();
}

void SoundView::onGoToTime()
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Go to time..."));
	dlg.setMinimumWidth(280);

	auto *layout = new QVBoxLayout(&dlg);
	layout->addWidget(new QLabel(tr("Time (seconds):")));
	auto *edit = new QLineEdit(&dlg);
	layout->addWidget(edit);
	layout->addStretch();

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	connect(edit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);

	if (dlg.exec() != QDialog::Accepted)
		return;

	bool ok;
	double t = edit->text().toDouble(&ok);
	if (!ok)
	{
		QMessageBox::critical(this, tr("Go to time"),
			tr("Invalid time value."));
		return;
	}

	// Clamp to the sound's time range.
	double dur = m_sound->duration();
	if (t < 0) t = 0;
	if (t > dur) t = dur;

	// Place the cursor (point selection) at the requested time.
	m_model->setSelection(t, t);

	// If the point is already inside the current window, we're done.
	if (t >= m_model->windowStart() && t <= m_model->windowEnd())
		return;

	// Otherwise, centre the window on the requested time, keeping the same duration.
	double win_dur = m_model->windowDuration();
	double new_start = t - win_dur / 2.0;
	double new_end = t + win_dur / 2.0;

	// Shift the window so it stays within the sound boundaries.
	if (new_start < 0) {
		new_end -= new_start; // shift right
		new_start = 0;
	}
	if (new_end > dur) {
		new_start -= (new_end - dur); // shift left
		new_end = dur;
	}
	if (new_start < 0) new_start = 0;

	m_model->setViewport(new_start, new_end);
}

void SoundView::onSelectWindow()
{
	QDialog dlg(this);
	dlg.setWindowTitle(tr("Set selection window..."));
	dlg.setMinimumWidth(300);

	auto *layout = new QVBoxLayout(&dlg);

	layout->addWidget(new QLabel(tr("From (seconds):")));
	auto *from_edit = new QLineEdit(&dlg);
	layout->addWidget(from_edit);

	layout->addWidget(new QLabel(tr("To (seconds):")));
	auto *to_edit = new QLineEdit(&dlg);
	layout->addWidget(to_edit);

	layout->addStretch();

	auto *button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	layout->addWidget(button_box);

	connect(button_box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(button_box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

	// Allow pressing Enter to accept.
	connect(from_edit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);
	connect(to_edit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);

	if (dlg.exec() == QDialog::Accepted)
	{
		bool ok1, ok2;
		double from = from_edit->text().toDouble(&ok1);
		double to = to_edit->text().toDouble(&ok2);

		if (!ok1 || !ok2)
		{
			QMessageBox::critical(this, tr("Selection error"),
				tr("Could not parse the time values."));
			return;
		}

		if (from < 0) from = 0;
		if (to > m_sound->duration()) to = m_sound->duration();

		if (from >= to)
		{
			QMessageBox::critical(this, tr("Selection error"),
				tr("The start time must be less than the end time."));
			return;
		}

		m_model->setViewport(from, to);
	}
}


// ─────────────────────────────────────────────────
//  Measurements: helpers
// ─────────────────────────────────────────────────

static void writeToOutput(const QString &heading, QString body)
{
	while (body.endsWith('\n'))
		body.chop(1);
	auto *output = OutputPanel::instance();
	if (output)
		output->appendResult(heading, body);
}

static void writeError(QWidget *parent, const QString &msg)
{
	QMessageBox::critical(parent, QObject::tr("Measurement error"), msg);
}

// Read the global Hz decimal-places setting (0 = round to nearest Hz).
static int hzDecimals()
{
	try { return Settings::get_int("display", "hz_decimals"); }
	catch (...) { return 0; }
}


// ─────────────────────────────────────────────────
//  Measurements: Formants
// ─────────────────────────────────────────────────

void SoundView::onGetFormants()
{
	if (!m_model->hasSelection())
	{
		QMessageBox::warning(this, tr("Cannot measure formants"),
			tr("First select a point or a portion of the signal."));
		return;
	}

	double t;
	if (m_model->hasSpanSelection())
		t = (m_model->selectionStart() + m_model->selectionEnd()) / 2.0;
	else
		t = m_model->selectionStart();

	try
	{
		String category("formants");
		int nformant = (int) Settings::get_number(category, "number_of_formants");
		double nyquist = Settings::get_number(category, "max_frequency");
		double win_size = Settings::get_number(category, "window_size");
		int lpc_order = (int) Settings::get_number(category, "lpc_order");

		QString heading = tr("Formants at %1 s").arg(t, 0, 'f', 4);
		QString body;

		for (int ch : m_visible_channels)
		{
			auto formants = m_sound->get_formants(ch, t, nformant, nyquist, win_size, lpc_order);

			if (!m_sound->is_mono())
				body += tr("  Channel %1:\n").arg(ch);

			for (int i = 0; i < nformant; i++)
			{
				double freq = formants(i, 0);
				double bw = formants(i, 1);
				QString indent = m_sound->is_mono() ? "" :  "  ";
				if (freq > 0)
					body += tr("%1F%2 = %3 Hz  (bandwidth = %4 Hz)\n").arg(indent).arg(i + 1).arg(freq, 0, 'f', hzDecimals()).arg(bw, 0, 'f', hzDecimals());
				else
					body += tr("%1F%2 = undefined\n").arg(indent).arg(i + 1);
			}
		}

		writeToOutput(heading, body);
	}
	catch (std::exception &e)
	{
		writeError(this, QString::fromUtf8(e.what()));
	}
}

void SoundView::onGetMeanFormants()
{
	double t1, t2;
	if (m_model->hasSpanSelection())
	{
		t1 = m_model->selectionStart();
		t2 = m_model->selectionEnd();
	}
	else
	{
		t1 = m_model->windowStart();
		t2 = m_model->windowEnd();
	}

	try
	{
		String category("formants");
		int nformant = (int) Settings::get_number(category, "number_of_formants");
		double nyquist = Settings::get_number(category, "max_frequency");
		double win_size = Settings::get_number(category, "window_size");
		int lpc_order = (int) Settings::get_number(category, "lpc_order");
		double time_step = Settings::get_number(category, "time_step");

		// Build a set of time points across the interval.
		Array<double> times;
		for (double t = t1; t <= t2; t += time_step)
			times.append(t);
		if (times.empty())
			times.append((t1 + t2) / 2.0);

		QString heading = tr("Mean formants (%1–%2 s)").arg(t1, 0, 'f', 4).arg(t2, 0, 'f', 4);
		QString body;

		for (int ch : m_visible_channels)
		{
			auto formants = m_sound->get_formants(ch, times, nformant, nyquist, win_size, lpc_order);

			if (!m_sound->is_mono())
				body += tr("  Channel %1:\n").arg(ch);

			for (int i = 0; i < nformant; i++)
			{
				double freq = formants(i, 0);
				double bw = formants(i, 1);
				QString indent = m_sound->is_mono() ? "  " : "    ";
				if (freq > 0)
					body += tr("%1F%2 = %3 Hz  (bandwidth = %4 Hz)\n").arg(indent).arg(i + 1).arg(freq, 0, 'f', hzDecimals()).arg(bw, 0, 'f', hzDecimals());
				else
					body += tr("%1F%2 = undefined\n").arg(indent).arg(i + 1);
			}
		}

		writeToOutput(heading, body);
	}
	catch (std::exception &e)
	{
		writeError(this, QString::fromUtf8(e.what()));
	}
}


// ─────────────────────────────────────────────────
//  Measurements: Pitch
// ─────────────────────────────────────────────────

void SoundView::onGetPitch()
{
	if (!m_model->hasSelection())
	{
		QMessageBox::warning(this, tr("Cannot measure pitch"),
			tr("First select a point or a portion of the signal."));
		return;
	}

	double t;
	if (m_model->hasSpanSelection())
		t = (m_model->selectionStart() + m_model->selectionEnd()) / 2.0;
	else
		t = m_model->selectionStart();

	try
	{
		String category("pitch_tracking");
		auto method_str = Settings::get_string(category, "method");
		auto method = Sound::get_pitch_tracker(method_str);
		double min_pitch = Settings::get_number(category, "minimum_pitch");
		double max_pitch = Settings::get_number(category, "maximum_pitch");
		double threshold = Settings::get_number(category, "voicing_threshold");

		QString heading = tr("Pitch at %1 s").arg(t, 0, 'f', 4);
		QString body;

		for (int ch : m_visible_channels)
		{
			double f0 = m_sound->get_pitch(ch, method, t, min_pitch, max_pitch, threshold);
			QString value = (f0 > 0) ? tr("%1 Hz").arg(f0, 0, 'f', hzDecimals()) : tr("undefined");

			if (m_sound->is_mono())
				body += tr("%1").arg(value);
			else
				body += tr("  Channel %1: %2\n").arg(ch).arg(value);
		}

		writeToOutput(heading, body);
	}
	catch (std::exception &e)
	{
		writeError(this, QString::fromUtf8(e.what()));
	}
}

void SoundView::onGetMeanPitch()
{
	double t1, t2;
	if (m_model->hasSpanSelection())
	{
		t1 = m_model->selectionStart();
		t2 = m_model->selectionEnd();
	}
	else
	{
		t1 = m_model->windowStart();
		t2 = m_model->windowEnd();
	}

	try
	{
		String category("pitch_tracking");
		auto method_str = Settings::get_string(category, "method");
		auto method = Sound::get_pitch_tracker(method_str);
		double min_pitch = Settings::get_number(category, "minimum_pitch");
		double max_pitch = Settings::get_number(category, "maximum_pitch");
		double threshold = Settings::get_number(category, "voicing_threshold");

		QString heading = tr("Mean pitch (%1–%2 s)").arg(t1, 0, 'f', 4).arg(t2, 0, 'f', 4);
		QString body;

		for (int ch : m_visible_channels)
		{
			double f0 = m_sound->get_mean_pitch(ch, method, t1, t2, min_pitch, max_pitch, threshold);
			QString value = (f0 > 0) ? tr("%1 Hz").arg(f0, 0, 'f', hzDecimals()) : tr("undefined");

			if (m_sound->is_mono())
				body += tr("%1").arg(value);
			else
				body += tr("  Channel %1: %2\n").arg(ch).arg(value);
		}

		writeToOutput(heading, body);
	}
	catch (std::exception &e)
	{
		writeError(this, QString::fromUtf8(e.what()));
	}
}


// ─────────────────────────────────────────────────
//  Measurements: Intensity
// ─────────────────────────────────────────────────

void SoundView::onGetIntensity()
{
	if (!m_model->hasSelection())
	{
		QMessageBox::warning(this, tr("Cannot measure intensity"),
			tr("First select a point or a portion of the signal."));
		return;
	}

	double t;
	if (m_model->hasSpanSelection())
		t = (m_model->selectionStart() + m_model->selectionEnd()) / 2.0;
	else
		t = m_model->selectionStart();

	try
	{
		QString heading = tr("Intensity at %1 s").arg(t, 0, 'f', 4);
		QString body;

		for (int ch : m_visible_channels)
		{
			double dB = m_sound->get_intensity(ch, t);
			QString value = tr("%1 dB").arg(dB, 0, 'f', 1);

			if (m_sound->is_mono())
				body += tr("%1").arg(value);
			else
				body += tr("  Channel %1: %2\n").arg(ch).arg(value);
		}

		writeToOutput(heading, body);
	}
	catch (std::exception &e)
	{
		writeError(this, QString::fromUtf8(e.what()));
	}
}

void SoundView::onGetMeanIntensity()
{
	double t1, t2;
	if (m_model->hasSpanSelection())
	{
		t1 = m_model->selectionStart();
		t2 = m_model->selectionEnd();
	}
	else
	{
		t1 = m_model->windowStart();
		t2 = m_model->windowEnd();
	}

	try
	{
		QString heading = tr("Mean intensity (%1–%2 s)").arg(t1, 0, 'f', 4).arg(t2, 0, 'f', 4);
		QString body;

		for (int ch : m_visible_channels)
		{
			double dB = m_sound->get_mean_intensity(ch, t1, t2);
			QString value = tr("%1 dB").arg(dB, 0, 'f', 1);

			if (m_sound->is_mono())
				body += tr("%1").arg(value);
			else
				body += tr("  Channel %1: %2\n").arg(ch).arg(value);
		}

		writeToOutput(heading, body);
	}
	catch (std::exception &e)
	{
		writeError(this, QString::fromUtf8(e.what()));
	}
}

void SoundView::onViewSpectralMoments()
{
	using namespace speech;

	if (!m_model->hasSelection())
	{
		QMessageBox::warning(this, tr("Spectral moments"),
			tr("Please select a portion of the signal or place the cursor first."));
		return;
	}

	double t1, t2;

	if (m_model->hasSpanSelection())
	{
		t1 = m_model->selectionStart();
		t2 = m_model->selectionEnd();
	}
	else
	{
		double cursor = m_model->selectionStart();

		double default_dur = 0.025;

		QDialog dlg(this);
		dlg.setWindowTitle(tr("Spectral moments — window duration"));
		dlg.setMinimumWidth(280);

		auto *layout = new QVBoxLayout(&dlg);
		layout->addWidget(new QLabel(
			tr("Window duration (seconds), centered at %1 s:").arg(cursor, 0, 'f', 4)));
		auto *edit = new QLineEdit(QString::number(default_dur, 'f', 4), &dlg);
		layout->addWidget(edit);
		layout->addStretch();
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
		layout->addWidget(buttons);
		connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
		connect(edit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);

		if (dlg.exec() != QDialog::Accepted)
			return;

		bool ok;
		double dur = edit->text().toDouble(&ok);
		if (!ok || dur <= 0)
		{
			QMessageBox::critical(this, tr("Spectral moments"),
				tr("Invalid window duration."));
			return;
		}

		double half = dur / 2.0;
		t1 = cursor - half;
		t2 = cursor + half;

		if (t1 < 0) { t2 -= t1; t1 = 0; }
		if (t2 > m_sound->duration()) { t1 -= (t2 - m_sound->duration()); t2 = m_sound->duration(); }
		if (t1 < 0) t1 = 0;
	}

	// Read pre-emphasis from spectrogram settings.
	double preemph = 50.0;
	WindowType window_type = WindowType::Hamming;
	double max_freq = 0.0;

	try
	{
		String category("spectrogram");
		preemph = Settings::get_number(category, "preemphasis_threshold");
		max_freq = Settings::get_number(category, "frequency_range");

		String win = Settings::get_string(category, "window_type");
		if (win == "Bartlett")        window_type = WindowType::Bartlett;
		else if (win == "Blackman")   window_type = WindowType::Blackman;
		else if (win == "Gaussian")   window_type = WindowType::Gaussian;
		else if (win == "Hamming")    window_type = WindowType::Hamming;
		else if (win == "Hann")       window_type = WindowType::Hann;
		else if (win == "Rectangular") window_type = WindowType::Rectangular;
	}
	catch (...) {}

	int channel = 0;
	if (!m_show_average && !m_visible_channels.empty())
		channel = m_visible_channels.front();

	try
	{
		double duration = t2 - t1;
		double center = (t1 + t2) / 2.0;
		auto sm = compute_spectral_moments_at(m_sound, channel, center,
			duration, window_type, 0.0, max_freq, preemph);

		QString heading = tr("Spectral moments at %1 s (window %2 s)")
			.arg(center, 0, 'f', 4).arg(duration, 0, 'f', 4);
		QString body;

		if (std::isfinite(sm.cog))
			body += tr("  COG:      %1 Hz\n").arg(sm.cog, 0, 'f', 1);
		if (std::isfinite(sm.spread))
			body += tr("  Spread:   %1 Hz\n").arg(sm.spread, 0, 'f', 1);
		if (std::isfinite(sm.skewness))
			body += tr("  Skewness: %1\n").arg(sm.skewness, 0, 'f', 4);
		if (std::isfinite(sm.kurtosis))
			body += tr("  Kurtosis: %1\n").arg(sm.kurtosis, 0, 'f', 4);

		writeToOutput(heading, body);
	}
	catch (std::exception &e)
	{
		writeError(this, QString::fromUtf8(e.what()));
	}
}

void SoundView::updatePlotVisibility()
{
	// Helper: set widget and its trailing separator line to the same visibility.
	auto setVisible = [](QWidget *widget, QFrame *line, bool visible)
	{
		widget->setVisible(visible);
		line->setVisible(visible);
	};

	// Waveforms + lines: honour both m_show_waveform and channel visibility.
	setVisible(m_waveforms[0], m_wave_lines[0], m_show_waveform && m_show_average);
	for (int c = 1; c <= m_sound->nchannel(); c++)
	{
		bool ch_visible = std::find(m_visible_channels.begin(),
			m_visible_channels.end(), c) != m_visible_channels.end();
		setVisible(m_waveforms[c], m_wave_lines[c], m_show_waveform && ch_visible);
	}

	// Spectrograms + lines.
	setVisible(m_spectrograms[0], m_spectrogram_lines[0], m_show_spectrogram && m_show_average);
	for (int c = 1; c <= m_sound->nchannel(); c++)
	{
		bool ch_visible = std::find(m_visible_channels.begin(),
			m_visible_channels.end(), c) != m_visible_channels.end();
		setVisible(m_spectrograms[c], m_spectrogram_lines[c], m_show_spectrogram && ch_visible);
	}

	// Pitch tracks + lines.
	setVisible(m_pitches[0], m_pitch_lines[0], m_show_pitch && m_show_average);
	for (int c = 1; c <= m_sound->nchannel(); c++)
	{
		bool ch_visible = std::find(m_visible_channels.begin(),
			m_visible_channels.end(), c) != m_visible_channels.end();
		setVisible(m_pitches[c], m_pitch_lines[c], m_show_pitch && ch_visible);
	}

	// Intensity tracks + lines.
	setVisible(m_intensities[0], m_intensity_lines[0], m_show_intensity && m_show_average);
	for (int c = 1; c <= m_sound->nchannel(); c++)
	{
		bool ch_visible = std::find(m_visible_channels.begin(),
			m_visible_channels.end(), c) != m_visible_channels.end();
		setVisible(m_intensities[c], m_intensity_lines[c], m_show_intensity && ch_visible);
	}

	// Formants are overlaid on spectrograms, not separate widgets.
	for (auto *sg : m_spectrograms)
		sg->setShowFormants(m_show_formants);

	// Glottal pulses are overlaid on waveforms, not separate widgets.
	for (auto *wf : m_waveforms)
		wf->setShowGlottalPulses(m_show_glottal_pulses);

	m_y_axis->update();
	updateTopPlot();
}

void SoundView::updateTopPlot()
{
	// Clear the top flag on all plots.
	for (auto *wf : m_waveforms) wf->setTopPlot(false);
	for (auto *sg : m_spectrograms) sg->setTopPlot(false);
	for (auto *pw : m_pitches) pw->setTopPlot(false);
	for (auto *iw : m_intensities) iw->setTopPlot(false);

	// The top plot is the first non-hidden widget in layout order:
	// waveforms → spectrograms → pitches → intensities.
	// We use !isHidden() rather than isVisible() because the latter
	// returns false during construction when the parent window has
	// not been shown yet.
	for (auto *wf : m_waveforms) {
		if (!wf->isHidden()) { wf->setTopPlot(true); return; }
	}
	for (auto *sg : m_spectrograms) {
		if (!sg->isHidden()) { sg->setTopPlot(true); return; }
	}
	for (auto *pw : m_pitches) {
		if (!pw->isHidden()) { pw->setTopPlot(true); return; }
	}
	for (auto *iw : m_intensities) {
		if (!iw->isHidden()) { iw->setTopPlot(true); return; }
	}
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
	// Clear annotation event info — it will be re-set by AnnotationView::onEventSelected
	// if the selection originated from an event click.
	m_annot_status_text.clear();
	updateStatusText();
}

void SoundView::onSelectionCleared()
{
	m_zoom_sel_action->setEnabled(false);
	m_play_sel_action->setEnabled(false);
	m_annot_status_text.clear();
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
