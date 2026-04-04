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
 * Purpose: Undoable commands for dataset views: row/column deletion, column addition, rename, duplicate, move.        *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_DATASET_COMMANDS_HPP
#define PHONOMETRICA_DATASET_COMMANDS_HPP

#include <vector>
#include <phon/gui/command.hpp>
#include <phon/gui/dataset_model.hpp>

namespace phonometrica {

class DatasetView;


// ─────────────────────────────────────────────────
//  DeleteDatasetRowsCommand
// ─────────────────────────────────────────────────
// Recorded after rows have been removed.

class DeleteDatasetRowsCommand : public Command
{
public:

	struct RemovedRow
	{
		int source_row;  // 0-based
		Dataset::SavedRow data;
	};

	DeleteDatasetRowsCommand(DatasetView *view, std::vector<RemovedRow> removed) :
		m_view(view), m_removed(std::move(removed)) {}

	bool execute() override;
	void undo() override;

	String description() const override { return "Delete rows"; }

private:

	DatasetView *m_view;
	std::vector<RemovedRow> m_removed; // sorted ascending by source_row
};


// ─────────────────────────────────────────────────
//  DeleteDatasetColumnsCommand
// ─────────────────────────────────────────────────
// Recorded after columns have been removed.

class DeleteDatasetColumnsCommand : public Command
{
public:

	struct RemovedColumn
	{
		int col;  // 0-based
		Dataset::SavedColumn data;
	};

	DeleteDatasetColumnsCommand(DatasetView *view, std::vector<RemovedColumn> removed) :
		m_view(view), m_removed(std::move(removed)) {}

	bool execute() override;
	void undo() override;

	String description() const override { return "Delete columns"; }

private:

	DatasetView *m_view;
	std::vector<RemovedColumn> m_removed; // sorted ascending by col
};


// ─────────────────────────────────────────────────
//  AddDatasetColumnCommand
// ─────────────────────────────────────────────────
// Recorded after a recode/transform/metric adds a column.
// The new column is always appended at the end.

class AddDatasetColumnCommand : public Command
{
public:

	AddDatasetColumnCommand(DatasetView *view) :
		m_view(view) {}

	bool execute() override;
	void undo() override;

	String description() const override { return "Add column"; }

private:

	DatasetView *m_view;
	Dataset::SavedColumn m_saved;
	bool m_has_saved = false;
};


// ─────────────────────────────────────────────────
//  RenameDatasetColumnCommand
// ─────────────────────────────────────────────────

class RenameDatasetColumnCommand : public Command
{
public:

	RenameDatasetColumnCommand(DatasetView *view, int section,
	                           QString old_name, QString new_name) :
		m_view(view), m_section(section),
		m_old_name(std::move(old_name)), m_new_name(std::move(new_name)) {}

	bool execute() override;
	void undo() override;

	String description() const override { return "Rename column"; }

private:

	DatasetView *m_view;
	int m_section;
	QString m_old_name;
	QString m_new_name;
};


// ─────────────────────────────────────────────────
//  DuplicateDatasetColumnCommand
// ─────────────────────────────────────────────────
// Recorded after a column has been duplicated.
// Undo removes the inserted copy.

class DuplicateDatasetColumnCommand : public Command
{
public:

	DuplicateDatasetColumnCommand(DatasetView *view, int dest_col) :
		m_view(view), m_dest_col(dest_col) {}

	bool execute() override;
	void undo() override;

	String description() const override { return "Duplicate column"; }

private:

	DatasetView *m_view;
	int m_dest_col; // 1-based destination position
	Dataset::SavedColumn m_saved;
	bool m_has_saved = false;
};


// ─────────────────────────────────────────────────
//  MoveDatasetColumnCommand
// ─────────────────────────────────────────────────
// Wraps move_column. Undo moves back.

class MoveDatasetColumnCommand : public Command
{
public:

	MoveDatasetColumnCommand(DatasetView *view, int src, int dest) :
		m_view(view), m_src(src), m_dest(dest) {}

	bool execute() override;
	void undo() override;

	String description() const override { return "Move column"; }

private:

	DatasetView *m_view;
	int m_src;  // 1-based original position
	int m_dest; // 1-based destination in post-removal array
};


} // namespace phonometrica

#endif // PHONOMETRICA_DATASET_COMMANDS_HPP
