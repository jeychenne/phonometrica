// Phonometrica engine — Unicode-path file handle (see header).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/os/file.hpp>

#include <phon/core/variant.hpp>
#include <phon/os/file_system.hpp> // PHON_WINDOWS

#include <cstdio>

namespace phonometrica {

std::FILE *os_open_file(const String &path, const char *mode)
{
#if PHON_WINDOWS
	std::u16string up = path.to_utf16();
	std::wstring wpath(up.begin(), up.end());
	std::u16string um = String(mode).to_utf16();
	std::wstring wmode(um.begin(), um.end());
	return _wfopen(wpath.data(), wmode.data());
#else
	return std::fopen(path.data(), mode);
#endif
}

void File::skip_bom()
{
	if (!handle)
		return;
	unsigned char bom[3];
	size_t n = std::fread(bom, 1, 3, handle);
	if (!(n == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF))
		std::rewind(handle); // not a UTF-8 BOM: rewind so no bytes are lost
}

String File::read_all()
{
	String out;
	if (!handle)
		return out;
	char buf[8192];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof(buf), handle)) > 0)
		out.append(Substring(buf, n));
	return out;
}

String File::read_line()
{
	String out;
	if (!handle)
		return out;
	int c;
	while ((c = std::fgetc(handle)) != EOF)
	{
		if (c == '\n')
			break;
		out.push_back(static_cast<char>(c));
	}
	// Strip a trailing '\r' so a CRLF file reads the same as an LF file.
	if (out.size() > 0 && out.data()[out.size() - 1] == '\r')
		out.chop(out.size() - 1);
	return out;
}

List File::read_lines()
{
	List out;
	if (!handle)
		return out;
	for (;;)
	{
		int c = std::fgetc(handle);
		if (c == EOF)
			break;
		std::ungetc(c, handle); // there is at least one more line
		out.append(Variant(read_line().to_value()));
	}
	return out;
}

void File::write(const String &text)
{
	if (handle)
		std::fwrite(text.data(), 1, static_cast<size_t>(text.size()), handle);
}

void File::write_line(const String &text)
{
	write(text);
	if (handle)
		std::fputc('\n', handle);
}

} // namespace phonometrica
