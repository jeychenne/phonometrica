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
 * Created: 09/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Thin wrappers around QFileDialog static methods that automatically read and update the                     *
 *          "last_directory" application setting so that every file/directory chooser in the GUI                       *
 *          remembers the most-recently visited location for the duration of the session.                              *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FILE_DIALOG_HPP
#define PHONOMETRICA_FILE_DIALOG_HPP

#include <QFileDialog>
#include <QStringList>
#include <QWidget>
#include <phon/application/settings.hpp>
#include <phon/string.hpp>

namespace phonometrica {

// Returns the last-used directory as a QString (empty string if not set).
inline QString lastDirectory()
{
    try
    {
        auto dir = Settings::get_last_directory();
        if (!dir.empty())
            return QString::fromUtf8(dir.data(), (int) dir.size());
    }
    catch (...) {}
    return QString();
}

// Persists a file or directory path so that its parent directory becomes the
// next starting point.  Passing an empty string is a no-op.
inline void setLastDirectory(const QString &path)
{
    if (!path.isEmpty())
    {
        auto bytes = path.toUtf8();
        Settings::set_last_directory(String(bytes.constData(), bytes.size()));
    }
}

// ── Wrappers ──────────────────────────────────────────────────────────────────

inline QString getOpenFileName(QWidget *parent,
                               const QString &caption,
                               const QString &filter = QString())
{
    auto path = QFileDialog::getOpenFileName(parent, caption, lastDirectory(), filter);
    setLastDirectory(path);
    return path;
}

inline QStringList getOpenFileNames(QWidget *parent,
                                    const QString &caption,
                                    const QString &filter = QString())
{
    auto paths = QFileDialog::getOpenFileNames(parent, caption, lastDirectory(), filter);
    if (!paths.isEmpty())
        setLastDirectory(paths.first());
    return paths;
}

// filter      – the file-type filter string, e.g. tr("CSV files (*.csv)")
// defaultName – optional filename suggestion (leaf name only), e.g. "untitled.phon"
//               Combined with lastDirectory() so the dialog opens in the right
//               folder and pre-fills the filename.
inline QString getSaveFileName(QWidget *parent,
                               const QString &caption,
                               const QString &filter = QString(),
                               const QString &defaultName = QString())
{
    QString dir = lastDirectory();
    QString startPath;
    if (!defaultName.isEmpty())
        startPath = dir.isEmpty() ? defaultName : dir + QLatin1Char('/') + defaultName;
    else
        startPath = dir;
    auto path = QFileDialog::getSaveFileName(parent, caption, startPath, filter);
    setLastDirectory(path);
    return path;
}

inline QString getExistingDirectory(QWidget *parent, const QString &caption)
{
    auto dir = QFileDialog::getExistingDirectory(parent, caption, lastDirectory());
    setLastDirectory(dir);
    return dir;
}

} // namespace phonometrica

#endif // PHONOMETRICA_FILE_DIALOG_HPP
