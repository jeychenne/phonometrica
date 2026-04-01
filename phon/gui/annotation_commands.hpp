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
 * Created: 26/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Undoable commands for annotation layer management.                                                         *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_ANNOTATION_COMMANDS_HPP
#define PHONOMETRICA_ANNOTATION_COMMANDS_HPP

#include <phon/gui/command.hpp>
#include <phon/string.hpp>

namespace phonometrica {

class AnnotationView;

// ─────────────────────────────────────────────────
//  AddLayerCommand
// ─────────────────────────────────────────────────

class AddLayerCommand : public Command
{
public:

	AddLayerCommand(AnnotationView *view, intptr_t index, String name, bool has_instants) :
		m_view(view), m_index(index), m_name(std::move(name)), m_has_instants(has_instants) {}

	bool execute() override;
	void undo() override;

private:

	AnnotationView *m_view;
	intptr_t m_index;
	String m_name;
	bool m_has_instants;
};


// ─────────────────────────────────────────────────
//  RemoveLayerCommand
// ─────────────────────────────────────────────────

class RemoveLayerCommand : public Command
{
public:

	RemoveLayerCommand(AnnotationView *view, intptr_t index) :
		m_view(view), m_index(index) {}

	bool execute() override;
	void undo() override;

private:

	AnnotationView *m_view;
	intptr_t m_index;

	// Saved state for undo: the label and type of the removed layer.
	String m_saved_name;
	bool m_saved_has_instants = false;
};

} // namespace phonometrica

#endif // PHONOMETRICA_ANNOTATION_COMMANDS_HPP
