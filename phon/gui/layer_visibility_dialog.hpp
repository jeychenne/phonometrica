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
 * Purpose: Dialog to select which annotation layers are visible. Presents a checkbox per layer with its name and      *
 *          type (interval/instant).                                                                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_LAYER_VISIBILITY_DIALOG_HPP
#define PHONOMETRICA_LAYER_VISIBILITY_DIALOG_HPP

#include <vector>
#include <QDialog>
#include <QCheckBox>
#include <phon/application/annotation.hpp>

namespace phonometrica {

class LayerVisibilityDialog : public QDialog
{
	Q_OBJECT

public:

	LayerVisibilityDialog(QWidget *parent, const Handle<Annotation> &annot,
	                      const std::vector<bool> &current_visibility);

	// Returns one bool per layer (1-based: index 0 is unused).
	std::vector<bool> visibility() const;

private:

	std::vector<QCheckBox *> m_checks; // index 0 = layer 1
};

} // namespace phonometrica

#endif // PHONOMETRICA_LAYER_VISIBILITY_DIALOG_HPP
