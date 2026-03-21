/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 28/02/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: project metadata.                                                                                          *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_METADATA_HPP
#define PHONOMETRICA_METADATA_HPP

#include <chrono>
#include <phon/string.hpp>
#include <phon/utils/xml.hpp>

namespace phonometrica {


using Date = std::chrono::year_month_day;

struct Author
{
	String name;
	String email;

	void ToXml(xml_node root);
};

//----------------------------------------------------------------------------------------------------------------------

class Version
{
public:

    Version(Date date, const String &description, const String &number = String());

    Date date() const { return m_date; }
    void set_date(Date date) { m_date = date; }
    String date_as_string() const;
    static Date parse_date(const String &date);

	String number() const;
	void set_number(const String &number);

	String description() const;
	void set_description(const String &desc);

	const Array<Author> &authors() const;
	void add_author(Author author);
	void remove_author(const String &name);
	void remove_author(size_t index);

	void to_xml(xml_node root);

private:

	Array<Author> m_authors;

    Date m_date;

	String m_description;

	String m_number;
};


//----------------------------------------------------------------------------------------------------------------------

class Changelog
{
public:
	Changelog() = default;

	bool modified() const;
	void reset_modifications();

	size_t version_count() const;
	Version &get_version(size_t index);
	void add_version(Version version, bool modify = true);
	void remove_version(size_t index, bool modify = true);

	void from_xml(xml_node root);
	void to_xml(xml_node root);

private:

	void mark_modified(bool value);

	Array<Version> m_versions;

	bool m_modified = false;
};

} // phonometrica

#endif // PHONOMETRICA_METADATA_HPP
