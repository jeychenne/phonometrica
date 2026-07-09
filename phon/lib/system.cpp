// Phonometrica engine — system / filesystem standard library (architecture §12).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Path and directory functions, ported from the old engine's func_system.hpp. Every
// operation goes through phon/os/file_system.* — the vendored, Unicode-correct path
// layer — so a script manipulates UTF-8 paths that resolve to the right file on Linux,
// macOS, and Windows alike. OS failures surface as FileSystemError and are re-raised
// here as a script `[System error]`.

#include <phon/lib/lib.hpp>
#include <phon/os/file_system.hpp>
#include <phon/runtime/native_traits.hpp>
#include <phon/types/list.hpp>
#include <phon/types/string.hpp>
#include <phon/vm/isolate.hpp>

namespace phonometrica {

namespace fs = filesystem;

namespace {

// Run a filesystem op, converting a FileSystemError into a script [System error].
template<class F>
auto guarded(Isolate &iso, F &&f) -> decltype(f())
{
	try
	{
		return f();
	}
	catch (const FileSystemError &e)
	{
		iso.raise(String("[System error] ") + e.what(), 0);
	}
}

} // namespace

void register_system_lib()
{
	// --- pure path manipulation (no OS access, cannot fail) ---
	register_function("get_path_separator", [] { return fs::separator(); });
	register_function("get_base_name", [](const String &p) { return fs::base_name(p.view()); });
	register_function("get_directory", [](const String &p) { return fs::directory_name(p); });
	register_function("join_path",
	                  [](const String &a, const String &b) { return fs::join(a.view(), b.view()); });
	register_function("strip_extension", [](const String &p) { return fs::strip_ext(p); });
	register_function("get_extension", [](const String &p) { return fs::ext(p); });
	register_function("get_extension",
	                  [](const String &p, bool lower) { return fs::ext(p, lower); });
	register_function("nativize", [](const String &p) { return fs::nativize(p); });
	register_function("genericize", [](const String &p) { return fs::genericize(p); });
	register_function("split_extension", [](const String &p) {
		auto pair = fs::split_ext(p);
		List out;
		out.append(Variant(pair.first.to_value()));
		out.append(Variant(pair.second.to_value()));
		return out;
	});
	register_function("get_os_name", [] {
#if PHON_WINDOWS
		return String("windows");
#elif defined(__APPLE__)
		return String("macos");
#else
		return String("linux");
#endif
	});

	// --- directory/location queries (may hit the OS) ---
	register_function("get_current_directory",
	                  [](Isolate &iso) { return guarded(iso, [] { return fs::current_directory(); }); });
	register_function("get_user_directory", [] { return fs::user_directory(); });
	register_function("get_temp_directory", [] { return fs::temp_directory(); });
	register_function("get_temp_name", [] { return fs::temp_filename(); });
	register_function("get_full_path", [](Isolate &iso, const String &p) {
		return guarded(iso, [&] { return fs::full_path(p); });
	});
	register_function("exists", [](const String &p) { return fs::exists(p); });
	register_function("is_directory", [](const String &p) { return fs::is_directory(p); });
	register_function("is_document", [](const String &p) { return fs::is_file(p); });
	register_function("list_directory", [](Isolate &iso, const String &p) {
		return guarded(iso, [&] { return fs::list_directory(p, false); });
	});
	register_function("list_directory", [](Isolate &iso, const String &p, bool hidden) {
		return guarded(iso, [&] { return fs::list_directory(p, hidden); });
	});

	// --- mutating operations ---
	register_function("set_current_directory", [](Isolate &iso, const String &p) {
		guarded(iso, [&] {
			fs::set_current_directory(p);
			return 0;
		});
	});
	register_function("create_directory", [](Isolate &iso, const String &p) {
		guarded(iso, [&] { return fs::create_directory(p); });
	});
	register_function("remove_directory", [](Isolate &iso, const String &p) {
		guarded(iso, [&] { return fs::remove_directory(p, false); });
	});
	register_function("remove_directory", [](Isolate &iso, const String &p, bool recursive) {
		guarded(iso, [&] { return fs::remove_directory(p, recursive); });
	});
	register_function("remove_file", [](Isolate &iso, const String &p) {
		guarded(iso, [&] { return fs::remove_file(p); });
	});
	register_function("remove_path", [](Isolate &iso, const String &p) {
		guarded(iso, [&] { return fs::remove(p); });
	});
	register_function("rename", [](Isolate &iso, const String &from, const String &to) {
		guarded(iso, [&] {
			fs::rename(from, to);
			return 0;
		});
	});
}

} // namespace phonometrica
