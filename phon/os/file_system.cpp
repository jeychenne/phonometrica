// Phonometrica engine — Unicode-correct path routines (see header).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/os/file_system.hpp>

#include <phon/core/variant.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#if PHON_WINDOWS
	#include <windows.h>
	// clang-format off
	#include <shlwapi.h>
	#include <shlobj.h>
	// clang-format on
#else
	#include <dirent.h>
	#include <sys/stat.h>
	#include <sys/types.h>
	#include <unistd.h>
#endif

namespace phonometrica {
namespace filesystem {

#if PHON_WINDOWS
	#define PHON_SEP_STR "\\"
	#define PHON_SEP_CHAR '\\'
	#define PHON_HOME_ENV "USERPROFILE"
#else
	#define PHON_SEP_STR "/"
	#define PHON_SEP_CHAR '/'
	#define PHON_HOME_ENV "HOME"
#endif

namespace {

constexpr intptr_t MAX_SIZE = 4096;

std::string sys_error_message()
{
#if PHON_WINDOWS
	DWORD code = GetLastError();
	if (code == 0)
		return "unknown error";
	LPSTR buf = nullptr;
	DWORD n = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
	                             FORMAT_MESSAGE_IGNORE_INSERTS,
	                         nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
	                         reinterpret_cast<LPSTR>(&buf), 0, nullptr);
	std::string msg = n && buf ? std::string(buf, n) : "unknown error";
	if (buf)
		LocalFree(buf);
	return msg;
#else
	return std::strerror(errno);
#endif
}

void check_end(String &path)
{
	if (path.empty() || path.data()[path.size() - 1] != PHON_SEP_CHAR)
		path.push_back(PHON_SEP_CHAR);
}

#if PHON_WINDOWS
// UTF-8 String -> UTF-16 wide string for the wide Win32 API (wchar_t is 16-bit here).
std::wstring to_wide(const String &s)
{
	std::u16string u = s.to_utf16();
	return std::wstring(u.begin(), u.end());
}
std::wstring to_wide(const char *s)
{
	return to_wide(String(s));
}
// UTF-16 (Win32) -> engine UTF-8 String.
String from_wide(const wchar_t *w, size_t len)
{
	std::u16string u(reinterpret_cast<const char16_t *>(w), len);
	return String::from_utf16(u);
}
#endif

} // namespace

String separator()
{
	return String(PHON_SEP_STR);
}

String full_path(const String &relative_path)
{
	// Expand a leading '~' to the home directory (both platforms).
	if (relative_path.size() > 0 && relative_path.data()[0] == '~')
	{
		const char *home = std::getenv(PHON_HOME_ENV);
		String result(home ? home : "");
		result.append(Substring(relative_path.data() + 1,
		                        static_cast<size_t>(relative_path.size() - 1)));
		return result;
	}

	String result;
#if PHON_WINDOWS
	std::wstring in = to_wide(String("\\\\?\\") + relative_path.view()); // Unicode long-path prefix
	std::wstring out(MAX_SIZE, 0);
	DWORD size = GetFullPathNameW(in.data(), MAX_SIZE, out.data(), nullptr);
	if (size > 0)
		result = from_wide(out.data(), size);
#else
	char buffer[MAX_SIZE];
	if (realpath(relative_path.data(), buffer) != nullptr)
		result = String(buffer);
#endif
	if (result.empty())
		throw FileSystemError("cannot resolve path \"" + std::string(relative_path.data(),
		                                                             static_cast<size_t>(
		                                                                 relative_path.size())) +
		                      "\": " + sys_error_message());
	return result;
}

String user_directory()
{
#if PHON_WINDOWS
	WCHAR path[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, path)))
		return from_wide(path, wcslen(path));
	return String();
#else
	const char *dir = std::getenv(PHON_HOME_ENV);
	return dir ? String(dir) : String();
#endif
}

String current_directory()
{
#if PHON_WINDOWS
	DWORD size = GetCurrentDirectoryW(0, nullptr);
	std::wstring dir(size, 0);
	if (GetCurrentDirectoryW(size, dir.data()) != 0)
		return from_wide(dir.data(), wcslen(dir.data()));
#else
	char dir[MAX_SIZE];
	if (getcwd(dir, MAX_SIZE) != nullptr)
		return String(dir);
#endif
	throw FileSystemError(sys_error_message());
}

