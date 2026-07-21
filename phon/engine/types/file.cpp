// Phonometrica engine — Unicode-path file handle (see header).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/types/file.hpp>

#include <phon/engine/base/file_system.hpp> // PHON_WINDOWS
#include <phon/engine/base/unicode.hpp>      // utf16_decode, REPLACEMENT
#include <phon/engine/core/variant.hpp>

#include <bit>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

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

namespace {

constexpr bool host_big_endian = (std::endian::native == std::endian::big);

uint16_t bswap16(uint16_t x) noexcept { return static_cast<uint16_t>((x << 8) | (x >> 8)); }
uint32_t bswap32(uint32_t x) noexcept
{
	return (x << 24) | ((x & 0x0000FF00u) << 8) | ((x & 0x00FF0000u) >> 8) | (x >> 24);
}

bool file_big_endian(Encoding e) noexcept
{
	return e == Encoding::Utf16be || e == Encoding::Utf32be;
}

} // namespace

bool encoding_from_name(const String &name, Encoding &out)
{
	std::string n(name.data(), static_cast<size_t>(name.size()));
	if (n == "utf-8" || n == "utf8" || n == "ascii") { out = Encoding::Utf8; return true; }
	if (n == "utf-16" || n == "utf16")
	{
		out = host_big_endian ? Encoding::Utf16be : Encoding::Utf16le;
		return true;
	}
	if (n == "utf-32" || n == "utf32")
	{
		out = host_big_endian ? Encoding::Utf32be : Encoding::Utf32le;
		return true;
	}
	if (n == "utf16-le" || n == "utf-16-le") { out = Encoding::Utf16le; return true; }
	if (n == "utf16-be" || n == "utf-16-be") { out = Encoding::Utf16be; return true; }
	if (n == "utf32-le" || n == "utf-32-le") { out = Encoding::Utf32le; return true; }
	if (n == "utf32-be" || n == "utf-32-be") { out = Encoding::Utf32be; return true; }
	return false;
}

const char *encoding_name(Encoding enc)
{
	switch (enc)
	{
		case Encoding::Utf8: return "utf-8";
		case Encoding::Utf16le: return "utf16-le";
		case Encoding::Utf16be: return "utf16-be";
		case Encoding::Utf32le: return "utf32-le";
		case Encoding::Utf32be: return "utf32-be";
		case Encoding::Undefined: return "utf-8"; // never stored; ctor sentinel only
	}
	return "utf-8";
}

namespace {
// Map a File::Mode to a stdio mode string (binary, so byte offsets are exact) plus the
// read/write disposition, mirroring the script open_file's decode_mode.
const char *mode_to_cmode(File::Mode m, bool &writable, bool &at_start) noexcept
{
	switch (m)
	{
		case File::Read: writable = false; at_start = true; return "rb";
		case File::Write: writable = true; at_start = false; return "wb";
		case File::Append: writable = true; at_start = false; return "ab";
		case File::ReadPlus: writable = true; at_start = true; return "r+b";
		case File::WritePlus: writable = true; at_start = true; return "w+b";
		case File::AppendPlus: writable = true; at_start = false; return "a+b";
		default: break;
	}
	writable = false;
	at_start = true;
	return "rb";
}
} // namespace

File::File(const String &path, Mode mode, Encoding enc)
{
	if (path.empty())
		throw std::runtime_error("[System error] cannot open a file with an empty path");
	bool w, at_start;
	const char *c_mode = mode_to_cmode(mode, w, at_start);
	handle = os_open_file(path, c_mode);
	if (!handle)
		throw std::runtime_error(std::string("[System error] cannot open file '") +
		                         std::string(path.data(), static_cast<size_t>(path.size())) + "'");
	writable = w;
	encoding = Encoding::Utf8;
	// A read that starts at byte 0 BOM-sniffs the encoding (UTF-8 default); a forced
	// non-Undefined encoding then overrides it for a BOM-less file. Writing is UTF-8.
	if (!w && at_start)
	{
		detect_encoding();
		if (enc != Encoding::Undefined)
			encoding = enc;
	}
}

