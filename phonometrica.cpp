/***********************************************************************************************************************
*                                                                                                                      *
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
 * Created: 21/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: main application.                                                                                          *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/file.hpp>

#ifdef PHON_GUI
#include <clocale>
#include <cstdlib>
#include <QFile>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <QApplication>
#include <phon/gui/main_window.hpp>
#include <phon/application/settings.hpp>
#include <phon/application/project.hpp>
#include <phon/application/constants.hpp>
#else
#include <phon/runtime.hpp>
#endif

using namespace phonometrica;

static void show_usage()
{
	std::cout << "Usage: phonometrica [option] file" << std::endl;
	std::cout << "Options: " << std::endl;
	std::cout << " -l\t(list)\tlist bytecode (disassemble) file" << std::endl;
	std::cout << " -r\t(run)\texecute file" << std::endl;
	std::cout << " -a\t(all)\tdisassemble and execute file" << std::endl;
}

#ifdef PHON_GUI
// Decide whether the argv invocation is a script-interpreter call (handled in
// text mode) or a "open these files in the GUI" call. Script-interpreter mode
// is restricted to:
//   * `phonometrica -l|-r|-a <script>`  (explicit switches)
//   * `phonometrica <script.phon>`      (bare .phon file, no other args)
// Everything else — including any .phon-project, .phon-annot, .wav, .csv,
// directories, or multiple paths — goes to the GUI.
static bool argv_is_script_invocation(int argc, char **argv)
{
	if (argc <= 1)
		return false;

	String first(argv[1]);
	if (first == "-l" || first == "-r" || first == "-a")
		return true;

	// Bare single argument: only treat as a script if it actually has the
	// .phon extension. A .phon-project, .phon-annot, etc. is NOT a script —
	// those are project assets that should open in the GUI.
	if (argc == 2)
	{
		auto qpath = QString::fromUtf8(argv[1]);
		QFileInfo info(qpath);
		// QFileInfo::suffix() is the chars after the LAST dot. For "foo.phon"
		// → "phon"; for "foo.phon-project" → "phon-project". So a strict equality
		// check on "phon" naturally excludes the compound extensions.
		if (info.suffix().compare(QLatin1String("phon"), Qt::CaseInsensitive) == 0)
			return true;
	}

	return false;
}
#endif

// register_script_api — subset of initialize() that is safe to call
// without QApplication. Registers the scripting type system and global
// functions (load, fit, compare, ...) so script-interpreter mode can
// run scripts that use the full data/stats API. Does NOT touch:
//   - Settings I/O (no config file read/write)
//   - Audio device probing (no RtAudio sound-format setup)
//   - Bundled .phon initialization scripts (signal, speech_analysis, ...)
// All Qt resource access through QResource works without QApplication
// because compiled-in resources are registered automatically at static
// init time.
static void register_script_api(Runtime &rt)
{
#ifdef PHON_GUI
	// The `phon` namespace is a Table held as an isolate global (engine roadmap E2).
	// It is CoW: C++-side writers must re-fetch it via get_global and write back
	// through add_global (see Settings).
	rt.add_global("phon", Variant::make(Table()));
	Project::preinitialize(rt);
	Project::create(rt);
	Project::initialize(rt);
#endif
}

static void initialize(Runtime &rt)
{
#ifdef PHON_GUI
    // Set up the script loader from Qt resources.
    Settings::load_script = [](const String &name) -> String {
        auto qname = QString::fromUtf8(name.data(), (int) name.size());
        QFile file(QString(":/std/%1.phon").arg(qname));
        if (!file.open(QIODevice::ReadOnly))
        {
            throw error("Cannot load bundled script \"%\"", name);
        }
        auto bytes = file.readAll();
        return {bytes.constData(), bytes.size()};
    };

	register_script_api(rt);
	Settings::initialize(&rt);
	Settings::read();

	Sound::set_sound_formats();

	run_script(rt, initialize);
	run_script(rt, signal);
	// NOTE: speech_analysis.phon is loaded in MainWindow::postInitialize(), not
	// here: it calls natives registered by setShellFunctions() (get_current_sound
	// & co.), and the new engine resolves names at compile time — running it
	// before the window exists is a compile error.
#endif // PHON_GUI
}

static void finalize(Runtime &)
{
#ifdef PHON_GUI
	Settings::write();
#endif
}

int main(int argc, char **argv)
{
#ifdef PHON_GUI
	// Decide upfront: text mode (script interpreter) vs GUI mode (with
	// optional argv file paths). The GUI is the default when arguments are
	// passed; only -l/-r/-a switches and bare .phon scripts opt into text mode.
	bool text_mode = argv_is_script_invocation(argc, argv);
#else
	bool text_mode = (argc > 1);
#endif

	if (text_mode)
	{
		// Note: the old Runtime took argv[0] to derive the resources directory on
		// Windows/macOS (Settings uses it there); the new engine has no program_path.
		// TODO(A7): restore a program-path channel for the Windows/macOS builds.
		Runtime runtime;

		// Register the scripting API (data/stats/document) so scripts run
		// from the command line have access to load(), fit(), compare(),
		// and the full type system. This is a subset of the full GUI
		// initialize() that omits settings I/O, audio device probing, and
		// bundled .phon scripts (none of which a script-runner needs).
		register_script_api(runtime);

		int error_code = 0;

		try
		{
			if (argc > 2)
			{
				String option(argv[1]), path(argv[2]);

				if (option == "-l")
				{
					auto listing = runtime.disassemble(File::read_all(path));
					utils::print(stdout, listing.c_str());
				}
				else if (option == "-r")
				{
					runtime.do_file(path);
				}
				else if (option == "-a")
				{
					auto listing = runtime.disassemble(File::read_all(path));
					utils::print(stdout, listing.c_str());
					runtime.do_file(path);
				}
				else
				{
					show_usage();
					error_code = 1;
				}
			}
			else
			{
				String path(argv[1]);
				runtime.do_file(path);
			}
		}
		catch (RuntimeError &e)
		{
			utils::fprintf(stderr, "Error on line %:\n", e.line);
			utils::print(stderr, e.what());
			utils::print(stderr, "\n");
			// Structured backtrace (innermost first), copied off the script
			// Error value at the do_string/do_file boundary.
			for (const auto &fr : e.frames)
			{
				utils::fprintf(stderr, "  at % (line %)", fr.function.c_str(), fr.line);
				if (!fr.file.empty())
					utils::fprintf(stderr, " in %", fr.file.c_str());
				utils::print(stderr, "\n");
			}
			error_code = 1;
		}
		catch (std::bad_alloc &)
		{
			utils::print(stderr, "out of memory error\n");
			error_code = 1;
		}
		catch (std::exception &e)
		{
			utils::print(stderr, e.what());
			utils::print(stderr, "\n");
			error_code = 1;
		}

#ifdef PHON_GUI
		// The project holds engine handles: it must die before the Runtime.
		// Left to its static destructor, it would be destroyed in atexit
		// handlers, after the Runtime — a use-after-free.
		Project::destroy();
#endif
		return error_code;
	}

	// GUI mode (with or without argv file paths).
#ifdef PHON_GUI
	QApplication app(argc, argv);

	// QApplication sets LC_ALL to the system locale, which breaks strtod/sscanf
	// parsing of numbers with '.' as decimal separator (e.g. French locale uses ',').
	// Force LC_NUMERIC back to "C" so that pugixml's as_double(), strtod(), sscanf("%lf")
	// and all other C-level numeric parsing use '.' consistently.
	std::setlocale(LC_NUMERIC, "C");
	QApplication::setApplicationName("Phonometrica");
	QApplication::setOrganizationName("Phonometrica");
	QGuiApplication::setDesktopFileName("Phonometrica");

#ifdef PHON_WINDOWS
	app.setStyleSheet(
		"QMainWindow::separator { "
		"    background: palette(mid); "
		"    width: 1px; "
		"    height: 1px; "
		"}"
		"QMainWindow::separator:hover { "
		"    background: palette(highlight); "
		"}"
	);
#endif

#ifndef PHON_MACOS
	app.setWindowIcon(QIcon(":/icons/phonometrica.svg"));
#endif

	// Collect any file paths from argv. argv_is_script_invocation() returned
	// false, so anything from argv[1] onward is a path to open. On macOS, file
	// associations are dispatched as QFileOpenEvents (handled by MainWindow's
	// event filter), not via argv — but if someone runs the binary from the
	// shell with a path, we still pick it up here.
	QStringList argv_paths;
	for (int i = 1; i < argc; i++)
	{
		QString p = QString::fromUtf8(argv[i]);
		if (p.isEmpty())
			continue;
		// Skip any leftover -psn_* args macOS sometimes passes to launched
		// bundles. Defensive — current macOS versions don't, but old ones did.
		if (p.startsWith(QLatin1String("-psn_")))
			continue;
		argv_paths.append(p);
	}

	Runtime runtime; // see the text-mode note about argv[0]/program_path
	initialize(runtime);

	int result;
	{
		// Scoped so the window (whose views hold engine handles and reference
		// the project) is destroyed before Project::destroy() below, which in
		// turn must run before the Runtime dies at the end of main.
		MainWindow window(runtime);
		window.show();

		// Queue argv paths BEFORE postInitialize() so the autoload-recent-project
		// logic can see them and skip itself.
		if (!argv_paths.isEmpty())
			window.setPendingArgvPaths(argv_paths);

		Settings::post_initialize();
		window.postInitialize();

		// Headless smoke-test hook: PHON_GUI_SMOKE=<seconds> quits the event loop
		// after the given delay so the full startup/shutdown cycle (including the
		// settings write in finalize()) can run unattended, e.g. under
		// QT_QPA_PLATFORM=offscreen in CI.
		if (const char *smoke = std::getenv("PHON_GUI_SMOKE"))
		{
			int seconds = std::max(1, atoi(smoke));
			QTimer::singleShot(seconds * 1000, &app, &QApplication::quit);
		}

		result = app.exec();
		finalize(runtime);
	}
	// The project holds engine handles: destroy it while the Runtime is alive
	// (its static singleton would otherwise die in atexit handlers, after the
	// Runtime — a use-after-free).
	Project::destroy();
	return result;
#else
	show_usage();
	return 1;
#endif
}
