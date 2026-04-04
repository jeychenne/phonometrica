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
 * Created: 04/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Undoable commands for concordance views: row deletion, column addition, column renaming.                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CONCORDANCE_COMMANDS_HPP
#define PHONOMETRICA_CONCORDANCE_COMMANDS_HPP

#include <vector>
#include <phon/gui/command.hpp>
#include <phon/gui/conc/concordance_model.hpp>

namespace phonometrica {

class ConcordanceView;


// ─────────────────────────────────────────────────
//  DeleteMatchesCommand
// ─────────────────────────────────────────────────
// Recorded after rows have already been removed from the model.
// Stores the removed matches so undo can restore them.

class DeleteMatchesCommand : public Command
{
public:

	struct RemovedMatch
	{
		int source_row;     // 0-based row in the source model
		AutoMatch match;
	};

	DeleteMatchesCommand(ConcordanceView *view, std::vector<RemovedMatch> removed) :
		m_view(view), m_removed(std::move(removed)) {}

	bool execute() override;
	void undo() override;

	String description() const override { return "Delete rows"; }

private:

	ConcordanceView *m_view;
	std::vector<RemovedMatch> m_removed; // sorted ascending by source_row
};


// ─────────────────────────────────────────────────
//  AddConcAuxColumnCommand
// ─────────────────────────────────────────────────
// Recorded after a recode/transform/metric operation has appended
// an aux column to the concordance. Undo removes the last aux column.

class AddConcAuxColumnCommand : public Command
{
public:

	AddConcAuxColumnCommand(ConcordanceView *view) :
		m_view(view) {}

	bool execute() override;
	void undo() override;

	String description() const override { return "Add column"; }

private:

	ConcordanceView *m_view;

	// Saved column for undo (populated on first undo, reused on subsequent undos).
	Concordance::AuxColumn m_saved_col;
	bool m_has_saved = false;
};


// ─────────────────────────────────────────────────
//  RenameConcColumnCommand
// ─────────────────────────────────────────────────
// Wraps a header rename via the alias system.

class RenameConcColumnCommand : public Command
{
public:

	RenameConcColumnCommand(ConcordanceView *view, int section,
	                        QString old_name, QString new_name) :
		m_view(view), m_section(section),
		m_old_name(std::move(old_name)), m_new_name(std::move(new_name)) {}

	bool execute() override;
	void undo() override;

	String description() const override { return "Rename column"; }

private:

	ConcordanceView *m_view;
	int m_section;
	QString m_old_name;
	QString m_new_name;
};


} // namespace phonometrica

#endif // PHONOMETRICA_CONCORDANCE_COMMANDS_HPP
