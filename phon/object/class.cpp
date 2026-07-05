// Phonometrica engine — class registry, intervals, renumbering, metaclasses.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/object/class.hpp>

#include <phon/base/alloc.hpp>
#include <phon/core/cell.hpp>
#include <phon/core/vector.hpp>
#include <phon/object/instance.hpp>

#include <cstring>

namespace phonometrica {

namespace {

// A class-object cell: its dispatch class is a metaclass; `desc` is the class it
// denotes. Used so a class can appear as a Value (e.g. the `Float` in cast).
struct ClassObjectCell
{
	Cell header;
	Class *desc;
};

struct Registry
{
	Vector<Class *> by_id;       // stable id -> Class* (append-only)
	Vector<Class *> owned;       // dynamically-allocated descriptors to delete
	Vector<char *> owned_names;  // their copied names to free
	Vector<Cell *> class_objects; // class-object cells to free at shutdown
	uint32_t epoch = 0;

	~Registry()
	{
		for (intptr_t i = 0; i < class_objects.size(); ++i)
			cell_free(class_objects[i]);
		for (intptr_t i = 0; i < owned.size(); ++i)
		{
			if (owned[i]->fields)
				sys_free(owned[i]->fields);
			delete owned[i];
		}
		for (intptr_t i = 0; i < owned_names.size(); ++i)
			sys_free(owned_names[i]);
	}
};

Registry &reg()
{
	static Registry r;
	return r;
}

void link_child(Class *base, Class *c)
{
	c->next_sibling = nullptr;
	if (base->last_child == nullptr)
		base->first_child = base->last_child = c;
	else
	{
		base->last_child->next_sibling = c;
		base->last_child = c;
	}
}

void renumber_rec(Class *c, uint32_t &counter)
{
	c->interval_lo = counter++;
	for (Class *ch = c->first_child; ch != nullptr; ch = ch->next_sibling)
		renumber_rec(ch, counter);
	c->interval_hi = counter - 1;
}

char *copy_cstr(const char *s)
{
	size_t n = s ? std::strlen(s) : 0;
	char *out = static_cast<char *>(sys_alloc(static_cast<intptr_t>(n) + 1));
	if (n)
		std::memcpy(out, s, n);
	out[n] = '\0';
	return out;
}

} // namespace

uint32_t register_class(Class *c)
{
	PHON_ASSERT(c != nullptr);
	Vector<Class *> &t = reg().by_id;
	uint32_t id = c->id;
	if (static_cast<intptr_t>(id) >= t.size())
	{
		intptr_t old = t.size();
		t.resize(static_cast<intptr_t>(id) + 1);
		for (intptr_t i = old; i < t.size(); ++i)
			t[i] = nullptr;
	}
	t[static_cast<intptr_t>(id)] = c;
	if (c->base != nullptr)
		link_child(c->base, c);
	return id;
}

Class *add_class(const char *name, Class *base, uint16_t flags, intptr_t instance_size)
{
	PHON_ASSERT(base != nullptr);
	Registry &r = reg();
	Class *c = new Class{};
	c->id = static_cast<uint32_t>(r.by_id.size());
	char *owned_name = copy_cstr(name);
	c->name = owned_name;
	c->base = base;
	c->flags = flags;
	c->instance_size = instance_size;
	r.owned.push_back(c);
	r.owned_names.push_back(owned_name);
	register_class(c);
	renumber_types();
	return c;
}

Class *add_user_class(const char *name, Class *base, bool is_ref, bool is_open,
                      const FieldInfo *own, int32_t n_own)
{
	PHON_ASSERT(base != nullptr);
	uint16_t flags = static_cast<uint16_t>(is_ref ? CLASS_REF : CLASS_VALUE);
	if (!is_open)
		flags |= CLASS_SEALED;

	int32_t base_n = base->field_count;
	int32_t total = base_n + n_own;

	Class *c = add_class(name, base, flags); // assigns id, links, renumbers

	FieldInfo *arr = nullptr;
	if (total > 0)
	{
		arr = static_cast<FieldInfo *>(
		    sys_alloc(static_cast<intptr_t>(total) * static_cast<intptr_t>(sizeof(FieldInfo))));
		for (int32_t i = 0; i < base_n; ++i)
			arr[i] = base->fields[i]; // inherit base fields (and their accessors)
		for (int32_t i = 0; i < n_own; ++i)
			arr[base_n + i] = own[i];
	}
	c->fields = arr;
	c->field_count = total;
	c->instance_size = static_cast<intptr_t>(sizeof(Cell)) +
	                   static_cast<intptr_t>(total) * static_cast<intptr_t>(sizeof(Value));
	c->finalize = &instance_finalize;
	c->clone = &instance_clone_hook;
	return c;
}

int32_t field_slot(const Class *c, Symbol name) noexcept
{
	for (int32_t i = 0; i < c->field_count; ++i)
		if (c->fields[i].name == name)
			return i;
	return -1;
}

const FieldInfo *field_at(const Class *c, int32_t slot) noexcept
{
	return (slot >= 0 && slot < c->field_count) ? &c->fields[slot] : nullptr;
}

Class *get_class(uint32_t id) noexcept
{
	Vector<Class *> &t = reg().by_id;
	PHON_ASSERT_MSG(static_cast<intptr_t>(id) < t.size() && t[static_cast<intptr_t>(id)] != nullptr,
	                "get_class: unregistered class id");
	return t[static_cast<intptr_t>(id)];
}

bool has_class(uint32_t id) noexcept
{
	Vector<Class *> &t = reg().by_id;
	return static_cast<intptr_t>(id) < t.size() && t[static_cast<intptr_t>(id)] != nullptr;
}

intptr_t class_count() noexcept
{
	return reg().by_id.size();
}

void renumber_types()
{
	uint32_t counter = 0;
	renumber_rec(get_class(CID_OBJECT), counter);
	++reg().epoch;
}

uint32_t type_epoch() noexcept
{
	return reg().epoch;
}

void registry_shutdown()
{
	// Frees dynamic descriptors and class objects; the singleton's destructor
	// does the same at process exit, so this is only for explicit teardown.
	Registry &r = reg();
	for (intptr_t i = 0; i < r.class_objects.size(); ++i)
		cell_free(r.class_objects[i]);
	r.class_objects.clear();
}

// --- subtyping ---

uint32_t class_of(Value v) noexcept
{
	if (v.is_double())
		return CID_FLOAT;
	if (v.is_int())
		return CID_INTEGER;
	if (v.is_cell())
		return v.as_cell()->class_id();
	if (v.is_symbol())
		return CID_SYMBOL;
	if (v.is_null())
		return CID_NULL;
	return CID_BOOLEAN; // true / false
}

Class *class_of_desc(Value v) noexcept
{
	return get_class(class_of(v));
}

bool value_is_a(Value v, const Class *sup) noexcept
{
	return is_a(class_of_desc(v), sup);
}

// --- metaclasses and class objects ---

Class *metaclass_of(Class *c)
{
	if (c->meta_id != 0)
		return get_class(c->meta_id);
	// A metaclass is a leaf under the root metaclass Class; exact match is all
	// `cast` needs (you never cast to "a subtype of the Float class object").
	Class *mc = add_class(c->name, get_class(CID_CLASS),
	                      CLASS_BUILTIN | CLASS_META | CLASS_REF | CLASS_SEALED);
	c->meta_id = mc->id;
	return mc;
}

Value class_object(Class *c)
{
	if (c->class_object != nullptr)
		return Value::make_cell(c->class_object);
	Class *mc = metaclass_of(c);
	Cell *cell = cell_alloc(mc->id, static_cast<intptr_t>(sizeof(ClassObjectCell)));
	reinterpret_cast<ClassObjectCell *>(cell)->desc = c;
	c->class_object = cell;                // the registry's permanent reference (rc stays >= 1)
	reg().class_objects.push_back(cell);
	return Value::make_cell(cell);
}

Class *class_denoted_by(Value v) noexcept
{
	if (!v.is_cell())
		return nullptr;
	Cell *cell = v.as_cell();
	Class *k = get_class(cell->class_id());
	if (!(k->flags & CLASS_META))
		return nullptr;
	return reinterpret_cast<ClassObjectCell *>(cell)->desc;
}

} // namespace phonometrica
