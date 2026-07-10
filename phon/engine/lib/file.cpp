// Phonometrica engine — file I/O standard library (architecture §12).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// The script `File` type and its operations, ported from the old engine's
// func_file.hpp. `File` is registered as a reference class (add_class<File>) whose
// finalizer closes the handle; instances come from `open(path[, mode])` (a factory —
// the class is not script-constructible). Paths are opened Unicode-correctly through
// os_open_file (phon/os/file.*); content is UTF-8.

#include <phon/engine/lib/lib.hpp>
#include <phon/engine/object/class.hpp>
#include <phon/engine/types/file.hpp>
#include <phon/engine/runtime/native_traits.hpp>
#include <phon/engine/types/list.hpp>
#include <phon/engine/types/string.hpp>
#include <phon/engine/vm/interpreter.hpp> // stringify (write_lines)
#include <phon/engine/vm/isolate.hpp>

#include <string>

namespace phonometrica {

namespace {

// Map a script mode ("r"/"w"/"a"/"r+"/"w+"/"a+") to a binary C stdio mode (binary so
// line-ending translation is ours, not the platform's), plus the writable/at-start
// flags. Returns false on an unknown mode.
bool decode_mode(const String &mode, std::string &c_mode, bool &writable, bool &at_start)
{
	std::string m(mode.data(), static_cast<size_t>(mode.size()));
	if (m == "r") { c_mode = "rb"; writable = false; at_start = true; }
	else if (m == "w") { c_mode = "wb"; writable = true; at_start = false; }
	else if (m == "a") { c_mode = "ab"; writable = true; at_start = false; }
	else if (m == "r+") { c_mode = "r+b"; writable = true; at_start = true; }
	else if (m == "w+") { c_mode = "w+b"; writable = true; at_start = true; }
	else if (m == "a+") { c_mode = "a+b"; writable = true; at_start = false; }
	else return false;
	return true;
}

Handle<File> do_open(Isolate &iso, const String &path, const char *c_mode, bool writable,
                     bool skip_bom)
{
	std::FILE *h = os_open_file(path, c_mode);
	if (!h)
		iso.raise(String("[System error] cannot open file '") + path.view() + "'", 0);
	Handle<File> f = Handle<File>::make(h, writable);
	if (skip_bom)
		f->skip_bom();
	return f;
}

File *checked(Isolate &iso, const Handle<File> &f)
{
	if (!f->is_open())
		iso.raise(String("[System error] the file is closed"), 0);
	return f.get();
}

} // namespace

void register_file_lib()
{
	// Register the File reference class (once). Must precede the function registrations
	// below, whose Handle<File> parameters dispatch on class_of<File>().
	if (!class_of<File>())
		add_class<File>("File", get_class(CID_OBJECT));

	// open_file(path): read, UTF-8. open_file(path, mode): explicit mode. (`open` is a
	// reserved class/function modifier keyword, so the file opener is `open_file`.)
	register_function("open_file", [](Isolate &iso, const String &path) {
		return do_open(iso, path, "rb", false, /*skip_bom=*/true);
	});
	register_function("open_file", [](Isolate &iso, const String &path, const String &mode) {
		std::string c_mode;
		bool writable, at_start;
		if (!decode_mode(mode, c_mode, writable, at_start))
			iso.raise(String("[Argument error] invalid file mode '") + mode.view() + "'", 0);
		// Only skip a BOM when the cursor genuinely starts at byte 0 in a readable file.
		bool skip_bom = at_start && (mode.data()[0] == 'r');
		return do_open(iso, path, c_mode.c_str(), writable, skip_bom);
	});

	// --- reading ---
	register_function("read", [](Isolate &iso, Handle<File> f) { return checked(iso, f)->read_all(); });
	register_function("read_line",
	                  [](Isolate &iso, Handle<File> f) { return checked(iso, f)->read_line(); });
	register_function("read_lines",
	                  [](Isolate &iso, Handle<File> f) { return checked(iso, f)->read_lines(); });
	register_function("eof", [](Handle<File> f) { return f->at_end(); });

	// read_file(path): open, read all, close — a convenience for the common case.
	register_function("read_file", [](Isolate &iso, const String &path) {
		return do_open(iso, path, "rb", false, true)->read_all();
	});

	// --- writing ---
	register_function("write",
	                  [](Isolate &iso, Handle<File> f, const String &text) { checked(iso, f)->write(text); });
	register_function("write_line", [](Isolate &iso, Handle<File> f, const String &text) {
		checked(iso, f)->write_line(text);
	});
	register_function("write_lines", [](Isolate &iso, Handle<File> f, List lines) {
		File *file = checked(iso, f);
		for (intptr_t i = 1; i <= lines.size(); ++i)
			file->write_line(stringify(lines.get(i).value()));
	});

	// --- positioning / lifetime ---
	register_function("close", [](Handle<File> f) { f->close(); });
	register_function("rewind", [](Handle<File> f) { f->rewind(); });
	register_function("tell", [](Handle<File> f) { return f->tell(); });
	register_function("seek", [](Handle<File> f, int64_t pos) { f->seek(pos); });
}

} // namespace phonometrica
