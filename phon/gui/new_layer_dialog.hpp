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
 * Created: 25/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for creating a new annotation layer. Asks for the layer name, type (intervals or instants), and     *
 *          position (1-based index).                                                                                  *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_NEW_LAYER_DIALOG_HPP
#define PHONOMETRICA_NEW_LAYER_DIALOG_HPP

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QRadioButton>

namespace phonometrica {

class NewLayerDialog : public QDialog
{
	Q_OBJECT

public:

	NewLayerDialog(QWidget *parent, intptr_t current_layer_count);

	QString layerName() const;
	intptr_t layerIndex() const;
	bool hasInstants() const;

private:

	QLineEdit *m_name_edit;
	QSpinBox *m_index_spin;
	QRadioButton *m_interval_radio;
	QRadioButton *m_instant_radio;
};

} // namespace phonometrica

#endif // PHONOMETRICA_NEW_LAYER_DIALOG_HPP
