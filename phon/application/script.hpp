/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 28/02/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: Script file.                                                                                               *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SCRIPT_HPP
#define PHONOMETRICA_SCRIPT_HPP

#include <phon/application/vfs.hpp>

namespace phonometrica {

class Script final : public Document
{
public:

	explicit Script(Directory *parent, String path = String());

	const String &content() const;

	void set_content(String value, bool mutate = true);

    // A script can only be modified in a script view. We don't update the script's content every time the text is
    // changed in the view. Instead, we inform the script that it has been modified with this method. When the view
    // is closed, the user will be asked whether they want to save the modifications or not. Modifications can also
    // be saved via the save button in the script view.
    void set_pending_modifications() { m_content_modified = true; }

	String label() const override;

    static void initialize(Runtime &rt);

private:

	void load() override;

	void write() override;

	String m_content;

};


namespace traits {
template<> struct maybe_cyclic<Script> : std::false_type { };
}

} // namespace phonometrica

#endif // PHONOMETRICA_SCRIPT_HPP
