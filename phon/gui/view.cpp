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

void View::undo()
{
	m_commands.undo();
	emit undoRedoChanged(canUndo(), canRedo());
}

void View::redo()
{
	m_commands.redo();
	emit undoRedoChanged(canUndo(), canRedo());
}

bool View::submit(AutoCommand cmd)
{
	bool ok = m_commands.submit(std::move(cmd));
	emit undoRedoChanged(canUndo(), canRedo());
	return ok;
}

} // namespace phonometrica
