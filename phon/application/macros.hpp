/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
