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
 * Created: 25/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: In-app help dispatcher. Resolves the location of the bundled Sphinx HTML                                   *
 *          documentation at runtime and hands the URL off to the system's default                                     *
 *          browser via QDesktopServices. The HTML is shipped on disk alongside the                                    *
 *          executable (Windows / build dir), in Contents/Resources/docs (macOS .app                                   *
 *          bundle) or in <prefix>/share/phonometrica/docs (Linux install).                                            *
 *                                                                                                                     *
 *          Public API is unchanged from the previous QTextBrowser-based version:                                      *
 *          call HelpBrowser::showPage("sound") from anywhere. Empty anchor opens                                      *
 *          the index page.                                                                                            *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_HELP_BROWSER_HPP
#define PHONOMETRICA_HELP_BROWSER_HPP

#include <QString>

class QWidget;

namespace phonometrica {

class HelpBrowser
{
public:

	// Open the help page identified by `anchor` in the user's default browser.
	// `anchor` is a Sphinx page name relative to the doc root, without extension.
	// Examples: "sound", "scripting/index", "intro/install".
	// An empty anchor opens the top-level index page.
	// `parent` is used only as the parent of the warning dialog shown when the
	// documentation cannot be located on disk.
	static void showPage(const QString &anchor = {}, QWidget *parent = nullptr);

private:

	// Resolve the absolute path of the docs root directory by trying a small set
	// of platform-specific candidates relative to applicationDirPath(). Returns
	// an empty string if no candidate is a readable directory.
	static QString resolveDocsRoot();
};

} // namespace phonometrica

#endif // PHONOMETRICA_HELP_BROWSER_HPP
