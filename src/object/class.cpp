// Phonometrica engine — class registry implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include "object/class.hpp"

#include "core/vector.hpp"

namespace phonometrica {

namespace {

// Process-global, append-indexed table (§6). A Meyers singleton sidesteps
// static-init-order issues; class registration happens at bootstrap.
Vector<Class *> &table()
{
	static Vector<Class *> t;
	return t;
}

} // namespace

uint32_t register_class(Class *c)
{
	PHON_ASSERT(c != nullptr);
	Vector<Class *> &t = table();
	uint32_t id = c->id;
	if (static_cast<intptr_t>(id) >= t.size())
	{
		intptr_t old = t.size();
		t.resize(static_cast<intptr_t>(id) + 1);
		for (intptr_t i = old; i < t.size(); ++i)
			t[i] = nullptr;
	}
	t[static_cast<intptr_t>(id)] = c;
	return id;
}

Class *get_class(uint32_t id) noexcept
{
	Vector<Class *> &t = table();
	PHON_ASSERT_MSG(static_cast<intptr_t>(id) < t.size() && t[static_cast<intptr_t>(id)] != nullptr,
	                "get_class: unregistered class id");
	return t[static_cast<intptr_t>(id)];
}

bool has_class(uint32_t id) noexcept
{
	Vector<Class *> &t = table();
	return static_cast<intptr_t>(id) < t.size() && t[static_cast<intptr_t>(id)] != nullptr;
}

intptr_t class_count() noexcept
{
	return table().size();
}

} // namespace phonometrica
