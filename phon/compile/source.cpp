// Phonometrica engine — source buffers. See header.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/compile/source.hpp>

#include <cstdio>
#include <stdexcept>

namespace phonometrica {

Source Source::from_string(std::string text, std::string name)
{
	Source src;
	src.m_text = std::move(text);
	src.m_name = std::move(name);
	src.index_lines();
	return src;
}

Source Source::from_file(const std::string &path)
{
	std::FILE *f = std::fopen(path.c_str(), "rb");
	if (!f)
		throw std::runtime_error("cannot open file: " + path);

	std::string text;
	char buf[65536];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
		text.append(buf, n);
	std::fclose(f);

	return from_string(std::move(text), path);
}

void Source::index_lines()
{
	m_line_starts.clear();
	m_line_starts.push_back(0);
	for (size_t i = 0; i < m_text.size(); ++i)
	{
		if (m_text[i] == '\n')
			m_line_starts.push_back(static_cast<intptr_t>(i + 1));
	}
	// A trailing '\n' produces a phantom line start at end-of-buffer; drop it so
	// line_count() reflects content lines (an empty buffer keeps its single line).
	if (m_line_starts.size() > 1 && m_line_starts.back() == static_cast<intptr_t>(m_text.size()))
		m_line_starts.pop_back();
}

std::string_view Source::line(intptr_t n) const noexcept
{
	if (n < 1 || n > line_count())
		return {};

	intptr_t start = m_line_starts[static_cast<size_t>(n - 1)];
	intptr_t end;
	if (n < line_count())
		end = m_line_starts[static_cast<size_t>(n)] - 1; // exclude the '\n'
	else
		end = static_cast<intptr_t>(m_text.size());

	// A CRLF line ends with '\r' just before the '\n' we already excluded.
	if (end > start && m_text[static_cast<size_t>(end - 1)] == '\r')
		--end;

	return std::string_view(m_text.data() + start, static_cast<size_t>(end - start));
}

std::string Source::caret(intptr_t line_no, intptr_t column, intptr_t length) const
{
	std::string_view src = line(line_no);

	// Expand tabs to 4 spaces so the caret aligns under a fixed-width render, and
	// track how the caret column shifts as we do.
	std::string rendered;
	intptr_t caret_col = 0;
	for (intptr_t i = 0; i < static_cast<intptr_t>(src.size()); ++i)
	{
		if (i == column)
			caret_col = static_cast<intptr_t>(rendered.size());
		if (src[static_cast<size_t>(i)] == '\t')
			rendered.append(4, ' ');
		else
			rendered.push_back(src[static_cast<size_t>(i)]);
	}
	if (column >= static_cast<intptr_t>(src.size()))
		caret_col = static_cast<intptr_t>(rendered.size());

	std::string out;
	out.append(rendered);
	out.push_back('\n');
	out.append(static_cast<size_t>(caret_col), ' ');
	out.push_back('^');
	for (intptr_t i = 1; i < length; ++i)
		out.push_back('~');
	return out;
}

} // namespace phonometrica
