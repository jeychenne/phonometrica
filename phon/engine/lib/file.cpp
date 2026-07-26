// Phonometrica engine — file I/O standard library (architecture §12).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// The script `File` type and its operations, ported from the old engine's
// func_file.hpp. `File` is registered as a reference class (add_class<File>) whose
// finalizer closes the handle; instances come from the `File(path[, mode])` factory
// generic, also registered as `open_file` — a foreign class is never allocated by NEW,
// so a call on the class object is redirected to the generic of the same name.
// Paths are opened Unicode-correctly through
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

// Open a file. When `detect` is set (a read starting at byte 0), the byte-order mark is
// sniffed to pick the encoding; a non-null `forced` then overrides it (for BOM-less
// UTF-16/32 files) while keeping the cursor past any BOM that was present.
Handle<File> do_open(Isolate &iso, const String &path, const char *c_mode, bool writable,
                     bool detect, const Encoding *forced = nullptr)
{
	std::FILE *h = os_open_file(path, c_mode);
	if (!h)
		iso.raise(String("[System error] cannot open file '") + path.view() + "'", 0);
	Handle<File> f = Handle<File>::make(h, writable);
	if (detect)
		f->detect_encoding();
	if (forced)
		f->encoding = *forced;
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

	// File(path): read, encoding auto-detected from the BOM (UTF-8 default).
	// File(path, mode): explicit mode, same auto-detection on read.
	// File(path, mode, encoding): force an encoding (for BOM-less UTF-16/32 files).
	// A factory generic under the class's own name: `File(…)` resolves to the class
	// object, and the lowerer redirects the call here (design §6). `open_file` is kept
	// as an alias — it predates constructor calls and reads well at a call site.
	// (`open` is a reserved class/function modifier keyword, so the alias is `open_file`.)
	for (const char *name : {"File", "open_file"})
	{
		register_function(name, [](Isolate &iso, const String &path) {
			return do_open(iso, path, "rb", false, /*detect=*/true);
		});
		register_function(name, [](Isolate &iso, const String &path, const String &mode) {
			std::string c_mode;
			bool writable, at_start;
			if (!decode_mode(mode, c_mode, writable, at_start))
				iso.raise(String("[Argument error] invalid file mode '") + mode.view() + "'", 0);
			// Auto-detect the encoding only when the cursor genuinely starts at byte 0 of a
			// readable file.
			bool detect = at_start && (mode.data()[0] == 'r');
			return do_open(iso, path, c_mode.c_str(), writable, detect);
		});
		register_function(name,
		                  [](Isolate &iso, const String &path, const String &mode, const String &enc) {
			std::string c_mode;
			bool writable, at_start;
			if (!decode_mode(mode, c_mode, writable, at_start))
				iso.raise(String("[Argument error] invalid file mode '") + mode.view() + "'", 0);
			Encoding forced;
			if (!encoding_from_name(enc, forced))
				iso.raise(String("[Argument error] unknown file encoding '") + enc.view() + "'", 0);
			bool detect = at_start && (mode.data()[0] == 'r');
			return do_open(iso, path, c_mode.c_str(), writable, detect, &forced);
		});
	}

	// --- reading ---
	register_function("read", [](Isolate &iso, Handle<File> f) { return checked(iso, f)->read_all(); });
	// read_line(file) -> String?  — `null` once the file is exhausted, so the reader
	// composes with `while var line = read_line(f) do`. The check is a peek *before*
	// reading, which is what keeps a genuine blank line in the middle of a file
	// distinct from the end: "a\n\nb\n" reads "a", "", "b", null.
	register_function("read_line", [](Isolate &iso, Handle<File> f) -> Variant {
		File *file = checked(iso, f);
		if (file->at_end())
			return Variant::null();
		return Variant::make(file->read_line());
	});
	register_function("read_lines",
	                  [](Isolate &iso, Handle<File> f) { return checked(iso, f)->read_lines(); });
	register_function("eof", [](Handle<File> f) { return f->at_end(); });
	// encoding(file): the file's text encoding, as detected/forced on open.
	register_function("encoding",
	                  [](Handle<File> f) { return String(encoding_name(f->encoding)); });

	// read_file(path): open, read all, close — a convenience for the common case. The
	// encoding is auto-detected from the BOM, so a UTF-16/32 file reads back as UTF-8.
	register_function("read_file", [](Isolate &iso, const String &path) {
		return do_open(iso, path, "rb", false, /*detect=*/true)->read_all();
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
