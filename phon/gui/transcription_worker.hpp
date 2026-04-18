/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2026 Julien Eychenne                                                                                  *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more        *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 17/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Background worker that runs Transcriber::transcribe() off the GUI thread. Intended to be moved to a        *
 * QThread. Progress is reported via a signal; the result Layer is held in the worker and read by the owner only       *
 * after the thread has been joined (wait()).                                                                          *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_TRANSCRIPTION_WORKER_HPP
#define PHONOMETRICA_TRANSCRIPTION_WORKER_HPP

#include <atomic>
#include <QObject>
#include <QString>
#include <phon/application/sound.hpp>
#include <phon/application/annotation_data.hpp>
#include <phon/application/transcriber.hpp>

namespace phonometrica {

class TranscriptionWorker final : public QObject
{
	Q_OBJECT

public:

	// The worker captures the sound handle and options; it will run on whichever thread
	// it has been moved to when run() is invoked.
	TranscriptionWorker(Handle<Sound> sound, Transcriber::Options opts, QObject *parent = nullptr);

	// Safe to call from any thread. Causes the whisper callback to return cancellation,
	// after which run() will exit with an error message.
	void cancel();

	// Accessors — valid only after the worker's owning thread has finished (wait()).
	bool succeeded() const { return m_succeeded; }
	const Layer &result() const { return m_layer; }
	const QString &errorMessage() const { return m_error; }

public slots:

	// Performs the transcription. Emits progress() during inference, then finished() exactly once.
	void run();

signals:

	// Emitted from the worker thread. 0..100 percent.
	void progress(int percent);

	// Emitted exactly once, from the worker thread, when run() returns.
	void finished();

private:

	// Concurrency note on m_sound:
	//   Handle<T> uses a non-atomic int32_t refcount (see Object::ref_count). This is safe here
	//   because every refcount mutation happens on the GUI thread:
	//     - The Handle is copied into the worker at construction (GUI thread: +1).
	//     - run() only dereferences m_sound (*m_sound, m_sound->...); no refcount touches.
	//     - The worker is destroyed after QThread::wait() returns, back on the GUI thread (-1).
	//   If a future refactor captures m_sound into a callable that runs on the worker thread, or
	//   releases the handle from run(), the refcount becomes racy. Keep Sound access read-only
	//   from the worker thread.

	Handle<Sound>        m_sound;
	Transcriber::Options m_opts;
	std::atomic<bool>    m_cancel{false};

	// Results (written only from run(), read only after join).
	Layer   m_layer;
	QString m_error;
	bool    m_succeeded = false;
};

} // namespace phonometrica

#endif // PHONOMETRICA_TRANSCRIPTION_WORKER_HPP
