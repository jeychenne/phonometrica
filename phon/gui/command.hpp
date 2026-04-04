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
 * Created: 26/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Command pattern for undo/redo support. A Command encapsulates a reversible action. The                     *
 *          CommandProcessor maintains two stacks (undo and redo) and executes/reverses commands.                       *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_COMMAND_HPP
#define PHONOMETRICA_COMMAND_HPP

#include <memory>
#include <vector>
#include <phon/string.hpp>

namespace phonometrica {

// Abstract base class for an undoable command.
class Command
{
public:

	virtual ~Command() = default;

	// Execute the command for the first time. Returns true on success.
	virtual bool execute() = 0;

	// Reverse the effect of execute().
	virtual void undo() = 0;

	// Re-apply after undo. The default calls execute().
	virtual void redo() { execute(); }

	// Human-readable description of this command (e.g. "Add anchor").
	// Used for status display; not required.
	virtual String description() const { return {}; }
};

using AutoCommand = std::unique_ptr<Command>;


//----------------------------------------------------------------------------------------------------------------------

class CommandProcessor
{
public:

	CommandProcessor() = default;

	// Maximum number of commands kept on the undo stack.
	static constexpr size_t max_stack_size = 50;

	// Submit a new command: execute it and push it onto the undo stack.
	// Clears the redo stack. Returns true if the command succeeded.
	bool submit(AutoCommand cmd)
	{
		if (!cmd->execute())
			return false;

		push_undo(std::move(cmd));
		m_redo_stack.clear();
		return true;
	}

	// Record a command that has already been executed externally.
	// Pushes it onto the undo stack without calling execute().
	// Clears the redo stack. Use this when the action was performed
	// by existing code and you just want to register it for undo.
	void record(AutoCommand cmd)
	{
		push_undo(std::move(cmd));
		m_redo_stack.clear();
	}

	bool can_undo() const { return !m_undo_stack.empty(); }
	bool can_redo() const { return !m_redo_stack.empty(); }

	void undo()
	{
		if (m_undo_stack.empty())
			return;

		auto cmd = std::move(m_undo_stack.back());
		m_undo_stack.pop_back();
		cmd->undo();
		m_redo_stack.push_back(std::move(cmd));
	}

	void redo()
	{
		if (m_redo_stack.empty())
			return;

		auto cmd = std::move(m_redo_stack.back());
		m_redo_stack.pop_back();
		cmd->redo();
		m_undo_stack.push_back(std::move(cmd));
	}

	void clear()
	{
		m_undo_stack.clear();
		m_redo_stack.clear();
	}

private:

	void push_undo(AutoCommand cmd)
	{
		if (m_undo_stack.size() >= max_stack_size)
			m_undo_stack.erase(m_undo_stack.begin());
		m_undo_stack.push_back(std::move(cmd));
	}

	std::vector<AutoCommand> m_undo_stack;
	std::vector<AutoCommand> m_redo_stack;
};

} // namespace phonometrica

#endif // PHONOMETRICA_COMMAND_HPP
