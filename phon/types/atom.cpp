// Phonometrica engine — atom table implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/types/atom.hpp>

#include <phon/base/alloc.hpp>
#include <phon/core/hash.hpp>
#include <phon/core/flat_hash_map.hpp>
#include <phon/core/vector.hpp>

#include <cstring>
#include <mutex>

namespace phonometrica {

namespace {

struct SubstringHash
{
	uint64_t operator()(std::string_view s) const noexcept
	{
		return hash_bytes(s.data(), static_cast<intptr_t>(s.size()));
	}
};

// The table owns each atom's bytes in its own allocation (stable address for the
// process lifetime). The destructor frees them so the suite is ASan-leak-clean.
struct AtomTable
{
	std::mutex mutex;
	FlatHashMap<std::string_view, Symbol, SubstringHash> index; // bytes -> Symbol
	Vector<std::string_view> names;                             // Symbol id -> bytes

	AtomTable()
	{
		// Reserve id 0 (NO_SYMBOL) so the first real atom is id 1.
		names.push_back(std::string_view());
	}

	~AtomTable()
	{
		// names[0] is the reserved empty view; the rest own heap buffers.
		for (intptr_t i = 1; i < names.size(); ++i)
			sys_free(const_cast<char *>(names[i].data()));
	}

	Symbol intern(std::string_view s)
	{
		std::lock_guard<std::mutex> lock(mutex);
		auto it = index.find(s);
		if (it != index.end())
			return it->second;

		// Copy the bytes into a stable, NUL-terminated buffer.
		intptr_t len = static_cast<intptr_t>(s.size());
		char *buf = static_cast<char *>(sys_alloc(len + 1));
		if (len > 0)
			std::memcpy(buf, s.data(), static_cast<size_t>(len));
		buf[len] = '\0';
		std::string_view stored(buf, static_cast<size_t>(len));

		Symbol sym{static_cast<uint32_t>(names.size())};
		names.push_back(stored);
		index.insert(stored, sym);
		return sym;
	}

	std::string_view name(Symbol s)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (static_cast<intptr_t>(s.id) >= names.size())
			return std::string_view();
		return names[static_cast<intptr_t>(s.id)];
	}

	intptr_t count()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return names.size() - 1; // exclude the reserved id 0
	}
};

AtomTable &table()
{
	static AtomTable t;
	return t;
}

} // namespace

Symbol intern(std::string_view s)
{
	return table().intern(s);
}

Symbol intern(const char *bytes, intptr_t len)
{
	return table().intern(std::string_view(bytes, static_cast<size_t>(len)));
}

std::string_view symbol_name(Symbol s)
{
	return table().name(s);
}

intptr_t symbol_count()
{
	return table().count();
}

} // namespace phonometrica