void set_current_directory(const String &path)
{
	bool err;
#if PHON_WINDOWS
	err = (SetCurrentDirectoryW(to_wide(path).data()) == 0);
#else
	err = (chdir(path.data()) != 0);
#endif
	if (err)
		throw FileSystemError("cannot change directory to \"" +
		                      std::string(path.data(), static_cast<size_t>(path.size())) +
		                      "\": " + sys_error_message());
}

String join(std::string_view s1, std::string_view s2)
{
	if (s1.empty())
		return String(s2.data(), static_cast<intptr_t>(s2.size()));
	String path(s1.data(), static_cast<intptr_t>(s1.size()));
	check_end(path);
	path.append(Substring(s2.data(), s2.size()));
	return path;
}

String temp_directory()
{
#if PHON_WINDOWS
	wchar_t path[MAX_PATH];
	DWORD length = GetTempPathW(MAX_PATH, path);
	return from_wide(path, length);
#elif defined(P_tmpdir)
	return String(P_tmpdir);
#else
	return String("/tmp");
#endif
}

String temp_filename()
{
	// A random 24-hex-character name in the temp directory.
	static thread_local std::mt19937_64 rng(std::random_device{}());
	static const char *hex = "0123456789abcdef";
	std::uniform_int_distribution<int> pick(0, 15);
	std::string name = "phon_";
	for (int i = 0; i < 24; ++i)
		name.push_back(hex[pick(rng)]);
	return join(temp_directory(), name);
}

String base_name(std::string_view path)
{
	auto pos = path.rfind(PHON_SEP_CHAR);
	if (pos == std::string_view::npos)
		return String(path.data(), static_cast<intptr_t>(path.size()));
	++pos; // skip the separator
	return String(path.data() + pos, static_cast<intptr_t>(path.size() - pos));
}

String directory_name(const String &path)
{
	const char *str = path.data();
	for (intptr_t i = path.size(); i-- > 0;)
		if (str[i] == PHON_SEP_CHAR)
			return String(path.data(), i);
	return String();
}

bool exists(const String &path)
{
	if (path.empty())
		return false;
#if PHON_WINDOWS
	return PathFileExistsW(to_wide(path).data()) != 0;
#else
	struct stat st;
	return stat(path.data(), &st) == 0;
#endif
}

bool is_directory(const String &path)
{
	if (path.empty())
		return false;
#if PHON_WINDOWS
	return PathIsDirectoryW(to_wide(path).data()) != 0;
#else
	struct stat st;
	if (stat(path.data(), &st) != 0)
		return false;
	return S_ISDIR(st.st_mode);
#endif
}

bool is_file(const String &path)
{
	if (path.empty())
		return false;
#if PHON_WINDOWS
	auto w = to_wide(path);
	return PathFileExistsW(w.data()) && !PathIsDirectoryW(w.data());
#else
	struct stat st;
	if (stat(path.data(), &st) != 0)
		return false;
	return S_ISREG(st.st_mode);
#endif
}

bool create_directory(const String &path)
{
	bool err;
#if PHON_WINDOWS
	err = CreateDirectoryW(to_wide(path).data(), nullptr) == 0;
#else
	err = mkdir(path.data(), S_IXUSR | S_IRUSR | S_IWUSR | S_IRGRP | S_IXGRP) != 0;
#endif
	if (err)
		throw FileSystemError("cannot create directory \"" +
		                      std::string(path.data(), static_cast<size_t>(path.size())) +
		                      "\": " + sys_error_message());
	return true;
}

namespace {

// Enumerate a directory's entries (excluding "." / ".."), invoking `fn` per name.
template<class Fn>
void read_dir_entries(const String &path, Fn fn, bool include_hidden)
{
#if PHON_WINDOWS
	std::wstring spec = to_wide(path);
	spec.append(L"\\*");
	WIN32_FIND_DATAW ffd;
	HANDLE h = FindFirstFileW(spec.data(), &ffd);
	if (h == INVALID_HANDLE_VALUE)
		throw FileSystemError("cannot open directory \"" +
		                      std::string(path.data(), static_cast<size_t>(path.size())) + "\"");
	do
	{
		std::wstring name(ffd.cFileName, wcslen(ffd.cFileName));
		if (name == L"." || name == L"..")
			continue;
		if (include_hidden || name[0] != L'.')
			fn(from_wide(name.data(), name.size()));
	} while (FindNextFileW(h, &ffd) != 0);
	FindClose(h);
#else
	DIR *dir = opendir(path.data());
	if (dir == nullptr)
		throw FileSystemError("cannot open directory \"" +
		                      std::string(path.data(), static_cast<size_t>(path.size())) + "\"");
	dirent *entry;
	while ((entry = readdir(dir)) != nullptr)
	{
		if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, ".."))
			continue;
		if (include_hidden || entry->d_name[0] != '.')
			fn(String(entry->d_name));
	}
	closedir(dir);
