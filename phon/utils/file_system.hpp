/**********************************************************************************************************************
 *                                                                                                                    *
 * Copyright (C) 2019-2026 Julien Eychenne <jeychenne@gmail.com>                                                      *
 *                                                                                                                    *
 * The contents of this file are subject to the Mozilla Public License Version 2.0 (the "License"); you may not use   *
 * this file except in compliance with the License. You may obtain a copy of the License at                           *
 * http://www.mozilla.org/MPL/.                                                                                       *
 *                                                                                                                    *
 * Created: 20/02/2019                                                                                                *
 *                                                                                                                    *
 * Purpose: path manipulation routines. Since the A1 base-type swap this is a forwarder onto the NEW engine's         *
 * filesystem layer (base/file_system.hpp), which was ported from this module and extended to app parity              *
 * (list_directory returning Array<String>, append, application_directory — engine DEVIATIONS item 24). Note two      *
 * signature deltas vs the old module: nativize/genericize return a new String instead of mutating in place, and      *
 * temp_file/clear_directory (unused) are gone.                                                                       *
 *                                                                                                                    *
 **********************************************************************************************************************/

#ifndef PHONOMETRICA_FILESYSTEM_HPP
#define PHONOMETRICA_FILESYSTEM_HPP

#include <phon/engine/base/file_system.hpp>

#endif // PHONOMETRICA_FILESYSTEM_HPP
