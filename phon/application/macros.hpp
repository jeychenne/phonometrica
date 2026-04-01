/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 13/01/2021                                                                                                 *
 *                                                                                                                     *
 * purpose: A bunch of macros related to the user interface.                                                           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_MACROS_HPP
#define PHONOMETRICA_MACROS_HPP

// Control key as displayed in tooltips
#if PHON_MACOS
#define CTRL_KEY "⌘"
#else
#define CTRL_KEY "ctrl+"
#endif

// Return key symbol
#if PHON_WINDOWS
#define RETURN_KEY "Return"
#else
#define RETURN_KEY "↵"
#endif

// Default ratios for the main window
#define DEFAULT_PROJECT_RATIO 0.17
#define DEFAULT_INFO_RATIO 0.8
#define DEFAULT_CONSOLE_RATIO 0.8



#endif // PHONOMETRICA_MACROS_HPP
