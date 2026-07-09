// Phonometrica engine — cross-platform, Unicode-correct path routines.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Vendored and adapted from Phonometrica's phon/utils/file_system.* so the new engine
// handles Unicode paths identically on every platform: on Windows the wide Win32 API
// (GetFullPathNameW, FindFirstFileW, …) is driven from a UTF-16 conversion of the
// engine's UTF-8 String (to_wide, below); on POSIX the UTF-8 bytes are passed straight
// to the C library. This is the layer the `system` stdlib module (lib/system.cpp) is
// built on. Failures throw FileSystemError; the stdlib wrapper re-raises them as a
// script [System error].

#ifndef PHON_OS_FILE_SYSTEM_HPP
#define PHON_OS_FILE_SYSTEM_HPP

#include <phon/types/list.hpp>
#include <phon/types/string.hpp>

#include <stdexcept>
#include <string>
#include <utility>

// Platform detection (the engine had none before — Linux-only in practice). POSIX
// covers Linux and macOS; everything wide-API goes through PHON_WINDOWS.
#if defined(_WIN32)
	#define PHON_WINDOWS 1
	#define PHON_POSIX 0
#else
	#define PHON_WINDOWS 0
	#define PHON_POSIX 1
#endif

namespace phonometrica {

// A filesystem operation that failed at the OS boundary (thrown by the routines below,
// caught in lib/system.cpp and re-raised as a script error).
class FileSystemError final : public std::runtime_error
{
public:
	explicit FileSystemError(const std::string &msg) : std::runtime_error(msg) {}
};

namespace filesystem {

// Absolute, canonical path (expands a leading '~'). Windows: GetFullPathNameW; POSIX:
// realpath.
String full_path(const String &relative_path);

// The current user's home directory ($HOME / %USERPROFILE% / SHGetFolderPathW).
String user_directory();

// The process working directory.
String current_directory();
void set_current_directory(const String &path);

// The native path separator ("/" or "\\").
String separator();

// Join two path components with exactly one separator between them.
String join(std::string_view s1, std::string_view s2);
template<typename... Args>
String join(std::string_view s1, std::string_view s2, Args... args)
{
	return join(join(s1, s2), args...);
}

// The system temporary directory, and a fresh unique path inside it.
String temp_directory();
String temp_filename();

// The last path component (after the final separator).
String base_name(std::string_view path);
// Everything up to (not including) the final separator.
String directory_name(const String &path);

bool create_directory(const String &path);
bool remove_directory(const String &dir, bool recursive);
bool remove_file(const String &path);
bool remove(const String &path); // dispatches to remove_directory / remove_file

// Directory entries (names only), sorted; hidden (dotfile) entries optional. Returns a
// List of String, one per entry.
List list_directory(const String &path, bool include_hidden = false);

bool exists(const String &path);
bool is_directory(const String &path);
bool is_file(const String &path);

void rename(const String &old_name, const String &new_name);

// (base, extension) with the dot kept on the extension; extension empty if none.
std::pair<String, String> split_ext(const String &path);
String ext(const String &path, bool lower = false, bool strip_dot = false);
String strip_ext(const String &path);

// Separator conversion (no-ops on POSIX); return a new String (value semantics).
String nativize(const String &path);
String genericize(const String &path);

} // namespace filesystem
} // namespace phonometrica

#endif // PHON_OS_FILE_SYSTEM_HPP