#endif
}

} // namespace

List list_directory(const String &path, bool include_hidden)
{
	std::vector<String> names;
	read_dir_entries(path, [&](String s) { names.push_back(std::move(s)); }, include_hidden);
	std::sort(names.begin(), names.end());
	List out;
	for (auto &n : names)
		out.append(Variant(n.to_value()));
	return out;
}

bool remove_directory(const String &dir, bool recursive)
{
	if (!is_directory(dir))
		throw FileSystemError("\"" + std::string(dir.data(), static_cast<size_t>(dir.size())) +
		                      "\" is not a directory");
	if (recursive)
	{
		List entries = list_directory(dir, true);
		for (intptr_t i = 1; i <= entries.size(); ++i)
		{
			String name = String::from_value(entries.get(i).value());
			String path = join(dir, name);
			if (is_directory(path))
				remove_directory(path, true);
			else
				remove_file(path);
		}
	}
	bool err;
#if PHON_WINDOWS
	err = RemoveDirectoryW(to_wide(dir).data()) == 0;
#else
	err = rmdir(dir.data()) != 0;
#endif
	if (err)
		throw FileSystemError("cannot remove directory \"" +
		                      std::string(dir.data(), static_cast<size_t>(dir.size())) +
		                      "\": " + sys_error_message());
	return true;
}

bool remove_file(const String &path)
{
	if (!exists(path))
		throw FileSystemError("cannot remove \"" +
		                      std::string(path.data(), static_cast<size_t>(path.size())) +
		                      "\": no such file");
	bool err;
#if PHON_WINDOWS
	err = DeleteFileW(to_wide(path).data()) == 0;
#else
	err = std::remove(path.data()) != 0;
#endif
	if (err)
		throw FileSystemError("cannot remove file \"" +
		                      std::string(path.data(), static_cast<size_t>(path.size())) +
		                      "\": " + sys_error_message());
	return true;
}

bool remove(const String &path)
{
	return is_directory(path) ? remove_directory(path, true) : remove_file(path);
}

void rename(const String &old_name, const String &new_name)
{
	int result;
#if PHON_WINDOWS
	result = _wrename(to_wide(old_name).data(), to_wide(new_name).data());
#else
	result = std::rename(old_name.data(), new_name.data());
#endif
	if (result != 0)
		throw FileSystemError("cannot rename \"" +
		                      std::string(old_name.data(), static_cast<size_t>(old_name.size())) +
		                      "\": " + sys_error_message());
}

std::pair<String, String> split_ext(const String &path)
{
	const char *data = path.data();
	intptr_t i;
	for (i = path.size(); i-- > 0;)
	{
		if (data[i] == PHON_SEP_CHAR)
			return {path, String()}; // separator before any dot: no extension
		if (data[i] == '.')
			break;
	}
	if (i <= 0)
		return {path, String()};
	return {String(data, i), String(data + i, path.size() - i)};
}

String strip_ext(const String &path)
{
	return split_ext(path).first;
}

String ext(const String &path, bool lower, bool strip_dot)
{
	const char *data = path.data();
	intptr_t i;
	for (i = path.size(); i-- > 0;)
	{
		if (data[i] == PHON_SEP_CHAR)
			return String();
		if (data[i] == '.')
		{
			if (strip_dot)
				++i;
			break;
		}
	}
	if (i < 0)
		return String();
	String result(data + i, path.size() - i);
	return lower ? result.to_lower() : result;
}

String nativize(const String &path)
{
#if PHON_WINDOWS
	std::string s(path.data(), static_cast<size_t>(path.size()));
	for (char &c : s)
		if (c == '/')
			c = '\\';
	return String(s);
#else
	return path;
#endif
}

String genericize(const String &path)
{
#if PHON_WINDOWS
	std::string s(path.data(), static_cast<size_t>(path.size()));
	for (char &c : s)
		if (c == '\\')
			c = '/';
	return String(s);
#else
	return path;
#endif
}

} // namespace filesystem
} // namespace phonometrica
