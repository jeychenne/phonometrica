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
 * Created: 12/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Welcome / start view displayed in the first tab when no document is open.                                  *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_START_VIEW_HPP
#define PHONOMETRICA_START_VIEW_HPP

#include <QWidget>
#include <QVBoxLayout>
#include <phon/string.hpp>

namespace phonometrica {

class StartView : public QWidget
{
	Q_OBJECT

public:

	explicit StartView(QWidget *parent = nullptr);

	// Rebuild the recent-projects list (call after a project is opened/closed).
	void refreshRecentProjects();

protected:

	void showEvent(QShowEvent *event) override;

signals:

	void openProjectRequested();
	void addFilesRequested();
	void newScriptRequested();
	void newAnnotationRequested();
	void documentationRequested();
	void recentProjectRequested(const String &path);

private:

	// Layout container for the recent-project entries.
	QVBoxLayout *m_recent_layout = nullptr;

	// Label shown above the recent list (hidden when empty).
	QWidget *m_recent_section = nullptr;
};

} // namespace phonometrica

#endif // PHONOMETRICA_START_VIEW_HPP
