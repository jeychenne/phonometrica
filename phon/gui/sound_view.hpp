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
#include <QLabel>
#include <QTimer>
#include <phon/gui/view.hpp>
#include <phon/gui/time_model.hpp>
#include <phon/application/sound.hpp>

namespace phonometrica {

class AudioPlayer;
class WaveformWidget;
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

	Handle<Sound> sound() const { return m_sound; }
	TimeModel *timeModel() const { return m_model; }

protected:

	// Subclass hook: AnnotationView will add annotation layers here.
	virtual void addAnnotationLayers(QLayout *layout) {}

private slots:

	void onViewAll();
	void onZoomIn();
	void onZoomOut();
	void onZoomToSelection();
	void onMoveForward();
	void onMoveBackward();
	void onToggleMouseTracking(bool checked);
	void onSelectionChanged(double t1, double t2);
	void onSelectionCleared();
	void onViewportChanged(double start, double end);

	// Playback
	void onPlayWindow();
	void onPlaySelection();
	void onStop();
	void onPlaybackTick();

private:

	void setupUi();
	void createToolBar();
	void createWaveforms(QLayout *layout);
	void updateStatusText();
	void startPlayback(double from, double to);
	void stopPlayback();

	Handle<Sound> m_sound;
	TimeModel *m_model;

	QToolBar *m_toolbar = nullptr;
	QLabel *m_status_label = nullptr;

	WaveBar *m_wavebar = nullptr;
	SoundZoom *m_zoom = nullptr;

	// Channel 0 = average, channels 1..N = individual.
	std::vector<WaveformWidget *> m_waveforms;

	// Track which channels are visible.
	std::vector<int> m_visible_channels;
	bool m_show_average = false;

	QAction *m_zoom_sel_action = nullptr;
	QAction *m_play_sel_action = nullptr;
	QAction *m_play_action = nullptr;

	// Audio playback
	std::unique_ptr<AudioPlayer> m_player;
	QTimer *m_playback_timer = nullptr;
	bool m_was_playing = false;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SOUND_VIEW_HPP
