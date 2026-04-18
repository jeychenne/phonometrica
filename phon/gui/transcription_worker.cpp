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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/gui/transcription_worker.hpp>

namespace phonometrica {

TranscriptionWorker::TranscriptionWorker(Handle<Sound> sound, Transcriber::Options opts, QObject *parent) :
	QObject(parent),
	m_sound(std::move(sound)),
	m_opts(std::move(opts))
{
}

void TranscriptionWorker::cancel()
{
	m_cancel.store(true, std::memory_order_relaxed);
}

void TranscriptionWorker::run()
{
	try
	{
		Transcriber transcriber;

		m_layer = transcriber.transcribe(*m_sound, m_opts,
			[this](int percent, const String &) -> bool {
				// Report progress to the GUI thread via queued signal.
				emit progress(percent);
				return !m_cancel.load(std::memory_order_relaxed);
			});

		m_succeeded = true;
	}
	catch (std::exception &e)
	{
		m_succeeded = false;
		m_error = QString::fromUtf8(e.what());
	}

	emit finished();
}

} // namespace phonometrica
