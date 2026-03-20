/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 19/07/2025                                                                                                 *
 *                                                                                                                     *
 * Purpose: memory pool (used for aliases).                                                                            *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_ARENA_HPP
#define PHONOMETRICA_ARENA_HPP

#include <vector>
#include <memory>
#include <type_traits>

#ifdef PHON_MULTITHREAD_ARENA
#include <mutex>
#endif

namespace phonometrica {

// Harmless but useless arena policy.
template<typename T>
struct ArenaPolicy
{
	static void mark(T *) { }

	static void unmark(T *) { }

	static bool marked(T *) { return true; }
};


template<typename T, class Policy = ArenaPolicy<T>, size_t PageSize = 4096>
class Arena final
{
private:

	// A rough estimate of the number of items that can fit in 4 memory pages.
	static constexpr size_t Count = (PageSize * 4 - sizeof(T)) / (sizeof(T) + sizeof(void*));

	static constexpr size_t last_index = Count - 1;


	typedef typename std::aligned_storage<sizeof(T), alignof(T)>::type Storage;

	struct Node
	{
		Storage value;
		Node *next;
	};


public:

	Arena()
	{
		pages.reserve(32);
		add_page();
	}

	Arena(const Arena &) = delete;

	Arena(Arena &&) = delete;

	~Arena() = default;

	// Return an uninitialized chunk of memory large enough to store a value of type T.
	void *alloc()
	{
#ifdef PHON_MULTITHREAD_ARENA
		mutex.lock();
#endif
		if (free_list == nullptr)
		{
			add_page();
		}

		Node *n = free_list;
		free_list = n->next;
#ifdef PHON_MULTITHREAD_ARENA
		mutex.unlock();
#endif
		mark(n);

		return &n->value;
	}

	// Put back value slot into the free list.
	void free(T *v)
	{
		auto n = reinterpret_cast<Node *>(v);

		unmark(n);
#ifdef PHON_MULTITHREAD_ARENA
		mutex.lock();
#endif
		n->next = free_list;
		free_list = n;
#ifdef PHON_MULTITHREAD_ARENA
		mutex.unlock();
#endif
	}

	// Remove memory pages which are unused. This is relatively unlikely to happen, but still worth trying...
	void compact()
	{
		auto count = pages.size();

		while (count-- > 0)
		{
			auto page = pages[count].get();
			bool used = false;

			for (size_t i = 0; i < Count; ++i)
			{
				if (marked(&page[i]))
				{
					used = true;
					break;
				}
			}

			if (!used)
			{
				// Remove page items from the free list.
				Node *start_address = &page[0];
				Node *end_address = &page[last_index];
				Node **node_address = &free_list;

				while (*node_address)
				{
					Node *n = *node_address;

					if (start_address <= n and n <= end_address)
					{
						// Detach node
						*node_address = n->next;
					}
					else
					{
						// Move to next node
						node_address = &n->next;
					}
				}

				pages.erase(pages.begin() + count);
			}
		}
	}

private:

	Node *free_list;

#ifdef PHON_MULTITHREAD_ARENA
	std::mutex mutex;
#endif

	// Keep track of all allocated pages
	std::vector<std::unique_ptr<Node[]>> pages;


	void mark(Node *n)
	{
		Policy::mark(reinterpret_cast<T*>(&n->value));
	}

	void unmark(Node *n)
	{
		Policy::unmark(reinterpret_cast<T*>(&n->value));
	}

	bool marked(Node *n) const
	{
		return Policy::marked(reinterpret_cast<T*>(&n->value));
	}

	void link_nodes(Node *page)
	{
		for (size_t i = 0; i < last_index; ++i)
		{
			page[i].next = &page[i + 1];
		}

		page[last_index].next = nullptr;
	}

	void add_page()
	{
		pages.emplace_back(new Node[Count]);
		auto page = pages.back().get();
		link_nodes(page);
		free_list = &page[0];
	}
};


} // namespace phonometrica

#endif // PHONOMETRICA_ARENA_HPP
