// Phonometrica engine — a Unicode-path file handle (the script `File` type).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Wraps a C `FILE *` opened through os_open_file, which uses `_wfopen` with a UTF-16
// path on Windows and `fopen` on POSIX — so a script can open a file by its UTF-8 path
// on any platform (the same technique as Phonometrica's utils::open_file). `File` is a
// cell-headed reference class registered by lib/file.cpp via the embedding add_class<>
// path, so its finalizer (~File) closes the handle when the value dies. Content is
// treated as UTF-8; a leading UTF-8 BOM is skipped on read.

#ifndef PHON_OS_FILE_HPP
#define PHON_OS_FILE_HPP

#include <phon/core/cell.hpp>
#include <phon/types/list.hpp>
#include <phon/types/string.hpp>

#include <cstdio>

namespace phonometrica {

struct Class;

// Open a file by UTF-8 path with the given C stdio mode (Windows: wide _wfopen).
// Returns null on failure (the caller reports the error).
std::FILE *os_open_file(const String &path, const char *mode);

struct File
{
	Cell header; // cell-headed: first member is the Cell (add_class<File> contract)
	std::FILE *handle = nullptr;
	bool writable = false;
	static inline Class *phon_class = nullptr;

	File(std::FILE *h, bool w) noexcept : handle(h), writable(w) {}
	~File() { close(); }

	bool is_open() const noexcept { return handle != nullptr; }
	void close() noexcept
	{
		if (handle)
		{
			std::fclose(handle);
			handle = nullptr;
		}
	}
	// True when no more data can be read. Peeks one byte (feof only latches *after* a
	// read past the end, so a file whose final newline was just consumed is not yet at
	// feof — the peek reports end correctly). Harmless on a write handle.
	bool at_end() noexcept
	{
		if (!handle)
			return true;
		int c = std::fgetc(handle);
		if (c == EOF)
			return true;
		std::ungetc(c, handle);
		return false;
	}
	void rewind() noexcept
	{
		if (handle)
			std::rewind(handle);
	}
	void seek(intptr_t pos) noexcept
	{
		if (handle)
			std::fseek(handle, static_cast<long>(pos), SEEK_SET);
	}
	intptr_t tell() const noexcept { return handle ? static_cast<intptr_t>(std::ftell(handle)) : 0; }

	// Skip a leading UTF-8 BOM (EF BB BF) if present; otherwise leave the position at 0.
	void skip_bom();

	String read_all();
	String read_line();
	List read_lines();
	void write(const String &text);
	void write_line(const String &text);
};

} // namespace phonometrica

#endif // PHON_OS_FILE_HPP
