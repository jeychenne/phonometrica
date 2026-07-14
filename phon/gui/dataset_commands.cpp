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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/gui/dataset_commands.hpp>
#include <phon/gui/dataset_view.hpp>

namespace phonometrica {

// ─────────────────────────────────────────────────
//  DeleteDatasetRowsCommand
// ─────────────────────────────────────────────────

bool DeleteDatasetRowsCommand::execute()
{
	// Called on redo: re-remove the same rows (bottom to top).
	auto *model = m_view->dsModel();
	for (int i = (int) m_removed.size() - 1; i >= 0; i--)
		m_removed[i].data = model->extractRow(m_removed[i].source_row);

	m_view->refreshAfterChange();
	return true;
}

void DeleteDatasetRowsCommand::undo()
{
	// Restore rows in ascending order.
	auto *model = m_view->dsModel();
	for (auto &rr : m_removed)
		model->insertRow(rr.source_row, rr.data);

	m_view->refreshAfterChange();
}


// ─────────────────────────────────────────────────
//  DeleteDatasetColumnsCommand
// ─────────────────────────────────────────────────

bool DeleteDatasetColumnsCommand::execute()
{
	// Called on redo: re-remove the same columns (right to left).
	auto *model = m_view->dsModel();
	for (int i = (int) m_removed.size() - 1; i >= 0; i--)
	{
		m_removed[i].data = model->extractColumn(m_removed[i].col);
		m_view->adjustFiltersAfterColumnRemove(m_removed[i].col);
	}

	m_view->refreshAfterChange();
	return true;
}

void DeleteDatasetColumnsCommand::undo()
{
	// Restore columns in ascending order (left to right).
	auto *model = m_view->dsModel();
	for (auto &rc : m_removed)
	{
		model->insertColumn(rc.col, std::move(rc.data));
		m_view->adjustFiltersAfterColumnInsert(rc.col);
	}

	m_view->refreshAfterChange();
}


// ─────────────────────────────────────────────────
//  AddDatasetColumnCommand
// ─────────────────────────────────────────────────

bool AddDatasetColumnCommand::execute()
{
	// Called on redo: restore the saved column at the end.
	if (m_has_saved)
	{
		auto *model = m_view->dsModel();
		int col = model->columnCount(); // insert at end (0-based = current count)
		model->insertColumn(col, std::move(m_saved));
		m_has_saved = false;
		m_view->adjustFiltersAfterColumnInsert(col);
		m_view->refreshAfterChange();
	}
	// On first call (via record()), the column was already added.
	return true;
}

void AddDatasetColumnCommand::undo()
{
	auto *model = m_view->dsModel();
	int last = model->columnCount() - 1;
	m_saved = model->extractColumn(last);
	m_has_saved = true;
	m_view->adjustFiltersAfterColumnRemove(last);
	m_view->refreshAfterChange();
}


// ─────────────────────────────────────────────────
//  RenameDatasetColumnCommand
// ─────────────────────────────────────────────────

bool RenameDatasetColumnCommand::execute()
{
	m_view->dsModel()->setHeaderData(m_section, Qt::Horizontal, m_new_name, Qt::EditRole);
	m_view->refreshAfterChange();
	return true;
}

void RenameDatasetColumnCommand::undo()
{
	m_view->dsModel()->setHeaderData(m_section, Qt::Horizontal, m_old_name, Qt::EditRole);
	m_view->refreshAfterChange();
}


// ─────────────────────────────────────────────────
//  DuplicateDatasetColumnCommand
// ─────────────────────────────────────────────────

bool DuplicateDatasetColumnCommand::execute()
{
	// Called on redo: restore the saved column.
	if (m_has_saved)
	{
		auto *model = m_view->dsModel();
		model->insertColumn(m_dest_col, std::move(m_saved));
		m_has_saved = false;
		m_view->adjustFiltersAfterColumnInsert(m_dest_col);
		m_view->refreshAfterChange();
	}
	// On first call (via record()), the column was already duplicated.
	return true;
}

void DuplicateDatasetColumnCommand::undo()
{
	auto *model = m_view->dsModel();
	m_saved = model->extractColumn(m_dest_col);
	m_has_saved = true;
	m_view->adjustFiltersAfterColumnRemove(m_dest_col);
	m_view->refreshAfterChange();
}


// ─────────────────────────────────────────────────
//  MoveDatasetColumnCommand
// ─────────────────────────────────────────────────

bool MoveDatasetColumnCommand::execute()
{
	// Called on redo: move src → dest.
	auto ds = m_view->dsModel()->dataset();
	ds->move_column(m_src, m_dest);
	ds->set_content_modified(true);
	m_view->adjustFiltersAfterColumnMove(m_src, m_dest);
	m_view->dsModel()->refreshAll();
	m_view->refreshAfterChange();
	return true;
}

void MoveDatasetColumnCommand::undo()
{
	auto ds = m_view->dsModel()->dataset();
	ds->move_column(m_dest, m_src);
	ds->set_content_modified(true);
	m_view->adjustFiltersAfterColumnMove(m_dest, m_src);
	m_view->dsModel()->refreshAll();
	m_view->refreshAfterChange();
}


} // namespace phonometrica
