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
 * Created: 05/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QFontDatabase>
#include <QStringList>
#include <phon/application/settings.hpp>
#include <phon/gui/font_helpers.hpp>

namespace phonometrica {

// Return a monospace QFont that exists on the current platform.
// Tries the user's saved preference first, then platform defaults.
QFont defaultMonoFont(int pointSize)
{
#if defined(Q_OS_MACOS)
    const int platformSize = 13;
    const QStringList candidates = { "Menlo", "Monaco", "Courier New" };
#elif defined(Q_OS_WIN)
    const int platformSize = 10;
    const QStringList candidates = { "Consolas", "Courier New" };
#else
    const int platformSize = 12;
    const QStringList candidates = { "Monospace", "DejaVu Sans Mono",
                                     "Liberation Mono", "Courier New" };
#endif

    if (pointSize <= 0)
        pointSize = platformSize;

    // Try the user's saved preference.
    try {
        auto name = Settings::get_string("font", "name");
        auto savedFamily = QString::fromUtf8(name.data(), (int) name.size());
        if (QFontDatabase::hasFamily(savedFamily))
        {
            auto size = (int) Settings::get_number("font", "size");
            if (size > 0 && pointSize == platformSize)
                pointSize = size;
            return QFont(savedFamily, pointSize);
        }
    } catch (...) {
        // No saved preference or bad value — fall through.
    }

    // Try platform defaults.
    for (const auto &family : candidates)
    {
        if (QFontDatabase::hasFamily(family))
            return QFont(family, pointSize);
    }

    // Ultimate fallback: let Qt pick any monospace font.
    QFont f;
    f.setStyleHint(QFont::Monospace);
    f.setFamily(QStringLiteral("monospace"));
    f.setPointSize(pointSize);
    return f;
}

// Try a specific family first (e.g. from a combo box selection),
// fall back to the platform default.
QFont monoFont(const QString &preferredFamily, int pointSize)
{
    if (!preferredFamily.isEmpty() && QFontDatabase::hasFamily(preferredFamily))
    {
        if (pointSize <= 0)
            pointSize = defaultMonoFont().pointSize();
        return QFont(preferredFamily, pointSize);
    }
    return defaultMonoFont(pointSize);
}

} // namespace phonometrica