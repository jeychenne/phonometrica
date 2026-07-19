/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 19/07/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: entry point of phon_stats, the headless statistics host (MIGRATION_NOTES step 4b). Registers the           *
 * statistics natives on a NEW-engine Runtime and runs the given script: `phon_stats <script.phon>`.                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cstdio>
#include <exception>
#include <string>

#include <phon/runtime.hpp>
#include <phon/string.hpp>

namespace phonometrica::stats_host {
void register_bindings(Runtime &rt);
}

int main(int argc, char **argv)
{
	using namespace phonometrica;

	const char *script = nullptr;
	Runtime rt;
	stats_host::register_bindings(rt);

	for (int i = 1; i < argc; ++i)
	{
		std::string arg(argv[i]);
		if (arg == "-I" && i + 1 < argc)
		{
			rt.add_import_path(String(argv[++i]));
		}
		else if (!script)
		{
			script = argv[i];
		}
		else
		{
			std::fprintf(stderr, "usage: phon_stats [-I import_dir]... <script.phon>\n");
			return 2;
		}
	}
	if (!script)
	{
		std::fprintf(stderr, "usage: phon_stats [-I import_dir]... <script.phon>\n");
		return 2;
	}

	try
	{
		rt.do_file(String(script));
	}
	catch (RuntimeError &e)
	{
		// Not a std::exception: the engine's script-error carrier (message + line).
		std::fprintf(stderr, "%.*s (line %d)\n", (int) e.message.size(), e.message.data(),
		             e.line);
		return 1;
	}
	catch (std::exception &e)
	{
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}

	return 0;
}
