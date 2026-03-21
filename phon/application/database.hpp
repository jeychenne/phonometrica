/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 28/02/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: store metadata for files which can't store their own metadata (e.g. TextGrid).                             *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_DATABASE_HPP
#define PHONOMETRICA_DATABASE_HPP

#include <set>
#include <phon/string.hpp>
#include <phon/third_party/sqlite/sqlite3.h>
#include <phon/runtime/typed_object.hpp>
#include <phon/utils/signal.hpp>
#include <phon/application/annotation.hpp>

namespace phonometrica {

class Document;
class Annotation;
class Property;


class Database
{
public:

	explicit Database(const String &path);

	virtual ~Database();

	void execute(std::string_view sql, bool notify = true);

	void parse(const String &sql);

	/* Read next row from the statement. Returns whether a row was successfully read */
	bool read_row();

	/* Get a field in the current row */
	inline String get_field(int j);

	String get_column_name(int j) const;

	int field_count() const;

	void finalize_statement();

	bool has_column(std::string_view col);

	void close();

	/* Check whether the query returned any rows and clean up */
	bool check_statement();

	void commit();

	String path() const { return m_path; }

protected:

	/* Database connection */
	sqlite3 *db;

	/* Current statement, if any */
	sqlite3_stmt *statement;

	String m_path;

	String escape_string(const String &str);
};


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MetaDatabase final : public Database
{
	Signal<const String &> saving_metadata;

public:

	MetaDatabase(const String &path, bool create_table);

	void create_main_table();

	bool has_file(const String &path);

	void add_category(const String &cat);

	void remove_category_from_file(const String &path, const String &cat);

	void add_file(Document &file);

	void save_file_metadata(Document &file);

	void add_metadata_to_file(const phonometrica::Handle<Document> &file);

	std::set<String> get_categories();

	Signal<const Property &> notify_property;

	// Send annotation and sound path. The project will associate the Annotation with
	// the corresponding Sound file whenever possible.
	Signal<const Handle<Annotation> &, const String &> notify_annotation_needs_sound;

private:

	String get_value(const String &path, const String &cat);

	String get_sound_path_if_exists(const Document &file) const;
};


} // phonometrica
#endif // PHONOMETRICA_DATABASE_HPP
