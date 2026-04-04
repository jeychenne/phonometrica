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

void View::record(AutoCommand cmd)
{
	m_commands.record(std::move(cmd));
	emit undoRedoChanged(canUndo(), canRedo());
}

} // namespace phonometrica
