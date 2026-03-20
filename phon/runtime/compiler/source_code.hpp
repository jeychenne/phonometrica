/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 18/07/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: represents a chunk of source code, loaded from memory or from a text file.                                 *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SOURCE_CODE_HPP
#define PHONOMETRICA_SOURCE_CODE_HPP

#include <phon/string.hpp>

namespace phonometrica {

class SourceCode final
{
public:

    SourceCode();

    String filename() const;

    void dispose() { delete this; }

    bool empty() const { return m_lines.empty(); }

    const String &path() const { return m_path; }

    // Set source code from a file on disk
    void load_file(const String &path);

    // Set source code from a string
    void load_code(const String &code);

    String get_line(intptr_t index) const;

    intptr_t size() const;

    // This is used by AST visitors. It is less detailed than an error reported by the scanner
    // since we can't use the token's position, but it's better than nothing...
    void report_error(const char *error_type, intptr_t line_no,  const std::string &hint = std::string());

private:

    // Current file (empty if memory buffer)
    String m_path;

    // Current file
    Array<String> m_lines;

};

} // namespace phonometrica

#endif // PHONOMETRICA_SOURCE_CODE_HPP
