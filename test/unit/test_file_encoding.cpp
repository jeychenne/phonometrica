// Phonometrica engine — multi-encoding File reads (UTF-16/UTF-32, LE/BE).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Writes byte-exact fixtures (BOM + code units, explicit byte order so the test is
// host-endianness independent) and reads them back through the engine's `open_file` /
// `read`, checking they transcode to the expected UTF-8 — including a 4-byte codepoint
// (surrogate pair in UTF-16). Also covers BOM auto-detection and a forced encoding for a
// BOM-less file. Writing stays UTF-8, matching the old engine.

#include <phon/runtime.hpp>
#include <phon/string.hpp>
#include <phon/error.hpp>
#include <phon/engine/core/variant.hpp>
#include <phon/engine/types/file.hpp>

#include "test_framework.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

using namespace phonometrica;

namespace {

// "Aé€🎵" — one ASCII, one 2-byte, one 3-byte, and one 4-byte (astral) codepoint, the
// last of which needs a surrogate pair in UTF-16.
const char *kText = "Aé€🎵";

std::filesystem::path tmp_path(const char *name)
{
	return std::filesystem::temp_directory_path() / (std::string("phon_enc_") + name);
}

void write_bytes(const std::filesystem::path &p, const std::string &bytes)
{
	std::ofstream out(p, std::ios::binary);
	out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void put16(std::string &b, uint16_t u, bool be)
{
	if (be) { b.push_back(char(u >> 8)); b.push_back(char(u & 0xFF)); }
	else    { b.push_back(char(u & 0xFF)); b.push_back(char(u >> 8)); }
}
void put32(std::string &b, uint32_t u, bool be)
{
	if (be) { for (int s = 24; s >= 0; s -= 8) b.push_back(char((u >> s) & 0xFF)); }
	else    { for (int s = 0; s <= 24; s += 8) b.push_back(char((u >> s) & 0xFF)); }
}

std::string encode16(const std::u16string &u16, bool be, bool bom)
{
	std::string b;
	if (bom)
		put16(b, 0xFEFF, be);
	for (char16_t c : u16)
		put16(b, static_cast<uint16_t>(c), be);
	return b;
}
std::string encode32(const std::u32string &u32, bool be, bool bom)
{
	std::string b;
	if (bom)
		put32(b, 0xFEFF, be);
	for (char32_t c : u32)
		put32(b, static_cast<uint32_t>(c), be);
	return b;
}

// read( open_file(path[, mode, enc]) ) as a String, via a throwaway Runtime.
String read_through_engine(const std::string &open_expr)
{
	Runtime rt;
	return rt.do_string(String(("read(" + open_expr + ")").c_str())).to<String>();
}

} // namespace

TEST_CASE("file encoding: UTF-16/32 with a BOM auto-detect and transcode to UTF-8")
{
	const String expected(kText);
	std::u16string u16 = expected.to_utf16();
	std::u32string u32 = expected.to_utf32();

	struct Case { const char *name; std::string bytes; const char *enc; };
	Case cases[] = {
	    {"u16le_bom", encode16(u16, /*be=*/false, /*bom=*/true), "utf16-le"},
	    {"u16be_bom", encode16(u16, /*be=*/true, /*bom=*/true), "utf16-be"},
	    {"u32le_bom", encode32(u32, /*be=*/false, /*bom=*/true), "utf32-le"},
	    {"u32be_bom", encode32(u32, /*be=*/true, /*bom=*/true), "utf32-be"},
	};
	for (auto &c : cases)
	{
		auto p = tmp_path(c.name);
		write_bytes(p, c.bytes);
		std::string open_expr = "open_file(\"" + p.string() + "\")";
		CHECK(read_through_engine(open_expr) == expected);
		// The detected encoding is reported.
		Runtime rt;
		String enc = rt.do_string(String(("encoding(open_file(\"" + p.string() + "\"))").c_str()))
		                 .to<String>();
		CHECK(enc == String(c.enc));
		std::filesystem::remove(p);
	}
}

TEST_CASE("file encoding: a UTF-8 BOM is consumed and the rest reads as UTF-8")
{
	const String expected(kText);
	std::string bytes = "\xEF\xBB\xBF";
	bytes.append(expected.data(), static_cast<size_t>(expected.size()));
	auto p = tmp_path("u8bom");
	write_bytes(p, bytes);
	CHECK(read_through_engine("open_file(\"" + p.string() + "\")") == expected);
	std::filesystem::remove(p);
}

TEST_CASE("file encoding: a forced encoding reads a BOM-less UTF-16 file")
{
	const String expected(kText);
	auto p = tmp_path("u16le_nobom");
	write_bytes(p, encode16(expected.to_utf16(), /*be=*/false, /*bom=*/false));
	// Without a BOM and without a forced encoding this would be misread as UTF-8; forcing
	// utf16-le decodes it correctly.
	CHECK(read_through_engine("open_file(\"" + p.string() + "\", \"r\", \"utf16-le\")") == expected);
	std::filesystem::remove(p);
}

TEST_CASE("file encoding: line reads honour the encoding and strip CRLF")
{
	// Two CRLF-terminated lines in UTF-16BE-with-BOM.
	std::u16string u16 = String("line one\r\nlíne two\r\n").to_utf16();
	auto p = tmp_path("u16be_lines");
	write_bytes(p, encode16(u16, /*be=*/true, /*bom=*/true));

	Runtime rt;
	// Read the lines and check element count and values from the returned List.
	String joined =
	    rt.do_string(String(("var f = open_file(\"" + p.string() +
	                         "\")\nvar ls = read_lines(f)\nls[1] & \"|\" & ls[2] & \"|\" & len(ls)")
	                            .c_str()))
	        .to<String>();
	CHECK(joined == String("line one|líne two|2"));
	std::filesystem::remove(p);
}

// A1 stage 1: the standalone C++ File(path, Mode) ctor + format(), the app's direct
// file-I/O surface (old Phonometrica parity). Writing is UTF-8; the read ctor
// auto-detects the encoding from a BOM.
TEST_CASE("file: the path-opening ctor writes with format() and reads back")
{
	auto p = tmp_path("ctor_format.txt");
	{
		File out(String(p.string().c_str()), File::Write, Encoding::Utf8);
		out.format("xmin = %.2f\n", 1.5);
		out.format("name = \"%s\"\n", "café"); // UTF-8 bytes pass through
		out.write(String("done\n"));
	} // ~File closes it

	{
		File in(String(p.string().c_str())); // read, BOM auto-detect (none → UTF-8)
		CHECK(in.is_open());
		CHECK(in.encoding == Encoding::Utf8);
		CHECK(in.read_line() == String("xmin = 1.50"));
		CHECK(in.read_line() == String("name = \"café\""));
		CHECK(in.read_line() == String("done"));
		CHECK(in.at_end());
	}
	std::filesystem::remove(p);

	// Opening a nonexistent file for reading throws (standalone object → std::exception).
	bool threw = false;
	try
	{
		File bad(String("/nonexistent_dir_xyz/nope.txt"), File::Read);
	}
	catch (const std::exception &)
	{
		threw = true;
	}
	CHECK(threw);
}
