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
 * Purpose: Provide the missing CppAD::local::temp_file() symbol. CppAD is vendored header-only, but its debug-mode   *
 *          NaN checking (guarded by #ifndef NDEBUG in forward.hpp) calls this function whose implementation lives in  *
 *          a .cpp source file we don't compile. This stub satisfies the linker in debug builds.                        *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cstdio>
#include <cstdlib>
#include <string>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <cppad/local/temp_file.hpp>

namespace CppAD { namespace local {

std::string temp_file(void)
{
#ifdef _WIN32
	char buf[L_tmpnam];
	if (std::tmpnam(buf)) return std::string(buf);
	return "cppad_tmp";
#else
	std::string tpl = "/tmp/cppad_XXXXXX";
	int fd = mkstemp(&tpl[0]);
	if (fd >= 0) {
		close(fd);
		return tpl;
	}
	return "/tmp/cppad_tmp";
#endif
}

} } // namespace CppAD::local
