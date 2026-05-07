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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMessageBox>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <phon/gui/help_browser.hpp>

namespace phonometrica {

QString HelpBrowser::resolveDocsRoot()
{
	const QString appDir = QCoreApplication::applicationDirPath();
	QStringList candidates;

#if defined(Q_OS_MAC)
	// Inside the .app bundle, applicationDirPath() is Phonometrica.app/Contents/MacOS,
	// and the Sphinx output is copied into Contents/Resources/docs at build time.
	candidates << appDir + QStringLiteral("/../Resources/docs");
#elif defined(Q_OS_WIN)
	// Inno Setup ships the docs in a subdirectory next to phonometrica.exe.
	candidates << appDir + QStringLiteral("/docs");
#else
	// Linux installed via the .deb (CMAKE_INSTALL_PREFIX=/usr/local):
	//   /usr/local/bin/phonometrica           -> applicationDirPath()
	//   /usr/local/share/phonometrica/docs/   -> docs root
	candidates << appDir + QStringLiteral("/../share/phonometrica/docs");
#endif

	// Final fallback for running directly from the build directory on any
	// platform: the POST_BUILD step copies the Sphinx output to <build>/docs.
	candidates << appDir + QStringLiteral("/docs");

	for (const QString &path : std::as_const(candidates))
	{
		QFileInfo fi(path);
		if (fi.exists() && fi.isDir())
		{
			return fi.canonicalFilePath();
		}
	}

	return {};
}


void HelpBrowser::showPage(const QString &anchor, QWidget *parent)
{
	const QString root = resolveDocsRoot();
	if (root.isEmpty())
	{
		QMessageBox::information(parent, QObject::tr("Help"),
			QObject::tr("The documentation could not be found on disk.\n\n"
			            "If you are running Phonometrica from a build directory, "
			            "make sure the project was configured with PHON_BUILD_DOCS=ON "
			            "and that sphinx-build is installed (pip install sphinx).\n\n"
			            "If you are running an installed copy, please report this as a "
			            "packaging bug."));
		return;
	}

	// Accept either "sound" or "sound.html" from callers — normalise so we never
	// produce something like "sound.html.html". An empty anchor means the index
	// landing page.
	QString page;
	if (anchor.isEmpty())
	{
		page = QStringLiteral("index.html");
	}
	else if (anchor.endsWith(QLatin1String(".html"), Qt::CaseInsensitive))
	{
		page = anchor;
	}
	else
	{
		page = anchor + QStringLiteral(".html");
	}

	QString fullPath = root + QLatin1Char('/') + page;
	QFileInfo fi(fullPath);

	// If the requested page cannot be found, fall back to the index rather than
	// silently failing — a missing anchor is almost always a stale call site
	// and showing the index is more useful than a broken file:// URL.
	if (!fi.exists())
	{
		fullPath = root + QStringLiteral("/index.html");
		fi.setFile(fullPath);
		if (!fi.exists())
		{
			QMessageBox::information(parent, QObject::tr("Help"),
				QObject::tr("The documentation directory was found, but the index "
				            "page is missing. Please reinstall Phonometrica or "
				            "rebuild the documentation."));
			return;
		}
	}

	const QUrl url = QUrl::fromLocalFile(fi.canonicalFilePath());
	if (!QDesktopServices::openUrl(url))
	{
		QMessageBox::warning(parent, QObject::tr("Help"),
			QObject::tr("Could not launch the default web browser to display:\n%1")
				.arg(url.toLocalFile()));
	}
}

} // namespace phonometrica
