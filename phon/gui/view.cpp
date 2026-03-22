/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header. This file exists so that MOC can generate the Q_OBJECT machinery                               *
 *          (vtable, staticMetaObject, signal implementations) for the View base class.                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/gui/view.hpp>

namespace phonometrica {

// Nothing here — the class is defined entirely in the header.
// This .cpp file exists solely so that Qt's MOC has a translation unit
// in which to emit the generated meta-object code for View.

} // namespace phonometrica
