/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/05/2020                                                                                                 *
 *                                                                                                                     *
 * Purpose: Class object. Each object stores a pointer to its class, which provides runtime type information (RTTI)    *
 * and basic polymorphic methods.                                                                                      *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CLASS_HPP
#define PHONOMETRICA_CLASS_HPP

#include <typeinfo>
#include <vector>
#include <phon/string.hpp>
#include <phon/runtime/typed_object.hpp>
#include <phon/dictionary.hpp>
#include <phon/runtime/callback.hpp>

namespace phonometrica {

class Function;
class Instance;

// Classes are objects too.
class Class final
{

public:

	enum class Index
	{
		Object,
		Class,
		Null,
		Boolean,
		Number,
		Integer,
		Float,
		String,
		Regex,
		List,
		Array,
		Table,
		Set,
		File,
		Function,
		Closure,
		Module,
		Iterator,
		ListIterator,
		TableIterator,
		StringIterator,
		FileIterator,
		RegexIterator,
		Instance,
		Foreign
	};


	Class(String name, Class *parent, const std::type_info *info, Index index = Index::Foreign);

	Class(const Class &) = delete;

	Class(Class &&) = delete;

	~Class() = default;

	String name() const { return _name; }

	size_t depth() const { return _depth; }

	bool inherits(const Class *base) const;

	int get_distance(const Class *base) const;

	const std::type_info *type_info() const { return _info; }

	template<class T>
	static Class *get()
	{
		using Type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
		return detail::ClassDescriptor<Type>::get();
	}

	template<class T>
	static String get_name()
	{
		return get<T>()->name();
	}

	bool operator==(const Class &other) const { return this == &other; }

	Object *object() { return _object; }

	Handle<Function> get_constructor();

	Handle<Function> get_initializer();

	Handle<Function> get_method(const String &name, bool must_exist = true);

	Handle<Function> get_to_string_method();

	void add_constructor(NativeCallback cb, std::initializer_list<Handle<Class>> sig, ParamBitset ref = ParamBitset());

	void add_constructor(Handle<Function> f);

	void add_method(const String &name, NativeCallback cb, std::initializer_list<Handle<Class>> sig, ParamBitset ref = ParamBitset());

	void add_method(const String &name, Handle<Function> f);

	void traverse_members(const GCCallback &callback);

	bool is_user_defined() const;

	void set_instance_field(const String &key, Variant value);

	void initialize_instance(Instance &instance);

	// For user types only.
	bool clonable() const;

	// Used to create a user-defined reference type.
	void make_unclonable();

private:

	friend class Runtime;
	friend class Object;

	// Polymorphic methods for type erasure.
	size_t (*hash)(const Object*) = nullptr;
	void (*traverse)(Collectable*, const GCCallback&) = nullptr;
	Object *(*clone)(const Object*) = nullptr;
	String (*to_string)(const Object*) = nullptr;
	int (*compare)(const Object*, const Object*) = nullptr;
	bool (*equal)(const Object*, const Object*) = nullptr;

	void set_object(Object *o) { _object = o; }

	// We need to manually finalize members that refer to a class before classes are finalized by the runtime's destructor.
	void finalize();

	// Name given to the class when it was created
	String _name;

	// Pointer to the object the class is wrapped in.
	Object *_object = nullptr;

	// Inheritance depth (0 for Object, 1 for direct subclasses of Object, etc.).
	size_t _depth;

	// C++ type for builtin types (null for user-defined types). This is used to safely downcast
	// objects to TObject<T>. Since [object] doesn't use C++'s virtual inheritance, we can't
	// use dynamic_cast for that purpose.
	const std::type_info *_info;

	// Non-owning array of Class pointers, representing the class's inheritance hierarchy. The first element represents
	// the top-most class, and is always Object. The last element is the class itself. This allows constant-time lookup
	// using the class's inheritance depth.
	std::vector<Class*> _bases;

	// Instance fields are stored when the class is created, and they are copied into each newly created instance.
	Dictionary<Variant> methods, instance_fields;

	// For debugging.
	Index index;

	// For user types.
	bool is_clonable = true;

	// Name of methods.
	static String ctor_string, init_string, tostr_string;
};

//---------------------------------------------------------------------------------------------------------------------

template<class T>
Handle<Class> get_class()
{
	return Handle<Class>(static_cast<TObject<Class>*>(detail::ClassDescriptor<T>::get()->object()));
}


namespace meta {

static inline
String to_string(const Class &klass)
{
	return String::format("<class %s>", klass.name().data());
}

static inline
void traverse(Class &cls, const GCCallback &callback)
{
	cls.traverse_members(callback);
}


} // namespace phonometrica::meta

} // namespace phonometrica

#endif // PHONOMETRICA_CLASS_HPP
