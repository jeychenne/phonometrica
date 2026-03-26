/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
