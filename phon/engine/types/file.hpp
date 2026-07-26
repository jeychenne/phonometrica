// Phonometrica engine — a Unicode-path file handle (the script `File` type).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Wraps a C `FILE *` opened through os_open_file, which uses `_wfopen` with a UTF-16
// path on Windows and `fopen` on POSIX — so a script can open a file by its UTF-8 path
// on any platform (the same technique as Phonometrica's utils::open_file). `File` is a
// cell-headed reference class registered by lib/file.cpp via the embedding add_class<>
// path, so its finalizer (~File) closes the handle when the value dies.
//
// Reading is multi-encoding (ported from the old engine): the byte-order mark is sniffed
// on open (`detect_encoding`) and UTF-16/UTF-32, little- or big-endian, are transcoded to
// UTF-8 on the fly; an explicit encoding can also be forced for BOM-less files. Absent a
// BOM the encoding defaults to UTF-8. Writing is always UTF-8 (no BOM), as in the old
// engine — UTF-16/32 output was never supported.

#ifndef PHON_OS_FILE_HPP
#define PHON_OS_FILE_HPP

#include <phon/engine/core/cell.hpp>
#include <phon/engine/types/list.hpp>
#include <phon/engine/types/string.hpp>

#include <cstdio>

namespace phonometrica {

struct Class;

// Open a file by UTF-8 path with the given C stdio mode (Windows: wide _wfopen).
// Returns null on failure (the caller reports the error).
std::FILE *os_open_file(const String &path, const char *mode);

// The text encoding of a file's bytes. Only UTF-8 is written; the UTF-16/32 variants are
// read-only, distinguished by byte order (the endianness of the stored code units).
enum class Encoding : uint8_t
{
	Utf8,
	Utf16le,
	Utf16be,
	Utf32le,
	Utf32be,
	// Sentinel for the File(path, mode) ctor: "auto-detect from the BOM" (old
	// Phonometrica parity). Never stored on a File — the ctor resolves it to a concrete
	// encoding on open.
	Undefined,
};

// Map an encoding name ("utf-8"/"utf-16"/"utf16-le"/"utf16-be"/"utf-32"/… ) to an
// Encoding; "utf-16"/"utf-32" without a suffix pick the host's byte order. Returns false
// on an unknown name. `"" ` / "auto" is not handled here — that is the BOM-sniff path.
bool encoding_from_name(const String &name, Encoding &out);

// The canonical name of an encoding (for an `encoding(file)` query).
const char *encoding_name(Encoding enc);

// A plain C++ class — no engine machinery on it. Usable as a standalone stack object,
// and exposed to scripts by boxing (add_class<File> in lib/file.cpp; Handle<File>).
struct File
{
	std::FILE *handle = nullptr;
	bool writable = false;
	Encoding encoding = Encoding::Utf8;

	// Open modes (old Phonometrica parity). Bit flags so `mode & Read` / `mode & Write`
	// work; the Plus variants open for update ("+").
	enum Mode : uint8_t
	{
		Read = 1,
		Write = 2,
		Plus = 4,
		Append = 8,
		ReadPlus = Read | Plus,
		WritePlus = Write | Plus,
		AppendPlus = Append | Plus,
	};

	File(std::FILE *h, bool w) noexcept : handle(h), writable(w) {}

	// Open `path` in `mode` as a standalone C++ object (the app's direct file I/O; the
	// script surface goes through open_file instead). Throws std::runtime_error if the
	// file cannot be opened. On a read that starts at byte 0 the encoding is BOM-sniffed
	// (UTF-8 default); a non-Undefined `enc` then forces it for a BOM-less file. Writing
	// is always UTF-8 with no BOM.
	explicit File(const String &path, Mode mode = Read, Encoding enc = Encoding::Undefined);

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

	// Sniff a leading byte-order mark, set `encoding`, and position the cursor past the
	// BOM. With no BOM the encoding stays UTF-8 and the cursor returns to byte 0. Called
	// on open for a readable handle.
	void detect_encoding();

	String read_all();
	// Reads to the next '\n' (stripping a trailing '\r'). At end of file this returns
	// an empty string, indistinguishable from a blank line — test `at_end()` first if
	// the difference matters. The script-facing `read_line` wraps exactly that check
	// and returns `null` instead, so scripts can loop on it.
	String read_line();
	List read_lines();
	// Read a whole file in one call (old Phonometrica parity).
	static String read_all(const String &path, Encoding enc = Encoding::Undefined);
	void write(const String &text);
	void write(char c)
	{
		if (handle)
			std::fputc(c, handle);
	}
	void write_line(const String &text);
	// printf-formatted write (old Phonometrica parity): formats `fmt` and writes the
	// resulting UTF-8 bytes. Used by the app's text exporters (e.g. Praat TextGrids).
	void format(const char *fmt, ...);

private:
	// Read the next Unicode codepoint from a UTF-16/UTF-32 handle (honouring byte order),
	// returning false at end of file. Not used for UTF-8, which is read byte-wise.
	bool next_codepoint(char32_t &cp);
};

} // namespace phonometrica

#endif // PHON_OS_FILE_HPP
