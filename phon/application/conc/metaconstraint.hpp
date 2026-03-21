/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 01/02/2021                                                                                                 *
 *                                                                                                                     *
 * Purpose: Search constraint on metadata.                                                                             *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_METACONSTRAINT_HPP
#define PHONOMETRICA_METACONSTRAINT_HPP

#include <phon/string.hpp>
#include <phon/regex.hpp>
#include <phon/application/vfs.hpp>
#include <phon/utils/xml.hpp>

namespace phonometrica {

struct MetaConstraint
{
	MetaConstraint() = default;

	virtual ~MetaConstraint() = default;

	virtual bool filter(const Document * file) const = 0;

	virtual void to_xml(xml_node node) = 0;
};

using AutoMetaConstraint = std::shared_ptr<MetaConstraint>;

//----------------------------------------------------------------------------------------------------------------------

struct DescMetaConstraint final : public MetaConstraint
{
	enum class Operator
	{
		Equal = 0, // this must be 0 to match the selector in the GUI
		NotEqual,
		Contains,
		NotContains,
		Match,
		NotMatch
	};

	DescMetaConstraint(Operator op, const String &value);

	static const char * op_to_name(Operator op);

	static Operator name_to_op(std::string_view name);

	bool filter(const Document * file) const override;

	void to_xml(xml_node node) override;

	Operator op;

	String value;

	// If we use a match on the regex, compile the regex once
	std::unique_ptr<Regex> regex;

};

//----------------------------------------------------------------------------------------------------------------------

struct PropertyMetaConstraint : public MetaConstraint
{
	PropertyMetaConstraint(const String &category) :
		MetaConstraint(), category(category)
	{ }

	String category;
};

struct TextMetaConstraint : public PropertyMetaConstraint
{
	TextMetaConstraint(const String &category, Array<String> values) :
		PropertyMetaConstraint(category), values(std::move(values))
	{ }

	bool filter(const Document * file) const override;

	void to_xml(xml_node node) override;

	Array<String> values;
};

struct NumericMetaConstraint : public PropertyMetaConstraint
{
	enum class Operator
	{
		None = 0, // this must be 0 to match the selector in the GUI
		Equal,
		NotEqual,
		Less,
		LessEqual,
		Greater,
		GreaterEqual,
		InclusiveRange,
		ExclusiveRange
	};

	NumericMetaConstraint(const String &category, Operator op, const std::pair<double,double> &value) :
		PropertyMetaConstraint(category), op(op), value(value)
	{ }

	bool filter(const Document * file) const override;

	static const char * op_to_name(Operator op);

	static Operator name_to_op(std::string_view name);

	void to_xml(xml_node node) override;

	bool check_value(double num) const;

	Operator op;

	// The second member is only used by InclusiveRange.
	std::pair<double,double> value;
};

struct BooleanMetaConstraint : public PropertyMetaConstraint
{
	BooleanMetaConstraint(const String &category, bool value) :
		PropertyMetaConstraint(category), value(value)
	{ }

	bool filter(const Document * file) const override;

	void to_xml(xml_node node) override;

	bool value;
};

} // namespace phonometrica

#endif // PHONOMETRICA_METACONSTRAINT_HPP