void File::format(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if (static_cast<size_t>(n) < sizeof(buf))
	{
		write(String(std::string_view(buf, static_cast<size_t>(n))));
		return;
	}
	std::string big(static_cast<size_t>(n) + 1, '\0');
	va_start(ap, fmt);
	std::vsnprintf(big.data(), big.size(), fmt, ap);
	va_end(ap);
	write(String(std::string_view(big.data(), static_cast<size_t>(n))));
}

void File::detect_encoding()
{
	if (!handle)
		return;
	unsigned char bom[4] = {0, 0, 0, 0};
	size_t n = std::fread(bom, 1, 4, handle);

	// UTF-32 BOMs must be tested before the UTF-16 ones: a UTF-32LE BOM (FF FE 00 00)
	// begins with the UTF-16LE BOM (FF FE). Position the cursor just past the matched BOM.
	if (n >= 4 && bom[0] == 0x00 && bom[1] == 0x00 && bom[2] == 0xFE && bom[3] == 0xFF)
	{
		encoding = Encoding::Utf32be;
		std::fseek(handle, 4, SEEK_SET);
	}
	else if (n >= 4 && bom[0] == 0xFF && bom[1] == 0xFE && bom[2] == 0x00 && bom[3] == 0x00)
	{
		encoding = Encoding::Utf32le;
		std::fseek(handle, 4, SEEK_SET);
	}
	else if (n >= 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF)
	{
		encoding = Encoding::Utf8;
		std::fseek(handle, 3, SEEK_SET);
	}
	else if (n >= 2 && bom[0] == 0xFE && bom[1] == 0xFF)
	{
		encoding = Encoding::Utf16be;
		std::fseek(handle, 2, SEEK_SET);
	}
	else if (n >= 2 && bom[0] == 0xFF && bom[1] == 0xFE)
	{
		encoding = Encoding::Utf16le;
		std::fseek(handle, 2, SEEK_SET);
	}
	else
	{
		encoding = Encoding::Utf8; // no BOM: default, and put every byte back
		std::rewind(handle);
	}
}

bool File::next_codepoint(char32_t &cp)
{
	const bool swap = (file_big_endian(encoding) != host_big_endian);
	if (encoding == Encoding::Utf32le || encoding == Encoding::Utf32be)
	{
		uint32_t u;
		if (std::fread(&u, 4, 1, handle) != 1)
			return false;
		if (swap)
			u = bswap32(u);
		cp = (u <= 0x10FFFFu && !(u >= 0xD800u && u <= 0xDFFFu)) ? u : unicode::REPLACEMENT;
		return true;
	}
	// UTF-16: read one code unit, and a trailing unit when it is a high surrogate.
	uint16_t units[2];
	if (std::fread(&units[0], 2, 1, handle) != 1)
		return false;
	if (swap)
		units[0] = bswap16(units[0]);
	const uint16_t *end = units + 1;
	if (units[0] >= 0xD800u && units[0] <= 0xDBFFu)
	{
		if (std::fread(&units[1], 2, 1, handle) == 1)
		{
			if (swap)
				units[1] = bswap16(units[1]);
			end = units + 2;
		}
	}
	bool valid = false;
	char32_t out = unicode::REPLACEMENT;
	unicode::utf16_decode(units, end, &out, &valid);
	cp = out; // utf16_decode already substitutes U+FFFD on an invalid/unpaired surrogate
	return true;
}

String File::read_all(const String &path, Encoding enc)
{
	File infile(path, Read, enc);
	return infile.read_all();
}

String File::read_all()
{
	String out;
	if (!handle)
		return out;
	if (encoding == Encoding::Utf8)
	{
		char buf[8192];
		size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), handle)) > 0)
			out.append(Substring(buf, n));
		return out;
	}
	// UTF-16/32: transcode every codepoint to UTF-8.
	char32_t cp;
	while (next_codepoint(cp))
		out.append(cp);
	return out;
}

String File::read_line()
{
	String out;
	if (!handle)
		return out;
	if (encoding == Encoding::Utf8)
	{
		int c;
		while ((c = std::fgetc(handle)) != EOF)
		{
			if (c == '\n')
				break;
			out.push_back(static_cast<char>(c));
		}
	}
	else
	{
		char32_t cp;
		while (next_codepoint(cp))
		{
			if (cp == U'\n')
				break;
			out.append(cp);
		}
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
	while (!at_end())
		out.append(Variant(read_line().to_value()));
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
