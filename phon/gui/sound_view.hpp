/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: View for sound files. Displays toolbar, waveforms (one per channel + average), and a wavebar for           *
 *          navigation. This is the base upon which AnnotationView will build later.                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SOUND_VIEW_HPP
#define PHONOMETRICA_SOUND_VIEW_HPP

#include <vector>
#include <memory>
#include <QToolBar>
#include <QToolButton>
#include <QLabel>
#include <QTimer>
#include <QMenu>
#include <QActionGroup>
#include <QFrame>
#include <QVBoxLayout>
#include <phon/gui/view.hpp>
#include <phon/gui/time_model.hpp>
#include <phon/gui/time_axis_widget.hpp>
#include <phon/gui/y_axis_widget.hpp>
#include <phon/application/sound.hpp>

namespace phonometrica {

class AudioPlayer;
class WaveformWidget;
class SpectrogramWidget;
class PitchWidget;
class IntensityWidget;
class WaveBar;
class SoundZoom;

class SoundView : public View
{
	Q_OBJECT

public:

	SoundView(const Handle<Sound> &sound, QWidget *parent = nullptr);

	~SoundView() override;

	QString label() const override;
	String path() const override;
	QString helpAnchor() const override { return QStringLiteral("sound"); }

	Handle<Sound> sound() const { return m_sound; }
	TimeModel *timeModel() const { return m_model; }

protected:

	// Subclasses that override addAnnotationLayers/addAnnotationToolbar must pass
	// DeferInit to the SoundView constructor, then call initialize() at the end of
	// their own constructor (after the vtable is fully constructed).
	enum DeferInit { Deferred };
	SoundView(const Handle<Sound> &sound, DeferInit, QWidget *parent = nullptr);

	// Completes the UI setup. Called automatically by the public constructor, or
	// manually by subclasses that used the deferred constructor.
	void initialize();

	// Subclass hook: AnnotationView will add annotation layers here.
	virtual void addAnnotationLayers(QLayout *layout) {}

	// Subclass hook: AnnotationView will add annotation-specific toolbar actions here.
	virtual void addAnnotationToolbar(QToolBar *toolbar) {}

	void keyPressEvent(QKeyEvent *event) override;

private slots:

	void onViewAll();
	void onZoomIn();
	void onZoomOut();
	void onZoomToSelection();
	void onMoveForward();
	void onMoveBackward();
	void onToggleMouseTracking(bool checked);
	void onToggleWaveform(bool checked);
	void onToggleSpectrogram(bool checked);
	void onSpectrogramSettings();
	void onToggleIntensity(bool checked);
	void onIntensitySettings();
	void onToggleFormants(bool checked);
	void onFormantSettings();
	void onTogglePitch(bool checked);
	void onPitchSettings();
	void onSelectWindow();
	void onGetFormants();
	void onGetMeanFormants();
	void onGetPitch();
	void onGetMeanPitch();
	void onGetIntensity();
	void onGetMeanIntensity();
	void onScalingChanged(QAction *action);
	void onSelectionChanged(double t1, double t2);
	void onSelectionCleared();
	void onViewportChanged(double start, double end);
	void onChannelAction(QAction *action);

	// Playback
	void onPlayWindow();
	void onPlaySelection();
	void onStop();
	void onPlaybackTick();

	// Y-axis readout from child widgets
	void onYValueDescription(const QString &text);

private:

	void setupUi();
	void createToolBar();
	void createWaveforms(QVBoxLayout *layout);
	void createSpectrograms(QVBoxLayout *layout);
	void createPitchTracks(QVBoxLayout *layout);
	void createIntensityTracks(QVBoxLayout *layout);
	QFrame *createSeparator();
	void updatePlotVisibility();
	void updateTopPlot();
	void updateStatusText();
	void startPlayback(QAction *source, double from, double to);
	void stopPlayback();

	Handle<Sound> m_sound;
	TimeModel *m_model;

	QToolBar *m_toolbar = nullptr;
	TimeAxisWidget *m_time_axis = nullptr;
	YAxisWidget *m_y_axis = nullptr;
	QLabel *m_status_label = nullptr;

	WaveBar *m_wavebar = nullptr;
	SoundZoom *m_zoom = nullptr;

	// Channel 0 = average, channels 1..N = individual.
	std::vector<WaveformWidget *> m_waveforms;

	// One spectrogram per channel (same indexing as waveforms).
	std::vector<SpectrogramWidget *> m_spectrograms;

	// One pitch track per channel (same indexing as waveforms).
	std::vector<PitchWidget *> m_pitches;

	// One intensity track per channel (same indexing as waveforms).
	std::vector<IntensityWidget *> m_intensities;

	// Separator lines: one per widget (same indexing). Each line follows its widget in the layout.
	std::vector<QFrame *> m_wave_lines;
	std::vector<QFrame *> m_spectrogram_lines;
	std::vector<QFrame *> m_pitch_lines;
	std::vector<QFrame *> m_intensity_lines;

	// Track which channels are visible.
	std::vector<int> m_visible_channels;
	bool m_show_average = false;
	bool m_show_waveform = true;
	bool m_show_spectrogram = false;
	bool m_show_pitch = false;
	bool m_show_intensity = false;
	bool m_show_formants = false;

	QAction *m_zoom_sel_action = nullptr;
	QAction *m_play_sel_action = nullptr;
	QAction *m_play_action = nullptr;

	// Waveform menu: show toggle + channel actions.
	QAction *m_show_wave_action = nullptr;

	// Spectrogram menu: show toggle.
	QAction *m_show_spectrogram_action = nullptr;

	// Intensity menu: show toggle.
	QAction *m_show_intensity_action = nullptr;

	// Pitch menu: show toggle.
	QAction *m_show_pitch_action = nullptr;

	// Formant menu: show toggle.
	QAction *m_show_formants_action = nullptr;

	// Audio playback
	std::unique_ptr<AudioPlayer> m_player;
	QTimer *m_playback_timer = nullptr;
	bool m_was_playing = false;
	QAction *m_active_play_action = nullptr;

	// Y-axis readout text from the widget under the cursor.
	QString m_y_value_text;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SOUND_VIEW_HPP
