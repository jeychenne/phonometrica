/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
};

using AutoCommand = std::unique_ptr<Command>;


//----------------------------------------------------------------------------------------------------------------------

class CommandProcessor
{
public:

	CommandProcessor() = default;

	// Submit a new command: execute it and push it onto the undo stack.
	// Clears the redo stack. Returns true if the command succeeded.
	bool submit(AutoCommand cmd)
	{
		if (!cmd->execute())
			return false;

		m_undo_stack.push_back(std::move(cmd));
		m_redo_stack.clear();
		return true;
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

	std::vector<AutoCommand> m_undo_stack;
	std::vector<AutoCommand> m_redo_stack;
};

} // namespace phonometrica

#endif // PHONOMETRICA_COMMAND_HPP
