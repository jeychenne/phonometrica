/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 28/02/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: a property is a key/value pair which can be used to add metadata to VFile subclasses. The value can be a   *
 * Boolean, an number or a string.                                                                                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PROPERTY_HPP
#define PHONOMETRICA_PROPERTY_HPP

#include <string>
#include <set>
#include <unordered_map>
#include <memory>
#include <phon/string.hpp>
#include <phon/utils/ref_count.hpp>
#include <phon/utils/any.hpp>

namespace phonometrica {

class Property final
{
public:

	// Invalid property
	Property() = default;

	Property(String category, std::any value);

	Property(const Property &) = default;

	Property(Property &&) noexcept = default;

	~Property() = default;

	static Property from_string(const String &category, const String &value);

	void swap(Property &other) noexcept;

	Property &operator=(const Property &other) noexcept;
	Property &operator=(Property &&other) noexcept;

	bool operator==(const Property &other) const;
	bool operator!=(const Property &other) const;
	bool operator<(const Property &other) const;

	String category() const;

	String value() const;

	bool boolean_value() const;

	double numeric_value() const;

	const String &text_value() const;

	static const std::set<String> & get_categories();

	static std::set<String> get_categories_by_type(const std::type_info &type);

	static std::set<String> get_values(const String &category);

	static const std::type_info &get_type(const String &category);

	static bool is_boolean(const String &category);

	static bool is_numeric(const String &category);

	static bool is_text(const String &category);

	const std::type_info &type() const;

	const char *xml_type_name() const;

	String type_name() const; // as displayed in the information panel

	static const std::type_info &parse_xml_type_name(std::string_view name);

	static void remove(const Property &p);

	String to_string() const;

	bool is_boolean() const;

	bool is_numeric() const;

	bool is_text() const;

	static String false_string();

	static String true_string();

	static String undefined_string();

	bool valid() const { return bool(impl); }

	static Property parse(const String &type, const String &category, const String &value);

	static bool has_category(const String &category);

	static intptr_t category_count();

private:

	static std::set<Property> known_properties;
	static std::set<String> known_categories;
	static std::unordered_map<String, const std::type_info*> the_property_types;

	struct Data : public Countable<Data>
	{
		Data(String category, std::any value) :
				category(std::move(category)), value(std::move(value))
		{ }

		~Data() = default;

		String category;
		std::any value;
	};

	IntrusivePtr<Data> impl;
};


} // namespace phonometrica

#endif // PHONOMETRICA_PROPERTY_HPP
